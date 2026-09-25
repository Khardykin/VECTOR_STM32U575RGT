/**
  ******************************************************************************
  * @file    vector_tick.h
  * @brief   ЕДИНАЯ точка, откуда весь код VectorLib берёт время
  *
  *          ЗАЧЕМ: чтобы не искать по проекту HAL_GetTick()/HAL_Delay() и не
  *          гадать, от какого источника считается таймаут. Источник выбирается
  *          ОДНИМ макросом VECTOR_TICK_SOURCE в vector_config.h:
  *
  *            0 = HAL     -> HAL_GetTick(), тайм-база TIM6, разрешение 1 мс.
  *                           Тик даёт прерывание TIM6_IRQn с приоритетом 15
  *                           (самый низкий в проекте), поэтому его ОБСЛУЖИВАНИЕ
  *                           может задерживаться/теряться, если ядро надолго
  *                           остановлено отладчиком или есть шквал прерываний.
  *            1 = RTOS    -> tx_time_get(), тик ThreadX (SysTick, 100 Гц),
  *                           разрешение 10 мс. SysTick имеет приоритет 4, то
  *                           есть обслуживается раньше UART/EXTI (0..1)? нет -
  *                           позже их, но раньше TIM6 (15), поэтому он
  *                           устойчивее к задержкам.
  *
  *          ВАЖНО ПОНИМАТЬ: ThreadX НЕ перенастраивает тайм-базу HAL и не
  *          трогает TIM6. Это два независимых таймера:
  *             TIM6   (1 кГц, приоритет 15) -> HAL_IncTick() -> HAL_GetTick()
  *             SysTick(100 Гц, приоритет 4) -> _tx_timer_interrupt() -> тик RTOS
  *          MX_ThreadX_Init()/tx_kernel_enter() настраивает только SysTick (в
  *          tx_initialize_low_level.S). Если HAL_GetTick() отстаёт от реального
  *          времени, значит прерывание TIM6 не обслуживается 1000 раз в секунду
  *          - причину показывает отчёт "tick:" в логе (см. docs/CODE_MAP.md 7).
  *
  *          ПРАВИЛА ИСПОЛЬЗОВАНИЯ:
  *            VTICK_MS()          - только чтение текущего времени (разность
  *                                  двух отсчётов корректна и при переполнении
  *                                  за счёт беззнакового вычитания);
  *            VTICK_SLEEP_MS(x)   - сон ПОТОКА, CPU не грузит. Только после
  *                                  старта планировщика;
  *            VTICK_BUSYWAIT_MS(x)- активная задержка для main() ДО планировщика
  *                                  (в потоке не использовать: мешает другим).
  ******************************************************************************
  */
#ifndef VECTOR_TICK_H
#define VECTOR_TICK_H

#include <stdint.h>
#include "vector_config.h"

#if (VECTOR_TICK_SOURCE == 1)

#include "tx_api.h"

/* Тик ThreadX = 100 Гц (10 мс). Переводим в миллисекунды. */
#define VTICK_RESOLUTION_MS   (1000u / TX_TIMER_TICKS_PER_SECOND)
#define VTICK_MS()            ((uint32_t)(tx_time_get() * VTICK_RESOLUTION_MS))
#define VTICK_SLEEP_MS(ms)    ((void)tx_thread_sleep((((uint32_t)(ms)) + VTICK_RESOLUTION_MS - 1u) \
                                                     / VTICK_RESOLUTION_MS))
#define VTICK_BUSYWAIT_MS(ms) HAL_Delay(ms)

#else /* VECTOR_TICK_SOURCE == 0: HAL */

#include "stm32u5xx_hal.h"
#include "tx_api.h"

#define VTICK_RESOLUTION_MS   1u
#define VTICK_MS()            ((uint32_t)HAL_GetTick())
/* Сон потока всё равно через RTOS: HAL_Delay в потоке - это активное ожидание,
   оно не отдаёт CPU другим потокам.                                          */
#define VTICK_SLEEP_MS(ms)    ((void)tx_thread_sleep((ULONG)(ms)))
#define VTICK_BUSYWAIT_MS(ms) HAL_Delay(ms)

#endif /* VECTOR_TICK_SOURCE */

/* Сколько миллисекунд прошло с момента t0 (корректно и через переполнение). */
#define VTICK_ELAPSED_MS(t0)  ((uint32_t)(VTICK_MS() - (t0)))

#endif /* VECTOR_TICK_H */
