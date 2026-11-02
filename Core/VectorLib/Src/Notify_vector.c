/**
  ******************************************************************************
  * @file    Notify_vector.c
  * @brief   Движок НЕБЛОКИРУЮЩИХ оповещений: фазы свет/звук/вибро по дедлайнам
  *          VTICK. Подробное описание модели - в Notify_vector.h.
  *
  *          КОНТЕКСТ: Notify_Tick() - поток LVGL Task (lvgl_task_function в
  *          main_vector.c, цикл ~10 мс). Notify_Start/Notify_StartEx/
  *          Notify_Alarm/Notify_AlarmEx/Notify_Stop() - любой поток (пишут
  *          только volatile-запрос, движок разбирает его в свой Tick).
  *
  *          Машина фазы: ON (каналы маски включены; ждём период t_on_ms или
  *          конец звука; blink - LED мигают 100 мс) -> OFF (каналы выключены;
  *          пауза t_off_ms) -> повтор фазы (repeats) / следующая фаза /
  *          повтор сценария (count, 0 = вечно) / СТОП. Вытеснение и Stop всегда
  *          гасят активные каналы (светодиод не залипнет, вибро не зависнет).
  *
  *          ВКЛЮЧЕНИЕ ПРИБОРА - порт активной ветки Button_Run_Start из Avis
  *          (docs/Avis_Menu_Functionality.c.md): VIBRO_ON 500 мс -> пауза
  *          200 мс -> «короткий сигнал» SOUND+LED1+LED2 200 мс -> пауза
  *          200 мс -> «длинный сигнал» SOUND+LED1+LED2 800 мс. Отличие одно:
  *          вместо SOUND_ON() (непрерывный тон GD32) - писк audio_beep() из
  *          внутренней flash: внешняя SPI-flash на момент включения может быть
  *          ещё не готова, а 240 мс писка укладываются в окна 200/800 мс.
  *          Светодиоды в фазах 2 и 3 РАЗНЫЕ (LED1, затем LED2) - как в
  *          согласованной с автором таблице; для «обе лампы вместе, как в
  *          Avis» достаточно заменить маску на (LED1|LED2).
  *
  *          ТРЕВОГИ - порт LedSoundVibroPlay(switch_flag_led, time_pulse,
  *          time_pause, mode_blink_led, block_sound, block_led, test_sound)
  *          из Avis_Optional_Functions.c.md: там звук+вибро+свет стартуют
  *          ОДНИМ тактом - у нас это одна фаза с маской каналов; pulse/pause
  *          в единицах 100 мс (NOTIFY_ALARM_UNIT_MS); block_sound=ON -> в маске
  *          нет BEEP/SOUND (вибро остаётся, как в Avis: VIBRO_ON не зависит от
  *          block_sound); block_led=ON -> в маске нет LED; mode_blink_led=ON ->
  *          blink=1 (ритм SET_TGL - 100 мс). Период/пауза/мигание/число
  *          повторов ЗАДАЮТСЯ вызовом Notify_AlarmEx() (указание автора:
  *          «у остальных тревог задать период и число повторов»), значения 0 -
  *          «по умолчанию из таблицы» (таблица = параметры вызовов Avis).
  ******************************************************************************
  */
/* Инклуды проекта - В ОДНОМ МЕСТЕ (агрегатор main_vector.h): Notify_vector.h,
   main.h (SET_ON/SET_OFF(ALARM_LED_1/2)), vector_tick.h (тик RTOS),
   vector_macros.h (Vibro_On/Off), vector_log.h, audio_player.h (audio_beep/
   audio_play_now/audio_stop/is_busy), audio_ids.h (SND_*).                  */
#include "main_vector.h"

/* Защитная выдержка фазы NOTIFY_WAIT_END: очередь плеера может стартовать не
   мгновенно (audio_is_busy()==0 ещё до начала) - раньше неё конец не решать. */
#define NOTIFY_WAIT_GRACE_MS   (80u)

/* ============================================================================
 *                                 МАКРОСЫ
 * ============================================================================ */

/* Звук тревоги по умолчанию: писк audio_beep() (внутренняя flash, не зависит
   от SPI-образа). Если нужен клип образа (SND_TONE_1K и т.п.) - добавьте в
   маску NOTIFY_CH_SOUND и укажите sound_idx в таблице s_alarm_tab; тон тогда
   оборвётся концом периода (phase_off -> audio_stop), как SOUND_OFF в Avis.  */
#define NOTIFY_ALARM_SOUND_IDX (SND_KOLOKOL_1)

/* Перевод единиц Avis (100 мс) в миллисекунды с защитой от переполнения
   uint16 (pulse <= 255 -> 25500 мс; pause <= 655 -> 65500 мс).              */
#define NOTIFY_ALARM_MS(v, cap) \
  ((uint16_t)(((uint32_t)(v) * NOTIFY_ALARM_UNIT_MS > (uint32_t)(cap)) \
              ? (uint32_t)(cap) : ((uint32_t)(v) * NOTIFY_ALARM_UNIT_MS)))

/* ============================================================================
 *                                ПЕРЕМЕННЫЕ
 * ============================================================================ */

/* Состояния движка.                                                         */
typedef enum
{
  NS_IDLE = 0,   /* ничего не идёт                                           */
  NS_ON,         /* каналы фазы включены                                     */
  NS_OFF         /* пауза после фазы                                         */
} notify_state_t;

/* Запросы из других потоков (только volatile-записи, разбирает Tick).
   Поля запроса (0 = «взять из таблицы сценария») пишет тот же поток, что и
   s_req_seq - последним присваиванием, поэтому гонки параметров нет: движок
   читает их только когда s_req_seq != NULL.                                 */
static volatile const notify_seq_t *s_req_seq    = NULL; /* старт сценария   */
static volatile uint8_t             s_req_stop   = 0u;   /* 1 = стоп запрос  */
static volatile uint16_t            s_req_period = 0u;   /* override t_on_ms */
static volatile uint16_t            s_req_pause  = 0u;   /* override t_off_ms*/
static volatile uint8_t             s_req_rep    = 0u;   /* override repeats */
static volatile uint8_t             s_req_count  = 1u;   /* повторы сценария */

/* Рабочая фаза: копия первой фазы сценария с применёнными override - чтобы
   Notify_StartEx/Notify_AlarmEx не правили const-таблицу во flash.           */
static notify_phase_t s_phase;

/* Состояние движка (владелец - поток LVGL Task).                            */
static const notify_seq_t *s_seq      = NULL;  /* текущий сценарий           */
static uint8_t  s_idx      = 0u;               /* номер фазы                 */
static uint8_t  s_rep      = 0u;               /* осталось повторов фазы     */
static uint8_t  s_cycles   = 0u;               /* осталось повторов сценария */
static uint8_t  s_state    = (uint8_t)NS_IDLE;
static uint8_t  s_wait_end = 0u;               /* 1 = ждём конец звука       */
static uint8_t  s_blink    = 0u;               /* 1 = LED фазы мигают        */
static uint8_t  s_blink_on = 0u;               /* текущий уровень мигания    */
static uint32_t s_t0       = 0u;               /* VTICK старта окна фазы     */
static uint32_t s_t_ms     = 0u;               /* длительность окна, мс      */
static uint32_t s_tb       = 0u;               /* VTICK последнего переключения blink */

/* ---------------------------------------------------------------------------
 * Готовые сценарии
 * -------------------------------------------------------------------------*/

/* ВКЛЮЧЕНИЕ ПРИБОРА - дословный порт АКТИВНОЙ ветки (#else) Button_Run_Start
   из Avis (docs/Avis_Menu_Functionality.c.md, строки 1224..1240):

       VIBRO_ON();  Delay(500); VIBRO_OFF();
       Delay(200);                                  <- пауза перед звуком
       SOUND_ON();  SET_ON(ALARM_LED_1);            <- «короткий сигнал»
       Delay(200);
       SOUND_OFF(); SET_OFF(ALARM_LED_1);
       Delay(200);                                  <- пауза между пиками
       SOUND_ON();  SET_ON(ALARM_LED_2);            <- «длинный сигнал»
       Delay(800);
       SOUND_OFF(); SET_OFF(ALARM_LED_2);

   Строка таблицы = одна фаза: звук и свет стартуют ОДНОВРЕМЕННО (одна маска,
   как SOUND_ON+SET_ON в одном такте Avis), фазы идут отдельными строками
   (указание автора: «свет, звук и вибро - отдельными фазами»); t_off_ms фазы -
   это Delay() ПОСЛЕ неё в Avis, у последней фазы паузы нет.

   Звук - BEEP (audio_beep(), клип 240 мс из внутренней flash), а не SOUND_ON
   Avis (непрерывный тон): писк не зависит от образа внешней SPI-flash.
   Плата: клип 240 мс чуть длиннее окна 200 мс «короткого сигнала» (доигрывает
   в паузе, phase_off его не обрывает) и короче окна 800 мс «длинного» (LED2
   горит дольше звука). Если нужен ровный тон ТОЧНО как в Avis - замените маску
   BEEP на SOUND и sound_idx на SND_TONE_1K (3 с): тогда phase_off() оборвёт
   клип ровно концом окна (200/800 мс), как SOUND_OFF().                     */
static const notify_phase_t s_poweron_phases[] =
{
  /*  маска каналов (notify_ch_t)              период пауза повт миг sound_idx
      sound_idx - клип SND_* (audio_ids.h) ТОЛЬКО для бита NOTIFY_CH_SOUND;
      для BEEP/VIBRO/LED поле не используется - поэтому здесь 0 (в прежней
      таблице этот 0 сбивал с толку: «какой это звук, SND_KOLOKOL_1?» - никакой,
      SND_KOLOKOL_1 = 0 лишь совпадал по значению с индексом колокола).      */
  { NOTIFY_CH_VIBRO,                               500u, 200u, 1u, 0u, 0u },  /* вибро 0.5 с          */
  { (uint8_t)(NOTIFY_CH_BEEP | NOTIFY_CH_LED1),    200u, 200u, 1u, 0u, 0u },  /* короткий сигнал      */
  { (uint8_t)(NOTIFY_CH_BEEP | NOTIFY_CH_LED2),    800u,   0u, 1u, 0u, 0u },  /* длинный сигнал       */
};
const notify_seq_t NotifySeq_PowerOn =
{
  .ph    = s_poweron_phases,
  .n     = (uint8_t)(sizeof(s_poweron_phases) / sizeof(s_poweron_phases[0])),
  .prio  = NOTIFY_PRIO_UI,
  .count = 1u                       /* один прогон, как в Avis               */
};

/* Таблица тревог - порт вызовов LedSoundVibroPlay(id, pulse, pause, blink,
   block_sound, block_led, test_sound) из Indication() Avis: pulse/pause в
   единицах 100 мс, перевод в мс - при старте (NOTIFY_ALARM_MS); block_sound=ON
   -> без BEEP/SOUND, block_led=ON -> без LED; у Avis звук+вибро идут вместе,
   поэтому вибро в маске всегда, звук - где block_sound=OFF. blink=1 у Avis
   (mode_blink_led) -> SET_TGL обеих LED: мигают ОБА светодиода фазы.
   count по умолчанию - 1 пачка; «тревога до подтверждения» = Notify_AlarmEx
   с count=0 (NOTIFY_REPEAT_FOREVER) и Notify_Stop() по квитанции.            */
typedef struct
{
  uint8_t  ch;         /* маска каналов                                      */
  uint8_t  pulse;      /* период, единицы 100 мс (time_pulse Avis)           */
  uint8_t  pause;      /* пауза, единицы 100 мс (time_pause Avis)            */
  uint8_t  blink;      /* 1 = мигать LED (mode_blink_led Avis)               */
  uint16_t sound_idx;  /* клип образа для бита SOUND (BEEP игнорирует)       */
} notify_alarm_def_t;

/* Индексируется id тревоги (0x00 не используется - у Avis это «всё выкл»). */
static const notify_alarm_def_t s_alarm_tab[16] =
{
  /* 0x00 */ { 0u, 0u, 0u, 0u, 0u },                                  /* - */
  /* 0x01 SYSERR   */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_LED1 | NOTIFY_CH_LED2),
                         10u, 5u, 0u, NOTIFY_ALARM_SOUND_IDX },   /* block_sound=ON */
  /* 0x02 EXCEEDED */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP | NOTIFY_CH_LED1 | NOTIFY_CH_LED2),
                         10u, 2u, 0u, NOTIFY_ALARM_SOUND_IDX },
  /* 0x03 LIMIT3   */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP | NOTIFY_CH_LED1 | NOTIFY_CH_LED2),
                          5u, 3u, 0u, NOTIFY_ALARM_SOUND_IDX },
  /* 0x04 LIMIT2   */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP | NOTIFY_CH_LED1 | NOTIFY_CH_LED2),
                          5u, 5u, 0u, NOTIFY_ALARM_SOUND_IDX },
  /* 0x05 LIMIT1   */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP | NOTIFY_CH_LED1 | NOTIFY_CH_LED2),
                          6u, 9u, 1u, NOTIFY_ALARM_SOUND_IDX },   /* blink=ON */
  /* 0x06 LOWBAT   */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP | NOTIFY_CH_LED1 | NOTIFY_CH_LED2),
                          2u, 3u, 0u, NOTIFY_ALARM_SOUND_IDX },
  /* 0x07 SENSFAIL */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_LED1 | NOTIFY_CH_LED2),
                         10u, 5u, 0u, NOTIFY_ALARM_SOUND_IDX },   /* block_sound=ON */
  /* 0x08 SOS      */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP | NOTIFY_CH_LED1 | NOTIFY_CH_LED2),
                          6u, 4u, 1u, NOTIFY_ALARM_SOUND_IDX },   /* blink=ON */
  /* 0x09 GROUP    */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP | NOTIFY_CH_LED1 | NOTIFY_CH_LED2),
                         10u, 5u, 1u, NOTIFY_ALARM_SOUND_IDX },   /* blink=ON */
  /* 0x0A BACKGR   */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP | NOTIFY_CH_LED1 | NOTIFY_CH_LED2),
                         10u, 2u, 1u, NOTIFY_ALARM_SOUND_IDX },   /* blink=ON */
  /* 0x0B CHARGE   */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP),
                          6u, 2u, 0u, NOTIFY_ALARM_SOUND_IDX },   /* block_led=ON */
  /* 0x0C */ { 0u, 0u, 0u, 0u, 0u },                                  /* - */
  /* 0x0D */ { 0u, 0u, 0u, 0u, 0u },                                  /* - */
  /* 0x0E */ { 0u, 0u, 0u, 0u, 0u },                                  /* - */
  /* 0x0F TEST     */ { (uint8_t)(NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP),
                          5u, 10u, 0u, NOTIFY_ALARM_SOUND_IDX },  /* block_led=ON, test_sound=ON */
};

/* ---------------------------------------------------------------------------
 * Каналы: включить/выключить маску фазы
 * -------------------------------------------------------------------------*/

/* Включить все каналы маски (Avis: SOUND_ON+VIBRO_ON+SET_ON(LED) одним тактом
   LedSoundVibroPlay). Бит SOUND стартует клип образа (sound_idx), бит BEEP -
   писк внутренней flash; blink-биты LED остаются ВЫКЛ до первого тика
   мигания (blink_on=0) - как state_step у Avis.                              */
static void phase_on(const notify_phase_t *p)
{
  if ((p->ch & (uint8_t)NOTIFY_CH_LED1) != 0u)
  {
    SET_ON(ALARM_LED_1);
  }
  if ((p->ch & (uint8_t)NOTIFY_CH_LED2) != 0u)
  {
    SET_ON(ALARM_LED_2);
  }
  if ((p->ch & (uint8_t)NOTIFY_CH_VIBRO) != 0u)
  {
    Vibro_On();
  }
  if ((p->ch & (uint8_t)NOTIFY_CH_BEEP) != 0u)
  {
    audio_beep();                                   /* 240 мс, внутренняя flash */
  }
  if ((p->ch & (uint8_t)NOTIFY_CH_SOUND) != 0u)
  {
    (void)audio_play_now(p->sound_idx);             /* клип образа (SND_*)      */
  }

  /* Ждать конец воспроизведения (а не таймер) - только если в маске есть звук
     и период фазы задан как NOTIFY_WAIT_END.                                 */
  s_wait_end = (((p->ch & (uint8_t)(NOTIFY_CH_BEEP | NOTIFY_CH_SOUND)) != 0u) &&
                (p->t_on_ms == NOTIFY_WAIT_END)) ? 1u : 0u;
  s_blink    = p->blink;
  s_blink_on = 1u;      /* LED уже включены (SET_ON выше)                     */
  s_t0       = VTICK_MS();
  s_tb       = s_t0;
  s_t_ms     = s_wait_end ? (uint32_t)NOTIFY_WAIT_GRACE_MS : (uint32_t)p->t_on_ms;
  s_state    = (uint8_t)NS_ON;
}

/* Мигание LED фазы (blink=1): переключение каждые NOTIFY_BLINK_PERIOD_MS -
   порт SET_TGL(ALARM_LED_1/2) каждого такта LedSoundVibroPlay Avis.
   КОНТЕКСТ: Notify_Tick, состояние NS_ON.                                   */
static void phase_blink_tick(const notify_phase_t *p)
{
  if ((VTICK_ELAPSED_MS(s_tb) >= (uint32_t)NOTIFY_BLINK_PERIOD_MS) &&
      ((p->ch & (uint8_t)(NOTIFY_CH_LED1 | NOTIFY_CH_LED2)) != 0u))
  {
    s_tb = VTICK_MS();
    s_blink_on = (uint8_t)(s_blink_on ^ 1u);
    if (s_blink_on != 0u)
    {
      if ((p->ch & (uint8_t)NOTIFY_CH_LED1) != 0u) { SET_ON(ALARM_LED_1);  }
      if ((p->ch & (uint8_t)NOTIFY_CH_LED2) != 0u) { SET_ON(ALARM_LED_2);  }
    }
    else
    {
      if ((p->ch & (uint8_t)NOTIFY_CH_LED1) != 0u) { SET_OFF(ALARM_LED_1); }
      if ((p->ch & (uint8_t)NOTIFY_CH_LED2) != 0u) { SET_OFF(ALARM_LED_2); }
    }
  }
}

/* Выключить все каналы маски. Бит BEEP не трогаем: писк audio_beep() доигрывает
   сам (240 мс < периода фаз), как и в прежней версии движка.                 */
static void phase_off(const notify_phase_t *p)
{
  if ((p->ch & (uint8_t)NOTIFY_CH_LED1) != 0u)
  {
    SET_OFF(ALARM_LED_1);
  }
  if ((p->ch & (uint8_t)NOTIFY_CH_LED2) != 0u)
  {
    SET_OFF(ALARM_LED_2);
  }
  if ((p->ch & (uint8_t)NOTIFY_CH_VIBRO) != 0u)
  {
    Vibro_Off();
  }
  if ((p->ch & (uint8_t)NOTIFY_CH_SOUND) != 0u)
  {
    audio_stop();                                   /* обрыв, если ON < клипа */
  }
}

/* Полный останов: погасить активные каналы и сбросить машину.               */
static void seq_halt(void)
{
  if ((s_seq != NULL) && (s_state != (uint8_t)NS_IDLE) && (s_idx < s_seq->n))
  {
    /* Первая фаза - из рабочей копии s_phase (как и в Tick): у тревоги таблица
       живёт в сменном слоте Notify_AlarmEx, и к моменту стопа слот мог быть
       перезаписан свежей тревогой с другой маской - гасить надо ТО, что
       включали, иначе каналы прежней маски залипнут.                        */
    phase_off((s_idx == 0u) ? &s_phase : &s_seq->ph[s_idx]);
  }
  s_seq      = NULL;
  s_state    = (uint8_t)NS_IDLE;
  s_idx      = 0u;
  s_rep      = 0u;
  s_cycles   = 0u;
  s_wait_end = 0u;
  s_blink    = 0u;
  s_blink_on = 0u;
}

/* Старт фазы s_idx текущего s_seq (повторы инициализируются). Первая фаза
   берётся из рабочей копии s_phase (override Notify_StartEx/Notify_AlarmEx),
   остальные - из const-таблицы сценария.                                    */
static void seq_begin_phase(void)
{
  const notify_phase_t *p = (s_idx == 0u) ? &s_phase : &s_seq->ph[s_idx];
  s_rep = (p->repeats > 0u) ? p->repeats : 1u;
  phase_on(p);
}

/* Применить запрос старта: копия фазы + override + счётчик повторов сценария.
   КОНТЕКСТ: Notify_Tick (поле s_req_* уже прочитаны вызывающим).             */
static void seq_apply(const notify_seq_t *ns)
{
  uint16_t period = s_req_period;
  uint16_t pause  = s_req_pause;
  uint8_t  rep    = s_req_rep;
  uint8_t  cnt    = s_req_count;

  s_phase = ns->ph[0];                    /* рабочая копия первой фазы        */
  if (period != 0u) { s_phase.t_on_ms  = period; }
  if (pause  != 0u) { s_phase.t_off_ms = pause;  }
  if (rep    != 0u) { s_phase.repeats  = rep;    }

  s_seq    = ns;
  s_idx    = 0u;
  s_cycles = (cnt == 0u) ? 0u : cnt;      /* 0 = вечно (NOTIFY_REPEAT_FOREVER)*/
  seq_begin_phase();
}

/* ---------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Сброс движка. КОНТЕКСТ: Measure Task (Menu_Init), до первого Tick.        */
void Notify_Init(void)
{
  s_req_seq    = NULL;
  s_req_stop   = 0u;
  s_req_period = 0u;
  s_req_pause  = 0u;
  s_req_rep    = 0u;
  s_req_count  = 1u;
  seq_halt();
}

/* Запрос старта сценария с параметрами по умолчанию из таблицы (count из
   структуры). КОНТЕКСТ: ЛЮБОЙ поток (только volatile-запись; применится в
   ближайший Notify_Tick, ~10 мс). Отказ по приоритету - в лог из Tick
   (владелец движка).                                                        */
void Notify_Start(const notify_seq_t *seq)
{
  if ((seq == NULL) || (seq->ph == NULL) || (seq->n == 0u))
  {
    return;
  }
  s_req_period = 0u;                /* 0 = «взять из таблицы»                */
  s_req_pause  = 0u;
  s_req_rep    = 0u;
  s_req_count  = seq->count;
  s_req_stop   = 0u;                /* свежий старт отменяет прежний стоп    */
  s_req_seq    = seq;               /* публикация запроса - ПОСЛЕДНЕЙ записью*/
}

/* То же, но с ЗАДАВАЕМЫМИ периодом/паузой/повторами первой фазы и числом
   прогонов всей последовательности (указание автора: «задать период и число
   повторов»). 0 в period/pause/repeats = значение из таблицы; 0 в count =
   NOTIFY_REPEAT_FOREVER (до Notify_Stop/вытеснения). КОНТЕКСТ: ЛЮБОЙ поток. */
void Notify_StartEx(const notify_seq_t *seq,
                    uint16_t period_ms, uint16_t pause_ms,
                    uint8_t repeats, uint8_t count)
{
  if ((seq == NULL) || (seq->ph == NULL) || (seq->n == 0u))
  {
    return;
  }
  s_req_period = period_ms;
  s_req_pause  = pause_ms;
  s_req_rep    = repeats;
  s_req_count  = count;
  s_req_stop   = 0u;
  s_req_seq    = seq;
}

/* Тревога по id (NOTIFY_ALARM_*) с параметрами ПО УМОЛЧАНИЮ из таблицы
   s_alarm_tab (порт LedSoundVibroPlay Avis: период/пауза в единицах 100 мс,
   звук+вибро+свет одновременно одной фазой). 1 пачка, без повторов.
   КОНТЕКСТ: ЛЮБОЙ поток.                                                    */
void Notify_Alarm(uint8_t alarm_id)
{
  Notify_AlarmEx(alarm_id, 0u, 0u, 0u, 1u);
}

/* Тревога с ЗАДАВАЕМЫМИ периодом/паузой/миганием/числом повторов:
     alarm_id  - NOTIFY_ALARM_* (таблица s_alarm_tab: маска каналов, значения
                 по умолчанию);
     period_ms - период пачки, мс; 0 = по умолчанию (pulse*100 из таблицы);
     pause_ms  - пауза, мс; 0 = по умолчанию (pause*100 из таблицы);
     blink     - 1 = мигать LED фазы (100 мс, mode_blink_led Avis), 0 = ровно;
     count     - сколько раз прогнать пачку; 0 = NOTIFY_REPEAT_FOREVER
                 (тревога до подтверждения: Notify_Stop() по квитанции).
   КОНТЕКСТ: ЛЮБОЙ поток.                                                    */
void Notify_AlarmEx(uint8_t alarm_id,
                    uint16_t period_ms, uint16_t pause_ms,
                    uint8_t blink, uint8_t count)
{
  /* Слоты запроса: фаза/таблица тревоги живут в СТАТИКЕ до применения в Tick
     (Notify_StartEx принимает указатель, а движок читает его асинхронно).
     Два чередующихся слота (счётчик s_alarm_slot): Tick копирует фазу
     опубликованного слота в свою s_phase, а следующий вызов пишет ДРУГОЙ слот
     - гонки «писатель vs читатель» нет даже при вызове из двух потоков.     */
  static notify_phase_t s_alarm_phase[2];
  static notify_seq_t   s_alarm_seq[2];
  static volatile uint8_t s_alarm_slot = 0u;
  const notify_alarm_def_t *def;
  notify_phase_t ph;
  uint8_t slot;

  if (alarm_id >= (uint8_t)(sizeof(s_alarm_tab) / sizeof(s_alarm_tab[0])))
  {
    return;                         /* неизвестный id - не тревожим            */
  }
  def = &s_alarm_tab[alarm_id];
  if (def->ch == 0u)
  {
    return;                         /* id зарезервирован (0x00/0x0C..0x0E)     */
  }

  /* Фаза тревоги: маска каналов из таблицы; период/пауза - из вызова либо
     по умолчанию (единицы Avis 100 мс -> мс, с защитой от переполнения);
     звук+вибро+свет стартуют ОДНОВРЕМЕННО - одной фазой (как LedSoundVibroPlay). */
  ph.ch        = def->ch;
  ph.t_on_ms   = (period_ms != 0u) ? period_ms : NOTIFY_ALARM_MS(def->pulse, 65500u);
  ph.t_off_ms  = (pause_ms  != 0u) ? pause_ms  : NOTIFY_ALARM_MS(def->pause, 65500u);
  ph.repeats   = 1u;                /* «пачка» = 1 ВКЛ/ВЫКЛ; повторы - count   */
  ph.blink     = blink;
  ph.sound_idx = def->sound_idx;

  slot = (uint8_t)(s_alarm_slot ^ 1u);
  s_alarm_phase[slot] = ph;
  s_alarm_seq[slot].ph    = &s_alarm_phase[slot];
  s_alarm_seq[slot].n     = 1u;
  s_alarm_seq[slot].prio  = NOTIFY_PRIO_ALARM;   /* тревога вытесняет UI/сервис*/
  s_alarm_seq[slot].count = count;               /* 0 = до подтверждения      */
  s_alarm_slot = slot;
  Notify_StartEx(&s_alarm_seq[slot], 0u, 0u, 0u, count);
}

/* Запрос стопа (погасить каналы). КОНТЕКСТ: ЛЮБОЙ поток.                    */
void Notify_Stop(void)
{
  s_req_seq  = NULL;
  s_req_stop = 1u;
}

/* 1 = сценарий идёт или запрошен.                                           */
uint8_t Notify_Busy(void)
{
  return ((s_state != (uint8_t)NS_IDLE) || (s_req_seq != NULL)) ? 1u : 0u;
}

/* Движок. КОНТЕКСТ: ТОЛЬКО поток LVGL Task (lvgl_task_function в
   main_vector.c), вызов ~каждые 10 мс. Неблокирующий: вся работа - сравнение
   дедлайна и, по событию, включение/выключение каналов маски.               */
void Notify_Tick(void)
{
  const notify_phase_t *p;

  /* 1) Запрос стопа - гасим каналы.                                         */
  if (s_req_stop != 0u)
  {
    s_req_stop = 0u;
    seq_halt();
  }

  /* 2) Запрос старта - применяем с учётом приоритета.                       */
  if (s_req_seq != NULL)
  {
    /* s_req_seq объявлен volatile (пишут чужие потоки): читаем ОДИН раз и
       явным приведением снимаем volatile - дальше указателем владеет движок
       (s_seq не volatile). Таблицы сценариев - const во flash либо static-
       копия Notify_AlarmEx, поэтому снятие квалификатора безопасно; без каста
       GCC ругался -Wdiscarded-qualifiers.                                   */
    const notify_seq_t *ns = (const notify_seq_t *)s_req_seq;
    s_req_seq = NULL;
    if ((s_seq == NULL) || (ns->prio >= s_seq->prio))
    {
      seq_halt();          /* вытеснение: погасить текущий сценарий          */
      seq_apply(ns);
      LOG_I(VLOG_M_SYS,  "notify: seq start prio=%u n=%u count=%u period=%u pause=%u",
            (uint32_t)ns->prio, (uint32_t)ns->n, (uint32_t)s_cycles,
            (uint32_t)s_phase.t_on_ms, (uint32_t)s_phase.t_off_ms);
    }
    else
    {
      LOG_W(VLOG_M_SYS,  "notify: seq REJECT prio=%u < cur %u",
            (uint32_t)ns->prio, (uint32_t)s_seq->prio);
    }
  }

  if ((s_seq == NULL) || (s_state == (uint8_t)NS_IDLE))
  {
    return;
  }
  p = (s_idx == 0u) ? &s_phase : &s_seq->ph[s_idx];

  /* 3) Фаза ON: ждём окно t_on (или защитную выдержку при WAIT_END), затем
        для звука - ещё и фактический конец воспроизведения; blink - мигаем.  */
  if (s_state == (uint8_t)NS_ON)
  {
    if (s_blink != 0u)
    {
      phase_blink_tick(p);
    }
    if (VTICK_ELAPSED_MS(s_t0) < s_t_ms)
    {
      return;
    }
    if ((s_wait_end != 0u) && (audio_is_busy() != 0u))
    {
      return;              /* звук ещё идёт - опрос каждый тик                */
    }
    phase_off(p);
    s_state = (uint8_t)NS_OFF;
    s_t0    = VTICK_MS();
    s_t_ms  = (uint32_t)p->t_off_ms;
    return;
  }

  /* 4) Фаза OFF: пауза кончилась - повтор фазы / следующая фаза / повтор
        сценария (count, 0 = вечно) / стоп.                                   */
  if (VTICK_ELAPSED_MS(s_t0) < s_t_ms)
  {
    return;
  }
  if (s_rep > 1u)
  {
    s_rep--;
    phase_on(p);           /* повтор той же фазы                              */
    return;
  }
  s_idx++;
  if (s_idx < s_seq->n)
  {
    seq_begin_phase();     /* следующая фаза                                  */
    return;
  }
  if ((s_cycles == 0u) || (s_cycles > 1u))
  {
    if (s_cycles > 1u)
    {
      s_cycles--;
    }
    s_idx = 0u;
    seq_begin_phase();     /* повтор сценария (0 = до Notify_Stop/вытеснения) */
    return;
  }
  seq_halt();              /* сценарий завершён                               */
}
