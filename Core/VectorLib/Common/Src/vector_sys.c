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
  *          секунду. Почему так бывает и что делать - docs/SYSTEM.md, 7.2.
  *
  *          Отчёт сравнивает ОБА источника за один интервал, поэтому по нему
  *          сразу видно, кто врёт:
  *            tick 10 s: tx=+1000 hal=+10000 cb=+10000 psc=159 arr=999 keys=+0
  *            clk meas: wall(RTC/LSI)=+10 s for a 10 s tick window, core ran 100% of wall
  *            clk meas: dwt=+1600 Mcyc, must be 1600 Mcyc (1000 ticks x load 1599999), core=160 MHz
  *            irq load: edges=+0 rx4=+12 rx2=+0 rearm4=+0
  *
  *          Строки "clk meas" (VECTOR_DBG_CLOCK_PROBE) меряют НАСТЕННОЕ время
  *          окна по RTC на LSI - единственному источнику, который не зависит ни
  *          от кварца, ни от того, остановлено ли ядро отладчиком. Подробности
  *          и разбор двух похожих картин (простой под отладчиком против
  *          неверного SYSTEM_CLOCK) - у блока #if VECTOR_DBG_CLOCK_PROBE ниже
  *          и в docs/SYSTEM.md, раздел 7.
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

#if VECTOR_DBG_CLOCK_PROBE
#include "rtc.h"        /* hrtc: RTC на LSI - независимый от HSE эталон времени */
#endif

volatile uint32_t sys_dbg_cb_all   = 0;
volatile uint32_t sys_dbg_hal_tick = 0;
volatile uint32_t sys_dbg_tim_psc  = 0;
volatile uint32_t sys_dbg_tim_arr  = 0;
volatile uint32_t sys_dbg_reports  = 0;

#if VECTOR_DBG_CLOCK_PROBE
/* ------------------------------------------------------------ эталон времени
 * ЗАЧЕМ. Всё время в проекте считается от тика RTOS, а тик - от SysTick,
 * перезагруз которого ЖЁСТКО зашит в Core/Src/tx_initialize_low_level.S:
 *     SYSTEM_CLOCK   = 160000000
 *     SYSTICK_CYCLES = ((SYSTEM_CLOCK / 100) - 1)      -> 1599999
 * То есть период тика = 1599999 / РЕАЛЬНАЯ частота ядра. Если реальный кварц
 * не 16 МГц, а 8 МГц, SYSCLK получится 80 МГц, тик станет 20 мс вместо 10 мс
 * и ВСЯ шкала VTICK_* (паузы, таймауты, отчёты) поедет вдвое. При этом:
 *   - UART продолжит работать только если делители считались от той же
 *     (неверной) частоты - текст в логе выглядит нормально;
 *   - SAI возьмёт тактовую вдвое ниже и звук пойдёт в 2 раза медленнее и на октаву
 *     ниже, а selftest этого НЕ ЗАМЕТИТ: он меряет длительность тем же
 *     HAL-тиком, который замедлился ровно во столько же раз (240 == 240).
 * Нужен ВНЕШНИЙ эталон. RTC тактируется от LSI (32 кГц, rtc.c:
 * RCC_RTCCLKSOURCE_LSI) и от HSE/PLL не зависит вообще.
 *
 * Точность LSI ~5% - заведомо хватает, чтобы отличить 10 с от 20 с.
 *
 * DWT->CYCCNT считает такты ядра. За одно и то же окно:
 *   реальная_частота_ядра = прирост_CYCCNT / реальное_время_по_RTC
 * Это прямое измерение SYSCLK, ни от каких предположений не зависящее.     */

/* Секунды по RTC (в пределах суток). КОНТЕКСТ: поток.
   HAL_RTC_GetTime() ОБЯЗАТЕЛЬНО сопровождается GetDate(): иначе теневые
   регистры календаря останутся заблокированными и следующий вызов зависнет. */
static uint32_t rtc_seconds(void)
{
  RTC_TimeTypeDef t = {0};
  RTC_DateTypeDef d = {0};

  HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
  (void)d;
  return ((uint32_t)t.Hours * 3600u) + ((uint32_t)t.Minutes * 60u) + (uint32_t)t.Seconds;
}
#endif /* VECTOR_DBG_CLOCK_PROBE */

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

#if VECTOR_DBG_CLOCK_PROBE
  /* DWT->CYCCNT включает tx_initialize_low_level.S, но БЕЗ DEMCR.TRCENA
     счётчик может не идти - включаем трассировку сами. Обнулять CYCCNT не
     нужно: важны только приросты.                                             */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL       |= DWT_CTRL_CYCCNTENA_Msk;

  /* Что реально зашито в SysTick прямо сейчас. Ожидаемое при 160 МГц и
     TX_TIMER_TICKS_PER_SECOND=100: load=1599999 ctrl=0x7.
     ctrl: bit0 ENABLE, bit1 TICKINT, bit2 CLKSOURCE(=1 -> процессорный такт). */
  LOG_I(VLOG_M_SYS, "clk cfg: core=%u MHz systick load=%u ctrl=%x dwt_cyc=%u",
        (uint32_t)(SystemCoreClock / 1000000u), (uint32_t)SysTick->LOAD,
        (uint32_t)SysTick->CTRL, (uint32_t)DWT->CYCCNT);
#endif
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
#if VECTOR_DBG_CLOCK_PROBE
  static uint32_t prev_sec        = 0;   /* секунды по RTC (LSI) - эталон   */
  static uint32_t prev_cyc        = 0;   /* такты ядра по DWT->CYCCNT       */
  static uint32_t clk_probe_armed = 0;   /* 0 = первый вызов, только замер  */
#endif

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

#if VECTOR_DBG_CLOCK_PROBE
  /* РЕАЛЬНАЯ длительность этого же окна - по RTC (тактируется от LSI и от
     HSE/PLL не зависит вообще) и по счётчику тактов ядра DWT->CYCCNT.

     Зачем это нужно. В логе терминал показывал 20 с на окно, которое прошивка
     считает 10-секундным (tx=+1000 при тике 10 мс). Две возможные причины:

       (а) ядро ПРОСТАИВАЕТ под отладчиком. Live Watch / автообновление
           Expressions в STM32CubeIDE периодически останавливает ядро; при
           остановке SysTick не считает, поэтому tx_time_get() отстаёт от
           настенных часов. Это НЕ ошибка прошивки.
       (б) реальная частота ядра вдвое ниже заявленной (например кварц 8 МГц,
           а SYSTEM_CLOCK в tx_initialize_low_level.S = 160000000). Тогда тик
           действительно 20 мс, и вместе с ним вдвое медленнее идут все паузы
           И звук: SAI берёт 4.096 МГц из PLL3 от того же HSE.

     Как их различить одним взглядом: DWT->CYCCNT считает такты ядра и при
     остановке ядра ТОЖЕ стоит. За окно из `ticks` тиков ядро обязано
     протактовать ровно ticks * (SysTick->LOAD + 1) - это инвариант, он не
     зависит ни от настенного времени, ни от частоты кварца. Поэтому:

       dwt ~= ticks * (LOAD+1)  ->  каждый тик отработан, шкала VTICK верна;
                                    отставание от настенных часов даёт только
                                    простой ядра (случай а).
       dwt заметно больше       ->  прерывания SysTick ТЕРЯЮТСЯ (кто-то держит
                                    CPU или маскирует прерывания) - это баг.

     А частоту кварца проверяют независимо от этих счётчиков: UART4 и USART2
     тактируются от PCLK1 (usart.c: RCC_UART4CLKSOURCE_PCLK1 /
     RCC_USART2CLKSOURCE_PCLK1) на 115200. Если PCLK1 реально вдвое ниже
     расчётного, реальный бод был бы 57600 и вместо лога шли бы кракозябры.
     Читаемый лог = дерево тактирования верное. Вторая проверка на слух:
     d_myvoice длительностью 8.10 с должен играть 8.10 с, а не 16.          */
  {
    uint32_t cyc   = DWT->CYCCNT;
    uint32_t sec   = rtc_seconds();
    uint32_t dsec  = (uint32_t)(sec - prev_sec) % 86400u;
    uint32_t dcyc  = (uint32_t)(cyc - prev_cyc);
    uint32_t ticks = (uint32_t)(VECTOR_LOG_TICK_REPORT_S * TX_TIMER_TICKS_PER_SECOND);
    uint32_t want  = ticks * ((uint32_t)SysTick->LOAD + 1u);
    uint32_t run_pct = 0;

    prev_sec = sec;
    prev_cyc = cyc;

    if (clk_probe_armed != 0u)
    {
      /* доля времени, которую ядро действительно протактовало (0..100).
         100 - ядро не останавливалось и частота ядра равна SystemCoreClock.  */
      if ((dsec != 0u) && (SystemCoreClock != 0u))
      {
        run_pct = (uint32_t)(((uint64_t)dcyc * 100u) /
                             ((uint64_t)dsec * (uint64_t)SystemCoreClock));
      }

      LOG_I(VLOG_M_SYS, "clk meas: wall(RTC/LSI)=+%u s for a %u s tick window, core ran %u%% of wall",
            dsec, (uint32_t)VECTOR_LOG_TICK_REPORT_S, run_pct);
      LOG_I(VLOG_M_SYS, "clk meas: dwt=+%u Mcyc, must be %u Mcyc (%u ticks x load %u), core=%u MHz",
            dcyc / 1000000u, want / 1000000u, ticks,
            (uint32_t)SysTick->LOAD, (uint32_t)(SystemCoreClock / 1000000u));

      if (dcyc > (want + (want / 50u)))
      {
        /* тактов ядра прошло БОЛЬШЕ, чем нужно на отсчитанные тики: часть
           прерываний SysTick не обслужена. Это уже не простой под отладчиком. */
        LOG_E(VLOG_M_SYS, "SYSTICK LOST: %u Mcyc elapsed for %u ticks (need %u Mcyc)",
              dcyc / 1000000u, ticks, want / 1000000u);
      }
      else if (run_pct < 90u)
      {
        LOG_W(VLOG_M_SYS,
              "core idle %u%% of wall time: debugger halts the core (Live Watch / "
              "Expressions refresh) OR real SYSCLK < %u MHz. Readable UART text at "
              "115200 (PCLK1) means the clock tree is fine -> it is HALTS, not a "
              "wrong crystal. Verify by ear: an 8.10 s sound must last 8.10 s.",
              (uint32_t)(100u - run_pct), (uint32_t)(SystemCoreClock / 1000000u));
      }
      else
      {
        /* ядро работало всё окно, тактов ровно сколько надо - время в порядке */
      }
    }
    else
    {
      /* первый вызов только запоминает точку отсчёта: prev_* были нулями */
      clk_probe_armed = 1u;
    }
  }
#endif /* VECTOR_DBG_CLOCK_PROBE */

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
