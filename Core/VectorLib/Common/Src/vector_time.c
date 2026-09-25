/**
  ******************************************************************************
  * @file    vector_time.c
  * @brief   Микросекундное время по DWT->CYCCNT (см. vector_time.h)
  ******************************************************************************
  */
#include "vector_time.h"
#include "main.h"          /* stm32u5xx_hal.h: DWT, CoreDebug, SystemCoreClock */

#define VTIME_US_DIV   (vtime_div)      /* тактов на микросекунду */

static uint32_t vtime_div = 160u;      /* 160 МГц / 1 МГц; уточняется в init */

/* Включить трассировочный счётчик циклов. КОНТЕКСТ: инициализация. */
void vtime_init(void)
{
  uint32_t hz = SystemCoreClock;

  if (hz == 0u) { hz = 160000000u; }
  vtime_div = hz / 1000000u;
  if (vtime_div == 0u) { vtime_div = 1u; }

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* разрешить DWT/ITM     */
//  DWT->LAR          = 0xC5ACCE55u;                  /* снять блокировку DWT   */
  DWT->CYCCNT       = 0u;
  DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;       /* запустить счётчик     */
}

/* Текущий отсчёт в мкс. КОНТЕКСТ: любой, включая ISR (чтение одного регистра). */
uint32_t vtime_us(void)
{
  return DWT->CYCCNT / VTIME_US_DIV;
}

/* Сколько мкс прошло с t0. Беззнаковое вычитание само обрабатывает
   переполнение 32-битного счётчика. КОНТЕКСТ: любой.                        */
uint32_t vtime_elapsed_us(uint32_t t0)
{
  return (uint32_t)(vtime_us() - t0);
}

uint32_t vtime_clock_hz(void)
{
  return vtime_div * 1000000u;
}
