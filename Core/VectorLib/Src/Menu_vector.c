/**
  ******************************************************************************
  * @file    Menu_vector.c
  * @brief ОСНОВА слоя меню: машина экранов + декодер жестов кнопок
  *
  *          Логика кнопок перенесена из Button_1/2/3_Menu (docs/Avis_Menu_2.c)
  *          по смыслу, но БЕЗ блокирующих ожиданий и без графики:
  *            1) декодер жеста из button_count/button_flag (EXTI ставит флаг и
  *               обнуляет счёт, Timer_Tick_1ms() на TIM3 1 кГц инкрементирует -
  *               единица = 1 мс). LONG и SHORT_LONG срабатывают ПО ТАЙМЕРУ, пока
  *               кнопка ещё зажата (по ним стартует обратный отсчёт), после них
  *               отпускание подавляется (long_fired); SHORT выдаётся ОТЛОЖЕННО -
  *               через VECTOR_MENU_DOUBLE_MS после отпускания, если второй клик
  *               не пришёл (двойное не должно давать лишнее одиночное);
  *            2) любой жест перезапускает TIMER_RTC_MAIN_RESET - авто-возврат
  *               на SCREEN_MAIN;
  *            3) switch(menu_status.screen) - навигация: Button1 вход/назад,
  *               Button2 вниз/инкремент, Button3 вверх/декремент; состав
  *               экранов и пунктов определит автор - пока каркас на имеющихся
  *               экранах SquareLine (см. menu_screens[]);
  *            3a) SCREEN_START - заставка/прогрев (указание автора): работает
  *               ТОЛЬКО выключение (LONG Button3 -> Menu_TurnOff, аналог Avis
  *               TurnOffSystem) и SOS (удержание Button1 3 с, button_sos ->
  *               Button_Sos); выход с экрана - по истечении TIMER_RTC_WARM
  *               (TIME_RTC_START, 60 с): device_turn = DEVICE_TURNED_ON и
  *               SCREEN_MEASURE. Авто-возврат на этом экране отключён.
  *               Включение и выключение прибора - ОДНА кнопка Button3:
  *               выключен -> Button_Run_Start (LONG), включён -> Menu_TurnOff;
  *            3b) Button3 SHORT_LONG на SCREEN_MEASURE - вход в SCREEN_MENU;
  *            4) Menu_Draw() - аналог DisplayIndication из Avis (не WEAK с
  *               коммита автора «up menu tft»): ЦЕЛИКОМ перерисовывает экран
  *               каждый проход - время, LCD_Fill_Template (поворот/язык/смена
  *               экрана), верхняя панель (LCD_Battery с фазой мигания),
  *               покранный switch (LCD_Menu/LCD_SubMenuInfo/LCD_SubMenuSetting),
  *               ui_apply_visibility. Хука «входа на экран» НЕ нужна
  *               (замечание автора п.1) - шаблон грузится внутри
  *               LCD_Fill_Template при смене menu_status.screen.
  *
  *          ОДНА ФИЗИЧЕСКАЯ КНОПКА - ДВЕ СТРУКТУРЫ (PB1): button1 (жесты
  *          навигации) и button_sos (SOS, свой порог 3 с). Декодер гасит только
  *          button1, button_sos продолжает считать, пока кнопка зажата. При
  *          включении/выключении и отмене отсчёта обе гасятся
  *          Menu_ResetButtons(), чтобы не остались «призрачные» жесты/SOS.
  *
  *          КОНТЕКСТ (замечание автора п.5 - по образцу
  *          indication_task_function из Avis_main.c): Menu_Init() - Measure
  *          Task (Vector_Run_Pre_Init, один раз); Vector_Run_Indication() -
  *          поток LVGL Task (main_vector.c) по timer.flag_100ms (100 мс),
  *          внутри по device_turn -> Menu_Run(). В Measure Task меню НЕ живёт.
  *          Из ISR не вызывать (тут таймеры прибора и SaveConfig).
  *
  *           (замечание автора п.3): приоритет жестов ПОСТОЯННЫЙ -
  *          Button1 > Button2 > Button3 (внутри кнопки LONG > SHORT >
  *          DOUBLE), единая цепочка else-if на экран начинается с g1.
  ******************************************************************************
  */
/* Инклуды проекта - В ОДНОМ МЕСТЕ (main_vector.h): кнопки button1..3
   (stm32u5xx_it.h), время VTICK_ и GetTick (vector_tick.h/vector_macros.h),
   лог, Alert_vector.h (AlertSeq_PowerOn - сценарий включения прибора), экран LCD_Init/backlight_set/
   LCD_* (TFT.h/LCD_platform.h/TFT_indicator.h при CONFIG_TYPE_LCD_TFT).   */
#include "main_vector.h"

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
/* Период опроса меню (Menu_Run по timer.flag_100ms): компенсирует квантование
   начала нажатия и окна DOUBLE, иначе настоящий двойной клик с паузой
   250-400 мс распадался бы на два одиночных. */
#define MENU_POLL_MS        ((uint32_t)VECTOR_MENU_POLL_MS)
/* SOS: удержание button_sos (ВТОРАЯ структура на ту же PB1, что и button1:
   stm32u5xx_it.c ставит его флаги вместе с button1, счётчик инкрементирует
   Timer_Tick_1ms) дольше обычного LONG, поэтому SOS не спорит с жестами меню
   (Avis: BUTTON_PRESS_LONG_SOS). */
#define BUTTON_PRESS_SOS    ((uint32_t)VECTOR_MENU_PRESS_SOS_MS)

/* ui_lv_init() (lv_init + lv_display_create + lv_i18n + ui_init + экраны
   SquareLine) выполняется ОДИН РАЗ и ДО ПОТОКОВ - в конце
   Vector_Run_Pre_Init() (main_vector.c). Порядок как в Avis (указание
   автора): «сначала инициализация ui, потом потоки, а инициализация экрана -
   во время включения по кнопке». Иначе TFT_Init() -> TFT_Rotation() ->
   lv_obj_invalidate(lv_scr_act()) отрабатывал по NULL (дисплей LVGL ещё не
   создан) и давал hard fault. Повторно звать ui_lv_init() нельзя: lv_init()
   в LVGL 9 от повтора защищён, а lv_display_create()/ui_init() создали бы
   второй дисплей и второй комплект экранов. Программное выключение
   (Menu_TurnOff -> LCD_DeInit) гасит только панель - объекты LVGL живут в
   RAM, поэтому при следующем включении достаточно TFT_Init + полная
   перерисовка (см. Button_Run_Start).                                     */

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

/* Номера особых страниц meas_pages[] (TFT_indicator.c, порядок задан автором):
   5 и 6 - ОДНА страница «Сообщения» (голосовые | чат), различаются только
   подсветкой; 7 - страница ошибок, в КОЛЬЦО СКРОЛЛА НЕ ВХОДИТ (открывается
   только g2 SHORT_LONG). Кольцо листания - 0..VECTOR_MEASURE_PAGES-2.       */
#define MEAS_PAGE_VOICE     ((uint8_t)5)
#define MEAS_PAGE_CHAT      ((uint8_t)6)
#define MEAS_PAGE_ERR       ((uint8_t)7)

/* Число страниц/строк подэкранов меню определяет слой индикации
   (TFT_indicator): info_total_pages считает LCD_SubMenuInfo по числу строк,
   строки настроек - Panel1..3 (LCD_SubMenuSetting). Без экрана листать нечего. */
#if (CONFIG_TYPE_LCD_TFT != 0)
#define MENU_INFO_PAGES     (lcd_sys.total_info_pages)
#define MENU_SETTING_ROWS   (3u)
#else
#define MENU_INFO_PAGES     (1u)
#define MENU_SETTING_ROWS   (1u)
#endif

/* Состояние декодера ОДНОЙ кнопки (владелец - Menu_Run, поток LVGL Task).
   В Avis аналогичное жило в полях Button_variables (last_short_click_time);
   у нас структура кнопки общая с ISR, поэтому декодер держит своё состояние
   отдельно - по индексу кнопки 0..2 (Button1..Button3).

   ПОЧЕМУ ТАК (опрос Menu_Run - раз в 100 мс):
     button_count идёт 1:1 мс (TIM3), поэтому начало нажатия восстанавливается
     точно как now - button_count (на отпускании счётчик заморожен и равен
     длительности); сам now квантуется 100-мс периодом, отсюда MENU_POLL_MS
     в окне DOUBLE.
     press_start_ms  - начало текущего/последнего нажатия;
     pending_short_ms- момент завершения ПЕРВОГО клика: !=0 - ждём второй,
                       SHORT ещё не выдан (отложенный SHORT);
     press_active    - фронт нажатия уже обработан (сброс long_fired);
     double_armed    - второй клик начался внутри окна DOUBLE (его отпускание
                       даст DOUBLE даже если окно успело истечь, пока он зажат);
     long_fired      - LONG/SHORT_LONG выдан по таймеру: отпускание этой кнопки
                       подавляется и НЕ заряжает pending_short_ms.            */
static uint32_t press_start_ms[3]   = {0u, 0u, 0u};
static uint32_t pending_short_ms[3] = {0u, 0u, 0u};
static uint8_t  press_active[3]     = {0u, 0u, 0u};
static uint8_t  double_armed[3]     = {0u, 0u, 0u};
static uint8_t  long_fired[3]       = {0u, 0u, 0u};

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

/* DOUBLE без отдельного спецдействия на текущем экране = обычный SHORT
   (требование автора): двойное нажатие не должно проглатываться впустую.
   Когда для экрана появится своё действие DOUBLE, его ветка ставится в
   menu_navigate() ДО этой нормализации.                                    */
static uint8_t menu_as_short(uint8_t gest)
{
  return (gest == (uint8_t)GEST_DOUBLE) ? (uint8_t)GEST_SHORT : gest;
}

/* Страница экрана измерений доступна в текущей сборке? Набор зависит от
   CONFIG_*: голосовые сообщения (стр. 5) есть только с CONFIG_G4, чат
   (стр. 6) - всегда; страница ошибок (стр. 7) в кольцо скролла не входит
   (открывается отдельным жестом g2 SHORT_LONG).                            */
static uint8_t meas_page_enabled(uint8_t page)
{
  if (page >= MEASURE_PAGES_COUNT) { return 0u; }
  if (page == MEAS_PAGE_ERR)       { return 0u; }
#if !CONFIG_G4
  if (page == MEAS_PAGE_VOICE)     { return 0u; }
#endif
  return 1u;
}

/* Шаг по кольцу страниц экрана измерений с пропуском недоступных: dir>0 -
   вниз, dir<0 - вверх. Кольцо - 0..MEASURE_PAGES_COUNT-2 (стр. ошибок не
   входит); из стр. 7 «вниз» попадаем на 0, «вверх» - на последнюю страницу
   кольца. Первый/последний элементы обрабатываются как обычно (модуль), при
   одной доступной странице или отсутствии доступных возвращаем cur - без
   зацикливания. КОНТЕКСТ: поток LVGL Task (menu_navigate).                  */
static uint8_t meas_page_step(uint8_t cur, int8_t dir)
{
  uint8_t ring;
  uint8_t p;
  uint8_t n;

  if (MEASURE_PAGES_COUNT < 2u) { return 0u; }
  ring = (uint8_t)(MEASURE_PAGES_COUNT - 1u);      /* 0..ring-1 - кольцо      */

  /* из страницы ошибок (вне кольца) - на ближайший край кольца */
  if (cur < ring)
  {
    p = cur;
  }
  else
  {
    p = (dir > 0) ? (uint8_t)(ring - 1u) : 0u;
  }

  for (n = 0u; n < ring; n++)
  {
    p = (dir > 0) ? (uint8_t)((p + 1u) % ring)
                  : (uint8_t)((p == 0u) ? (ring - 1u) : (p - 1u));
    if (meas_page_enabled(p) != 0u)
    {
      return p;
    }
  }
  return cur;     /* ни одной доступной страницы - остаёмся на месте */
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

  /* Иконка радиомодуля (ui_ImageLora): картинка - по конфигурации сборки
     (CONFIG_LORA / CONFIG_G4 / CONFIG_NBIOT), видимость - по битам
     ST_COMMON_BIT_TURN_ON_* в Config_common.State. Как в Avis
     DisplayIndication: статусные иконки выставляются ПОСЛЕ LCD_Fill_Template
     (его LCD_Image_SET(0) гасит все) и ДО ui_apply_visibility().        */
  LCD_RadioIcon();

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
    /* Заставка/прогрев: ui_ScreenStart пустой (только фон темы), картинку
       делает LCD_ToWarm() - num = 0 показывает логотип (SET_MIRAX), как
       Warm_Screen_statuses case 0 в Avis. Прочие num (общая информация,
       автокалибровка, тест) - задел автора: в VECTOR пока нет ни каналов
       SNS_CFG.Sensor[], ни калибровки. Обратный отсчёт до конца прогрева
       (LCD_CountdownToWarm) не выводится: на экране Start нет ни лейбла, ни
       панели - добавить их нужно в SquareLine (автор).                    */
    case SCREEN_START:
      LCD_ToWarm(&Sns_Cfg_struct, 0u, 0u);
      break;
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
  /* state_menu - ОДНО поле на две сущности (экономия памяти, структура для
     Expressions не меняется): страница измерений и курсор меню. При ЛЮБОМ
     переходе между SCREEN_MEASURE и SCREEN_MENU сбрасываем его в 0, чтобы
     номер страницы не наследовался курсором меню и наоборот. Возврат из
     INFO/SETTING в MENU курсор сохраняет - это не переход MEASURE<->MENU. */
  if ((screen == (uint8_t)SCREEN_MEASURE) ||
      ((screen == (uint8_t)SCREEN_MENU) &&
       (menu_status.screen_prev == (uint8_t)SCREEN_MEASURE)))
  {
    menu_status.state_menu = 0u;
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

/* Начало нажатия по счётчику: на отпускании button_count заморожен и равен
   длительности нажатия, поэтому начало = now - count (погрешность - только
   квантование now периодом опроса меню). Защита от unsigned-underflow в первые
   миллисекунды после старта (now < count). КОНТЕКСТ: поток.                   */
static uint32_t press_start_of(uint32_t now, uint32_t count)
{
  return (count <= now) ? (now - count) : 0u;
}

/* ============================================================================
 *                      ДЕКОДЕР ЖЕСТА ОДНОЙ КНОПКИ
 *
 * Пороги по-прежнему из Avis Button_1/2/3_Menu, но с двумя отличиями:
 *   - LONG и SHORT_LONG срабатывают ПО ТАЙМЕРУ (count >= BUTTON_PRESS_LONG),
 *     пока кнопка ещё зажата: по ним сразу запускается обратный отсчёт
 *     (выключение прибора, SOS). После срабатывания long_fired=1 - отпускание
 *     этой кнопки подавляется (ни SHORT, ни DOUBLE, ни pending);
 *   - SHORT выдаётся ОТЛОЖЕННО: сначала отпускание только запоминается
 *     (pending_short_ms), а сам SHORT формируется, когда окно DOUBLE истекло,
 *     а второй клик так и не пришёл. Иначе двойное нажатие давало бы лишнее
 *     одиночное действие (требование автора).
 *
 * Окно DOUBLE измеряется от НАЧАЛА второго нажатия (press_start = now - count;
 * на отпускании count заморожен и равен длительности), а не от момента опроса
 * отпускания - из-за 100-мс опроса Menu_Run() это иначе теряло бы настоящие
 * двойные клики с паузой 250-400 мс. MENU_POLL_MS компенсирует квантование.
 * DOUBLE = короткий+короткий; SHORT_LONG = короткий+длинный (в том же окне).
 *
 * Одному вызову соответствует не более одного жеста: ветки взаимоисключающие,
 * протухшие состояния гасятся в той же ветке, где обрабатываются.
 * КОНТЕКСТ: поток LVGL Task (Menu_Run каждый проход, 100 мс).                */
static void decode_button(uint8_t idx, volatile Button_variables *pb)
{
  uint32_t now  = GetTick();
  uint8_t  gest = (uint8_t)GEST_NONE;

  if (pb->button_flag != 0u)
  {
    /* ---- кнопка зажата: начало нажатия + взвод двойного + LONG по таймеру */
    press_start_ms[idx] = press_start_of(now, pb->button_count);

    if (press_active[idx] == 0u)
    {
      press_active[idx] = 1u;
      long_fired[idx]   = 0u;          /* новый фронт нажатия - новый жест   */
    }

    /* второй клик начался внутри окна DOUBLE? */
    if ((pending_short_ms[idx] != 0u) &&
        ((press_start_ms[idx] - pending_short_ms[idx]) <=
         (TIME_DOUBLE_CLICK + MENU_POLL_MS)))
    {
      double_armed[idx] = 1u;
    }

    if (pb->button_count >= BUTTON_PRESS_LONG)
    {
      /* длинное удержание фиксируем сразу, не дожидаясь отпускания */
      gest = (((pending_short_ms[idx] != 0u) || (double_armed[idx] != 0u))
              ? (uint8_t)GEST_SHORT_LONG : (uint8_t)GEST_LONG);
      pending_short_ms[idx] = 0u;      /* первый клик вошёл в SHORT_LONG     */
      double_armed[idx]     = 0u;
      long_fired[idx]       = 1u;      /* отпускание больше не даст жестов   */
      press_active[idx]     = 0u;
      /* гасим флаг и счёт: иначе LONG выстрелит снова на следующем проходе */
      pb->button_flag       = 0u;
      pb->button_count      = 0u;
    }
  }
  else
  {
    /* ---- кнопка отпущена (или флаг уже погашен длинным нажатием) */
    if (pb->button_count > BUTTON_PRESS_SHORT)
    {
      if (long_fired[idx] != 0u)
      {
        /* хвост LONG/SHORT_LONG: ничего не генерируем и pending не заряжаем */
        long_fired[idx]   = 0u;
        press_active[idx] = 0u;
      }
      else
      {
        press_start_ms[idx] = press_start_of(now, pb->button_count);

        if ((double_armed[idx] != 0u) ||
            ((pending_short_ms[idx] != 0u) &&
             ((press_start_ms[idx] - pending_short_ms[idx]) <=
              (TIME_DOUBLE_CLICK + MENU_POLL_MS))))
        {
          gest = (uint8_t)GEST_DOUBLE;
          pending_short_ms[idx] = 0u;
        }
        else
        {
          /* первый клик: SHORT пока НЕ выдаём - ждём окно DOUBLE */
          pending_short_ms[idx] = (now == 0u) ? 1u : now;
        }
        double_armed[idx] = 0u;
        press_active[idx] = 0u;
      }
      pb->button_count = 0u;
    }
    else if (pb->button_count > 0u)
    {
      /* короче антидребезга: дребезг, окно DOUBLE не трогаем, но взвод снимаем */
      pb->button_count  = 0u;
      double_armed[idx] = 0u;
      press_active[idx] = 0u;
      if (long_fired[idx] == 0u)
      {
        gest = (uint8_t)GEST_FAKE;
      }
      else
      {
        long_fired[idx] = 0u;
      }
    }
    else
    {
      if (long_fired[idx] != 0u)
      {
        /* отпускание после длинного жеста: гасим подавление */
        long_fired[idx]   = 0u;
        press_active[idx] = 0u;
        double_armed[idx] = 0u;
      }
      else if ((pending_short_ms[idx] != 0u) &&
               (double_armed[idx] == 0u) &&
               ((now - pending_short_ms[idx]) >= TIME_DOUBLE_CLICK))
      {
        /* окно истекло, второго клика нет - отложенный SHORT */
        gest = (uint8_t)GEST_SHORT;
        pending_short_ms[idx] = 0u;
      }
    }
  }

  if (gest != (uint8_t)GEST_NONE)
  {
    set_gesture(idx, gest);
    /* любой жест перезапускает таймер возврата на главный экран (Avis).
       FAKE - дребезг, авто-возврат он продлевать не должен.               */
    if (gest != (uint8_t)GEST_FAKE)
    {
      START_TIMER_RTC(TIMER_RTC_MAIN_RESET, VECTOR_MENU_AUTO_RETURN_S);
    }
  }
}

/* Сброс состояния кнопок и декодера. Одна ФИЗИЧЕСКАЯ кнопка PB1 несёт ДВЕ
   программные структуры: button1 (жесты навигации) и button_sos (SOS), поэтому
   гасим обе - иначе после включения/выключения или прерванного отсчёта
   останутся «призрачный» SHORT/DOUBLE (pending_short) и досчитанный SOS.
   button_pressed не трогаем: им владеет подсветка (Button_Run_Backlight).
   Звать из Menu_TurnOff(), Button_Run_Start(), Menu_Init() и при
   старте/отмене/завершении обратного отсчёта (выключение, SOS).
   КОНТЕКСТ: поток LVGL Task; Menu_Init - до потоков (только запись полей).    */
void Menu_ResetButtons(void)
{
  uint8_t i;

  for (i = 0u; i < 3u; i++)
  {
    press_start_ms[i]   = 0u;
    pending_short_ms[i] = 0u;
    press_active[i]     = 0u;
    double_armed[i]     = 0u;
    long_fired[i]       = 0u;
    menu_status.gestures[i] = (uint8_t)GEST_NONE;
  }

  /* Счётчики/флаги: декодер и Button_Run_Start работают по ним напрямую.
     Флаг гасим - жест, начатый до выключения/включения, не продолжается. */
  button1.button_count = 0u;
  button1.button_flag  = 0u;
  button2.button_count = 0u;
  button2.button_flag  = 0u;
  button3.button_count = 0u;
  button3.button_flag  = 0u;

  /* SOS - вторая структура на ту же PB1 (см. выше): гасим отдельно, чтобы
     удержание кнопки при включении не досчитало до порога 3 с.            */
  button_sos.button_count = 0u;
  button_sos.button_flag  = 0u;
}

/* ============================================================================
 *                     ВЫКЛЮЧЕНИЕ ПРИБОРА и SOS
 * ============================================================================ */

/* Выключение прибора - аналог TurnOffSystem() из Avis
   (docs/Avis_Menu_Functionality.c.md): индикация гасится, статусы обмена/звука
   и общие ошибки сбрасываются, ставится бит «прибор выключен», в журнал уходит
   TYPE_TURN_OFF, модули обмена деинициализируются, device_turn =
   DEVICE_TURNED_OFF. Дальше Vector_Run_Indication() в состоянии OFF вызывает
   Button_Run_Start() - включение долгим нажатием Button3 (как в Avis, где
   DisplayIndication при выключенном приборе крутит Button_Run_Start/TurnOffPower).
   Жест - LONG Button3 (1.5 с): он и есть подтверждение, поэтому отдельного
   обратного отсчёта нет. Включение и выключение - ОДНА кнопка Button3.
   ОТЛИЧИЯ от Avis (чего в проекте пока нет):
     - CountdownToShutdown()/TIMER_SHUTDOWN + LCD_CountdownToShutdown - обратный
       отсчёт на экране (автор: «потом сделаем»; задел - ROLLER_TURN_OFF и
       LCD_CountdownToShutdown() в TFT_indicator.c);
     - TurnOffPower() (SET_OFF(POWER_ON_*)): пина программной защёлки питания на
       этой плате нет (в .ioc не заведён, SET_ON(POWER_ON_MCU) в
       Button_Run_Start закомментирован), поэтому выключение ПРОГРАММНОЕ -
       экран гаснет (LCD_DeInit: подсветка 0 + RST), потоки видят
       DEVICE_TURNED_OFF;
     - очистки ошибок по каналам (Sensor[]/TurnOffPowerSensor): в SNS_CFG
       каналов нет (SNS_Dev_Type - задел), обнуляется Config_common.StateErr.
   КОНТЕКСТ: поток LVGL Task (menu_navigate).                                */
static void Menu_TurnOff(void)
{
  if (device_turn == DEVICE_TURNED_OFF){
    return;
  }

#if (CONFIG_TYPE_LCD_TFT != 0)
  ui_reset_visibility_flags();      /* иконки/панели заголовка - в «скрыто» (Avis) */
#endif
  CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND);      /* Avis: снять блокировку звука    */
  CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_DATA_EXCHANGE);    /* Avis: выйти из режима обмена    */
  Sns_Cfg_struct.Config_common.StateErr = 0;               /* Avis: очистка общих ошибок      */
  SET_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_OFF);           /* «прибор выключен» (логи не ведутся) */
#if CONFIG_SAVE_PARAM_LOG
  /* log_post() только СТАВИТ запись в RAM-очередь (во flash пишет Measure Task -
     Vector_RunFlashMemory, ~50 мс), поэтому из потока LVGL это безопасно.
     Если появится реальное снятие питания (пин POWER_ON_MCU), заменить на
     save_event_type_2(&Sns_Cfg_struct, TYPE_TURN_OFF) - он дренирует очередь
     синхронно до обесточивания.                                           */
  log_post(TYPE_TURN_OFF);
#endif
#if CONFIG_BLE
  Ble_DeInit(0);
#endif
#if CONFIG_LORA
  Lora_turnoff_system_set();
#endif
#if (CONFIG_TYPE_LCD_TFT != 0)
  /* Панель гасим ПОСЛЕ журнала и модулей: TFT_DeInit() = backlight_set(0) +
     LCD_RST_Clr() + TFT_Initialized = 0 (Menu_Draw после этого выходит по
     LCD_Get_flag_init() == 0 - LVGL не рисует в выключенную панель).     */
  LCD_DeInit();
#endif

  RESET_TIMER_RTC(TIMER_RTC_WARM);        /* прогрев следующего включения стартует с нуля */
  /* Гасим декодер и ОБЕ структуры PB1 (button1 + button_sos): отпускание
     Button3, которым выключили прибор, и удержание button1/button3 в момент
     выключения не должны дать «призрачный» SHORT/DOUBLE или досчитанный SOS
     после следующего включения. Счётчики кнопок при этом обнуляются - пока
     кнопку не отпустят, повторного LONG не будет (флаг тоже погашен).     */
  Menu_ResetButtons();

  device_turn = DEVICE_TURNED_OFF;
  /* Следующее включение снова начинается с заставки/прогрева: Menu_GoTo дёшево -
     LCD_DeInit уже выполнен, Menu_Draw() выйдет по флагу инициализации.   */
  Menu_GoTo((uint8_t)SCREEN_START);

  LOG_I(VLOG_M_SYS, "menu: turn off (TYPE_TURN_OFF queued)");
}

/* SOS: удержание button_sos >= BUTTON_PRESS_SOS (3 с). button_sos в
   stm32u5xx_it.c зеркалит Button1 (те же EXTI-флаги, счётчик свой - растёт в
   Timer_Tick_1ms), поэтому жест не конфликтует с декодером меню: LONG Button1
   (1.5 с) в навигации не используется, а SOS срабатывает на 3 с.
   Аналог Avis Button_Sos() (docs/Avis_Menu_Functionality.c.md): там условие
   CONFIG_LORA + ST_COMMON_BIT_TURN_ON_LORA + get_state_start_flag_lora(), затем
   START_TIMER(TIMER_PRESS_SOS, TIME_PRESS_SOS) и переход на
   STATE_SCREEN_COUNTDOWN_SOS - обратный отсчёт с отменой отпусканием.
   У нас SOS пока БЕЗ действия (автор: «обратный отсчёт будет, потом сделаем»):
   жест фиксируется, счётчик гасится и пишется лог. Для доделки понадобятся
   миллисекундный таймер обратного отсчёта (в enum 1 мс свободных слотов нет -
   добавить свой перед TIMER_END), LCD_CountdownToSOS() + ROLLER_SOS (уже есть)
   и Lora_SetControlLoraBit(ST_LORA_BIT_SOS, 1) под CONFIG_LORA.
   Работает на ЛЮБОМ экране (в Avis Button_Sos() вызывается из Button_Run, а не
   из экранного switch); на SCREEN_START это один из двух рабочих жестов.
   КОНТЕКСТ: поток LVGL Task (Menu_Run, 100 мс).                             */
static void Button_Sos(void)
{
  if (button_sos.button_flag != 0u)
  {
    if (button_sos.button_count >= BUTTON_PRESS_SOS)
    {
      /* удержание засчитано: гасим флаг, чтобы SOS не повторился до отпускания
         кнопки (Avis: button_sos.button_flag = 0)                          */
      button_sos.button_flag  = 0u;
      button_sos.button_count = 0u;
#if CONFIG_LORA
      /* TODO(автор): Lora_SetControlLoraBit(ST_LORA_BIT_SOS, 1) + обратный
         отсчёт на экране - см. комментарий выше.                           */
#endif
      LOG_I(VLOG_M_SYS, "menu: SOS hold %u ms (countdown and SOS signal - TODO)",
            (uint32_t)BUTTON_PRESS_SOS);
    }
  }
  else
  {
    button_sos.button_count = 0u;   /* кнопка отпущена - счётчик с нуля (Avis) */
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

	/* КВИТИРОВАНИЕ ЗЕЛЁНОЙ кнопкой (номер - ALERT_ACK_BUTTON, vector_config.h):
	   короткое нажатие гасит ИНДИКАЦИЮ приоритетного статуса (Alert_vector), а
	   сам СТАТУС и его таймеры остаются (требование автора п.2): неподвижность
	   продолжит отсчёт и отправит инцидент, если сотрудник не двигается.
	   Работает на ЛЮБОМ экране - до экранного switch.
	   Если квитировать нечего (активной индикации нет, либо она не снимается
	   вручную - «нет СИЗ/ГА/браслета»), жест НЕ съедаем: он уходит в навигацию
	   ниже. Кнопка выбирается по ALERT_ACK_BUTTON (1/2 -> Button1/2, иначе 3).
	   DOUBLE здесь тоже считается SHORT (menu_as_short, требование автора):
	   отдельного действия у квитирования нет.
	   КОНТЕКСТ: поток LVGL Task (Menu_Run, 100 мс).                          */
	{
		uint8_t *g_ack = (ALERT_ACK_BUTTON == 1u) ? &g1 :
					   ((ALERT_ACK_BUTTON == 2u) ? &g2 : &g3);
		if ((menu_as_short(*g_ack) == (uint8_t)GEST_SHORT) &&
			(Alert_AckButton() != (uint8_t)ALERT_NONE)){
			*g_ack = (uint8_t)GEST_NONE;
		}
	}


  /* Выключение прибора - ПОЭКРАННО (Menu_TurnOff): LONG Button2 на
     SCREEN_START уже работает, на SCREEN_MEASURE в Avis это LONG с обратным
     отсчётом (STATE_SCREEN_MEASURE_TO_COUNTDOWN -> CountdownToShutdown) -
     задел автора. Общий хук здесь убран: LONG Button2 на SCREEN_INFO_DEVICE и
     SCREEN_SETTING означает «назад», и глобальная ветка печатала бы
     «turn-off» одновременно с возвратом в меню.                         */
  /* ПРИОРИТЕТ ЖЕСТОВ ПОСТОЯННЫЙ (замечание автора п.3): на каждом
     экране цепочка else-if ВСЕГДА начинается с Button1 (вход/назад - важнее
     скролла), затем Button2 (вниз), затем Button3 (вверх); внутри кнопки
     LONG важнее SHORT. Так g1 не может «стать последним», как ранее.       */
  switch (menu_status.screen)
  {
    /* Заставка/прогрев (SCREEN_START): работают ТОЛЬКО выключение (LONG
       Button3 -> Menu_TurnOff) и SOS (удержание Button1 3 с - Button_Sos(),
       вызывается вне switch, в Menu_Run). Остальные жесты гасятся: листать во
       время прогрева нечего, а выход с экрана происходит сам - по истечении
       TIMER_RTC_WARM (Menu_Run) или по выключению. Авто-возврат
       TIMER_RTC_MAIN_RESET на этом экране отключён (Menu_Run): он короче
       прогрева (30 с против TIME_RTC_START 60 с) и увёл бы на SCREEN_MEASURE
       раньше времени.                                                   */
    case SCREEN_START:
      if (g3 == (uint8_t)GEST_LONG){
        Menu_TurnOff();
      }
      break;
    /* Главный экран (Avis: STATE_SCREEN_MEASURE).
       LONG Button3 - выключение (Menu_TurnOff); на страницах сообщений (5/6)
       тот же LONG открывает соответствующее сообщение (голосовые/чат).
       В Avis выключение отсюда - LONG Button1 с обратным отсчётом
       (STATE_SCREEN_MEASURE_TO_COUNTDOWN -> CountdownToShutdown), у нас это
       задел автора: TIMER_SHUTDOWN + LCD_CountdownToShutdown, ROLLER_TURN_OFF.
       Вход в меню - Button3 SHORT_LONG (короткий клик + длинное удержание в
       окне DOUBLE), см. ТЗ п.3. DOUBLE без спецдействия = SHORT.            */
    case SCREEN_MEASURE:
    {
	    static const uint8_t setting_screens[] = {
	    		SCREEN_MEASURE,
				SCREEN_MENU,
				SCREEN_VOICE_MESSAGE,
				SCREEN_TEXT_MESSAGE,
	    };

	    uint8_t sub = 0;

	    if (menu_as_short(g2) == (uint8_t)GEST_SHORT){
	    	/* страница вниз по кольцу с пропуском отключённых сборкой */
	    	menu_status.state_menu = meas_page_step(menu_status.state_menu, +1);
	    }
	    else if (g2 == (uint8_t)GEST_SHORT_LONG){
	    	/* короткий + длинный Button2 - страница ошибок (вне кольца) */
	    	menu_status.state_menu = MEAS_PAGE_ERR;
	    }
	    else if (menu_as_short(g3) == (uint8_t)GEST_SHORT){
	    	menu_status.state_menu = meas_page_step(menu_status.state_menu, -1);
	    }
	    else if (g3 == (uint8_t)GEST_LONG){
	    	if (menu_status.state_menu == MEAS_PAGE_VOICE){
	    		sub = 2;
	    	}
	    	else if (menu_status.state_menu == MEAS_PAGE_CHAT){
	    		sub = 3;
	    	}
	    	else {
	    		/* Выключение: Menu_TurnOff() уже увёл на SCREEN_START. Уходим
	    		   из switch, иначе Menu_GoTo(setting_screens[0]) ниже вернул бы
	    		   экран в SCREEN_MEASURE сразу после выключения.              */
	    		Menu_TurnOff();
	    		break;
	    	}
	    }
	    else if (g3 == (uint8_t)GEST_SHORT_LONG){
	    	/* ВХОД В МЕНЮ (ТЗ п.3): state_menu сбросит Menu_GoTo - это
	    	   переход MEASURE -> MENU (номер страницы не наследуется).    */
	    	sub = 1;
	    }

	    Menu_GoTo(setting_screens[sub]);
	    break;
    }

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
		  else if (menu_as_short(g3) == (uint8_t)GEST_SHORT){
			menu_status.state_menu = scroll_down(menu_status.state_menu, MENU_ITEMS_COUNT);
		  }
		  else if (menu_as_short(g2) == (uint8_t)GEST_SHORT){
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
		  else if (menu_as_short(g2) == (uint8_t)GEST_SHORT){
			menu_status.state_submenu = scroll_down(menu_status.state_submenu, MENU_INFO_PAGES);
		  }
		  else if (menu_as_short(g3) == (uint8_t)GEST_SHORT){
			menu_status.state_submenu = scroll_up(menu_status.state_submenu, MENU_INFO_PAGES);
		  }
		  break;

    /* Настройки: строки Panel1..3 (LCD_SubMenuSetting), Button2 LONG - назад.
       Вход в строку (Button1 SHORT) - когда автор определит состав настроек. */
    case SCREEN_SETTING:
      if (g2 == (uint8_t)GEST_LONG){
        Menu_GoTo((uint8_t)SCREEN_MENU);
      }
      else if (menu_as_short(g2) == (uint8_t)GEST_SHORT){
        menu_status.state_submenu = scroll_down(menu_status.state_submenu, MENU_SETTING_ROWS);
      }
      else if (menu_as_short(g3) == (uint8_t)GEST_SHORT){
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
  menu_status.screen          = (uint8_t)SCREEN_START;
  menu_status.screen_prev     = (uint8_t)SCREEN_START;
  TO_GO_STATE_SCREEN((uint8_t)SCREEN_START);   /* зеркало для LCD_Fill_Template */
  menu_status.state_menu      = 0u;
  menu_status.state_submenu   = 0u;
  menu_status.dirty_data      = 0u;
  menu_status.last_gesture_ms = 0u;
  menu_status.screen_since_ms = VTICK_MS();
  /* Слоты жестов и состояние декодера - в ноль: при старте кнопки могли
     остаться зажатыми (в т.ч. PB1 с зеркалом button_sos), а протухший жест
     выстрелил бы на первом же проходе Menu_Run().                        */
  Menu_ResetButtons();

  /* Авто-возврат на главный экран: как в Avis (TIMER_RTC_MAIN_RESET). */
  START_TIMER_RTC(TIMER_RTC_MAIN_RESET, VECTOR_MENU_AUTO_RETURN_S);

  /* Прогрев (SCREEN_START): таймер взведёт Button_Run_Start() при включении
     прибора (TIME_RTC_START). Здесь - сброс, чтобы флаг случайным образом не
     остался от прошлого включения и заставка не пролистнулась мгновенно.  */
  RESET_TIMER_RTC(TIMER_RTC_WARM);

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

  /* SOS - свой счётчик (button_sos), работает на любом экране: до декодера
     жестов, чтобы удержание Button1 3 с не «съедалось» экранной навигацией. */
  Button_Sos();

  menu_navigate();

  /* Заставка/прогрев: TIMER_RTC_WARM взведён при включении прибора
     (Button_Run_Start, TIME_RTC_START = 60 с). По истечении прибор переходит в
     рабочий режим - device_turn = DEVICE_TURNED_ON и экран измерений (Avis:
     Screen_status_flip case 7 - TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE) +
     device_turn = DEVICE_TURNED_ON). Всё время прогрева device_turn =
     DEVICE_TURNED_ON_1 («включён без звука/вибро - используется для прогрева и
     авто-выключения», shared_types.h): Menu_Run() при этом вызывается, но
     оповещения не запускаются.                                            */
  if ((menu_status.screen == (uint8_t)SCREEN_START) && (TEST_TIMER_RTC(TIMER_RTC_WARM) != 0)){
    RESET_TIMER_RTC(TIMER_RTC_WARM);
    device_turn = DEVICE_TURNED_ON;
    LOG_I(VLOG_M_SYS, "menu: warm-up %u s done -> SCREEN_MEASURE", (uint32_t)TIME_RTC_START);
    Menu_GoTo((uint8_t)SCREEN_MEASURE);
  }

  /* Авто-возврат на главный экран по истечении TIMER_RTC_MAIN_RESET.
     На SCREEN_START не действует (прогрев длиннее VECTOR_MENU_AUTO_RETURN_S). */
  if (TEST_TIMER_RTC(TIMER_RTC_MAIN_RESET) != 0){
    RESET_TIMER_RTC(TIMER_RTC_MAIN_RESET);
    if ((menu_status.screen != (uint8_t)SCREEN_MEASURE) &&
        (menu_status.screen != (uint8_t)SCREEN_START)){
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
  Button_Run_Backlight();
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
//    backlight_set(Sns_Cfg_struct.Config_common.Reserve[0]);
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
	if(button3.button_count >= BUTTON_PRESS_LONG){
		//------------------------------------------------------------------------
//		SET_ON(POWER_ON_MCU);
		//------------------------------------------------------------------------
		// Инициализация ЭКРАНА (панели) - при включении по кнопке. LVGL и UI
		// SquareLine к этому моменту уже созданы: ui_lv_init() вызывается ДО
		// потоков в конце Vector_Run_Pre_Init() (порядок из Avis, указание
		// автора: «сначала инициализация ui, потом потоки и инициализация
		// экрана во время включения по кнопке»). Это обязательно: TFT_Init()
		// внутри зовёт TFT_Rotation(), а там lv_obj_invalidate(lv_scr_act()) -
		// без созданного дисплея lv_scr_act() == NULL, и lv_obj_invalidate(NULL)
		// давал hard fault. TFT_Init само-guard'ится флагом TFT_Initialized,
		// повторно ui_lv_init() здесь звать НЕЛЬЗЯ (пересоздаст объекты).
		// КОНТЕКСТ: поток LVGL Task - тот же поток позже крутит lv_timer_handler
		// (LVGL однопоточен).
#if (CONFIG_TYPE_LCD_TFT != 0)
		if (LCD_Get_flag_init() == 0u){
			LCD_Init();
#if VECTOR_LCD_USE_LVGL
			/* RAM панели после включения (и после программного выключения
			   Menu_TurnOff -> LCD_DeInit) содержит мусор, а объекты LVGL живут
			   в RAM и не пересоздаются: помечаем активный экран и верхний слой
			   (там PanelHead/ImageMirax) грязными - ближайший lv_timer_handler()
			   (гейт TFT_Initialized == 1 в lvgl_task_function, main_vector.c) перерисует кадр
			   целиком.                                                      */
			lv_obj_invalidate(lv_screen_active());
			lv_obj_invalidate(lv_layer_top());
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
		// Индикация (замечание автора): НЕБЛОКИРУЮЩЕ - через движок индикации
		// Alert_vector: 3 писка ИЗ ПАМЯТИ МК (audio_beep - клип 240 мс во
		// внутренней flash, не зависит от внешней SPI-flash), пауза 300 мс.
		// Delay() здесь нельзя: фазы ведёт Alert_IndicationTick() (~10 мс) из потока
		// LVGL Task. Будущие тревоги - так же: const-таблица фаз "свет ->
		// звук -> вибро" в любом порядке (примеры в комментариях Alert_vector.c),
		// голосовое сообщение - одна фаза ALERT_CH_SOUND с SND_* из образа.
		Alert_Start(&AlertSeq_PowerOn);
	    //------------------------------------------------------------------------
		Menu_ResetButtons();
		/* Включение - это удержание Button3 до LONG, а на той же PB1 висит
		   button_sos: Menu_ResetButtons() гасит ОБЕ структуры и состояние
		   декодера, иначе отпускание/повторное нажатие дали бы «призрачный»
		   SHORT и досчитанный SOS сразу после включения. Счётчик button3 при
		   этом обнулён - пока кнопку не отпустят, повторного LONG не будет. */

	    /* Прибор включён БЕЗ звука/вибро - состояние прогрева (shared_types.h:
	       DEVICE_TURNED_ON_1 «используется для прогрева и авт выкл.»). Экран -
	       SCREEN_START (заставка), рабочий режим включит Menu_Run() по
	       истечении TIMER_RTC_WARM.                                        */
	    device_turn = DEVICE_TURNED_ON_1;
	    Menu_GoTo((uint8_t)SCREEN_START);
	    menu_status.state_menu = 0;      /* страницы измерений - с первой    */
	    START_TIMER_RTC(TIMER_RTC_WARM, TIME_RTC_START);
	    //вход в обычный режим работы!!!!!!!!!!!
	    START_TIMER_RTC(TIMER_RTC_LIGHT_RESET, TIME_RTC_LIGHT_RESET*12);
	    CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_OFF)
	    //------------------------------------------------------------------------
#if CONFIG_SAVE_PARAM_LOG
	    log_post(TYPE_TURN_ON);
#endif
	}
	else if(button3.button_flag == 0){
		button3.button_count = 0;
	}
}

/* Аналог Avis_Run_Indication (Avis_main.c) - вызывается из потока LVGL Task
   (main_vector.c) по timer.flag_100ms. КОНТЕКСТ: поток LVGL Task.
   device_turn: ON/ON_1 -> Menu_Run() (кнопки/меню/отрисовка); OFF и
   OFF_PRE_INIT -> ничего (в Avis при OFF здесь DisplayChargeIndicationTurnOff
   + Button_Run_Start - экран заряда при выключенном приборе, появится с
   продуктовой кнопкой включения, PLAN этап 6). PRE_INIT сюда не попадает:
   LVGL Task (когда будет создан) ждёт завершения Vector_Run_Pre_Init
   (device_turn/cnt_pre_init) - как receiver в main_vector.c (PLAN п.60).     */
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
