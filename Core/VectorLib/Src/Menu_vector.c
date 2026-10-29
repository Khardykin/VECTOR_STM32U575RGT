/**
  ******************************************************************************
  * @file    Menu_vector.c
  * @brief ОСНОВА слоя меню: машина экранов + декодер жестов кнопок
  *
  *          Логика кнопок перенесена из Button_1/2/3_Menu (docs/Avis_Menu_2.c)
  *          один-в-один по смыслу, но БЕЗ блокирующих ожиданий и без графики:
  *            1) декодер жеста из button_count/button_flag (EXTI ставит флаг и
  *               обнуляет счёт, Timer_Tick_1ms() на TIM3 1 кГц инкрементирует -
  *               единица = 1 мс); LONG срабатывает ДО отпускания (как в Avis);
  *            2) любой жест перезапускает TIMER_RTC_MAIN_RESET - авто-возврат
  *               на SCREEN_MAIN;
  *            3) switch(menu_status.screen) - навигация: Button1 вход/назад,
  *               Button2 вниз/инкремент, Button3 вверх/декремент; состав
  *               экранов и пунктов определит автор - пока каркас на имеющихся
  *               экранах SquareLine (см. menu_screens[]);
  *            4) Menu_Draw() - WEAK аналог DisplayIndication/LCD_Fill_Template:
  *               LVGL-порт подставит обновление виджетов; сам каркас графики
  *               не требует. Хука «входа на экран» НЕТ (замечание автора
  *               п.1): в Avis LCD_Fill_Template перерисовывает шаблон КАЖДЫЙ
  *               цикл, поэтому screen load/тексты порт сделает внутри
  *               Menu_Draw() при смене menu_status.screen.
  *
  *          КОНТЕКСТ (замечание автора п.5 - по образцу
  *          indication_task_function из Avis_main.c): Menu_Init() - Measure
  *          Task (Vector_Run_Pre_Init, один раз); Vector_Run_Indication() -
  *          поток LVGL Task (app_azure_rtos.c) по timer.flag_100ms (100 мс),
  *          внутри по device_turn -> Menu_Run(). В Measure Task меню НЕ живёт.
  *          Из ISR не вызывать (тут таймеры прибора и SaveConfig).
  *
  *           (замечание автора п.3): приоритет жестов ПОСТОЯННЫЙ -
  *          Button1 > Button2 > Button3 (внутри кнопки LONG > SHORT >
  *          DOUBLE), единая цепочка else-if на экран начинается с g1.
  ******************************************************************************
  */
#include "Vector_main.h"     /* агрегатор: таймеры прибора, SaveConfig, лог */
#include "stm32u5xx_it.h"    /* button1/button2/button3 (Button_variables) */
#include "Menu_vector.h"
#include "vector_tick.h"     /* GetTick через vector_macros.h */
#include "vector_macros.h"
#include "vector_log.h"
#include "TFT.h"             /* LCD_Init() -> TFT_Init() (Button_Run_Start) */
#include "LCD_platform.h"    /* backlight_set() (Button_Run_Backlight)      */
#include "Notify_vector.h"   /* оповещение включения: NotifySeq_PowerOn     */

#if CONFIG_DISPLAY

time_t  tmp_current = 0; // Время текущее

volatile menu_status_t menu_status = {0};

/* Тайминги кнопок (мс; button_count считает TIM3 1 кГц = мс). */
#define BUTTON_PRESS_SHORT  ((uint32_t)VECTOR_MENU_PRESS_SHORT_MS)
#define BUTTON_PRESS_LONG   ((uint32_t)VECTOR_MENU_PRESS_LONG_MS)
#define TIME_DOUBLE_CLICK   ((uint32_t)VECTOR_MENU_DOUBLE_MS)

/* Скролл пунктов меню: заглушка на VECTOR_MENU_ITEMS пунктов. Когда автор
   определит состав меню - заменить длину на фактическую для каждого экрана
   (в Avis: scroll_down(state_menu, 4) / (state_submenu, info_total_pages)). */
#define MENU_ITEMS_COUNT    ((uint8_t)VECTOR_MENU_ITEMS)

/* Последний момент короткого нажатия по каждой кнопке (для DOUBLE/SHORT_LONG).
   В Avis это button.last_short_click_time - поле структуры кнопки; у нас
   Button_variables без него, поэтому держим здесь (владелец - Menu_Run).    */
static uint32_t last_short_click_ms[3] = {0u, 0u, 0u};

/* ============================================================================
 *                              служебные
 * ============================================================================ */

/* Циклический скролл вниз/вверх (аналог scroll_down/scroll_up из Avis). */
static uint8_t scroll_down(uint8_t current, uint8_t max)
{
  if (max == 0u) { return 0u; }
  return (uint8_t)((current + 1u) % max);
}

static uint8_t scroll_up(uint8_t current, uint8_t max)
{
  if (max == 0u) { return 0u; }
  return (uint8_t)((current == 0u) ? (max - 1u) : (current - 1u));
}

/* WEAK-хуки: каркас работает и без них (пустые), LVGL-порт/слой индикации
   замещают их своей реализацией без правки этого файла.                     */
void Menu_Draw(void)
{
  /* TODO(LVGL-порт, этап 2c): аналог DisplayIndication - обновить виджеты
     текущего экрана: значения из sensors_status (T/H/P, батарея, ориентация),
     иконки статусов (SET_MUTE/BLE/GPS/LORA/LTE/EXCHANGE из ST_COMMON_BIT_*),
     время (Working_data.working_hours), Connected_device (данные Avis по BLE).
     Рисовать из LVGL Task или отсюда - решит порт (владелец дисплея один).  */
	tmp_current = (time_t)(Sns_Cfg_struct.Working_data.working_hours +
			Sns_Cfg_struct.Working_data.working_hours_offset);
}

/* ============================================================================
 *                          переходы между экранами
 * ============================================================================ */

/* Перейти на экран: только состояние/счётчики/лог. Отрисовку (включая
   lv_screen_load при смене screen) делает Menu_Draw() в следующем цикле -
   как в Avis, где DisplayIndication/LCD_Fill_Template перерисовывает всё
   каждый проход (замечание автора п.1). КОНТЕКСТ: поток LVGL Task.    */
void Menu_GoTo(uint8_t screen)
{
  if (screen >= (uint8_t)SCREEN_COUNT)
  {
    return;
  }
  if (menu_status.screen == screen)
  {
    return;
  }
  menu_status.screen_prev     = menu_status.screen;
  menu_status.screen          = screen;
  menu_status.state_submenu   = 0u;
  menu_status.screen_since_ms = VTICK_MS();
  menu_status.cnt_screens++;

  LOG_D(VLOG_M_SYS, "menu: screen %u -> %u",
        (uint32_t)menu_status.screen_prev, (uint32_t)screen);

  Menu_Draw();
}

/* Забрать готовый жест кнопки (1..3). Возврат: gesture_t, GEST_NONE если
   жеста нет; прочитанный жест гасится. КОНТЕКСТ: поток.                    */
uint8_t Menu_Gesture(uint8_t button)
{
  uint8_t g;

  if ((button < 1u) || (button > 3u))
  {
    return (uint8_t)GEST_NONE;
  }
  g = menu_status.gestures[button - 1u];
  menu_status.gestures[button - 1u] = (uint8_t)GEST_NONE;
  return g;
}

/* Положить жест (если слот занят прежним жестом - новый побеждает: события
   редкие, а протухший жест хуже потерянного).                              */
static void set_gesture(uint8_t idx, uint8_t gest)
{
  menu_status.gestures[idx] = gest;
  menu_status.cnt_gestures[idx]++;
  menu_status.last_gesture_ms = VTICK_MS();
}

/* ============================================================================
 *                      ДЕКОДЕР ЖЕСТА ОДНОЙ КНОПКИ
 * Пороги - как в Avis Button_1_Menu(): count >= BUTTON_PRESS_LONG -> LONG
 * (срабатывает ДО отпускания, button_flag гасится, чтобы не повторился);
 * count > BUTTON_PRESS_SHORT и кнопка отпущена (!button_flag) -> SHORT, а
 * если от предыдущего SHORT прошло меньше TIME_DOUBLE_CLICK -> DOUBLE.
 * ============================================================================ */
static void decode_button(uint8_t idx, volatile Button_variables *pb)
{
  uint32_t now = GetTick();
  uint8_t  gest = (uint8_t)GEST_NONE;

  if (pb->button_count >= BUTTON_PRESS_LONG)
  {
    /* длинное удержание - ещё до отпускания */
    if ((last_short_click_ms[idx] != 0u) &&
        ((now - last_short_click_ms[idx]) < TIME_DOUBLE_CLICK))
    {
      gest = (uint8_t)GEST_SHORT_LONG;
    }
    else
    {
      gest = (uint8_t)GEST_LONG;
    }
    pb->button_flag          = 0u;
    pb->button_count         = 0u;
    last_short_click_ms[idx] = 0u;
  }
  else if (pb->button_count > BUTTON_PRESS_SHORT)
  {
    if (pb->button_flag == 0u)     /* отпущена - жест завершён */
    {
      if ((last_short_click_ms[idx] != 0u) &&
          ((now - last_short_click_ms[idx]) < TIME_DOUBLE_CLICK))
      {
        gest = (uint8_t)GEST_DOUBLE;
        last_short_click_ms[idx] = 0u;
      }
      else
      {
        gest = (uint8_t)GEST_SHORT;
        last_short_click_ms[idx] = now;
      }
      pb->button_count = 0u;
    }
  }
  else if ((pb->button_count > 0u) && (pb->button_flag == 0u))
  {
    gest = (uint8_t)GEST_FAKE;     /* короче антидребезга */
    pb->button_count = 0u;
  }

  if (gest != (uint8_t)GEST_NONE)
  {
    set_gesture(idx, gest);
    /* любой жест перезапускает таймер возврата на главный экран (Avis) */
    START_TIMER_RTC(TIMER_RTC_MAIN_RESET, VECTOR_MENU_AUTO_RETURN_S);
  }
}

/* ============================================================================
 *                        НАВИГАЦИЯ ПО ЭКРАНАМ
 * Состав пунктов/статусов определит автор - пока каркас: на каждом экране
 * обрабатываются жесты, которых достаточно для обхода имеющихся экранов.
 * ============================================================================ */
static void menu_navigate(void)
{
  uint8_t g1 = Menu_Gesture(1u);
  uint8_t g2 = Menu_Gesture(2u);
  uint8_t g3 = Menu_Gesture(3u);


  if (g2 == (uint8_t)GEST_LONG)
  {
    /* TODO(автор): отключение прибора (device_turn = DEVICE_TURNED_OFF,
       событие TYPE_TURN_OFF в журнал, счётчик обратного отсчёта на
       экране). Здесь намеренно ничего не делаем.                        */
    LOG_D(VLOG_M_SYS, "menu: MAIN long press (turn-off not implemented)");
  }
  /* ПРИОРИТЕТ ЖЕСТОВ ПОСТОЯННЫЙ (замечание автора п.3): на каждом
     экране цепочка else-if ВСЕГДА начинается с Button1 (вход/назад - важнее
     скролла), затем Button2 (вниз), затем Button3 (вверх); внутри кнопки
     LONG важнее SHORT. Так g1 не может «стать последним», как ранее.       */
  switch (menu_status.screen)
  {
    /* Заставка: любое короткое нажатие - на главный. */
    case SCREEN_START:

      break;
    /* Главный экран (Avis: STATE_SCREEN_MEASURE).
       LONG Button1 в Avis - отключение прибора с обратным отсчётом: у нас
       обработчика выключения ещё нет (PLAN этап 6, TYPE_TURN_OFF) - оставлен
       хук-заглушка, чтобы не потерять жест.                                 */
    case SCREEN_MEASURE:
	    const uint8_t setting_screens[] = {
	    		SCREEN_MEASURE,
				SCREEN_MENU,
				SCREEN_VOICE_MESSAGE,
				SCREEN_TEXT_MESSAGE,
	    };

	   uint8_t sub = 0;
      if (g2 == (uint8_t)GEST_SHORT)
      {
    	  menu_status.state_menu = scroll_down(menu_status.state_menu, 6);
      }
      else if (g3 == (uint8_t)GEST_SHORT)
      {
    	  menu_status.state_menu = scroll_up(menu_status.state_menu, 6);
      }
      else if (g3 == (uint8_t)GEST_LONG){
    	  if(menu_status.state_menu == 5){
    		  sub = 2;
    	  }
    	  else if(menu_status.state_menu == 6){
    		  sub = 3;
    	  }
      }
      else if (g3 == (uint8_t)GEST_DOUBLE)
      {
    	  sub = 1;
    	  menu_status.state_menu = 0;
      }

      Menu_GoTo(setting_screens[sub]);
      break;

    case SCREEN_MENU:
      if (g2 == (uint8_t)GEST_SHORT)
      {
        menu_status.state_menu = scroll_down(menu_status.state_menu, MENU_ITEMS_COUNT);
      }
      else if (g3 == (uint8_t)GEST_SHORT)
      {
        menu_status.state_menu = scroll_up(menu_status.state_menu, MENU_ITEMS_COUNT);
      }
      else if (g2 == (uint8_t)GEST_LONG){
    	  Menu_GoTo((uint8_t)SCREEN_MEASURE);
      }
      break;

    default:
      Menu_GoTo((uint8_t)SCREEN_MEASURE);
      break;
  }

  /* Непрошенные жесты гасим (иначе зависнут в слоте и выстрелят позже). */
  (void)g1; (void)g2; (void)g3;
}

/* ============================================================================
 *                            ВНЕШНИЕ ТОЧКИ
 * ============================================================================ */

/* Инициализация меню. КОНТЕКСТ: Measure Task (из Vector_Run_Pre_Init).     */
void Menu_Init(void)
{
  uint8_t i;

  Notify_Init();   /* секвенсор оповещений (Notify_vector): до первого Tick */

  menu_status.screen          = (uint8_t)SCREEN_START;
  menu_status.screen_prev     = (uint8_t)SCREEN_START;
  menu_status.state_menu      = 0u;
  menu_status.state_submenu   = 0u;
  menu_status.dirty_data      = 0u;
  menu_status.last_gesture_ms = 0u;
  menu_status.screen_since_ms = VTICK_MS();
  for (i = 0; i < 3u; i++)
  {
    menu_status.gestures[i]     = (uint8_t)GEST_NONE;
    last_short_click_ms[i]      = 0u;
  }

  /* Авто-возврат на главный экран: как в Avis (TIMER_RTC_MAIN_RESET). */
  START_TIMER_RTC(TIMER_RTC_MAIN_RESET, VECTOR_MENU_AUTO_RETURN_S);

  LOG_I(VLOG_M_SYS, "menu: init (screen START, auto-return %u s)",
        (uint32_t)VECTOR_MENU_AUTO_RETURN_S);
}

/* Цикл меню (вызывается из Vector_Run_Indication(), поток LVGL Task,
   по timer.flag_100ms - 100 мс): жесты -> навигация -> авто-возврат ->
   dirty_data -> отрисовка. НЕ блокируется.                                 */
void Menu_Run(void)
{
  decode_button(0u, &button1);
  decode_button(1u, &button2);
  decode_button(2u, &button3);

  menu_navigate();

  /* Авто-возврат на главный экран по истечении TIMER_RTC_MAIN_RESET. */
  if (TEST_TIMER_RTC(TIMER_RTC_MAIN_RESET) != 0)
  {
    RESET_TIMER_RTC(TIMER_RTC_MAIN_RESET);
    if (menu_status.screen != (uint8_t)SCREEN_MEASURE)
    {
      LOG_D(VLOG_M_SYS, "menu: auto-return to SCREEN_MEASURE");
      Menu_GoTo((uint8_t)SCREEN_MEASURE);
    }
  }

  /* Avis Button_Menu(): при возврате на главный экран - сохранить конфиг,
     если в меню что-то меняли.                                              */
  if ((menu_status.screen == (uint8_t)SCREEN_MEASURE) && (menu_status.dirty_data != 0u))
  {
    menu_status.dirty_data = 0u;
    SaveConfig = 1u;
    LOG_I(VLOG_M_SYS, "menu: dirty data -> SaveConfig");
  }

  Menu_Draw();
}


//===========================================================================================================================
// Для регуляции подстветки, запуск таймера
void Button_Run_Backlight(void)
{
#if (CONFIG_TYPE_LCD_TFT == 0)
	  if(TEST_TIMER_RTC(TIMER_RTC_LIGHT_RESET)){
		RESET_TIMER_RTC(TIMER_RTC_LIGHT_RESET);
		button1.button_pressed = 0;
	  }
#else
//    backlight_set(Sns_Cfg_struct.Config_common.SoundPWM);
    if((button1.button_pressed) || (button2.button_pressed) || (button3.button_pressed)){
    	button1.button_pressed = 0;
    	button2.button_pressed = 0;
    	button3.button_pressed = 0;
    	backlight_set(40);
    	START_TIMER_RTC(TIMER_RTC_LIGHT_RESET, TIME_RTC_LIGHT_RESET*12);
    }
    if(TEST_TIMER_RTC(TIMER_RTC_LIGHT_RESET)){
    	RESET_TIMER_RTC(TIMER_RTC_LIGHT_RESET);
    	backlight_set(5);
    }
#endif
}
//===========================================================================================================================
//Обработка нажатия кнопки, при включение
//#define VIBRO_OFF() tmr_channel_enable(TMR4, TMR_SELECT_CHANNEL_1, FALSE);
//#define VIBRO_ON()  tmr_channel_enable(TMR4, TMR_SELECT_CHANNEL_1, TRUE);
//#define VIBRO_ST()	(TMR4->cctrl_bit.c1en)
void Button_Run_Start(void)
{
	Button_Run_Backlight();
	if(button1.button_count >= BUTTON_PRESS_LONG){
		//------------------------------------------------------------------------
//		SET_ON(POWER_ON_MCU);
		//------------------------------------------------------------------------
		// Инициализация LCD
	    LCD_Init();
		//------------------------------------------------------------------------
#if CONFIG_LORA
    Lora_DeInit(0);
#endif
#if CONFIG_LORA_G
    Lora_g_DeInit(0);
#endif
#if CONFIG_GPS
    Gps_DeInit(0);
#endif
		//------------------------------------------------------------------------
		// Индикация (замечание автора): НЕБЛОКИРУЮЩЕ - через секвенсор
		// Notify_vector: 3 писка ИЗ ПАМЯТИ МК (audio_beep - клип 240 мс во
		// внутренней flash, не зависит от внешней SPI-flash), пауза 300 мс.
		// Delay() здесь нельзя: фазы ведёт Notify_Tick() (~10 мс) из потока
		// LVGL Task. Будущие тревоги - так же: const-таблица фаз "свет ->
		// звук -> вибро" в любом порядке (примеры в комментариях Notify_vector.c),
		// голосовое сообщение - одна фаза NOTIFY_CH_SOUND с SND_* из образа.
		Notify_Start(&NotifySeq_PowerOn);
	    //------------------------------------------------------------------------
		button1.button_count = 1;
		button1.button_flag = 0;

	    device_turn = DEVICE_TURNED_ON_1;
	    //вход в обычный режим работы!!!!!!!!!!!
	    START_TIMER_RTC(TIMER_RTC_LIGHT_RESET, TIME_RTC_LIGHT_RESET*12);
	    CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_OFF)
	    //------------------------------------------------------------------------
#if CONFIG_SAVE_PARAM_LOG
	    log_post(TYPE_TURN_ON);
#endif
	}
	else if(button1.button_flag == 0){
		button1.button_count = 0;
	}
}

/* Аналог Avis_Run_Indication (Avis_main.c) - вызывается из потока LVGL Task
   (app_azure_rtos.c) по timer.flag_100ms. КОНТЕКСТ: поток LVGL Task.
   device_turn: ON/ON_1 -> Menu_Run() (кнопки/меню/отрисовка); OFF и
   OFF_PRE_INIT -> ничего (в Avis при OFF здесь DisplayChargeIndicationTurnOff
   + Button_Run_Start - экран заряда при выключенном приборе, появится с
   продуктовой кнопкой включения, PLAN этап 6). PRE_INIT сюда не попадает:
   LVGL Task (когда будет создан) ждёт завершения Vector_Run_Pre_Init
   (device_turn/cnt_pre_init) - как receiver в Vector_main.c (PLAN п.60).     */
void Vector_Run_Indication(void)
{
  switch (device_turn)
  {
    case DEVICE_TURNED_ON:
    case DEVICE_TURNED_ON_1:      /* включён без звука/вибро (прогрев) */
      Menu_Run();
      break;
    case DEVICE_TURNED_OFF:
      /* TODO(автор): индикация заряда при выключенном приборе (Avis:
         DisplayChargeIndicationTurnOff) и обработка кнопки включения.   */
    	Button_Run_Start();
      break;
    case DEVICE_TURNED_OFF_PRE_INIT:
      /* старт: потоки ещё не допущены в рабочий режим (PLAN п.60) - сюда
         вызов в норме не попадает, ветка - страховка.                     */
      break;
    default:
      break;
  }
}

#endif /* CONFIG_DISPLAY */
