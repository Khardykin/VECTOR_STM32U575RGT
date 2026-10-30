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
  *            4) Menu_Draw() - аналог DisplayIndication из Avis (не WEAK с
  *               коммита автора «up menu tft»): ЦЕЛИКОМ перерисовывает экран
  *               каждый проход - время, LCD_Fill_Template (поворот/язык/смена
  *               экрана), верхняя панель (LCD_Battery с фазой мигания),
  *               покранный switch (LCD_Menu/LCD_SubMenuInfo/LCD_SubMenuSetting),
  *               ui_apply_visibility. Хука «входа на экран» НЕ нужна
  *               (замечание автора п.1) - шаблон грузится внутри
  *               LCD_Fill_Template при смене menu_status.screen.
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
/* Инклуды проекта - В ОДНОМ МЕСТЕ (Vector_main.h): кнопки button1..3
   (stm32u5xx_it.h), время VTICK_ и GetTick (vector_tick.h/vector_macros.h),
   лог, Notify_vector.h (NotifySeq_PowerOn), экран LCD_Init/backlight_set/
   LCD_* (TFT.h/LCD_platform.h/TFT_indicator.h при CONFIG_TYPE_LCD_TFT).   */
#include "Vector_main.h"

#if CONFIG_DISPLAY

time_t  tmp_current = 0; // Время текущее

volatile menu_status_t menu_status = {0};

/* Фаза мигания индикации (Avis state_flashing_system): Menu_Draw инвертирует
   её раз в секунду по TIMER_RTC_DISP_FLASHING_CHARGE. Сейчас фазу ест
   LCD_Battery («зарядка»), дальше - ротация ошибок LCD_UpdateErrorDisplay. */
static uint8_t state_flashing_system = 0;

/* Тайминги кнопок (мс; button_count считает TIM3 1 кГц = мс). */
#define BUTTON_PRESS_SHORT  ((uint32_t)VECTOR_MENU_PRESS_SHORT_MS)
#define BUTTON_PRESS_LONG   ((uint32_t)VECTOR_MENU_PRESS_LONG_MS)
#define TIME_DOUBLE_CLICK   ((uint32_t)VECTOR_MENU_DOUBLE_MS)

/* Скролл пунктов меню: заглушка на VECTOR_MENU_ITEMS пунктов. Когда автор
   определит состав меню - заменить длину на фактическую для каждого экрана
   (в Avis: scroll_down(state_menu, 4) / (state_submenu, info_total_pages)). */
#define MENU_ITEMS_COUNT    ((uint8_t)VECTOR_MENU_ITEMS)

/* Страницы экрана измерений: сколько их и что на каждой - решает слой индикации
   (таблица meas_pages[] в TFT_indicator.c, отрисовка LCD_MeasureScreen). Число
   страниц = VECTOR_MEASURE_PAGES (vector_config.h): оно же - предел скролла
   state_menu в menu_navigate() и размер meas_pages[] (совпадение проверяет
   статический ассерт там же). Без экрана листать нечего (как MENU_INFO_PAGES). */
#if (CONFIG_TYPE_LCD_TFT != 0)
#define MEASURE_PAGES_COUNT ((uint8_t)VECTOR_MEASURE_PAGES)
#else
#define MEASURE_PAGES_COUNT (1u)
#endif

/* Число страниц/строк подэкранов меню определяет слой индикации
   (TFT_indicator): info_total_pages считает LCD_SubMenuInfo по числу строк,
   строки настроек - Panel1..3 (LCD_SubMenuSetting). Без экрана листать нечего. */
#if (CONFIG_TYPE_LCD_TFT != 0)
#define MENU_INFO_PAGES     (info_total_pages)
#define MENU_SETTING_ROWS   (3u)
#else
#define MENU_INFO_PAGES     (1u)
#define MENU_SETTING_ROWS   (1u)
#endif

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

/* Аналог DisplayIndication из Avis: ЦЕЛИКОМ обновляет экран каждый проход.
   КОНТЕКСТ: поток LVGL Task (Menu_Run зовёт каждые 100 мс, Menu_GoTo - сразу
   при переходе) - все вызовы LVGL из одного потока. Шаги как в Avis:
   время -> шаблон (поворот/язык/смена экрана) -> верхняя панель (батарея с
   фазой мигания) -> текущий экран (меню/инфо/настройки) -> видимость иконок.
   Экран измерений (SCREEN_MEASURE) рисует LCD_MeasureScreen() - TFT_indicator.c,
   строку ошибок ротует LCD_UpdateErrorDisplay() (текст - current_error_text,
   его же печатает режим MEAS_ERR). Дальше сюда встанут: иконки статусов
   (SET_GPS/BLE/LORA... по ST_COMMON_BIT_TURN_ON_*) и экраны сообщений (автор). */
void Menu_Draw(void)
{
  /* Текущее время прибора (автор): секунды RTC + сдвиг часового пояса. */
	tmp_current = (time_t)(Sns_Cfg_struct.Working_data.working_hours +
			Sns_Cfg_struct.Working_data.working_hours_offset);

#if (CONFIG_TYPE_LCD_TFT != 0)
  if (LCD_Get_flag_init() == 0u)
  {
    return;    /* панель не инициализирована (прибор выключен) - рисовать не на чем */
  }

  /* Шаблон экрана: поворот по акселерометру, язык, смена экрана по таблице
     scr_map (IS_STATE_SCREEN() - зеркало menu_status.screen из Menu_GoTo). */
  LCD_Fill_Template(&Sns_Cfg_struct);

  /* Фаза мигания: инверсия раз в секунду (RTC-таймер взведён в Menu_Init). */
  if (TEST_TIMER_RTC(TIMER_RTC_DISP_FLASHING_CHARGE))
  {
    RESET_TIMER_RTC(TIMER_RTC_DISP_FLASHING_CHARGE);
    START_TIMER_RTC(TIMER_RTC_DISP_FLASHING_CHARGE, TIME_RTC_DISP_FLASHING_CHARGE);
    state_flashing_system = (uint8_t)!state_flashing_system;
  }
  /* Верхняя панель (top layer): заряд батареи, % и анимация зарядки. */
  LCD_Battery(sensors_status.battery_charge_percent, state_flashing_system);

  /* Строка ошибок (в Avis - в DisplayIndication, каждый проход): хэш изменился
     -> пересборка очереди, фаза мигания -> ротация текста, результат -
     current_error_text[] (его печатает режим MEAS_ERR экрана измерений).
     Дёшево: LCD_CalculateErrorHash() - одно сравнение маски StateErr, очередь
     пересобирается только когда она реально изменилась. ui_next_state.info_str
     функция тоже ставит, но сейчас его никто не читает (ui_apply_visibility()
     работает только с иконками), т.е. на картинку вызов не влияет. Зовём ДО
     отрисовки экрана, чтобы текст ошибки и страница измерений попали в один
     кадр.                                                                 */
  LCD_UpdateErrorDisplay(&Sns_Cfg_struct, state_flashing_system);

  /* Покранная отрисовка (Avis: switch(IS_STATE_SCREEN()) в DisplayIndication). */
  switch (IS_STATE_SCREEN())
  {
    /* Экран измерений: страница state_menu -> режимы обеих панелей и их данные
       (таблицы meas_pages[]/meas_layout[], форматирование и раскладка - в
       TFT_indicator.c). Геометрию LCD_Measure() ставит только при смене
       режима/пересоздании экрана, тексты - через «умные» ui_set_*: кадр не
       инвалидируется, если значение не изменилось.                         */
    case SCREEN_MEASURE:
      LCD_MeasureScreen(menu_status.state_menu);
      break;
    case SCREEN_MENU:
      LCD_Menu(menu_status.state_menu);
      break;
    case SCREEN_INFO_DEVICE:
      LCD_SubMenuInfo(&Sns_Cfg_struct, menu_status.state_submenu);
      break;
    case SCREEN_SETTING:
      LCD_SubMenuSetting(&Sns_Cfg_struct, menu_status.state_submenu);
      break;
    default:   /* START/CONNECT/MESSAGE - экраны автора (ждут отрисовку) */
      break;
  }

  ui_apply_visibility();
  /* Вызов сохранён как в Avis (DisplayIndication), но LCD_UpdateScreen() в этом
     проекте - пустышка ((void)0, см. TFT.h): кадр уезжает на панель из
     lv_timer_handler() потока LVGL Task (my_flush_cb -> TFT_FlushBuffer),
     принудительно «обновлять экран» после отрисовки не нужно.             */
  LCD_UpdateScreen();
#endif /* CONFIG_TYPE_LCD_TFT */
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
  if(screen == SCREEN_MEASURE){
	  menu_status.state_menu = 0;
  }
  /* Зеркало для слоя индикации: LCD_Fill_Template выбирает экран по
     IS_STATE_SCREEN() (Avis: TO_GO_STATE_SCREEN в Button_*_Menu).       */
  TO_GO_STATE_SCREEN(screen);

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
	    if (g2 == (uint8_t)GEST_SHORT){
	    	menu_status.state_menu = scroll_down(menu_status.state_menu, MEASURE_PAGES_COUNT);
	    }
	    else if (g3 == (uint8_t)GEST_SHORT){
	    	menu_status.state_menu = scroll_up(menu_status.state_menu, MEASURE_PAGES_COUNT);
	    }
	    else if (g3 == (uint8_t)GEST_LONG){
    	  if(menu_status.state_menu == 5){
    		  sub = 2;
    	  }
    	  else if(menu_status.state_menu == 6){
    		  sub = 3;
    	  }
		  }
		  else if (g3 == (uint8_t)GEST_DOUBLE){
			  sub = 1;
			  menu_status.state_menu = 0;
		  }

		  Menu_GoTo(setting_screens[sub]);
		  break;

    case SCREEN_MENU:
      /* Button1 SHORT - вход в пункт (приоритет Button1, как в Avis
         Button_1_Menu): пункт списка -> экран. Пункт 3 «Журнал событий» -
         экран ещё не создан (автор), пока остаёмся на месте.            */
		  if (g1 == (uint8_t)GEST_SHORT){
			static const uint8_t item_screens[] = {
				(uint8_t)SCREEN_INFO_DEVICE,     /* 0 - Информация (Panel0) */
				(uint8_t)SCREEN_SETTING,         /* 1 - Настройки  (Panel1) */
				(uint8_t)SCREEN_CONNECT_DEVICE,  /* 2 - Контроль оборудования (Panel2) */
				(uint8_t)SCREEN_MENU,            /* 3 - Журнал событий (Panel3, экран - TODO) */
			};
			uint8_t item = menu_status.state_menu;
			if (item >= (uint8_t)(sizeof(item_screens) / sizeof(item_screens[0]))){
			  item = 0u;
			}
			Menu_GoTo(item_screens[item]);
		  }
		  else if (g2 == (uint8_t)GEST_SHORT){
			menu_status.state_menu = scroll_down(menu_status.state_menu, MENU_ITEMS_COUNT);
		  }
		  else if (g3 == (uint8_t)GEST_SHORT){
			menu_status.state_menu = scroll_up(menu_status.state_menu, MENU_ITEMS_COUNT);
		  }
		  else if (g2 == (uint8_t)GEST_LONG){
			  Menu_GoTo((uint8_t)SCREEN_MEASURE);
		  }
		  break;

    /* Информация о приборе: Button2/3 SHORT - листать страницы
       (info_total_pages считает LCD_SubMenuInfo), Button2 LONG - назад в
       меню (жест-паттерн автора: LONG Button2 = «назад»).               */
    case SCREEN_INFO_DEVICE:
		  if (g2 == (uint8_t)GEST_LONG){
			Menu_GoTo((uint8_t)SCREEN_MENU);
		  }
		  else if (g2 == (uint8_t)GEST_SHORT){
			menu_status.state_submenu = scroll_down(menu_status.state_submenu, MENU_INFO_PAGES);
		  }
		  else if (g3 == (uint8_t)GEST_SHORT){
			menu_status.state_submenu = scroll_up(menu_status.state_submenu, MENU_INFO_PAGES);
		  }
		  break;

    /* Настройки: строки Panel1..3 (LCD_SubMenuSetting), Button2 LONG - назад.
       Вход в строку (Button1 SHORT) - когда автор определит состав настроек. */
    case SCREEN_SETTING:
      if (g2 == (uint8_t)GEST_LONG){
        Menu_GoTo((uint8_t)SCREEN_MENU);
      }
      else if (g2 == (uint8_t)GEST_SHORT){
        menu_status.state_submenu = scroll_down(menu_status.state_submenu, MENU_SETTING_ROWS);
      }
      else if (g3 == (uint8_t)GEST_SHORT){
        menu_status.state_submenu = scroll_up(menu_status.state_submenu, MENU_SETTING_ROWS);
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
  TO_GO_STATE_SCREEN((uint8_t)SCREEN_START);   /* зеркало для LCD_Fill_Template */
  menu_status.state_menu      = 0u;
  menu_status.state_submenu   = 0u;
  menu_status.dirty_data      = 0u;
  menu_status.last_gesture_ms = 0u;
  menu_status.screen_since_ms = VTICK_MS();
  for (i = 0; i < 3u; i++){
    menu_status.gestures[i]     = (uint8_t)GEST_NONE;
    last_short_click_ms[i]      = 0u;
  }

  /* Авто-возврат на главный экран: как в Avis (TIMER_RTC_MAIN_RESET). */
  START_TIMER_RTC(TIMER_RTC_MAIN_RESET, VECTOR_MENU_AUTO_RETURN_S);

  /* Мигание зарядки/ошибок на экране (Avis: TIMER_RTC_DISP_FLASHING_SYSTEM). */
  state_flashing_system = 0u;
  START_TIMER_RTC(TIMER_RTC_DISP_FLASHING_CHARGE, TIME_RTC_DISP_FLASHING_CHARGE);

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
  if (TEST_TIMER_RTC(TIMER_RTC_MAIN_RESET) != 0){
    RESET_TIMER_RTC(TIMER_RTC_MAIN_RESET);
    if (menu_status.screen != (uint8_t)SCREEN_MEASURE){
      LOG_D(VLOG_M_SYS, "menu: auto-return to SCREEN_MEASURE");
      Menu_GoTo((uint8_t)SCREEN_MEASURE);
    }
  }

  /* Avis Button_Menu(): при возврате на главный экран - сохранить конфиг,
     если в меню что-то меняли.                                              */
  if ((menu_status.screen == (uint8_t)SCREEN_MEASURE) && (menu_status.dirty_data != 0u)){
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
		// Инициализация LCD: панель + LVGL/SquareLine UI. Один раз: TFT_Init
		// само-guard'ится флагом TFT_Initialized, а ui_lv_init повторно звать
		// нельзя (lv_init/ui_init пересоздадут объекты). КОНТЕКСТ: поток LVGL
		// Task - тот же поток позже крутит lv_timer_handler (LVGL однопоточен).
#if (CONFIG_TYPE_LCD_TFT != 0)
		if (LCD_Get_flag_init() == 0u){
			LCD_Init();
#if VECTOR_LCD_USE_LVGL
			ui_lv_init();
#endif
		}
#endif
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
