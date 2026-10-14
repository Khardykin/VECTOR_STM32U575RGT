/**
  ******************************************************************************
  * @file    Menu_vector.c
  * @brief   ОСНОВА слоя меню (v26): машина экранов + декодер жестов кнопок
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
  *            4) Menu_Draw() - WEAK аналог DisplayIndication: LVGL-порт
  *               подставит обновление виджетов; сам каркас графики не требует.
  *
  *          КОНТЕКСТ: Menu_Init() - Measure Task (Vector_Run_Pre_Init, один
  *          раз); Menu_Run() - Measure Task КАЖДЫЙ проход (50 мс). Из ISR не
  *          вызывать (тут таймеры прибора и SaveConfig).
  ******************************************************************************
  */
#include "Vector_main.h"     /* агрегатор: таймеры прибора, SaveConfig, лог */
#include "stm32u5xx_it.h"    /* button1/button2/button3 (Button_variables) */
#include "Menu_vector.h"
#include "vector_tick.h"     /* GetTick через vector_macros.h */
#include "vector_macros.h"
#include "vector_log.h"

#if CONFIG_DISPLAY

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
__attribute__((weak)) void Menu_Draw(void)
{
  /* TODO(LVGL-порт, этап 2c): аналог DisplayIndication - обновить виджеты
     текущего экрана: значения из sensors_status (T/H/P, батарея, ориентация),
     иконки статусов (SET_MUTE/BLE/GPS/LORA/LTE/EXCHANGE из ST_COMMON_BIT_*),
     время (Working_data.working_hours), Connected_device (данные Avis по BLE).
     Рисовать из LVGL Task или отсюда - решит порт (владелец дисплея один).  */
}

__attribute__((weak)) void Menu_EnterScreen(uint8_t screen)
{
  /* TODO(LVGL-порт): lv_screen_load(ui_Screen...) по screen_t + заполнение
     текстов (lv_i18n). Здесь - только лог перехода.                        */
  (void)screen;
}

/* ============================================================================
 *                          переходы между экранами
 * ============================================================================ */

/* Перейти на экран. Хук Menu_EnterScreen() зовётся ПОСЛЕ обновления
   menu_status, чтобы порт видел актуальный screen. КОНТЕКСТ: поток.        */
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

  Menu_EnterScreen(screen);
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

  switch (menu_status.screen)
  {
    /* Заставка: любое короткое нажатие - на главный. */
    case SCREEN_START:
      if ((g1 == (uint8_t)GEST_SHORT) || (g2 == (uint8_t)GEST_SHORT) ||
          (g3 == (uint8_t)GEST_SHORT))
      {
        Menu_GoTo((uint8_t)SCREEN_MAIN);
      }
      break;

    /* Главный экран (Avis: STATE_SCREEN_MEASURE).
       LONG Button1 в Avis - отключение прибора с обратным отсчётом: у нас
       обработчика выключения ещё нет (PLAN этап 6, TYPE_TURN_OFF) - оставлен
       хук-заглушка, чтобы не потерять жест.                                 */
    case SCREEN_MAIN:
      if (g1 == (uint8_t)GEST_SHORT)
      {
        Menu_GoTo((uint8_t)SCREEN_MEASUREMENTS);
      }
      else if (g1 == (uint8_t)GEST_LONG)
      {
        /* TODO(автор): отключение прибора (device_turn = DEVICE_TURNED_OFF,
           событие TYPE_TURN_OFF в журнал, счётчик обратного отсчёта на
           экране). Здесь намеренно ничего не делаем.                        */
        LOG_D(VLOG_M_SYS, "menu: MAIN long press (turn-off not implemented)");
      }
      else if (g2 == (uint8_t)GEST_SHORT)
      {
        Menu_GoTo((uint8_t)SCREEN_MENU);
      }
      else if (g3 == (uint8_t)GEST_SHORT)
      {
        Menu_GoTo((uint8_t)SCREEN_TEST_SOUND);
      }
      break;

    /* Измерения: Button2/Button3 - листать каналы/страницы (когда автор
       определит состав), Button1 - назад на главный.                        */
    case SCREEN_MEASUREMENTS:
      if (g1 == (uint8_t)GEST_SHORT)
      {
        Menu_GoTo((uint8_t)SCREEN_MAIN);
      }
      else if (g2 == (uint8_t)GEST_SHORT)
      {
        menu_status.state_submenu = scroll_down(menu_status.state_submenu, MENU_ITEMS_COUNT);
      }
      else if (g3 == (uint8_t)GEST_SHORT)
      {
        menu_status.state_submenu = scroll_up(menu_status.state_submenu, MENU_ITEMS_COUNT);
      }
      break;

    /* Меню: Button2 вниз, Button3 вверх (Avis: scroll_down/scroll_up),
       Button1 SHORT - войти в пункт, Button1 LONG - назад на главный.
       Пункты и куда они ведут - таблица автора (пока цикл по экранам).      */
    case SCREEN_MENU:
      if (g2 == (uint8_t)GEST_SHORT)
      {
        menu_status.state_menu = scroll_down(menu_status.state_menu, MENU_ITEMS_COUNT);
      }
      else if (g3 == (uint8_t)GEST_SHORT)
      {
        menu_status.state_menu = scroll_up(menu_status.state_menu, MENU_ITEMS_COUNT);
      }
      else if (g1 == (uint8_t)GEST_SHORT)
      {
        /* TODO(автор): switch (menu_status.state_menu) - переход в конкретный
           подраздел (инфо/настройки/сервис/калибровка/тест звука). Пока -
          демо-переход в тест звука как единственный реализованный подраздел. */
        Menu_GoTo((uint8_t)SCREEN_TEST_SOUND);
      }
      else if (g1 == (uint8_t)GEST_LONG)
      {
        Menu_GoTo((uint8_t)SCREEN_MAIN);
      }
      break;

    /* Тест звука: Button1 SHORT - назад; LONG - тоже назад (отмена).
       Сам проигрыватель дёргает потребитель хука Menu_EnterScreen/порт
       (audio_play_now(SND_*)), каркас звук не трогает.                      */
    case SCREEN_TEST_SOUND:
      if ((g1 == (uint8_t)GEST_SHORT) || (g1 == (uint8_t)GEST_LONG))
      {
        Menu_GoTo((uint8_t)SCREEN_MENU);
      }
      else if (g2 == (uint8_t)GEST_SHORT)
      {
        menu_status.state_submenu = scroll_down(menu_status.state_submenu, MENU_ITEMS_COUNT);
      }
      else if (g3 == (uint8_t)GEST_SHORT)
      {
        menu_status.state_submenu = scroll_up(menu_status.state_submenu, MENU_ITEMS_COUNT);
      }
      break;

    default:
      Menu_GoTo((uint8_t)SCREEN_MAIN);
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

  Menu_EnterScreen((uint8_t)SCREEN_START);
  LOG_I(VLOG_M_SYS, "menu: init (screen START, auto-return %u s)",
        (uint32_t)VECTOR_MENU_AUTO_RETURN_S);
}

/* Цикл меню (каждый проход Measure Task, 50 мс): жесты -> навигация ->
   авто-возврат -> dirty_data -> отрисовка. НЕ блокируется.                 */
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
    if (menu_status.screen != (uint8_t)SCREEN_MAIN)
    {
      LOG_D(VLOG_M_SYS, "menu: auto-return to MAIN");
      Menu_GoTo((uint8_t)SCREEN_MAIN);
    }
  }

  /* Avis Button_Menu(): при возврате на главный экран - сохранить конфиг,
     если в меню что-то меняли.                                              */
  if ((menu_status.screen == (uint8_t)SCREEN_MAIN) && (menu_status.dirty_data != 0u))
  {
    menu_status.dirty_data = 0u;
    SaveConfig = 1u;
    LOG_I(VLOG_M_SYS, "menu: dirty data -> SaveConfig");
  }

  Menu_Draw();
}

#endif /* CONFIG_DISPLAY */
