/**
  ******************************************************************************
  * @file    Menu_vector.h
  * @brief   ОСНОВА слоя меню (v26): машина экранов + жесты кнопок (по образцу
  *          Button_1/2/3_Menu и DisplayIndication из Avis_Menu_2.c, docs/)
  *
  *          Какие экраны и статусы будут - ЕЩЁ НЕ ИЗВЕСТНО (автор), поэтому
  *          здесь только КАРКАС:
  *            screen_t   - набор экранов ПО ИМЕЮЩИМСЯ экспортам SquareLine
  *                         (Start/HeadMain/Measurements/Menu/TestSound);
  *                         новые экраны добавляются в enum + в таблицу
  *                         menu_screens[] в Menu_vector.c;
  *            gesture_t  - жесты кнопок (SHORT/LONG/DOUBLE/SHORT_LONG/FAKE),
  *                         декодируются из button_count (счёт в TIM3 1 кГц,
  *                         ставит EXTI) - логика один в один Avis_Menu_2.c;
  *            menu_status - ЕДИНАЯ volatile-структура состояния (стиль
  *                         проекта: в Expressions одна строка);
  *            Menu_Draw()          - WEAK-хук: отрисовку подставит LVGL-порт
  *                         (этап 2c), каркас от графики не зависит.
  *                         Хука «входа на экран» НЕТ (v27, замечание автора
  *                         п.1): как в Avis DisplayIndication/LCD_Fill_Template,
  *                         экран перерисовывается ЦЕЛИКОМ каждый цикл, а
  *                         lv_screen_load порт делает внутри Menu_Draw() при
  *                         смене menu_status.screen.
  *
  *          Роли кнопок (как в Avis): Button1 (PB1) - вход/подтверждение/назад,
  *          Button2 (PB2) - вниз/инкремент, Button3 (PB3) - вверх/декремент.
  *          Любой жест перезапускает TIMER_RTC_MAIN_RESET - авто-возврат на
  *          SCREEN_MAIN; по возврату dirty_data -> SaveConfig = 1 (Avis:
  *          Button_Menu()). Вызовы (v27, замечание автора п.5 - по образцу
  *          indication_task_function из Avis_main.c): Menu_Init() - из
  *          Vector_Run_Pre_Init(); Vector_Run_Indication() - из потока LVGL
  *          Task (app_azure_rtos.c) по timer.flag_100ms, внутри по device_turn
  *          вызывает Menu_Run(). В Measure Task меню больше не живёт.
  *          Приоритет жестов ПОСТОЯННЫЙ (v27, замечание автора п.3):
  *          Button1 > Button2 > Button3, внутри кнопки LONG > SHORT.
  ******************************************************************************
  */
#ifndef MENU_VECTOR_H
#define MENU_VECTOR_H

#include <stdint.h>
#include "config_device.h"   /* CONFIG_DISPLAY */
#include "vector_config.h"   /* VECTOR_MENU_* */

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_DISPLAY

/* Экраны = существующие экспорты SquareLine (Drivers/ui_Vector/screens).
   Соответствие «экран <-> статусы прибора» определит автор - enum и таблица
   menu_screens[] расширяются в одном месте.                                 */
typedef enum
{
  SCREEN_START = 0,        /* ui_ScreenStart        - заставка при включении  */
  SCREEN_MAIN,             /* ui_ScreenHeadMain     - главный (Avis: MEASURE) */
  SCREEN_MEASUREMENTS,     /* ui_ScreenMeasurements - измерения               */
  SCREEN_MENU,             /* ui_ScreenMenu         - меню (скролл пунктов)   */
  SCREEN_TEST_SOUND,       /* ui_ScreenTestSound    - тест звука              */
  SCREEN_COUNT
} screen_t;

/* Жесты кнопок (Avis: STATE_BUTTON_AVIS). */
typedef enum
{
  GEST_NONE = 0,
  GEST_SHORT,              /* короткое нажатие                                */
  GEST_LONG,               /* удержание >= VECTOR_MENU_PRESS_LONG_MS          */
  GEST_DOUBLE,             /* два коротких подряд (< VECTOR_MENU_DOUBLE_MS)   */
  GEST_SHORT_LONG,         /* короткое, затем длинное в окне DOUBLE           */
  GEST_FAKE                /* короче антидребезга                             */
} gesture_t;

/* ЕДИНОЕ состояние меню (в Expressions - одна строка `menu_status`).        */
typedef struct
{
  volatile uint8_t  screen;             /* screen_t текущий экран            */
  volatile uint8_t  screen_prev;        /* предыдущий (для «назад»)          */
  volatile uint8_t  state_menu;         /* позиция скролла в SCREEN_MENU     */
  volatile uint8_t  state_submenu;      /* позиция внутри пункта (резерв)    */
  volatile uint8_t  dirty_data;         /* конфиг меняли -> SaveConfig=1 при
                                           возврате на SCREEN_MAIN (Avis)    */
  volatile uint8_t  gestures[3];        /* gesture_t кнопок 1..3: ставит
                                           Menu_Run(), снимает Menu_Gesture()*/
  volatile uint32_t cnt_gestures[3];    /* всего жестов по кнопкам (отладка) */
  volatile uint32_t last_gesture_ms;    /* метка последнего жеста            */
  volatile uint32_t screen_since_ms;    /* когда вошли на текущий экран      */
  volatile uint32_t cnt_screens;        /* всего переходов                   */
} menu_status_t;

extern volatile menu_status_t menu_status;

/* Инициализация: SCREEN_START, сброс жестов/таймеров. КОНТЕКСТ: поток
   Measure Task (из Vector_Run_Pre_Init, один раз).                          */
void Menu_Init(void);

/* Цикл меню: декодирование жестов кнопок 1..3, навигация по screen_t,
   авто-возврат на SCREEN_MAIN по TIMER_RTC_MAIN_RESET, dirty_data ->
   SaveConfig, вызов Menu_Draw(). КОНТЕКСТ: поток Measure Task, каждый
   проход (50 мс). НЕ блокируется.                                           */
void Menu_Run(void);

/* Перейти на экран (счётчики + лог; отрисовка - следующий цикл Menu_Draw).
   КОНТЕКСТ: поток LVGL Task (indication).                                   */
void Menu_GoTo(uint8_t screen);

/* Забрать готовый жест кнопки (1..3): возвращает gesture_t и гасит его.
   Для внешних потребителей (например, тест звука/отключение по LONG).
   КОНТЕКСТ: поток.                                                          */
uint8_t Menu_Gesture(uint8_t button);

/* WEAK-хуки (заменит LVGL-порт / слой индикации):
   Menu_Draw()        - аналог DisplayIndication/LCD_Fill_Template: обновить
                        виджеты текущего экрана ЦЕЛИКОМ каждый цикл (при смене
                        screen порт делает lv_screen_load внутри Menu_Draw).
   Vector_Run_Indication() - аналог Avis_Run_Indication: switch(device_turn),
                        ON/ON_1 -> Menu_Run(); вызывается из LVGL Task по
                        timer.flag_100ms (100 мс).                          */
void Menu_Draw(void);
void Vector_Run_Indication(void);

#endif /* CONFIG_DISPLAY */

#ifdef __cplusplus
}
#endif

#endif /* MENU_VECTOR_H */
