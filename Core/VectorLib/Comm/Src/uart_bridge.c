/**
  ******************************************************************************
  * @file    uart_bridge.c
  * @brief   Прозрачный мост UART4 <-> USART2 (см. uart_bridge.h)
  *
  *          Приём: побайтовое прерывание -> кольцевой буфер -> семафор.
  *          Передача: поток бриджа выгребает кольцо чанками до 64 байт и
  *          шлёт блокирующим HAL_UART_Transmit (контекст потока, не ISR).
  *
  *          Кольца single-producer (ISR) / single-consumer (поток), поэтому
  *          блокировки не нужны. Инвариант кольца: head пишет ТОЛЬКО ISR,
  *          tail двигает ТОЛЬКО поток; пустое = head == tail, полное =
  *          (head+1) % N == tail, то есть полезная вместимость N-1 байт.
  *
  *          ВАЖНО ПРО UART4: куб настроил его как Half-Duplex Single Wire
  *          (PC10, AF_OD, PC11 занят ACCEL_INT). Значит линия ОДНА и приёмник
  *          слышит собственную передачу - ниже это глушится (ub_transmit).
  *
  *          Лога на каждый чанк нет намеренно: при плотном трафике это
  *          километры строк, а лог ещё и делит с мостом UART4. Ошибки
  *          передачи логируются (LOG_W), всё остальное - в счётчиках.
  *
  *          ДИАГНОСТИКА: ub_dbg_err4/err2 (коды HAL_UART_ERROR_*), ub_rearm*
  *          (сколько раз приём перезапускали после ошибки), ub_ovf* (кольцо
  *          переполняется - поток не успевает), плюс лог VLOG_M_BRIDGE.
  ******************************************************************************
  */
#include "uart_bridge.h"
#include "vector_config.h"
#include "vector_log.h"

#if VECTOR_UART_BRIDGE_TEST
#include "usart.h"
#include "main.h"
#include "tx_api.h"

/* Глубина кольца, байт. 256 хватает с запасом: поток выгребает кольцо
   каждые несколько мс, а больше всё равно не удержать - при 115200 байт
   приходит максимум ~11.5 КБ/с. Полезная вместимость = UB_RING-1.
   Память: 2 кольца x UB_RING (было 2x1024 = 2 КБ, стало 512 Б).            */
#define UB_RING      VECTOR_UB_RING
#define UB_CHUNK     64u
#define UB_TX_TMO    20u
#define UB_STACK     2048u
#define UB_PRIORITY  12u

static uint8_t ub_r4[UB_RING];
static uint8_t ub_r2[UB_RING];
static volatile uint32_t ub_r4_head, ub_r4_tail;
static volatile uint32_t ub_r2_head, ub_r2_tail;
static volatile uint8_t ub_rx4_byte, ub_rx2_byte;

static TX_SEMAPHORE ub_sem;
static TX_THREAD    ub_thread;
static uint8_t      ub_stack[UB_STACK] __attribute__((aligned(8)));

volatile uint32_t ub_rx4_bytes = 0;
volatile uint32_t ub_rx2_bytes = 0;
volatile uint32_t ub_tx4_bytes = 0;
volatile uint32_t ub_tx2_bytes = 0;
volatile uint32_t ub_ovf4 = 0;
volatile uint32_t ub_ovf2 = 0;
volatile uint32_t ub_err  = 0;
volatile uint32_t ub_dbg_err4    = 0;   /* последний ErrorCode UART4  (HAL_UART_ERROR_*) */
volatile uint32_t ub_dbg_err2    = 0;   /* последний ErrorCode USART2                     */
volatile uint32_t ub_dbg_rearm4  = 0;   /* сколько раз перезаряжали приём UART4           */
volatile uint32_t ub_dbg_rearm2  = 0;   /* ... и USART2                                   */
volatile uint32_t ub_dbg_echo    = 0;   /* байт собственной передачи выброшено (полудуплекс) */
volatile uint32_t ub_dbg_fill4   = 0;   /* заполнение кольца UART4 (для отладчика)          */
volatile uint32_t ub_dbg_fill2   = 0;

/* ---------------------------------------------------------------- кольца --
 * Положить один байт. КОНТЕКСТ: ISR приёма UART.
 * Единственный производитель, поэтому head читается и пишется только здесь;
 * поток трогает только tail. При переполнении байт ТЕРЯЕТСЯ (не затирает
 * непрочитанное) и растёт *ovf - по нему видно, что поток не успевает.       */
static void ring_put(uint8_t *ring, volatile uint32_t *head, volatile uint32_t *tail,
                     uint8_t b, volatile uint32_t *ovf)
{
  uint32_t h = *head;
  uint32_t n = (h + 1u) % UB_RING;
  if (n == *tail)
  {
    (*ovf)++;
    return;
  }
  ring[h] = b;
  *head = n;                 /* публикуем байт ПОСЛЕ записи данных */
}

/* Забрать до maxn байт одним куском в dst. КОНТЕКСТ: поток бриджа.
   Копирование идёт из ring[tail] последовательно с переходом через 0, поэтому
   данные не рвутся даже если кусок "обернулся" вокруг конца кольца.
   Возврат: сколько байт реально вынуто (0 = кольцо пусто).                  */
static uint32_t ring_get_bulk(uint8_t *ring, volatile uint32_t *head, volatile uint32_t *tail,
                              uint8_t *dst, uint32_t maxn)
{
  uint32_t n = 0;
  uint32_t h = *head;                 /* снимок: head может двигаться из ISR */

  while ((n < maxn) && (*tail != h))
  {
    uint32_t t = *tail;
    dst[n++] = ring[t];
    *tail = (t + 1u) % UB_RING;       /* публикуем освобождение ПОСЛЕ чтения */
  }
  return n;
}

/* Сколько байт сейчас лежит в кольце (для отладки/лога). */
static uint32_t ring_fill(volatile uint32_t *head, volatile uint32_t *tail)
{
  uint32_t h = *head;
  uint32_t t = *tail;
  return (h >= t) ? (h - t) : (UB_RING - t + h);
}

/* ------------------------------------------------------------ ISR приёма --
 * КОНТЕКСТ: прерывание UART4/USART2. Один принятый байт -> кольцо ->
 * tx_semaphore_put (будит поток) -> снова HAL_UART_Receive_IT на 1 байт.
 * Ничего блокирующего здесь нет и быть не должно.                           */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4)
  {
    ring_put(ub_r4, &ub_r4_head, &ub_r4_tail, ub_rx4_byte, &ub_ovf4);
    ub_rx4_bytes++;
    (void)tx_semaphore_put(&ub_sem);
    (void)HAL_UART_Receive_IT(&huart4, (uint8_t *)&ub_rx4_byte, 1);
  }
  else if (huart->Instance == USART2)
  {
    ring_put(ub_r2, &ub_r2_head, &ub_r2_tail, ub_rx2_byte, &ub_ovf2);
    ub_rx2_bytes++;
    (void)tx_semaphore_put(&ub_sem);
    (void)HAL_UART_Receive_IT(&huart2, (uint8_t *)&ub_rx2_byte, 1);
  }
  else
  {
    /* не наш UART (USART1 занят логом, USART3 - BLE) */
  }
}

/* КОНТЕКСТ: ISR. Обработка ошибок приёма (ORE/FE/NE/PE).
   КРИТИЧНО: при ошибке HAL сам снимает приём с IT и НЕ вызывает
   RxCpltCallback, то есть без этого обработчика мост глохнет навсегда -
   например, после одного overrun (байт пришёл, пока мы были заняты).
   Здесь просто перезаряжаем приём и фиксируем код ошибки.                    */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4)
  {
    ub_dbg_err4 = huart->ErrorCode;
    ub_dbg_rearm4++;
    (void)HAL_UART_Receive_IT(&huart4, (uint8_t *)&ub_rx4_byte, 1);
  }
  else if (huart->Instance == USART2)
  {
    ub_dbg_err2 = huart->ErrorCode;
    ub_dbg_rearm2++;
    (void)HAL_UART_Receive_IT(&huart2, (uint8_t *)&ub_rx2_byte, 1);
  }
  else
  {
    /* не наш UART */
  }
}

/* ------------------------------------------------------------- передача --
 * Отправить блок в UART с учётом однопроводного полудуплекса.
 * Если UART в режиме HDSEL (у нас это UART4: куб настроил Half-Duplex Single
 * Wire, PC11 отдан под ACCEL_INT), линия ОДНА и приёмник слышит сам себя:
 * без обработки каждый отправленный байт вернулся бы в кольцо и ушёл обратно
 * во второй UART (эхо-шторм). Поэтому на время передачи глушим RXNE, а после
 * неё сбрасываем ORE/RDR и возвращаем прерывание приёма - состояние HAL
 * (RxState = BUSY_RX) при этом не ломается, в отличие от
 * HAL_HalfDuplex_EnableTransmitter/Receiver, которые его сбрасывают.
 * КОНТЕКСТ: поток бриджа.                                                    */
static HAL_StatusTypeDef ub_transmit(UART_HandleTypeDef *h, const uint8_t *p, uint16_t n,
                                     volatile uint32_t *head, volatile uint32_t *echo_cnt)
{
  HAL_StatusTypeDef st;
  uint8_t hd = ((h->Instance->CR3 & USART_CR3_HDSEL) != 0u) ? 1u : 0u;
  uint32_t head_before = 0;

  /* Если консольный лог настроен на этот же UART (VECTOR_LOG_UART), берём его
     блокировку: иначе строка лога и наш пакет перемешаются побайтно.        */
  vlog_bus_lock(h->Instance);

  if (hd != 0u)
  {
    head_before = *head;
    __HAL_UART_DISABLE_IT(h, UART_IT_RXNE);
  }

  st = HAL_UART_Transmit(h, (uint8_t *)(uintptr_t)p, n, UB_TX_TMO);

  if (hd != 0u)
  {
    /* выбросить собственное эхо, принятое за время передачи: откатываем head
       туда, где он был до передачи. В кольцо при этом могли попасть и честные
       байты собеседника (на одном проводе он в это время всё равно молчит),
       поэтому считаем и их - счётчик ub_dbg_echo. */
    {
      uint32_t now = *head;
      if (now != head_before)
      {
        *echo_cnt += (now >= head_before) ? (now - head_before)
                                          : (UB_RING - head_before + now);
        *head = head_before;
      }
    }
    __HAL_UART_CLEAR_FLAG(h, UART_CLEAR_OREF);   /* overrun от неотобранного байта */
    (void)h->Instance->RDR;                      /* и сам байт */
    __HAL_UART_ENABLE_IT(h, UART_IT_RXNE);
  }

  vlog_bus_unlock(h->Instance);
  return st;
}

/* ------------------------------------------------------------------ поток -
 * Тело потока моста. Спит на семафоре, по пробуждении выгребает ОБА кольца
 * чанками по 64 байта, пока они не опустеют (do-while по moved).             */
static void ub_thread_entry(ULONG arg)
{
  static uint8_t chunk[UB_CHUNK];
  (void)arg;

  for (;;)
  {
    uint32_t moved;

    (void)tx_semaphore_get(&ub_sem, TX_WAIT_FOREVER);

    /* выгребаем всё накопившееся, пока кольца не опустеют */
    do
    {
      moved = 0;

#if UART_BRIDGE_DIR_4_TO_2
      {
        uint32_t n = ring_get_bulk(ub_r4, &ub_r4_head, &ub_r4_tail, chunk, UB_CHUNK);
        if (n != 0u)
        {
          if (ub_transmit(&huart2, chunk, (uint16_t)n, &ub_r2_head, &ub_dbg_echo) == HAL_OK)
          {
            ub_tx2_bytes += n;
          }
          else
          {
            ub_err++;
            LOG_W(VLOG_M_BRIDGE, "usart2 tx fail (%u b)", n);
          }
          moved += n;
        }
      }
#endif

#if UART_BRIDGE_DIR_2_TO_4
      {
        uint32_t n = ring_get_bulk(ub_r2, &ub_r2_head, &ub_r2_tail, chunk, UB_CHUNK);
        if (n != 0u)
        {
          if (ub_transmit(&huart4, chunk, (uint16_t)n, &ub_r4_head, &ub_dbg_echo) == HAL_OK)
          {
            ub_tx4_bytes += n;
          }
          else
          {
            ub_err++;
            LOG_W(VLOG_M_BRIDGE, "uart4 tx fail (%u b)", n);
          }
          moved += n;
        }
      }
#endif
      ub_dbg_fill4 = ring_fill(&ub_r4_head, &ub_r4_tail);
      ub_dbg_fill2 = ring_fill(&ub_r2_head, &ub_r2_tail);
    }
    while (moved != 0u);
  }
}

/* ------------------------------------------------------------------- init -
 * Создать семафор и поток, запустить побайтовый приём на обоих UART.
 * КОНТЕКСТ: tx_application_define() (см. app_azure_rtos.c), до потоков.
 * Обработчики UART4_IRQn/USART2_IRQn включены в usart.c/gpdma.c.              */
void uart_bridge_init(void)
{
  (void)tx_semaphore_create(&ub_sem, "uart bridge", 0);
  (void)tx_thread_create(&ub_thread, "UART Bridge", ub_thread_entry, 0,
                         ub_stack, UB_STACK,
                         UB_PRIORITY, UB_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);

  /* старт приёма */
  (void)HAL_UART_Receive_IT(&huart4, (uint8_t *)&ub_rx4_byte, 1);
  (void)HAL_UART_Receive_IT(&huart2, (uint8_t *)&ub_rx2_byte, 1);

  LOG_I(VLOG_M_BRIDGE, "bridge on: uart4(hd=%u) <-> usart2, ring=%u",
        (uint32_t)((UART4->CR3 & USART_CR3_HDSEL) != 0u), (uint32_t)UB_RING);
}

#endif /* VECTOR_UART_BRIDGE_TEST */
