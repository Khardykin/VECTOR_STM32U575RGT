/**
  ******************************************************************************
  * @file    rf_thread.c
  * @brief   Поток RF: приём LoRa / BLE / LTE из колец UART и разбор кадров
  *
  *          Каркас обмена данными. Что здесь уже работает и что наполнять -
  *          см. rf_thread.h. Коротко:
  *
  *            ISR UART -> InputBuffer[TYPE_*] -> ЭТОТ ПОТОК -> парсер модуля
  *
  *          Поток один на все три линка: за проход он обходит включённые линки,
  *          забирает из каждого кольца не больше VECTOR_RF_BYTES_PER_CYCLE
  *          байт, отдаёт каждый байт в парсер модуля и параллельно копит кадр
  *          в line[]. Кадром считается последовательность байт, после которой
  *          линия молчала VECTOR_RF_FRAME_GAP_MS (или накопитель заполнился).
  *          Готовый кадр уходит в rf_<link>_frame().
  *
  *          Три места под наполнение на каждый линк (все сейчас пустые):
  *            rf_<link>_poll()     - периодика в потоке: таймауты, AT-машина,
  *                                   опрос состояния модуля. Сюда же ставится
  *                                   вызов существующего Lora_Receive() при
  *                                   CONFIG_LORA 1 (см. правило владельца
  *                                   кольца в rf_thread.h).
  *            rf_<link>_rx_byte()  - побайтовый разбор (свой протокол/своя
  *                                   машина состояний; общий накопитель кадра
  *                                   при этом можно не использовать).
  *            rf_<link>_frame()    - разбор кадра, собранного общим
  *                                   накопителем по паузе в линии.
  *          Передача - rf_send() (обёртка над transmit_buffer()).
  *
  *          КОНТЕКСТ: всё, кроме rf_notify(), выполняется в потоке RF -
  *          здесь можно блокироваться, спать, брать мьютексы и печатать лог.
  ******************************************************************************
  */
#include "rf_thread.h"

#if VECTOR_RF_THREAD

#include "tx_api.h"
#include "vector_log.h"
#include "vector_tick.h"
#include <string.h>

/* ------------------------------------------------------------------ RTOS -- */
static TX_THREAD    rf_thread;
static uint8_t      rf_stack[VECTOR_RF_STACK_SIZE] __attribute__((aligned(8)));
static TX_SEMAPHORE rf_wake;

/* Сколько ждать события, если heartbeat включён (VECTOR_RF_WAKE_TIMEOUT_MS).
   0 = честное TX_WAIT_FOREVER: поток встаёт только по rf_notify(). Время -
   только тик RTOS (vector_tick.h), как во всём проекте.                     */
#if (VECTOR_RF_WAKE_TIMEOUT_MS > 0u)
#define RF_WAKE_TMO   ((ULONG)VTICK_MS2TICKS(VECTOR_RF_WAKE_TIMEOUT_MS))
#else
#define RF_WAKE_TMO   TX_WAIT_FOREVER
#endif

/* --------------------------------------------------------------- состояние --
 * ЕДИНАЯ структура модуля (как audio_status в плеере): состояние линков,
 * счётчики приёма/передачи, счётчики потока. Начальные значения - здесь,
 * дальше их правят rf_init(), rf_link_service() и публичные функции.        */
volatile rf_status_t rf_status =
{
  .boot_stage = RF_BOOT_INIT,
  .link =
  {
    [RF_LINK_LORA] = { .state = RF_LINK_OFF, .enabled = (VECTOR_RF_LINK_LORA ? 1u : 0u) },
    [RF_LINK_BLE]  = { .state = RF_LINK_OFF, .enabled = (VECTOR_RF_LINK_BLE  ? 1u : 0u) },
    [RF_LINK_LTE]  = { .state = RF_LINK_OFF, .enabled = (VECTOR_RF_LINK_LTE  ? 1u : 0u) }
  }
};

/* ------------------------------------------------------------------ линки ---
 * Таблица линков: имя для лога, какое кольцо приёма читать и каким типом
 * передавать (transmit_buffer() в buffer.c сам выбирает UART по типу).
 *
 * UART линка здесь НЕ хранится намеренно: владелец порта - buffer.c/куб,
 * а не поток RF. Когда включите CONFIG_BLE/CONFIG_LORA, определите USART_BLE /
 * USART_RF в main.h (PLAN.md раздел 7, п.13) - transmit_buffer() подхватит их
 * сам, правки в этом файле не нужны.                                        */
typedef struct
{
  rf_link_id_t id;         /* какой линк                                    */
  const char  *name;       /* короткое имя для лога (ASCII)                 */
  uint8_t      rx_type;    /* индекс кольца приёма: InputBuffer[rx_type]     */
  uint8_t      tx_type;    /* аргумент transmit_buffer()                    */
  uint8_t      line[VECTOR_RF_LINE_LEN];  /* накопитель кадра               */
  uint16_t     line_len;   /* байт в накопителе                             */
} rf_link_ctx_t;

static rf_link_ctx_t rf_link[RF_LINK_COUNT] =
{
  { RF_LINK_LORA, "lora", (uint8_t)TYPE_LORA, (uint8_t)TYPE_LORA, { 0 }, 0u },
  { RF_LINK_BLE,  "ble",  (uint8_t)TYPE_BLE,  (uint8_t)TYPE_BLE,  { 0 }, 0u },
  { RF_LINK_LTE,  "lte",  (uint8_t)TYPE_LTE,  (uint8_t)TYPE_LTE,  { 0 }, 0u }
};

/* Сокращение: состояние/счётчики линка. */
#define RF_ST(c)   (rf_status.link[(c)->id])

/* ============================ НАПОЛНЕНИЕ: LoRa (S7678S) ====================
 * ЗАГЛУШКИ. Сюда ложится обмен с модулем LoRa: AT-машина (команды "mac tx",
 * "mac join otaa", ответы "ok"/"mac_rx"/"mac_err"), классы A/C, DR/частоты,
 * GPS-трек. Готовый драйвер уже есть - Lora_S7678S.c (Lora_Init, Lora_Receive,
 * Lora_DataSet, ...), он собирается при CONFIG_LORA 1.
 *
 * КАК ПОДКЛЮЧИТЬ, НЕ ЛОМАЯ ВЛАДЕЛЬЦА КОЛЬЦА:
 *   rf_lora_poll()  <- Lora_Init()/Lora_Receive(): драйвер сам читает
 *                      InputBuffer[TYPE_LORA], поэтому звать его можно ТОЛЬКО
 *                      отсюда (иначе два потребителя на одно SPSC-кольцо);
 *   rf_lora_rx_byte()/rf_lora_frame() <- свой разбор, если пойдёте не через
 *                      драйвер, а напрямую (тогда кольцо читает rf_thread и
 *                      Lora_Receive() не вызывается вовсе).
 * Статусы/звук по принятым пакетам - PLAN.md раздел 4 (таблица
 * "статус -> индекс звука", audio_play_now()).                              */
static void rf_lora_poll(rf_link_ctx_t *c)
{
  (void)c;
  /* TODO: периодика LoRa - AT-машина, джойн, подтверждённые пакеты, ретраи. */
}

static void rf_lora_rx_byte(rf_link_ctx_t *c, uint8_t b)
{
  (void)c;
  (void)b;
  /* TODO: побайтовый разбор ответа модуля LoRa. */
}

static void rf_lora_frame(rf_link_ctx_t *c, const uint8_t *data, uint16_t len)
{
  (void)c;
  (void)data;
  (void)len;
  /* TODO: разбор целого кадра/строки ответа LoRa. */
}

/* ============================ НАПОЛНЕНИЕ: BLE ==============================
 * ЗАГЛУШКИ. Сюда ложится обмен с BLE-модулем (USART3): приём кадров со
 * статусами прибора и командами, ответы на них, мост BLE -> LoRa.
 * Формат кадра и детект конца - PLAN.md раздел 5, этап 1 (IDLE-линия или
 * таймаут межбайтового интервала; общий накопитель здесь как раз по таймауту).
 * Статусы из кадра -> ST_COMMON (shared_types.h) -> звук/индикация.          */
static void rf_ble_poll(rf_link_ctx_t *c)
{
  (void)c;
  /* TODO: периодика BLE - keepalive, контроль соединения, очередь на отправку. */
}

static void rf_ble_rx_byte(rf_link_ctx_t *c, uint8_t b)
{
  (void)c;
  (void)b;
  /* TODO: побайтовый разбор протокола BLE-модуля. */
}

static void rf_ble_frame(rf_link_ctx_t *c, const uint8_t *data, uint16_t len)
{
  (void)c;
  (void)data;
  (void)len;
  /* TODO: разбор кадра BLE (статусы/команды), ответ, мост в LoRa. */
}

/* ============================ НАПОЛНЕНИЕ: LTE ==============================
 * ЗАГЛУШКИ. Сюда ложится обмен с сотовым модемом: AT-диалог, регистрация в
 * сети, отправка данных, контроль пинов LTE_EN/LTE_RESET/LTE_STATUS/LTE_LED
 * (main.h). Пины пока не трогаем - это уже логика питания модема.            */
static void rf_lte_poll(rf_link_ctx_t *c)
{
  (void)c;
  /* TODO: периодика LTE - AT-опрос состояния модема, перезапуск по таймауту. */
}

static void rf_lte_rx_byte(rf_link_ctx_t *c, uint8_t b)
{
  (void)c;
  (void)b;
  /* TODO: побайтовый разбор ответа модема LTE. */
}

static void rf_lte_frame(rf_link_ctx_t *c, const uint8_t *data, uint16_t len)
{
  (void)c;
  (void)data;
  (void)len;
  /* TODO: разбор кадра/строки ответа модема LTE. */
}

/* =============================================================== внутреннее */
/* Диспетчер: отдать байт парсеру своего модуля. КОНТЕКСТ: поток RF. */
static void rf_dispatch_byte(rf_link_ctx_t *c, uint8_t b)
{
  switch (c->id)
  {
    case RF_LINK_LORA: rf_lora_rx_byte(c, b); break;
    case RF_LINK_BLE:  rf_ble_rx_byte(c, b);  break;
    case RF_LINK_LTE:  rf_lte_rx_byte(c, b);  break;
    default: break;
  }
}

/* Диспетчер: отдать собранный кадр парсеру своего модуля. КОНТЕКСТ: поток RF. */
static void rf_dispatch_frame(rf_link_ctx_t *c)
{
  switch (c->id)
  {
    case RF_LINK_LORA: rf_lora_frame(c, c->line, c->line_len); break;
    case RF_LINK_BLE:  rf_ble_frame(c, c->line, c->line_len);  break;
    case RF_LINK_LTE:  rf_lte_frame(c, c->line, c->line_len);  break;
    default: break;
  }
}

/* Диспетчер: периодика модуля. КОНТЕКСТ: поток RF, один вызов на проход. */
static void rf_dispatch_poll(rf_link_ctx_t *c)
{
  switch (c->id)
  {
    case RF_LINK_LORA: rf_lora_poll(c); break;
    case RF_LINK_BLE:  rf_ble_poll(c);  break;
    case RF_LINK_LTE:  rf_lte_poll(c);  break;
    default: break;
  }
}

/* Кадр собран: отдать модулю и очистить накопитель. КОНТЕКСТ: поток RF. */
static void rf_frame_done(rf_link_ctx_t *c)
{
  if (c->line_len == 0u)
  {
    return;
  }

  RF_ST(c).cnt_rx_frames++;
  RF_ST(c).frame_len   = c->line_len;
  RF_ST(c).last_rx_ms  = VTICK_MS();
  RF_ST(c).state       = RF_LINK_IDLE;

  rf_dispatch_frame(c);

  c->line_len = 0u;
}

/* Обслужить ОДИН линк: забрать байты из его кольца, отдать парсеру модуля и
   заметить конец кадра по паузе в линии. КОНТЕКСТ: только поток RF (он -
   единственный потребитель кольца, см. buffer.h).
   Ограничение VECTOR_RF_BYTES_PER_CYCLE не даёт длинному приёму занять поток
   целиком: недочитанное заберёт следующий проход (он сразу же, по семафору). */
static void rf_link_service(rf_link_ctx_t *c)
{
  uint32_t guard = (uint32_t)VECTOR_RF_BYTES_PER_CYCLE;
  uint8_t  b;

  while ((guard > 0u) && (receive_buffer(&InputBuffer[c->rx_type], &b) != 0u))
  {
    guard--;

    RF_ST(c).cnt_rx_bytes++;
    RF_ST(c).last_byte_ms = VTICK_MS();
    if (RF_ST(c).state == RF_LINK_IDLE)
    {
      RF_ST(c).state = RF_LINK_RECEIVING;
    }

    /* 1) байт - в парсер модуля (побайтовая машина состояний) */
    rf_dispatch_byte(c, b);

    /* 2) байт - в общий накопитель кадра */
    if (c->line_len < (uint16_t)sizeof(c->line))
    {
      c->line[c->line_len++] = b;
    }
    else
    {
      /* накопитель полон: отдаём то, что есть, и продолжаем с этого байта -
         кадр длиннее буфера режется на части, счётчик это покажет */
      rf_frame_done(c);
      c->line[c->line_len++] = b;
    }
  }

  /* Конец кадра: в кольце пусто И линия молчала VECTOR_RF_FRAME_GAP_MS.
     Точность - период опроса потока (heartbeat или rf_notify из ISR).       */
  if ((c->line_len > 0u) &&
      (check_buffer(&InputBuffer[c->rx_type]) == 0u) &&
      (VTICK_ELAPSED_MS(RF_ST(c).last_byte_ms) >= (uint32_t)VECTOR_RF_FRAME_GAP_MS))
  {
    rf_frame_done(c);
  }

  /* периодика модуля: AT-машина, таймауты, очереди на отправку */
  rf_dispatch_poll(c);
}

/* --------------------------------------------------------------- поток ----
 * Тело потока RF: ЕДИНСТВЕННЫЙ потребитель колец приёма радио-линков.
 * Цикл: проснулись (rf_notify из ISR или heartbeat) -> обошли все включённые
 * линки -> снова ждём. Блокировок на внешнюю flash здесь нет, поэтому поток
 * не мешает плееру; приоритет - VECTOR_RF_PRIORITY.                         */
static void rf_thread_entry(ULONG arg)
{
  (void)arg;

  rf_status.boot_stage = RF_BOOT_RUNNING;

  LOG_I(VLOG_M_RF, "rf thread: lora=%u ble=%u lte=%u (gap=%u ms, poll=%u ms)",
        (uint32_t)rf_status.link[RF_LINK_LORA].enabled,
        (uint32_t)rf_status.link[RF_LINK_BLE].enabled,
        (uint32_t)rf_status.link[RF_LINK_LTE].enabled,
        (uint32_t)VECTOR_RF_FRAME_GAP_MS, (uint32_t)VECTOR_RF_WAKE_TIMEOUT_MS);

  for (;;)
  {
    uint32_t i;

    rf_status.cnt_wakes++;
    if (tx_semaphore_get(&rf_wake, RF_WAKE_TMO) != TX_SUCCESS)
    {
      rf_status.cnt_wake_timeouts++;   /* событий не было - холостой проход */
    }
    rf_status.cnt_cycles++;

    for (i = 0u; i < (uint32_t)RF_LINK_COUNT; i++)
    {
      if (rf_status.link[i].enabled != 0u)
      {
        rf_link_service(&rf_link[i]);
      }
    }
  }
}

/* =============================================================== публичное */
/* Инициализация модуля RF. КОНТЕКСТ: tx_application_define() (до планировщика,
   стек MSP) - как audio_init()/ext_init(). Создаёт семафор и поток; сами
   UART'ы настраивает CubeMX, здесь мы их не трогаем.
   Порядок: кольца -> RTOS-объекты -> поток (TX_AUTO_START: реально побежит
   после tx_kernel_enter()).                                                 */
void rf_init(void)
{
  uint32_t i;

  for (i = 0u; i < (uint32_t)RF_LINK_COUNT; i++)
  {
    /* Кольцо приёма линка. InputBuffer[] лежит в .bss и обнулён, но метки
       выставляем явно: производитель (ISR) и потребитель (поток) стартуют с
       begin == end == 0. ВНИМАНИЕ: Lora_Init() тоже зовёт Init_Buffer() для
       TYPE_LORA - при CONFIG_LORA 1 он обнулит кольцо, поэтому включать LoRa
       надо до старта приёма, а не на лету.                                 */
    Init_Buffer(&InputBuffer[rf_link[i].rx_type]);

    rf_link[i].line_len = 0u;
    rf_status.link[i].state = (rf_status.link[i].enabled != 0u) ? RF_LINK_IDLE
                                                                : RF_LINK_OFF;
  }

  (void)tx_semaphore_create(&rf_wake, "rf wake", 0);

  if (tx_thread_create(&rf_thread, "RF", rf_thread_entry, 0,
                       rf_stack, (ULONG)VECTOR_RF_STACK_SIZE,
                       (UINT)VECTOR_RF_PRIORITY, (UINT)VECTOR_RF_PRIORITY,
                       TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    LOG_E(VLOG_M_RF, "rf thread create FAIL (stack=%u prio=%u)",
          (uint32_t)VECTOR_RF_STACK_SIZE, (uint32_t)VECTOR_RF_PRIORITY);
    return;
  }

  LOG_I(VLOG_M_RF, "rf init: thread created, prio=%u stack=%u",
        (uint32_t)VECTOR_RF_PRIORITY, (uint32_t)VECTOR_RF_STACK_SIZE);
}

/* Разбудить поток: на линке появились данные. КОНТЕКСТ: ISR приёма UART -
   только неблокирующий tx_semaphore_put (правило проекта: из ISR никаких
   ожиданий, мьютексов и лога). Поток за один проход обходит ВСЕ линки,
   поэтому отдельной очереди событий на линк не нужно.

   КУДА СТАВИТЬ: в stm32u5xx_it.c в USART*_IRQHandler сразу после
   add_to_buffer(&InputBuffer[TYPE_*], byte) - тогда поток просыпается в тот же
   момент, а не по heartbeat (VECTOR_RF_WAKE_TIMEOUT_MS). Без rf_notify()
   каркас тоже работает: приём разбирается каждые 50 мс.                    */
void rf_notify(rf_link_id_t link)
{
  if ((uint32_t)link >= (uint32_t)RF_LINK_COUNT)
  {
    return;
  }
  (void)tx_semaphore_put(&rf_wake);
}

/* Включить/выключить линк в рантайме (например, по статусу
   ST_COMMON_BIT_TURN_ON_LORA / _BLE / _GSM из shared_types.h).
   КОНТЕКСТ: поток. При выключении накопитель кадра сбрасывается, чтобы
   следующий сеанс не начался с хвоста старого обмена.                       */
void rf_link_enable(rf_link_id_t link, uint8_t on)
{
  if ((uint32_t)link >= (uint32_t)RF_LINK_COUNT)
  {
    return;
  }

  rf_status.link[link].enabled = (on != 0u) ? 1u : 0u;
  rf_status.link[link].state   = (on != 0u) ? RF_LINK_IDLE : RF_LINK_OFF;
  rf_link[link].line_len       = 0u;

  LOG_I(VLOG_M_RF, "link %s -> %u", rf_link_name(link), (uint32_t)(on != 0u));
}

/* Включён ли линк. КОНТЕКСТ: любой (читается одно volatile-поле). */
uint8_t rf_link_enabled(rf_link_id_t link)
{
  if ((uint32_t)link >= (uint32_t)RF_LINK_COUNT)
  {
    return 0u;
  }
  return rf_status.link[link].enabled;
}

/* Короткое имя линка для лога (только ASCII - требование vector_log.h).
   Возвращает статическую строку, не освобождать. КОНТЕКСТ: любой.           */
const char *rf_link_name(rf_link_id_t link)
{
  if ((uint32_t)link >= (uint32_t)RF_LINK_COUNT)
  {
    return "?";
  }
  return rf_link[link].name;
}

/* Передать кадр по линку. КОНТЕКСТ: поток (transmit_buffer() при DMA_USART 0
   работает блокирующим HAL_UART_Transmit). Обёртка нужна, чтобы передача шла
   через одни и те же счётчики rf_status и с проверкой "линк включён".
   Возврат: 0 = передано, -1 = неверные аргументы, -2 = линк выключен.       */
int rf_send(rf_link_id_t link, uint8_t *data, uint16_t len)
{
  if (((uint32_t)link >= (uint32_t)RF_LINK_COUNT) || (data == (uint8_t *)0) || (len == 0u))
  {
    return -1;
  }
  if (rf_status.link[link].enabled == 0u)
  {
    return -2;
  }

  transmit_buffer(data, len, rf_link[link].tx_type);

  rf_status.link[link].cnt_tx_frames++;
  rf_status.link[link].cnt_tx_bytes += len;
  rf_status.link[link].last_tx_ms    = VTICK_MS();
  return 0;
}

#endif /* VECTOR_RF_THREAD */
