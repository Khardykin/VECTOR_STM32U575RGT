/**
  ******************************************************************************
  * @file    vector_sys.c
  * @brief   Приборы для диагностики системного времени (см. vector_sys.h)
  *
  *          Два независимых источника времени в проекте:
  *            SysTick (приоритет 4)  -> тик ThreadX, tx_time_get(), 100 Гц
  *            TIM6    (приоритет 15) -> HAL_IncTick(), HAL_GetTick(), 1 кГц
  *          ThreadX тайм-базу HAL не настраивает и не меняет: mx_ThreadX_Init()
  *          -> tx_kernel_enter() конфигурирует только SysTick (в
  *          tx_initialize_low_level.S). Если HAL_GetTick() отстаёт от реального
  *          времени, значит прерывание тайм-базы не обслуживается 1000 раз в
  *          секунду. Почему так бывает и что делать - docs/CODE_MAP.md, 7.2.
  *
  *          Отчёт сравнивает ОБА источника за один интервал, поэтому по нему
  *          сразу видно, кто врёт:
  *            tick 10 s: tx=+1000 hal=+10000 cb=+10000 psc=159 arr=999 keys=+0
  *            irq load: edges=+0 rx4=+12 rx2=+0 rearm4=+0
  ******************************************************************************
  */
#include "vector_sys.h"
#include "vector_config.h"
#include "vector_tick.h"
#include "vector_log.h"

#if VECTOR_AUDIO_DEMO_KEYS
#include "audio_demo.h"
#endif
#if VECTOR_UART_BRIDGE_TEST
#include "uart_bridge.h"
#endif

volatile uint32_t sys_dbg_cb_all   = 0;
volatile uint32_t sys_dbg_hal_tick = 0;
volatile uint32_t sys_dbg_tim_psc  = 0;
volatile uint32_t sys_dbg_tim_arr  = 0;
volatile uint32_t sys_dbg_reports  = 0;

/* Однократная инициализация. КОНТЕКСТ: main(), USER CODE 2 (до RTOS). */
void vector_sys_init(void)
{
#if VECTOR_DBG_FREEZE_TICK
  /* Остановка тайм-базы HAL, пока ядро стоит на брейкпоинте. БЕЗ этого TIM6
     считает и во время halt, но прерывание не обслуживается: за всю остановку
     засчитывается один тик, и HAL_GetTick() отстаёт от реального времени в
     десятки раз (в логе 11:19 было ~34x). SysTick при остановке ядра не идёт
     сам, поэтому с этой настройкой оба источника времени ведут себя одинаково.
     В CubeMX для U5 такого переключателя нет, поэтому он здесь; чтобы убрать
     и эту строку - поставьте VECTOR_DBG_FREEZE_TICK 0 и не halt'айте ядро.
     На боевой прошивке ни на что не влияет.                                  */
  DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_TIM6_STOP;
#endif

  LOG_I(VLOG_M_SYS, "timebase: RTOS tick=%u ms, HAL tick=TIM6 (prio 15), freeze_dbg=%u",
        (uint32_t)VTICK_RESOLUTION_MS, (uint32_t)VECTOR_DBG_FREEZE_TICK);
}

/* КОНТЕКСТ: прерывание тайм-базы (1 кГц). Только чтение регистров и
   инкременты: ни лога, ни блокирующих вызовов. HAL_GetTick() читается здесь
   НАМЕРЕННО - это и есть измеряемая величина; весь остальной проект
   пользуется VTICK_MS().                                                     */
void vector_sys_tick_hook(TIM_HandleTypeDef *htim)
{
  sys_dbg_cb_all++;
  sys_dbg_hal_tick = HAL_GetTick();

  if ((htim != (TIM_HandleTypeDef *)0) && (htim->Instance != (TIM_TypeDef *)0))
  {
    sys_dbg_tim_psc = htim->Instance->PSC;
    sys_dbg_tim_arr = htim->Instance->ARR;
  }
}

/* Отчёт о времени. КОНТЕКСТ: поток (печать блокирующая). Сам решает, пора ли:
   интервал VECTOR_LOG_TICK_REPORT_S секунд по тику RTOS.                     */
#if VECTOR_LOG_TICK_REPORT_S
void vector_sys_tick_report(void)
{
  static uint32_t last_tx    = 0;
  static uint32_t prev_cb    = 0;
  static uint32_t prev_hal   = 0;
  static uint32_t prev_key   = 0;
  static uint32_t prev_edges = 0;
  static uint32_t prev_rx4   = 0;
  static uint32_t prev_rx2   = 0;
  static uint32_t prev_rearm = 0;

  uint32_t tx    = tx_time_get();
  uint32_t cb    = sys_dbg_cb_all;
  uint32_t hal   = sys_dbg_hal_tick;
  uint32_t key   = 0;
  uint32_t edges = 0;
  uint32_t rx4   = 0;
  uint32_t rx2   = 0;
  uint32_t rearm = 0;

  if ((uint32_t)(tx - last_tx) < (VECTOR_LOG_TICK_REPORT_S * TX_TIMER_TICKS_PER_SECOND))
  {
    return;
  }
  last_tx = tx;

#if VECTOR_AUDIO_DEMO_KEYS
  key   = demo_dbg_press;
  edges = demo_dbg_edges;
#endif
#if VECTOR_UART_BRIDGE_TEST
  rx4   = ub_rx4_bytes;
  rx2   = ub_rx2_bytes;
  rearm = ub_dbg_rearm4;
#endif

  /* hal и cb должны быть в 10 раз больше tx: тайм-база HAL 1 кГц против
     SysTick 100 Гц. Если hal заметно меньше cb*1 - прерывания теряются.      */
  LOG_I(VLOG_M_SYS, "tick %u s: tx=+%u hal=+%u cb=+%u psc=%u arr=%u keys=+%u",
        (uint32_t)VECTOR_LOG_TICK_REPORT_S,
        (uint32_t)(VECTOR_LOG_TICK_REPORT_S * TX_TIMER_TICKS_PER_SECOND),
        hal - prev_hal, cb - prev_cb,
        sys_dbg_tim_psc, sys_dbg_tim_arr, key - prev_key);

  /* Детектор шквала прерываний: если edges/rx4/rx2/rearm4 растут тысячами за
     интервал при отсутствии нажатий и трафика, какое-то прерывание с
     приоритетом 0..1 не отдаёт CPU, и тайм-база (приоритет 15) голодает.     */
  LOG_I(VLOG_M_SYS, "irq load: edges=+%u rx4=+%u rx2=+%u rearm4=+%u",
        edges - prev_edges, rx4 - prev_rx4, rx2 - prev_rx2, rearm - prev_rearm);

  sys_dbg_reports++;
  prev_cb    = cb;
  prev_hal   = hal;
  prev_key   = key;
  prev_edges = edges;
  prev_rx4   = rx4;
  prev_rx2   = rx2;
  prev_rearm = rearm;
}
#else
void vector_sys_tick_report(void) { /* отчёт выключен конфигурацией */ }
#endif /* VECTOR_LOG_TICK_REPORT_S */
