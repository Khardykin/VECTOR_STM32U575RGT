/**
  ******************************************************************************
  * @file    vector_sys.h
  * @brief   Системные приборы: состояние тайм-базы HAL и детектор перегрузки
  *          прерываниями
  *
  *          ЗАЧЕМ ВЫНЕСЕНО СЮДА: всё это раньше лежало в main.c (USER CODE) и
  *          app_azure_rtos.c. Конфигурацию периферии должен делать CubeMX, а
  *          сгенерированные файлы - оставаться пустыми, поэтому в main.c от
  *          модуля осталось ровно две строки вызова.
  *
  *          ПОДКЛЮЧЕНИЕ (обе точки - USER CODE-секции, перегенерация их не
  *          трогает):
  *            main.c, USER CODE BEGIN 2:            vector_sys_init();
  *            main.c, USER CODE BEGIN Callback 1:   vector_sys_tick_hook(htim);
  *            app_azure_rtos.c, поток:              vector_sys_tick_report();
  ******************************************************************************
  */
#ifndef VECTOR_SYS_H
#define VECTOR_SYS_H

#include <stdint.h>
#include "stm32u5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Однократно из main() (USER CODE 2), до или после инициализации периферии -
   не важно. Печатает в лог конфигурацию времени и (VECTOR_DBG_FREEZE_TICK)
   разрешает остановку тайм-базы вместе с ядром под отладчиком.               */
void vector_sys_init(void);

/* Из HAL_TIM_PeriodElapsedCallback (USER CODE Callback 1). КОНТЕКСТ: ISR,
   поэтому только чтение регистров и инкременты.                              */
void vector_sys_tick_hook(TIM_HandleTypeDef *htim);

/* Периодический отчёт в лог. Звать из потока (например, из lvgl_thread_entry
   раз в 100 мс) - сам решает, пора ли печатать. Интервал отсчитывается по
   тику RTOS, а не по HAL: если тики HAL теряются, HAL-интервал не наступит
   никогда.                                                                   */
void vector_sys_tick_report(void);

/* --- счётчики (Expressions / Live Expressions) ----------------------------
 * Норма: sys_dbg_hal_tick растёт на 1000 за секунду РЕАЛЬНОГО времени,
 * sys_dbg_cb_all - столько же. Если hal отстаёт - прерывания тайм-базы
 * теряются (приоритет TIM6_IRQn = 15, самый низкий; ядро стоит под
 * отладчиком; или есть шквал прерываний 0..1).                                */
extern volatile uint32_t sys_dbg_cb_all;     /* вызовов колбэка таймера (от любого) */
extern volatile uint32_t sys_dbg_hal_tick;   /* HAL_GetTick() на момент замера      */
extern volatile uint32_t sys_dbg_tim_psc;    /* фактический PSC тайм-базы           */
extern volatile uint32_t sys_dbg_tim_arr;    /* фактический ARR тайм-базы           */
extern volatile uint32_t sys_dbg_reports;    /* сколько отчётов напечатано          */

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_SYS_H */
