/**
  ******************************************************************************
  * @file    uart_bridge.c
  * @brief   Прозрачный мост UART4 <-> USART2
  *
  *          Приём: побайтовое прерывание -> кольцевой буфер -> семафор.
  *          Передача: поток бриджа выгребает кольцо чанками до 64 байт и
  *          шлёт блокирующим HAL_UART_Transmit (контекст потока, не ISR).
  *
  *          Кольца single-producer / single-consumer (ISR пишет, поток читает),
  *          поэтому блокировки не нужны.
  ******************************************************************************
  */
#include "uart_bridge.h"
#include "usart.h"
#include "main.h"
#include "tx_api.h"

#define UB_RING      1024u
#define UB_CHUNK     64u
#define UB_TX_TMO    20u
#define UB_STACK     2048u
#define UB_PRIORITY  12u

static uint8_t ub_r4[UB_RING];
static uint8_t ub_r2[UB_RING];
static volatile uint32_t ub_r4_head, ub_r4_tail;
static volatile uint32_t ub_r2_head, ub_r2_tail;
static uint8_t ub_rx4_byte, ub_rx2_byte;

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

/* ---------------------------------------------------------------- кольца -- */
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
  *head = n;
}

static uint32_t ring_get_bulk(uint8_t *ring, volatile uint32_t *head, volatile uint32_t *tail,
                              uint8_t *dst, uint32_t maxn)
{
  uint32_t n = 0;
  while ((n < maxn) && (*tail != *head))
  {
    dst[n++] = ring[*tail];
    *tail = (*tail + 1u) % UB_RING;
  }
  return n;
}

/* ------------------------------------------------------------ ISR приёма -- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4)
  {
    ring_put(ub_r4, &ub_r4_head, &ub_r4_tail, ub_rx4_byte, &ub_ovf4);
    ub_rx4_bytes++;
    (void)tx_semaphore_put(&ub_sem);
    (void)HAL_UART_Receive_IT(&huart4, &ub_rx4_byte, 1);
  }
  else if (huart->Instance == USART2)
  {
    ring_put(ub_r2, &ub_r2_head, &ub_r2_tail, ub_rx2_byte, &ub_ovf2);
    ub_rx2_bytes++;
    (void)tx_semaphore_put(&ub_sem);
    (void)HAL_UART_Receive_IT(&huart2, &ub_rx2_byte, 1);
  }
  else
  {
    /* не наш UART */
  }
}

/* ------------------------------------------------------------------ поток - */
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
        if (n)
        {
          if (HAL_UART_Transmit(&huart2, chunk, (uint16_t)n, UB_TX_TMO) == HAL_OK)
          {
            ub_tx2_bytes += n;
          }
          else
          {
            ub_err++;
          }
          moved += n;
        }
      }
#endif

#if UART_BRIDGE_DIR_2_TO_4
      {
        uint32_t n = ring_get_bulk(ub_r2, &ub_r2_head, &ub_r2_tail, chunk, UB_CHUNK);
        if (n)
        {
          if (HAL_UART_Transmit(&huart4, chunk, (uint16_t)n, UB_TX_TMO) == HAL_OK)
          {
            ub_tx4_bytes += n;
          }
          else
          {
            ub_err++;
          }
          moved += n;
        }
      }
#endif
    }
    while (moved != 0u);
  }
}

/* ------------------------------------------------------------------- init - */
void uart_bridge_init(void)
{
  (void)tx_semaphore_create(&ub_sem, "uart bridge", 0);
  (void)tx_thread_create(&ub_thread, "UART Bridge", ub_thread_entry, 0,
                         ub_stack, UB_STACK,
                         UB_PRIORITY, UB_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);

  /* старт приёма; обработчики UART4_IRQn/USART2_IRQn уже включены в usart.c */
  (void)HAL_UART_Receive_IT(&huart4, &ub_rx4_byte, 1);
  (void)HAL_UART_Receive_IT(&huart2, &ub_rx2_byte, 1);
}
