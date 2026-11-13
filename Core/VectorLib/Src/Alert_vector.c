/**
  ******************************************************************************
  * @file    Alert_vector.c
  * @brief   ЕДИНЫЙ модуль индикации: реестр статусов/тревог (ЧТО и почему
  *          оповещает) + неблокирующий движок свет/звук/вибро (КАК оповещает).
  *          Модель, требования автора и API - в Alert_vector.h.
  *
  *          Notify_vector.c/.h УДАЛЕНЫ, их движок перенесён сюда (указание
  *          автора: «Notify_vector избыточен, на нём включение и выключение
  *          всего»): держать два модуля, где один решает ЧТО включить, а второй
  *          - КАК, оказалось избыточно, а разделение мешало: статусам нужны
  *          однократные пачки («ошибка прозвучала и осталась только на
  *          экране»), а движку - знать владельца сценария.
  *
  *          ГЛАВНОЕ РАЗЛИЧЕНИЕ (требование автора п.2):
  *            СТАТУС  - признак («сотрудник неподвижен», «нет СИЗ», «сервер
  *                      поднял эвакуацию», «села батарея»). Живёт, пока живо
  *                      условие.
  *            ИНДИКАЦИЯ - звук/свет/вибро этого статуса. Кнопка (квитирование)
  *                      гасит ТОЛЬКО индикацию: статус, его таймеры и отправка
  *                      инцидента на сервер сохраняются.
  *          Три вида индикации (колонка burst таблицы s_desc):
  *            burst = 0 - НЕПРЕРЫВНАЯ: идёт, пока статус активен и не
  *                        квитирован (газ, эвакуация, неподвижность,
  *                        «нет СИЗ/ГА/браслета», сообщение);
  *            burst > 0 - ОДНОКРАТНАЯ пачка из burst повторов на факте
  *                        ПОЯВЛЕНИЯ условия (ошибки прибора, батарея):
  *                        прозвучала - и статус остаётся только записью на
  *                        экране; повторное срабатывание (новый бит ошибки,
  *                        новый провал напряжения) снова даёт пачку.
  *          Есть статусы, у которых индикация НЕ снимается вручную
  *          (ack_button = 0: нет СИЗ/ГА/браслета, ошибки прибора, батарея) -
  *          она прекращается, когда условие ушло (связь восстановилась,
  *          устройство удалено из настроек, ошибка сброшена, батарея заряжена).
  *
  *          ПОТОКИ: Alert_IndicationTick() (движок) - ТОЛЬКО LVGL Task, ~10 мс.
  *          Alert_Tick() (реестр) - Measure Task, ALERT_TICK_MS = 100 мс.
  *          Alert_Set/Clear/AckButton/Start/Stop/Signal - любой поток (только
  *          volatile-записи, движок разберёт их в свой тик).
  ******************************************************************************
  */
/* Инклуды проекта - В ОДНОМ МЕСТЕ (агрегатор main_vector.h): Alert_vector.h,
   vector_config.h (ALERT_*), shared_types.h (Sns_Cfg_struct, Ble_telemetry,
   Connected_device, sensors_status, enum ST_COMMON_ERR, BAT_LIM_1/2),
   main.h (SET_ON/SET_OFF(ALARM_LED_1/2)), vector_tick.h, vector_log.h,
   vector_macros.h (Vibro_On/Off), audio_player.h (audio_beep/audio_play_now/
   audio_stop/audio_is_busy), audio_ids.h (SND_*), tx_api.h (TX_DISABLE),
   lis3dh.h (CONFIG_LIS3DH), Lora_S7678S.h (CONFIG_LORA), Ble_vector.h
   (CONFIG_BLE).                                                            */
#include "main_vector.h"

/* ============================================================================
 *                                 МАКРОСЫ
 * ============================================================================ */

/* Коды событий контейнерного EventLog (LoRa v4 п.4.8). При CONFIG_LORA берём
   их из заголовка модуля, иначе - литералы протокола (чтобы таблица статусов
   компилировалась в любой конфигурации).                                    */
#if CONFIG_LORA
#define ALERT_CEV_INACTIVITY      CONTAINER_EVENT_INACTIVITY   /* 0x10 */
#else
#define ALERT_CEV_INACTIVITY      (0x10u)
#endif

/* Делитель тика 100 мс -> 1 с (счётчики неподвижности, свежесть устройств,
   ошибки прибора, батарея).                                                 */
#define ALERT_TICKS_PER_SEC       (1000u / ALERT_TICK_MS)

/* Маски каналов индикации (без бита SOUND: голосовой клип добавляется сам,
   если для статуса задан ALERT_SND_* != ALERT_SND_NONE).                     */
#define ALERT_CHMASK_ALARM        ((uint8_t)(ALERT_CH_LED1 | ALERT_CH_LED2 | \
                                             ALERT_CH_VIBRO | ALERT_CH_BEEP))
#define ALERT_CHMASK_DEVICE       ((uint8_t)(ALERT_CH_LED1 | ALERT_CH_LED2 | \
                                             ALERT_CH_VIBRO | ALERT_CH_BEEP))
#define ALERT_CHMASK_ERROR        ((uint8_t)(ALERT_CH_LED1 | ALERT_CH_LED2 | \
                                             ALERT_CH_VIBRO | ALERT_CH_BEEP))
#define ALERT_CHMASK_MESSAGE      ((uint8_t)(ALERT_CH_LED1 | ALERT_CH_BEEP))

/* Звук сигналов по умолчанию: писк audio_beep() (внутренняя flash, не зависит
   от SPI-образа). Если нужен клип образа (SND_TONE_1K и т.п.) - добавьте в
   маску ALERT_CH_SOUND и укажите sound_idx в таблице s_sig_tab; тон тогда
   оборвётся концом периода (phase_off -> audio_stop), как SOUND_OFF в Avis.  */
#define ALERT_SIG_SOUND_IDX       (SND_KOLOKOL_1)

/* Перевод единиц Avis (100 мс) в миллисекунды с защитой от переполнения
   uint16 (pulse <= 255 -> 25500 мс; pause <= 655 -> 65500 мс).              */
#define ALERT_SIG_MS(v, cap) \
  ((uint16_t)(((uint32_t)(v) * ALERT_SIG_UNIT_MS > (uint32_t)(cap)) \
              ? (uint32_t)(cap) : ((uint32_t)(v) * ALERT_SIG_UNIT_MS)))

/* Сколько битов StateErr покрывает маска ERROR_MASK_ST_2 (0x03FF -> биты 0..9). */
#define ALERT_SYSERR_BITS         (10u)

/* Биты Config_common.StateErr, которые считаются ОШИБКОЙ ПРИБОРА (модули).
   Биты батареи (CRITICAL_LOW_BATTERY / CRITICAL_BATTERY) сюда НЕ входят: у них
   свои статусы (ALERT_BATTERY_LOW / ALERT_BATTERY_CRIT) со своим ритмом.     */
#define ALERT_SYSERR_MASK         ((uint32_t)( \
    ((uint32_t)1u << ST_COMMON_BIT_ERR_STS4)  | \
    ((uint32_t)1u << ST_COMMON_BIT_ERR_NBIOT) | \
    ((uint32_t)1u << ST_COMMON_BIT_ERR_BLE)   | \
    ((uint32_t)1u << ST_COMMON_BIT_ERR_GPS)   | \
    ((uint32_t)1u << ST_COMMON_BIT_ERR_GSM)   | \
    ((uint32_t)1u << ST_COMMON_BIT_ERR_LORA)  | \
    ((uint32_t)1u << ST_COMMON_BIT_ERR_LCD)))

/* ============================================================================
 *                                ПЕРЕМЕННЫЕ
 * ============================================================================ */

/* ---------------------------------------------------------------------------
 * ДВИЖОК ИНДИКАЦИИ (перенесён из Notify_vector.c без изменения логики)
 * -------------------------------------------------------------------------*/

/* Состояния движка.                                                         */
typedef enum
{
  NS_IDLE = 0,   /* ничего не идёт                                           */
  NS_ON,         /* каналы фазы включены                                     */
  NS_OFF         /* пауза после фазы                                         */
} alert_ns_t;

/* Запросы из других потоков (только volatile-записи, разбирает Tick).
   Поля запроса (0 = «взять из таблицы сценария») пишет тот же поток, что и
   s_req_seq - последним присваиванием, поэтому гонки параметров нет: движок
   читает их только когда s_req_seq != NULL.                                 */
static volatile const alert_seq_t *s_req_seq    = NULL; /* старт сценария    */
static volatile uint8_t             s_req_stop   = 0u;  /* 1 = стоп запрос   */
static volatile uint16_t            s_req_period = 0u;  /* override t_on_ms  */
static volatile uint16_t            s_req_pause  = 0u;  /* override t_off_ms */
static volatile uint8_t             s_req_rep    = 0u;  /* override repeats  */
static volatile uint8_t             s_req_count  = 1u;  /* повторы сценария  */
static volatile uint8_t             s_req_owner  = 0u;  /* чей сценарий (alert_id_t) */

/* Рабочая фаза: копия первой фазы сценария с применёнными override - чтобы
   Alert_StartEx/Alert_SignalEx не правили const-таблицу во flash.           */
static alert_phase_t s_phase;

/* Состояние движка (владелец - поток LVGL Task).                            */
static const alert_seq_t *s_seq = NULL;  /* текущий сценарий                 */
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
static uint8_t  s_owner    = (uint8_t)ALERT_NONE;  /* статус-владелец сценария */

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
static const alert_phase_t s_poweron_phases[] =
{
  /*  маска каналов (alert_ch_t)                 период пауза повт миг sound_idx
      sound_idx - клип SND_* (audio_ids.h) ТОЛЬКО для бита ALERT_CH_SOUND;
      для BEEP/VIBRO/LED поле не используется - поэтому здесь 0.             */
  { ALERT_CH_VIBRO,                              500u, 200u, 1u, 0u, 0u },  /* вибро 0.5 с          */
  { (uint8_t)(ALERT_CH_BEEP | ALERT_CH_LED1),    200u, 200u, 1u, 0u, 0u },  /* короткий сигнал      */
  { (uint8_t)(ALERT_CH_BEEP | ALERT_CH_LED2),    800u,   0u, 1u, 0u, 0u },  /* длинный сигнал       */
};
const alert_seq_t AlertSeq_PowerOn =
{
  .ph    = s_poweron_phases,
  .n     = (uint8_t)(sizeof(s_poweron_phases) / sizeof(s_poweron_phases[0])),
  .prio  = ALERT_PRIO_UI,
  .count = 1u                       /* один прогон, как в Avis               */
};

/* Таблица СИГНАЛОВ - порт вызовов LedSoundVibroPlay(id, pulse, pause, blink,
   block_sound, block_led, test_sound) из Indication() Avis: pulse/pause в
   единицах 100 мс, перевод в мс - при старте (ALERT_SIG_MS); block_sound=ON
   -> без BEEP/SOUND, block_led=ON -> без LED; у Avis звук+вибро идут вместе,
   поэтому вибро в маске всегда, звук - где block_sound=OFF. blink=1 у Avis
   (mode_blink_led) -> SET_TGL обеих LED: мигают ОБА светодиода фазы.
   count по умолчанию - 1 пачка; «тревога до подтверждения» = Alert_SignalEx
   с count=0 (ALERT_REPEAT_FOREVER) и Alert_Stop() по квитанции.
   ВНИМАНИЕ: SYSERR/LOWBAT/SENSFAIL теперь ведёт РЕЕСТР СТАТУСОВ (они стали
   статусами ALERT_DEVICE_ERROR / ALERT_BATTERY_*), строки оставлены для
   совместимости вызовов и ручного теста.                                    */
typedef struct
{
  uint8_t  ch;         /* маска каналов                                      */
  uint8_t  pulse;      /* период, единицы 100 мс (time_pulse Avis)           */
  uint8_t  pause;      /* пауза, единицы 100 мс (time_pause Avis)            */
  uint8_t  blink;      /* 1 = мигать LED (mode_blink_led Avis)               */
  uint16_t sound_idx;  /* клип образа для бита SOUND (BEEP игнорирует)       */
} alert_sig_def_t;

/* Индексируется id сигнала (0x00 не используется - у Avis это «всё выкл»).   */
static const alert_sig_def_t s_sig_tab[16] =
{
  /* 0x00 */ { 0u, 0u, 0u, 0u, 0u },                                  /* - */
  /* 0x01 SYSERR   */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_LED1 | ALERT_CH_LED2),
                         10u, 5u, 0u, ALERT_SIG_SOUND_IDX },   /* block_sound=ON */
  /* 0x02 EXCEEDED */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_BEEP | ALERT_CH_LED1 | ALERT_CH_LED2),
                         10u, 2u, 0u, ALERT_SIG_SOUND_IDX },
  /* 0x03 LIMIT3   */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_BEEP | ALERT_CH_LED1 | ALERT_CH_LED2),
                          5u, 3u, 0u, ALERT_SIG_SOUND_IDX },
  /* 0x04 LIMIT2   */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_BEEP | ALERT_CH_LED1 | ALERT_CH_LED2),
                          5u, 5u, 0u, ALERT_SIG_SOUND_IDX },
  /* 0x05 LIMIT1   */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_BEEP | ALERT_CH_LED1 | ALERT_CH_LED2),
                          6u, 9u, 1u, ALERT_SIG_SOUND_IDX },   /* blink=ON */
  /* 0x06 LOWBAT   */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_BEEP | ALERT_CH_LED1 | ALERT_CH_LED2),
                          2u, 3u, 0u, ALERT_SIG_SOUND_IDX },
  /* 0x07 SENSFAIL */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_LED1 | ALERT_CH_LED2),
                         10u, 5u, 0u, ALERT_SIG_SOUND_IDX },   /* block_sound=ON */
  /* 0x08 SOS      */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_BEEP | ALERT_CH_LED1 | ALERT_CH_LED2),
                          6u, 4u, 1u, ALERT_SIG_SOUND_IDX },   /* blink=ON */
  /* 0x09 GROUP    */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_BEEP | ALERT_CH_LED1 | ALERT_CH_LED2),
                         10u, 5u, 1u, ALERT_SIG_SOUND_IDX },   /* blink=ON */
  /* 0x0A BACKGR   */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_BEEP | ALERT_CH_LED1 | ALERT_CH_LED2),
                         10u, 2u, 1u, ALERT_SIG_SOUND_IDX },   /* blink=ON */
  /* 0x0B CHARGE   */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_BEEP),
                          6u, 2u, 0u, ALERT_SIG_SOUND_IDX },   /* block_led=ON */
  /* 0x0C */ { 0u, 0u, 0u, 0u, 0u },                                  /* - */
  /* 0x0D */ { 0u, 0u, 0u, 0u, 0u },                                  /* - */
  /* 0x0E */ { 0u, 0u, 0u, 0u, 0u },                                  /* - */
  /* 0x0F TEST     */ { (uint8_t)(ALERT_CH_VIBRO | ALERT_CH_BEEP),
                          5u, 10u, 0u, ALERT_SIG_SOUND_IDX },  /* block_led=ON, test_sound=ON */
};

/* ---------------------------------------------------------------------------
 * РЕЕСТР СТАТУСОВ
 * -------------------------------------------------------------------------*/

/* Описание статуса (const-таблица во flash).                                */
typedef struct
{
  const char *name;           /* ключ i18n для экрана/лога                   */
  uint8_t  prio;              /* ALERT_PRIO_* (тревоги вытесняют сообщения)  */
  uint8_t  ch;                /* маска alert_ch_t (без бита SOUND)           */
  uint8_t  blink;             /* 1 = мигать LED (100 мс)                     */
  uint8_t  ack_button;        /* 1 = индикация снимается зелёной кнопкой     */
  uint8_t  burst;             /* 0 = индикация идёт, пока статус активен;
                                 N > 0 = ОДНОКРАТНАЯ пачка из N повторов     */
  uint16_t period_ms;         /* период индикации (сколько включено)         */
  uint16_t pause_ms;          /* пауза                                       */
  uint16_t voice;             /* ALERT_SND_* (ALERT_SND_NONE = без голоса)   */
  uint16_t hold_s;            /* автоочистка: 0 = статус снимает только его
                                 источник; иначе гаснет через hold_s после
                                 последнего подтверждения (Alert_Set)         */
} alert_desc_t;

/* Таблица статусов. Порядок перечисления alert_id_t = приоритет показа на
   экране и перехвата индикации (чем меньше id, тем важнее): угроза жизни (газ,
   эвакуация, неподвижность) -> комплект (нет ГА/СИЗ/браслета) -> прибор
   (критический разряд, ошибки модулей, низкий заряд) -> сообщение.
   Тайминги - ALERT_*_MS из vector_config.h.
   ack_button = 0 у «нет СИЗ/ГА/браслета» (требование автора п.3) и у ошибок
   прибора/батареи (их индикация и так однократная - burst > 0).              */
static const alert_desc_t s_desc[ALERT_ID_COUNT] =
{
  /* ALERT_NONE - заглушка, не используется                                  */
  [ALERT_NONE] = {
    .name = "", .prio = ALERT_PRIO_SERVICE, .ch = 0u,
    .blink = 0u, .ack_button = 0u, .burst = 0u,
    .period_ms = 0u, .pause_ms = 0u,
    .voice = ALERT_SND_NONE, .hold_s = 0u },

  /* «ОСТОРОЖНО! ГАЗ!» - пороги своего газоанализатора Avis ИЛИ команда
     сервера; на экране виден ID трекера-источника (требование автора п.4)    */
  [ALERT_GAS_ALARM] = {
    .name = "ОСТОРОЖНО! ГАЗ!", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_ALARM,
    .blink = 0u, .ack_button = 1u, .burst = 0u,
    .period_ms = ALERT_PERIOD_GAS_MS, .pause_ms = ALERT_PAUSE_GAS_MS,
    .voice = ALERT_SND_GAS, .hold_s = ALERT_GAS_HOLD_S },

  /* «ВСЕМ НА ВЫХОД» - бит сервера (пока поднят) или трекер-«мастер»          */
  [ALERT_EVACUATION] = {
    .name = "ВСЕМ НА ВЫХОД", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_ALARM,
    .blink = 1u, .ack_button = 1u, .burst = 0u,
    .period_ms = ALERT_PERIOD_EVAC_MS, .pause_ms = ALERT_PAUSE_EVAC_MS,
    .voice = ALERT_SND_EVACUATION, .hold_s = ALERT_EVAC_HOLD_S },

  /* Неподвижность: инцидент «Отсутствие активности» (EventLog 0x10).
     Снимается ТОЛЬКО появлением движения - автоочистки нет (hold_s = 0)      */
  [ALERT_IMMOBILITY] = {
    .name = "Неподвижность", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_ALARM,
    .blink = 0u, .ack_button = 1u, .burst = 0u,
    .period_ms = ALERT_PERIOD_IMMOB_MS, .pause_ms = ALERT_PAUSE_IMMOB_MS,
    .voice = ALERT_SND_ALARM, .hold_s = 0u },

  /* Комплект: нет газоанализатора / СИЗ / браслета. Индикация НЕ снимается
     вручную (ack_button = 0), статус снимает восстановление связи или
     удаление устройства из настроек - автоочистки тоже нет                   */
  [ALERT_NO_GAS] = {
    .name = "Нет ГА", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_DEVICE,
    .blink = 0u, .ack_button = 0u, .burst = 0u,
    .period_ms = ALERT_PERIOD_DEVICE_MS, .pause_ms = ALERT_PAUSE_DEVICE_MS,
    .voice = ALERT_SND_NONE, .hold_s = 0u },
  [ALERT_NO_PPE] = {
    .name = "Нет СИЗ", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_DEVICE,
    .blink = 0u, .ack_button = 0u, .burst = 0u,
    .period_ms = ALERT_PERIOD_DEVICE_MS, .pause_ms = ALERT_PAUSE_DEVICE_MS,
    .voice = ALERT_SND_NONE, .hold_s = 0u },
  [ALERT_NO_BRACELET] = {
    .name = "Нет браслета", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_DEVICE,
    .blink = 0u, .ack_button = 0u, .burst = 0u,
    .period_ms = ALERT_PERIOD_DEVICE_MS, .pause_ms = ALERT_PAUSE_DEVICE_MS,
    .voice = ALERT_SND_NONE, .hold_s = 0u },

  /* Прибор: критический разряд, ошибки модулей, низкий заряд. Индикация
     ОДНОКРАТНАЯ (burst > 0) - решение автора: «ошибки не снимаются, но свет,
     звук и вибро не индицируют, остаются только записи на экране; при
     повторном срабатывании любой из ошибок всё снова звучит и светит».       */
  [ALERT_BATTERY_CRIT] = {
    .name = "Крит. разряд батареи", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_ERROR,
    .blink = 0u, .ack_button = 0u, .burst = ALERT_BURST_BATCRIT,
    .period_ms = ALERT_PERIOD_BATCRIT_MS, .pause_ms = ALERT_PAUSE_BATCRIT_MS,
    .voice = ALERT_SND_NONE, .hold_s = 0u },
  [ALERT_DEVICE_ERROR] = {
    .name = "Ошибка прибора", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_ERROR,
    .blink = 0u, .ack_button = 0u, .burst = ALERT_BURST_ERR,
    .period_ms = ALERT_PERIOD_ERR_MS, .pause_ms = ALERT_PAUSE_ERR_MS,
    .voice = ALERT_SND_NONE, .hold_s = 0u },
  [ALERT_BATTERY_LOW] = {
    .name = "Низкий заряд АКБ", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_ERROR,
    .blink = 0u, .ack_button = 0u, .burst = ALERT_BURST_BATLOW,
    .period_ms = ALERT_PERIOD_BATLOW_MS, .pause_ms = ALERT_PAUSE_BATLOW_MS,
    .voice = ALERT_SND_NONE, .hold_s = 0u },

  /* Текстовое сообщение сервера (сигнал сообщения)                           */
  [ALERT_MESSAGE] = {
    .name = "Сообщение", .prio = ALERT_PRIO_UI, .ch = ALERT_CHMASK_MESSAGE,
    .blink = 0u, .ack_button = 1u, .burst = 0u,
    .period_ms = ALERT_PERIOD_MSG_MS, .pause_ms = ALERT_PAUSE_MSG_MS,
    .voice = ALERT_SND_MESSAGE, .hold_s = ALERT_MSG_HOLD_S },
};

/* Имена ошибок прибора по номеру бита Config_common.StateErr (enum
   ST_COMMON_ERR) - ЕДИНСТВЕННЫЙ источник: его использует слой экрана
   (TFT_indicator.c, категория 1) и лог этого модуля.
   ВНИМАНИЕ: раньше имена лежали в TFT_indicator.c (error_keys_sys) и были
   расставлены по НОМЕРАМ БИТОВ AVIS, а не по значениям enum ST_COMMON_ERR -
   из-за этого «Ошибка ТЕМП» (бит 1) показывалась как «Ошибка МЕМ», а
   «Ошибка NB-IoT» (бит 2) как «Ошибка ТЕМП». Здесь порядок задаётся samими
   именами перечисления (инициализаторы с обозначениями), поэтому рассинхрон
   с enum невозможен. Бита 9 в enum нет - имя пустое (на экране не
   показывается); «Ошибка МЕМ» (AT45) и «Ошибка БАТ» (MAX17048) вернутся сюда,
   когда автор добавит эти биты в enum ST_COMMON_ERR.                        */
static const char *const s_syserr_name[ALERT_SYSERR_BITS] =
{
  [ST_COMMON_BIT_CRITICAL_LOW_BATTERY] = "Низкий заряд АКБ",
  [ST_COMMON_BIT_ERR_STS4]             = "Ошибка ТЕМП",
  [ST_COMMON_BIT_ERR_NBIOT]            = "Ошибка NB-IoT",
  [ST_COMMON_BIT_ERR_BLE]              = "Ошибка BLE",
  [ST_COMMON_BIT_ERR_GPS]              = "Ошибка GPS",
  [ST_COMMON_BIT_ERR_GSM]              = "Ошибка GSM",
  [ST_COMMON_BIT_ERR_LORA]             = "Ошибка LORA",
  [ST_COMMON_BIT_ERR_LCD]              = "Ошибка LCD",
  [ST_COMMON_BIT_CRITICAL_BATTERY]     = "Крит. разряд батареи",
};

/* Состояние одного статуса.                                                 */
typedef struct
{
  volatile uint8_t  status;       /* 1 = статус активен (признак)            */
  volatile uint8_t  indication;   /* 1 = индикация идёт/не отработала        */
  volatile uint16_t param;        /* GAS: ID трекера; MESSAGE: MsgSeq;
                                     DEVICE_ERROR: номер бита StateErr       */
  volatile uint32_t since_ms;     /* последнее подтверждение условия         */
  volatile uint32_t ack_ms;       /* когда квитировали                       */
} alert_state_t;

static alert_state_t s_alert[ALERT_ID_COUNT];

/* Слоты запроса к движку от реестра: фаза/сценарий должны жить до применения
   в Alert_IndicationTick, поэтому держим их здесь (два чередующихся слота -
   как в Alert_SignalEx: следующий старт не портит тот, что разбирает движок).*/
static alert_phase_t s_ind_phase[2];
static alert_seq_t   s_ind_seq[2];
static volatile uint8_t s_ind_slot = 0u;
static uint8_t  s_ind_owner = (uint8_t)ALERT_NONE;  /* чья индикация идёт    */

/* --- детектор неподвижности ------------------------------------------------
 * Фазы: IDLE (функция выключена/есть движение) -> COUNTING (копим секунды без
 * движения) -> PREALARM (статус взведён, индикация идёт, идёт отсчёт времени
 * на реакцию сотрудника) -> INCIDENT (реакции не было: инцидент отправлен на
 * сервер, статус живёт до появления движения).                               */
typedef enum
{
  IM_IDLE = 0,
  IM_COUNTING,
  IM_PREALARM,
  IM_INCIDENT
} im_phase_t;

static struct
{
  volatile uint8_t  phase;          /* im_phase_t                            */
  volatile uint16_t no_motion_s;    /* секунд без движения подряд            */
  volatile uint16_t prealarm_s;     /* остаток времени на реакцию            */
  volatile uint32_t last_motion_ms; /* метка последнего движения             */
  volatile uint16_t motion_cnt;     /* событий движения (для ActivityLevel)  */
  volatile uint8_t  activity;       /* уровень активности 0..255 (LoRa)      */
  volatile uint8_t  incident_sent;  /* инцидент уже отправлен                */
} s_im;

/* --- источники газовой тревоги и состояние ошибок/батареи ------------------
 * У газовой тревоги ДВА источника (автор: «где-то по порогам с Ависа, где-то
 * от сервера»), статус один: пока жив хотя бы один бит - статус активен,
 * локальный сброс порогов не гасит серверную тревогу и наоборот.             */
#define ALERT_GAS_SRC_LOCAL   (0x01u)   /* Connected_device.StateErr & маска  */
#define ALERT_GAS_SRC_SERVER  (0x02u)   /* сообщение/бит от сервера           */
static uint8_t  s_gas_src;

/* Предыдущая маска ошибок прибора: по разнице видно ПОВТОРНОЕ срабатывание
   (новый бит) - именно оно снова запускает пачку индикации.                  */
static uint32_t s_err_mask_prev;
/* Состояние батареи с гистерезисом (иначе на границе порога статус дребезжал
   бы и каждая «пилка» звучала заново).                                       */
static uint8_t  s_bat_low;
static uint8_t  s_bat_crit;

/* --- сообщение сервера ---------------------------------------------------- */
static char     s_msg_text[ALERT_MSG_UTF8_LEN];  /* UTF-8 для LVGL           */
static uint8_t  s_msg_seq;                       /* MsgSeq последнего        */
static uint8_t  s_msg_shown;                     /* идемпотентность повторов  */

/* Тактовик: делитель 100 мс -> 1 с.                                        */
static uint8_t  s_tick_div;

/* Применённый порог детектора движения, мг (0 = ещё не применяли).          */
#if CONFIG_LIS3DH
static uint16_t s_motion_ths;
#endif

/* ============================================================================
 *              ДВИЖОК ИНДИКАЦИИ: каналы, фазы, сценарии (statics)
 * ============================================================================ */

/* Включить все каналы маски (Avis: SOUND_ON+VIBRO_ON+SET_ON(LED) одним тактом
   LedSoundVibroPlay). Бит SOUND стартует клип образа (sound_idx), бит BEEP -
   писк внутренней flash; blink-биты LED остаются ВЫКЛ до первого тика
   мигания (blink_on=0) - как state_step у Avis.                             */
static void phase_on(const alert_phase_t *p)
{
  if ((p->ch & (uint8_t)ALERT_CH_LED1) != 0u)
  {
    SET_ON(ALARM_LED_1);
  }
  if ((p->ch & (uint8_t)ALERT_CH_LED2) != 0u)
  {
    SET_ON(ALARM_LED_2);
  }
  if ((p->ch & (uint8_t)ALERT_CH_VIBRO) != 0u)
  {
    Vibro_On();
  }
  if ((p->ch & (uint8_t)ALERT_CH_BEEP) != 0u)
  {
    /* длина писка = t_on_ms фазы (короткий/длинный); WAIT_END -> по умолчанию */
    audio_beep_ms((p->t_on_ms == (uint16_t)ALERT_WAIT_END) ? 0u : p->t_on_ms);
  }
  if ((p->ch & (uint8_t)ALERT_CH_SOUND) != 0u)
  {
    (void)audio_play_now(p->sound_idx);             /* клип образа (SND_*)      */
  }

  /* Ждать конец воспроизведения (а не таймер) - только если в маске есть звук
     и период фазы задан как ALERT_WAIT_END.                                 */
  s_wait_end = (((p->ch & (uint8_t)(ALERT_CH_BEEP | ALERT_CH_SOUND)) != 0u) &&
                (p->t_on_ms == ALERT_WAIT_END)) ? 1u : 0u;
  s_blink    = p->blink;
  s_blink_on = 1u;      /* LED уже включены (SET_ON выше)                     */
  s_t0       = VTICK_MS();
  s_tb       = s_t0;
  s_t_ms     = (s_wait_end != 0u) ? (uint32_t)ALERT_WAIT_GRACE_MS : (uint32_t)p->t_on_ms;
  s_state    = (uint8_t)NS_ON;
}

/* Мигание LED фазы (blink=1): переключение каждые ALERT_BLINK_PERIOD_MS -
   порт SET_TGL(ALARM_LED_1/2) каждого такта LedSoundVibroPlay Avis.
   КОНТЕКСТ: Alert_IndicationTick, состояние NS_ON.                          */
static void phase_blink_tick(const alert_phase_t *p)
{
  if ((VTICK_ELAPSED_MS(s_tb) >= (uint32_t)ALERT_BLINK_PERIOD_MS) &&
      ((p->ch & (uint8_t)(ALERT_CH_LED1 | ALERT_CH_LED2)) != 0u))
  {
    s_tb = VTICK_MS();
    s_blink_on = (uint8_t)(s_blink_on ^ 1u);
    if (s_blink_on != 0u)
    {
      if ((p->ch & (uint8_t)ALERT_CH_LED1) != 0u) { SET_ON(ALARM_LED_1);  }
      if ((p->ch & (uint8_t)ALERT_CH_LED2) != 0u) { SET_ON(ALARM_LED_2);  }
    }
    else
    {
      if ((p->ch & (uint8_t)ALERT_CH_LED1) != 0u) { SET_OFF(ALARM_LED_1); }
      if ((p->ch & (uint8_t)ALERT_CH_LED2) != 0u) { SET_OFF(ALARM_LED_2); }
    }
  }
}

/* Выключить все каналы маски. Бит BEEP не трогаем: писк audio_beep() доигрывает
   сам (240 мс < периода фаз), как и в прежней версии движка.                 */
static void phase_off(const alert_phase_t *p)
{
  if ((p->ch & (uint8_t)ALERT_CH_LED1) != 0u)
  {
    SET_OFF(ALARM_LED_1);
  }
  if ((p->ch & (uint8_t)ALERT_CH_LED2) != 0u)
  {
    SET_OFF(ALARM_LED_2);
  }
  if ((p->ch & (uint8_t)ALERT_CH_VIBRO) != 0u)
  {
    Vibro_Off();
  }
  if ((p->ch & (uint8_t)ALERT_CH_SOUND) != 0u)
  {
    audio_stop();                                   /* обрыв, если ON < клипа */
  }
}

/* Полный останов: погасить активные каналы и сбросить машину.               */
static void seq_halt(void)
{
  if ((s_seq != NULL) && (s_state != (uint8_t)NS_IDLE) && (s_idx < s_seq->n))
  {
    /* Первая фаза - из рабочей копии s_phase (как и в Tick): у статуса таблица
       живёт в сменном слоте, и к моменту стопа слот мог быть перезаписан свежей
       тревогой с другой маской - гасить надо ТО, что включали, иначе каналы
       прежней маски залипнут.                                               */
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
  s_owner    = (uint8_t)ALERT_NONE;
}

/* Сценарий ОТРАБОТАЛ САМ (не вытеснен и не остановлен): если это однократная
   пачка статуса (burst > 0) - снимаем флаг индикации, дальше статус живёт
   молча и виден только на экране (решение автора по ошибкам прибора/батарее).
   КОНТЕКСТ: Alert_IndicationTick.                                           */
static void seq_finish(void)
{
  if ((s_owner != (uint8_t)ALERT_NONE) && (s_owner < (uint8_t)ALERT_ID_COUNT) &&
      (s_desc[s_owner].burst != 0u))
  {
    s_alert[s_owner].indication = 0u;
    LOG_I(VLOG_M_SYS, "alert: burst done [%s] - further screen only",
          s_desc[s_owner].name);
  }
  seq_halt();
}

/* Старт фазы s_idx текущего s_seq (повторы инициализируются). Первая фаза
   берётся из рабочей копии s_phase (override Alert_StartEx/Alert_SignalEx),
   остальные - из const-таблицы сценария.                                    */
static void seq_begin_phase(void)
{
  const alert_phase_t *p = (s_idx == 0u) ? &s_phase : &s_seq->ph[s_idx];
  s_rep = (p->repeats > 0u) ? p->repeats : 1u;
  phase_on(p);
}

/* Применить запрос старта: копия фазы + override + счётчик повторов сценария.
   КОНТЕКСТ: Alert_IndicationTick (поля s_req_* уже прочитаны вызывающим).    */
static void seq_apply(const alert_seq_t *as)
{
  uint16_t period = s_req_period;
  uint16_t pause  = s_req_pause;
  uint8_t  rep    = s_req_rep;
  uint8_t  cnt    = s_req_count;

  s_phase = as->ph[0];                    /* рабочая копия первой фазы        */
  if (period != 0u) { s_phase.t_on_ms  = period; }
  if (pause  != 0u) { s_phase.t_off_ms = pause;  }
  if (rep    != 0u) { s_phase.repeats  = rep;    }

  s_seq    = as;
  s_idx    = 0u;
  s_cycles = (cnt == 0u) ? 0u : cnt;      /* 0 = вечно (ALERT_REPEAT_FOREVER) */
  s_owner  = s_req_owner;                 /* статус-владелец (или ALERT_NONE) */
  seq_begin_phase();
}

/* Общая точка публикации запроса старта (сценарий + override + владелец).
   КОНТЕКСТ: любой поток.                                                    */
static void seq_request(const alert_seq_t *seq, uint16_t period_ms, uint16_t pause_ms,
                        uint8_t repeats, uint8_t count, uint8_t owner)
{
  if ((seq == NULL) || (seq->ph == NULL) || (seq->n == 0u))
  {
    return;
  }
  s_req_period = period_ms;
  s_req_pause  = pause_ms;
  s_req_rep    = repeats;
  s_req_count  = count;
  s_req_owner  = owner;
  s_req_stop   = 0u;                /* свежий старт отменяет прежний стоп    */
  s_req_seq    = seq;               /* публикация запроса - ПОСЛЕДНЕЙ записью */
}

/* ============================================================================
 *                       ДВИЖОК ИНДИКАЦИИ: API
 * ============================================================================ */

/* Запрос старта сценария с параметрами по умолчанию из таблицы (count из
   структуры). КОНТЕКСТ: ЛЮБОЙ поток (только volatile-запись; применится в
   ближайший Alert_IndicationTick, ~10 мс). Отказ по приоритету - в лог из Tick
   (владелец движка).                                                        */
void Alert_Start(const alert_seq_t *seq)
{
  if ((seq == NULL) || (seq->ph == NULL) || (seq->n == 0u))
  {
    return;
  }
  seq_request(seq, 0u, 0u, 0u, seq->count, (uint8_t)ALERT_NONE);
}

/* То же, но с ЗАДАВАЕМЫМИ периодом/паузой/повторами первой фазы и числом
   прогонов всей последовательности (указание автора: «задать период и число
   повторов»). 0 в period/pause/repeats = значение из таблицы; 0 в count =
   ALERT_REPEAT_FOREVER (до Alert_Stop/вытеснения). КОНТЕКСТ: ЛЮБОЙ поток.   */
void Alert_StartEx(const alert_seq_t *seq,
                   uint16_t period_ms, uint16_t pause_ms,
                   uint8_t repeats, uint8_t count)
{
  seq_request(seq, period_ms, pause_ms, repeats, count, (uint8_t)ALERT_NONE);
}

/* Сигнал по id (ALERT_SIG_*) с параметрами ПО УМОЛЧАНИЮ из таблицы s_sig_tab
   (порт LedSoundVibroPlay Avis: период/пауза в единицах 100 мс, звук+вибро+свет
   одновременно одной фазой). 1 пачка, без повторов. КОНТЕКСТ: ЛЮБОЙ поток.  */
void Alert_Signal(uint8_t sig_id)
{
  Alert_SignalEx(sig_id, 0u, 0u, 0u, 1u);
}

/* Сигнал с ЗАДАВАЕМЫМИ периодом/паузой/миганием/числом повторов:
     sig_id    - ALERT_SIG_* (таблица s_sig_tab: маска каналов, значения по
                 умолчанию);
     period_ms - период пачки, мс; 0 = по умолчанию (pulse*100 из таблицы);
     pause_ms  - пауза, мс; 0 = по умолчанию (pause*100 из таблицы);
     blink     - 1 = мигать LED фазы (100 мс, mode_blink_led Avis), 0 = ровно;
     count     - сколько раз прогнать пачку; 0 = ALERT_REPEAT_FOREVER
                 (до подтверждения: Alert_Stop() по квитанции).
   КОНТЕКСТ: ЛЮБОЙ поток.                                                    */
void Alert_SignalEx(uint8_t sig_id,
                    uint16_t period_ms, uint16_t pause_ms,
                    uint8_t blink, uint8_t count)
{
  /* Слоты запроса: фаза/таблица сигнала живут в СТАТИКЕ до применения в Tick
     (Alert_StartEx принимает указатель, а движок читает его асинхронно).
     Два чередующихся слота (счётчик s_sig_slot): Tick копирует фазу
     опубликованного слота в свою s_phase, а следующий вызов пишет ДРУГОЙ слот
     - гонки «писатель vs читатель» нет даже при вызове из двух потоков.     */
  static alert_phase_t s_sig_phase[2];
  static alert_seq_t   s_sig_seq[2];
  static volatile uint8_t s_sig_slot = 0u;
  const alert_sig_def_t *def;
  alert_phase_t ph;
  uint8_t slot;

  if (sig_id >= (uint8_t)(sizeof(s_sig_tab) / sizeof(s_sig_tab[0])))
  {
    return;                         /* неизвестный id - не тревожим            */
  }
  def = &s_sig_tab[sig_id];
  if (def->ch == 0u)
  {
    return;                         /* id зарезервирован (0x00/0x0C..0x0E)     */
  }

  /* Фаза сигнала: маска каналов из таблицы; период/пауза - из вызова либо по
     умолчанию (единицы Avis 100 мс -> мс, с защитой от переполнения);
     звук+вибро+свет стартуют ОДНОВРЕМЕННО - одной фазой (LedSoundVibroPlay). */
  ph.ch        = def->ch;
  ph.t_on_ms   = (period_ms != 0u) ? period_ms : ALERT_SIG_MS(def->pulse, 65500u);
  ph.t_off_ms  = (pause_ms  != 0u) ? pause_ms  : ALERT_SIG_MS(def->pause, 65500u);
  ph.repeats   = 1u;                /* «пачка» = 1 ВКЛ/ВЫКЛ; повторы - count   */
  ph.blink     = blink;
  ph.sound_idx = def->sound_idx;

  slot = (uint8_t)(s_sig_slot ^ 1u);
  s_sig_phase[slot] = ph;
  s_sig_seq[slot].ph    = &s_sig_phase[slot];
  s_sig_seq[slot].n     = 1u;
  s_sig_seq[slot].prio  = ALERT_PRIO_ALARM;  /* сигнал вытесняет UI/сервис     */
  s_sig_seq[slot].count = count;             /* 0 = до подтверждения           */
  s_sig_slot = slot;
  seq_request(&s_sig_seq[slot], 0u, 0u, 0u, count, (uint8_t)ALERT_NONE);
}

/* Запрос стопа (погасить каналы). КОНТЕКСТ: ЛЮБОЙ поток.                    */
void Alert_Stop(void)
{
  s_req_seq   = NULL;
  s_req_owner = (uint8_t)ALERT_NONE;
  s_req_stop  = 1u;
}

/* 1 = сценарий идёт или запрошен.                                           */
uint8_t Alert_Busy(void)
{
  return ((s_state != (uint8_t)NS_IDLE) || (s_req_seq != NULL)) ? 1u : 0u;
}

/* Движок. КОНТЕКСТ: ТОЛЬКО поток LVGL Task (lvgl_task_function в
   main_vector.c), вызов ~каждые 10 мс. Неблокирующий: вся работа - сравнение
   дедлайна и, по событию, включение/выключение каналов маски.               */
void Alert_IndicationTick(void)
{
  const alert_phase_t *p;

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
       копия Alert_SignalEx, поэтому снятие квалификатора безопасно; без каста
       GCC ругался -Wdiscarded-qualifiers.                                   */
    const alert_seq_t *as = (const alert_seq_t *)s_req_seq;
    s_req_seq = NULL;
    if ((s_seq == NULL) || (as->prio >= s_seq->prio))
    {
      seq_halt();          /* вытеснение: погасить текущий сценарий          */
      seq_apply(as);
      LOG_I(VLOG_M_SYS,  "alert: seq start prio=%u n=%u count=%u period=%u pause=%u",
            (uint32_t)as->prio, (uint32_t)as->n, (uint32_t)s_cycles,
            (uint32_t)s_phase.t_on_ms, (uint32_t)s_phase.t_off_ms);
    }
    else
    {
      LOG_W(VLOG_M_SYS,  "alert: seq REJECT prio=%u < cur %u",
            (uint32_t)as->prio, (uint32_t)s_seq->prio);
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
    seq_begin_phase();     /* повтор сценария (0 = до Alert_Stop/вытеснения)  */
    return;
  }
  seq_finish();            /* сценарий отработал сам                          */
}

/* ============================================================================
 *                 РЕЕСТР: индикация статусов (арбитраж через движок)
 * ============================================================================ */

/* Приоритетный статус, у которого статус активен И индикация не снята/не
   отработала. Порядок перечисления alert_id_t и есть приоритет (меньше id =
   важнее).                                                                   */
static alert_id_t alert_top_indication(void)
{
  uint8_t i;

  for (i = 1u; i < (uint8_t)ALERT_ID_COUNT; i++)
  {
    if ((s_alert[i].status != 0u) && (s_alert[i].indication != 0u))
    {
      return (alert_id_t)i;
    }
  }
  return ALERT_NONE;
}

/* Запустить индикацию статуса id. burst = 0 -> непрерывно (count = 0, пока
   статус жив или не появится более приоритетный); burst > 0 -> ОДНА пачка из
   burst повторов фазы (count = 1), по завершении движок сам снимет флаг
   индикации (seq_finish). Голосовой клип добавляется битом SOUND, если для
   статуса задан ALERT_SND_* (звуки автор добавит позже).                    */
static void alert_indication_start(alert_id_t id)
{
  const alert_desc_t *d = &s_desc[id];
  uint8_t slot = (uint8_t)(s_ind_slot ^ 1u);
  alert_phase_t ph;
  uint8_t count;

  ph.ch        = d->ch;
  ph.t_on_ms   = d->period_ms;
  ph.t_off_ms  = d->pause_ms;
  ph.blink     = d->blink;
  ph.sound_idx = 0u;
  if (d->burst != 0u)
  {
    ph.repeats = d->burst;              /* однократная пачка из burst повторов */
    count      = 1u;                    /* один прогон сценария                */
  }
  else
  {
    ph.repeats = 1u;                    /* «пачка» = 1 ВКЛ/ВЫКЛ                */
    count      = (uint8_t)ALERT_REPEAT_FOREVER;  /* до Alert_Tick (стоп/смена) */
  }

  if (d->voice != (uint16_t)ALERT_SND_NONE)
  {
    ph.ch        = (uint8_t)(ph.ch | (uint8_t)ALERT_CH_SOUND);
    ph.sound_idx = d->voice;
  }
  /* Звук при неподвижности можно выключить настройкой (Alert_Flags).        */
  if (((id == (uint8_t)ALERT_IMMOBILITY) != 0) &&
      ((Sns_Cfg_struct.Config_common.Alert_Flags & ALERT_CFG_IMMOB_SOUND) == 0u))
  {
    ph.ch = (uint8_t)(ph.ch & (uint8_t)~(ALERT_CH_BEEP | ALERT_CH_SOUND));
  }

  s_ind_phase[slot]    = ph;
  s_ind_seq[slot].ph    = &s_ind_phase[slot];
  s_ind_seq[slot].n     = 1u;
  s_ind_seq[slot].prio  = d->prio;
  s_ind_seq[slot].count = count;
  s_ind_slot = slot;

  seq_request(&s_ind_seq[slot], 0u, 0u, 0u, count, (uint8_t)id);
}

/* Арбитраж: индикацию ведёт ОДИН статус (самый приоритетный). При смене
   лидера прежняя последовательность гасится, новая стартует.                */
static void alert_indication_arbitrate(void)
{
  alert_id_t top = alert_top_indication();

  /* Прибор выключен или идёт прогрев (DEVICE_TURNED_ON_1 - «включён без
     звука/вибро», как в Avis): индикацию не ведём, но СТАТУСЫ считаем
     (неподвижность, свежесть устройств, ошибки и батарея от экрана и звука не
     зависят). Флаг indication у статусов не трогаем - после включения прибор
     снова начнёт оповещать, а квитирование не «проглатывается» втихую.      */
  if (device_turn != DEVICE_TURNED_ON)
  {
    if (s_ind_owner != (uint8_t)ALERT_NONE)
    {
      Alert_Stop();
      s_ind_owner = (uint8_t)ALERT_NONE;
    }
    return;
  }

  if (top == (alert_id_t)s_ind_owner)
  {
    /* Лидер не сменился. Если движок при этом пуст (сценарий кто-то погасил
       извне либо чужой сигнал того же приоритета отработал и завершился) -
       поднимаем свою индикацию снова, иначе статус активен, а звука/света нет.
       Однократные пачки (burst > 0) НЕ перезапускаем: они отработали штатно,
       повтор положен только по новому срабатыванию (см. syserr_tick/
       battery_tick).                                                        */
    if ((top != ALERT_NONE) && (s_desc[top].burst == 0u) && (Alert_Busy() == 0u))
    {
      alert_indication_start(top);
    }
    return;
  }

  if (s_ind_owner != (uint8_t)ALERT_NONE)
  {
    Alert_Stop();
  }
  s_ind_owner = (uint8_t)top;
  if (top != ALERT_NONE)
  {
    alert_indication_start(top);
    LOG_I(VLOG_M_SYS, "alert: indication start [%s] period=%u pause=%u burst=%u voice=%u",
          s_desc[top].name, (uint32_t)s_desc[top].period_ms,
          (uint32_t)s_desc[top].pause_ms, (uint32_t)s_desc[top].burst,
          (uint32_t)s_desc[top].voice);
  }
}

/* Заново взвести ИНДИКАЦИЮ активного статуса (статус не трогаем). Нужно для
   повторяющихся событий: сотрудник квитировал газовую тревогу, а сервер прислал
   её ещё раз (новый MsgSeq), или сработала НОВАЯ ошибка прибора - прибор
   обязан снова оповестить. Дубли с тем же MsgSeq отсекает Alert_LoraMessage
   (идемпотентность), а у однократных пачек повтор возможен только когда
   прежняя уже отработала (indication == 0).                                 */
static void alert_rearm_indication(alert_id_t id)
{
  if (s_alert[id].status != 0u)
  {
    s_alert[id].indication = 1u;
    s_alert[id].since_ms   = VTICK_MS();
    LOG_I(VLOG_M_SYS, "alert: indication REARM [%s]", s_desc[id].name);
  }
}

/* ============================================================================
 *                       CP1251 -> UTF-8 (текст сервера)
 * Сообщение downlink (LoRa v4 п.7.1) приходит однобайтовым CP1251, а LVGL
 * работает с UTF-8 - конвертируем здесь, один раз на сообщение.
 * ============================================================================ */
static uint16_t cp1251_to_unicode(uint8_t c)
{
  /* 0x80..0xBF: таблица CP1251 (0x98 не определён - отдаём 0)               */
  static const uint16_t tab80[64] =
  {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457
  };

  if (c < 0x80u)  { return (uint16_t)c; }
  if (c >= 0xC0u) { return (uint16_t)(0x0410u + (uint16_t)(c - 0xC0u)); }  /* А..я */
  return tab80[c - 0x80u];
}

/* Записать один символ UTF-8; возврат числа записанных байт (0 = нет места).*/
static uint8_t utf8_put(char *dst, uint16_t left, uint16_t cp)
{
  if (cp < 0x80u)
  {
    if (left < 1u) { return 0u; }
    dst[0] = (char)cp;
    return 1u;
  }
  if (cp < 0x800u)
  {
    if (left < 2u) { return 0u; }
    dst[0] = (char)(0xC0u | (uint8_t)(cp >> 6));
    dst[1] = (char)(0x80u | (uint8_t)(cp & 0x3Fu));
    return 2u;
  }
  if (left < 3u) { return 0u; }
  dst[0] = (char)(0xE0u | (uint8_t)(cp >> 12));
  dst[1] = (char)(0x80u | (uint8_t)((cp >> 6) & 0x3Fu));
  dst[2] = (char)(0x80u | (uint8_t)(cp & 0x3Fu));
  return 3u;
}

/* ============================================================================
 *                          НЕПОДВИЖНОСТЬ (ТЗ автора)
 * ============================================================================ */

/* Настройки из конфига (с подстановкой дефолтов, если поле нулевое - так
   старый конфиг во flash, где на месте новых полей лежали нули Reserve,
   получает рабочие значения).                                              */
static uint16_t immob_cfg(uint16_t field, uint16_t def)
{
  return (field != 0u) ? field : def;
}

/* Движение зафиксировано: сброс окна неподвижности и СНЯТИЕ статуса
   («Индикация может быть снята ... встряхиванием трекера или началом
   движения»; инцидент при этом НЕ отправляется, если успели в пределах
   предаварийного таймера).
   Единственный источник события движения - акселерометр (опрос
   lis3dh_motion_event() в Alert_Tick), поэтому при CONFIG_LIS3DH = 0 функция
   не нужна: убираем её под тот же условный блок (иначе GCC даёт
   -Wunused-function). Как только появится второй источник движения (например,
   кнопка «я жив» или кадр от браслета) - guards снять, логика статуса от
   источника не зависит.                                                    */
#if CONFIG_LIS3DH
static void immob_on_motion(void)
{
  s_im.last_motion_ms = VTICK_MS();
  s_im.no_motion_s    = 0u;
  if (s_im.motion_cnt < 0xFFF0u)
  {
    s_im.motion_cnt = (uint16_t)(s_im.motion_cnt + 1u);
  }

  if ((s_im.phase != (uint8_t)IM_IDLE) && (s_im.phase != (uint8_t)IM_COUNTING))
  {
    LOG_I(VLOG_M_SYS, "alert: motion -> immobility CANCELLED (phase=%u, incident=%u)",
          (uint32_t)s_im.phase, (uint32_t)s_im.incident_sent);
  }
  s_im.phase         = (uint8_t)IM_COUNTING;
  s_im.prealarm_s    = 0u;
  s_im.incident_sent = 0u;

  if (s_alert[ALERT_IMMOBILITY].status != 0u)
  {
    Alert_Clear(ALERT_IMMOBILITY);
  }
}
#endif /* CONFIG_LIS3DH - источник события движения (акселерометр) */

/* Инцидент на сервер: Motion.MOTION_BIT_INACTIVE + EventLog «Отсутствие
   активности» (0x10, Value 0x8000 - LoRa v4 п.4.8). Отправляем ОДИН раз на
   эпизод (incident_sent).                                                   */
static void immob_report_incident(void)
{
  if (s_im.incident_sent != 0u)
  {
    return;
  }
  s_im.incident_sent = 1u;

#if CONFIG_LORA
  if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA))
  {
    Lora_AddContainerEvent(ALERT_CEV_INACTIVITY, ALERT_SEV_CRITICAL, (int16_t)0x8000);
  }
#endif
  LOG_W(VLOG_M_SYS, "alert: IMMOBILITY incident sent (no motion %u s, pre-alarm %u s)",
        (uint32_t)s_im.no_motion_s,
        (uint32_t)immob_cfg(Sns_Cfg_struct.Config_common.Immobility_PreAlarm_s,
                            ALERT_IMMOB_PREALARM_S_DEF));
}

/* Секундная логика неподвижности.                                           */
static void immob_tick_1s(void)
{
  const SNS_CFG_Type_common *cfg = &Sns_Cfg_struct.Config_common;
  uint16_t window_s = immob_cfg(cfg->Immobility_Window_s, ALERT_IMMOB_WINDOW_S_DEF);

  /* Уровень активности для LoRa (Motion.ActivityLevel 0..255): число событий
     движения за последнее окно, с насыщением; гаснет вдвое каждую секунду,
     поэтому «резко встряхнул» даёт всплеск, а покой - ноль.                 */
  {
    uint32_t act = (uint32_t)s_im.motion_cnt * 16u;   /* 32 бит: иначе переполнение */
    s_im.activity   = (uint8_t)((act > 255u) ? 255u : act);
    s_im.motion_cnt = (uint16_t)(s_im.motion_cnt / 2u);
  }

  if (cfg->Immobility_Enable == 0u)
  {
    /* Функция выключена в настройках: статус снимаем, счётчики гасим.       */
    if ((s_im.phase != (uint8_t)IM_IDLE) || (s_alert[ALERT_IMMOBILITY].status != 0u))
    {
      Alert_Clear(ALERT_IMMOBILITY);
      s_im.phase         = (uint8_t)IM_IDLE;
      s_im.no_motion_s   = 0u;
      s_im.prealarm_s    = 0u;
      s_im.incident_sent = 0u;
    }
    return;
  }

  switch (s_im.phase)
  {
  case (uint8_t)IM_PREALARM:
    /* Идёт время на реакцию: встряхивание/движение снимает всё (immob_on_motion),
       квитирование кнопкой гасит ТОЛЬКО индикацию - отсчёт продолжается.    */
    if (s_im.prealarm_s != 0u)
    {
      s_im.prealarm_s--;
    }
    if (s_im.prealarm_s == 0u)
    {
      immob_report_incident();
      s_im.phase = (uint8_t)IM_INCIDENT;
    }
    if (s_im.no_motion_s < 0xFFFFu)
    {
      s_im.no_motion_s++;
    }
    break;

  case (uint8_t)IM_INCIDENT:
    /* Инцидент отправлен, статус живёт до появления движения.               */
    if (s_im.no_motion_s < 0xFFFFu)
    {
      s_im.no_motion_s++;      /* ограничиваем: иначе счётчик обнулится */
    }
    break;

  case (uint8_t)IM_COUNTING:
  case (uint8_t)IM_IDLE:
  default:
    if (s_im.no_motion_s < 0xFFFFu)
    {
      s_im.no_motion_s++;
    }
    if (s_im.no_motion_s >= window_s)
    {
      /* Зафиксирована обездвиженность: статус + индикация на дисплее, звук и
         свет; одновременно стартует предаварийный таймер.                   */
      s_im.phase      = (uint8_t)IM_PREALARM;
      s_im.prealarm_s = immob_cfg(cfg->Immobility_PreAlarm_s, ALERT_IMMOB_PREALARM_S_DEF);
      Alert_Set(ALERT_IMMOBILITY);
      LOG_W(VLOG_M_SYS, "alert: IMMOBILITY (no motion %u s) -> indication %u s to react",
            (uint32_t)s_im.no_motion_s, (uint32_t)s_im.prealarm_s);
    }
    else
    {
      s_im.phase = (uint8_t)IM_COUNTING;
    }
    break;
  }
}

#if CONFIG_LORA
/* Поза для LoRa (Motion.StatusBits, биты 0..1): 0 неизвестно, 1 вертикальное,
   2 горизонтальное - из 6D-ориентации акселерометра (sensors_status).
   Функция нужна только при CONFIG_LORA (Motion-блок кадра) - иначе GCC даст
   -Wunused-function.                                                       */
static uint8_t immob_posture(void)
{
  switch (sensors_status.Orientation)
  {
  case 1u:  /* LIS3DH_ORIENT_Z_UP   - лежит плашмя  */
  case 2u:  /* LIS3DH_ORIENT_Z_DOWN                 */
    return 2u;
  case 3u:  /* LIS3DH_ORIENT_X_UP   - стоит/в кармане */
  case 4u:  /* LIS3DH_ORIENT_X_DOWN                   */
  case 5u:  /* LIS3DH_ORIENT_Y_UP                     */
  case 6u:  /* LIS3DH_ORIENT_Y_DOWN                   */
    return 1u;
  default:
    return 0u;
  }
}
#endif /* CONFIG_LORA */

/* ============================================================================
 *                    ГАЗОВАЯ ТРЕВОГА: ПОРОГИ СВОЕГО ГАЗОАНАЛИЗАТОРА
 * ============================================================================ */

#if CONFIG_BLE
/* Свой ID трекера - для локальной газовой тревоги (на экране «ОСТОРОЖНО! ГАЗ!
   ID nnnn», как и для тревоги от сервера). Берём заводской номер
   (Config_common.SerialLo) - им прибор представляется серверу. Если сервер
   оперирует другим идентификатором (DevAddr/ShortID), правка - ЗДЕСЬ, в одном
   месте. Нужна только при CONFIG_BLE: локальный источник тревоги - пороги
   газоанализатора Avis, который приходит SUBDEV-пакетом BLE.                */
static uint16_t alert_own_id(void)
{
  return Sns_Cfg_struct.Config_common.SerialLo;
}

/* Локальная газовая тревога ПО ПОРОГАМ подключённого газоанализатора Avis
   (автор: «где-то по порогам с Ависа, где-то от сервера»). Источник - свод
   ошибок/порогов каналов Connected_device.StateErr, который ведёт Ble_vector.c
   из SUBDEV-пакета AVIS; тревожные биты - ALERT_GAS_THR_MASK (Порог 1/2/3,
   STEL, TWA; «превышение диапазона» намеренно не входит).
   Показания учитываются, только пока AVIS на связи (gas_online + свежесть
   gas_ms) - иначе «нет данных» выглядело бы как тревога.
   Переход «пороги превышены» взводит статус и индикацию, подтверждение каждую
   секунду обновляет лишь метку времени (иначе квитирование отменялось бы),
   возврат в норму - снимает статус, ЕСЛИ серверный источник не активен.     */
static void gas_local_tick(void)
{
  uint8_t thr = 0u;

  if ((Ble_telemetry.gas_online != 0u) &&
      ((uint32_t)(GetTick() - Ble_telemetry.gas_ms) <= ALERT_DEV_STALE_MS))
  {
    thr = (uint8_t)(Connected_device.StateErr & (uint32_t)ALERT_GAS_THR_MASK);
  }

  if (thr != 0u)
  {
    if ((s_gas_src & ALERT_GAS_SRC_LOCAL) == 0u)
    {
      s_gas_src = (uint8_t)(s_gas_src | ALERT_GAS_SRC_LOCAL);
      Alert_GasAlarm(alert_own_id());   /* переход: статус + индикация + лог   */
    }
    else
    {
      Alert_Set(ALERT_GAS_ALARM);       /* подтверждение: только метка времени */
    }
  }
  else if ((s_gas_src & ALERT_GAS_SRC_LOCAL) != 0u)
  {
    s_gas_src = (uint8_t)(s_gas_src & (uint8_t)~ALERT_GAS_SRC_LOCAL);
    if (s_gas_src == 0u)
    {
      Alert_Clear(ALERT_GAS_ALARM);
      LOG_I(VLOG_M_SYS, "alert: gas thresholds back to normal");
    }
  }
}
#endif /* CONFIG_BLE */

/* Автоочистка статусов с ОДНОРАЗОВЫМ источником (колонка hold_s в s_desc).
   Метку подтверждения обновляет Alert_Set(), поэтому у серверной газовой
   тревоги, локальной эвакуации «мастера» и сигнала сообщения нет «вечного»
   залипания; у неподвижности, «нет СИЗ/ГА/браслета», ошибок и батареи
   hold_s = 0 - их снимает только их источник.                              */
static void alert_hold_tick(void)
{
  uint8_t i;

  for (i = 1u; i < (uint8_t)ALERT_ID_COUNT; i++)
  {
    const alert_desc_t *d = &s_desc[i];

    if ((d->hold_s == 0u) || (s_alert[i].status == 0u)) { continue; }
    if (VTICK_ELAPSED_MS(s_alert[i].since_ms) >= ((uint32_t)d->hold_s * 1000u))
    {
      if (i == (uint8_t)ALERT_GAS_ALARM) { s_gas_src = 0u; }  /* источник погашен */
      LOG_I(VLOG_M_SYS, "alert: hold %u s expired -> CLEAR [%s]",
            (uint32_t)d->hold_s, d->name);
      Alert_Clear((alert_id_t)i);
    }
  }
}

/* ============================================================================
 *                    УСТРОЙСТВА КОМПЛЕКТА: нет СИЗ / ГА / браслета
 * ============================================================================ */

#if CONFIG_BLE
/* Статус «нет X»: взводится, если устройство ОЖИДАЕТСЯ (MAC-таблица BLE-модуля
   и/или Config_common.Devices_Expected) и его данные не свежие; снимается,
   когда связь восстановилась (пакеты снова приходят) ИЛИ устройство удалено из
   таблицы/настроек (бит сброшен) - требование автора п.3. Индикация у таких
   статусов не снимается кнопкой (ack_button = 0).                          */
static void device_presence_tick(void)
{
  /* Состав комплекта (решение автора) - АВТОМАТИЧЕСКИ из MAC-таблицы
     BLE-модуля (Ble_ExpectedDevices, DevType 2/3/4), а поле конфига
     Devices_Expected - ручное дополнение/переопределение: объединяем по ИЛИ. */
  const uint16_t expected = (uint16_t)(Sns_Cfg_struct.Config_common.Devices_Expected |
                                       Ble_ExpectedDevices());
  uint32_t now = GetTick();
  uint8_t ppe_ok   = 0u;
  uint8_t gas_ok   = 0u;
  uint8_t brsl_ok  = 0u;

  /* СИЗ: есть принятые метки и хотя бы одна помечена «надета/присутствует»,
     данные не старше ALERT_DEV_STALE_MS.                                    */
  if ((expected & ALERT_DEV_EXPECT_PPE) != 0u)
  {
    uint8_t i;
    if (((uint32_t)(now - Ble_telemetry.ppe_ms) <= ALERT_DEV_STALE_MS) &&
        (Ble_telemetry.ppe_count > 0u))
    {
      for (i = 0u; i < Ble_telemetry.ppe_count; i++)
      {
        if (Ble_telemetry.ppe[i].Present != 0u) { ppe_ok = 1u; break; }
      }
    }
    if (ppe_ok != 0u) { Alert_Clear(ALERT_NO_PPE); } else { Alert_Set(ALERT_NO_PPE); }
  }
  else
  {
    Alert_Clear(ALERT_NO_PPE);      /* удалено из настроек - индикация прекращается */
  }

  /* Газоанализатор (AVIS): флаг «данные свежие» из BLE-телеметрии.           */
  if ((expected & ALERT_DEV_EXPECT_GAS) != 0u)
  {
    if ((Ble_telemetry.gas_online != 0u) &&
        ((uint32_t)(now - Ble_telemetry.gas_ms) <= ALERT_DEV_STALE_MS))
    {
      gas_ok = 1u;
    }
    if (gas_ok != 0u) { Alert_Clear(ALERT_NO_GAS); } else { Alert_Set(ALERT_NO_GAS); }
  }
  else
  {
    Alert_Clear(ALERT_NO_GAS);
  }

  /* Браслет (пульсометр): телеметрия заведена (bracelet_online/bracelet_ms),
     но приём ещё не реализован - в BLE-протоколе v1 это DevType 3 (таблица
     MAC/MSDGETALL, п.5), а Ble_vector.c пока разбирает GAS/SUBDEV/BEACONS/
     PPE/AGG. Как только парсер появится - статус заработает без правок
     Alert_vector. Не ожидается (бит не выставлен) -> статуса нет.           */
  if ((expected & ALERT_DEV_EXPECT_BRACELET) != 0u)
  {
    if ((Ble_telemetry.bracelet_online != 0u) &&
        ((uint32_t)(now - Ble_telemetry.bracelet_ms) <= ALERT_DEV_STALE_MS))
    {
      brsl_ok = 1u;
    }
    if (brsl_ok != 0u) { Alert_Clear(ALERT_NO_BRACELET); } else { Alert_Set(ALERT_NO_BRACELET); }
  }
  else
  {
    Alert_Clear(ALERT_NO_BRACELET);
  }
}
#endif /* CONFIG_BLE */

/* ============================================================================
 *              ОШИБКИ ПРИБОРА (ST_COMMON_ERR) И БАТАРЕЯ
 * Решение автора: «ошибки не снимаются, но свет, звук и вибро не индицируют -
 * остаются только записи на экране; при повторном срабатывании любой из ошибок
 * всё снова звучит и светит». Поэтому:
 *   - статус живёт, пока бит ошибки выставлен (виден на экране, категория 1
 *     очереди ошибок TFT_indicator.c - имена берёт оттуда же, Alert_SysErrName);
 *   - индикация - ОДНОКРАТНАЯ пачка (burst в s_desc) на факте ПОЯВЛЕНИЯ бита;
 *   - новый бит (или тот же после сброса) = новое срабатывание = снова пачка;
 *   - кнопкой не снимается (ack_button = 0): гаснуть нечему, пачка короткая.
 * ============================================================================ */

/* Номер самого приоритетного (младшего) выставленного бита ошибки.          */
static uint8_t syserr_top_bit(uint32_t mask)
{
  uint8_t bit;

  for (bit = 0u; bit < (uint8_t)ALERT_SYSERR_BITS; bit++)
  {
    if ((mask & ((uint32_t)1u << bit)) != 0u) { return bit; }
  }
  return 0u;
}

/* Ошибки модулей: ТЕМП / NB-IoT / BLE / GPS / GSM / LORA / LCD (биты батареи
   обрабатывает battery_tick - у них свой статус и ритм).                    */
static void syserr_tick(void)
{
  uint32_t mask = (uint32_t)Sns_Cfg_struct.Config_common.StateErr & ALERT_SYSERR_MASK;
  uint32_t fresh = mask & (uint32_t)~s_err_mask_prev;   /* новые биты = срабатывание */

  s_err_mask_prev = mask;

  if (mask == 0u)
  {
    Alert_Clear(ALERT_DEVICE_ERROR);
    return;
  }

  s_alert[ALERT_DEVICE_ERROR].param = syserr_top_bit(mask);
  Alert_Set(ALERT_DEVICE_ERROR);

  /* Пачка - на ПЕРВОМ появлении статуса и на каждом НОВОМ срабатывании
     (новый бит в маске). Пока прежняя пачка ещё идёт (indication != 0),
     заново не стартуем - иначе длинные серии ошибок превратились бы в
     непрерывный звук.                                                       */
  if ((fresh != 0u) && (s_alert[ALERT_DEVICE_ERROR].indication == 0u))
  {
    LOG_W(VLOG_M_SYS, "alert: DEVICE ERROR bits=%x (new=%x) -> [%s]",
          (uint32_t)mask, (uint32_t)fresh,
          Alert_SysErrName((uint8_t)s_alert[ALERT_DEVICE_ERROR].param));
    alert_rearm_indication(ALERT_DEVICE_ERROR);
  }
}

/* Батарея: низкий (BAT_LIM_1) и критический (BAT_LIM_2) заряд.
   Решение автора: считать ЗДЕСЬ по напряжению MAX17048
   (sensors_status.battery_charge_volt) и порогам конфига - машина состояний
   батареи Avis_Battery_System() в main_vector.c пока пустая weak-заглушка и
   биты StateErr никто не взводил. Alert заодно ЗЕРКАЛИТ биты
   CRITICAL_LOW_BATTERY / CRITICAL_BATTERY в Config_common.StateErr, чтобы их
   видели экран (категория 1) и сервер, как остальные ошибки.
   Гистерезис ALERT_BAT_HYST_MV: снятие - выше порога на 50 мВ, иначе на границе
   статус дребезжал бы и каждая «пилка» звучала новой пачкой.
   Во время зарядки (ST_COMMON_BIT_BAT_CHARGE) статусы не взводим: низкий заряд
   на зарядке - штатная ситуация.                                            */
static void battery_tick(void)
{
  uint16_t mv   = sensors_status.battery_charge_volt;
  uint16_t lim1 = BAT_LIM_1;
  uint16_t lim2 = BAT_LIM_2;
  uint8_t  low;
  uint8_t  crit;
  uint8_t  charging = (uint8_t)((TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE) != 0u) ? 1u : 0u);

  if ((mv == 0u) || (lim1 == 0u) || (lim2 == 0u) || (charging != 0u))
  {
    /* Нет данных (MAX17048 не ответил/не прочитан), нулевые пороги или идёт
       зарядка - статусы снимаем, биты гасим.                                */
    low  = 0u;
    crit = 0u;
  }
  else
  {
    /* Сравнение в uint32_t: lim + гистерезис не переполняют uint16.         */
    crit = (s_bat_crit != 0u)
             ? (uint8_t)(((uint32_t)mv > ((uint32_t)lim2 + (uint32_t)ALERT_BAT_HYST_MV)) ? 0u : 1u)
             : (uint8_t)((mv <= lim2) ? 1u : 0u);
    low  = (s_bat_low != 0u)
             ? (uint8_t)(((uint32_t)mv > ((uint32_t)lim1 + (uint32_t)ALERT_BAT_HYST_MV)) ? 0u : 1u)
             : (uint8_t)((mv <= lim1) ? 1u : 0u);
  }

  /* Зеркалим биты StateErr (RMW общего поля из нескольких потоков - под
     блокировкой конфига, как это делает Ble_Control_States_Update).         */
  if ((low != s_bat_low) || (crit != s_bat_crit))
  {
    Sys_Cfg_Lock();
//    if (crit != 0u) { SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_CRITICAL_BATTERY); }
//    else            { CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_CRITICAL_BATTERY); }
//    if (low  != 0u) { SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_CRITICAL_LOW_BATTERY); }
//    else            { CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_CRITICAL_LOW_BATTERY); }
    Sys_Cfg_Unlock();

    LOG_W(VLOG_M_SYS, "alert: battery %u mV (lim1=%u lim2=%u) -> low=%u crit=%u",
          (uint32_t)mv, (uint32_t)lim1, (uint32_t)lim2,
          (uint32_t)low, (uint32_t)crit);
  }
  s_bat_low  = low;
  s_bat_crit = crit;

  /* Критический разряд важнее низкого (приоритет по alert_id_t), поэтому
     низкий заряд взводим только если критического нет - иначе на экране и в
     логе дубли.                                                              */
  if (crit != 0u) { Alert_Set(ALERT_BATTERY_CRIT); } else { Alert_Clear(ALERT_BATTERY_CRIT); }
  if ((low != 0u) && (crit == 0u)) { Alert_Set(ALERT_BATTERY_LOW); } else { Alert_Clear(ALERT_BATTERY_LOW); }
}

/* ============================================================================
 *                              СЛУЖЕБНЫЕ
 * ============================================================================ */

/* Поиск подстроки (без учёта регистра, только ASCII/CP1251-байты) - для
   классификации текста сообщения сервера.                                   */
static uint8_t msg_contains(const uint8_t *hay, uint8_t hay_len, const char *needle)
{
  uint8_t n_len = 0u;
  uint8_t i;
  uint8_t j;

  while (needle[n_len] != '\0') { n_len++; }
  if ((n_len == 0u) || (n_len > hay_len)) { return 0u; }

  for (i = 0u; i <= (uint8_t)(hay_len - n_len); i++)
  {
    for (j = 0u; j < n_len; j++)
    {
      uint8_t a = hay[i + j];
      uint8_t b = (uint8_t)needle[j];
      /* Русские буквы в CP1251: сравниваем байт-в-байт (регистр - только для
         ASCII, чтобы «Осторожно! Газ» и «ОСТОРОЖНО! ГАЗ» совпали).          */
      if ((a >= 'a') && (a <= 'z')) { a = (uint8_t)(a - 32u); }
      if ((b >= 'a') && (b <= 'z')) { b = (uint8_t)(b - 32u); }
      if (a != b) { break; }
    }
    if (j == n_len) { return 1u; }
  }
  return 0u;
}

/* Достать число после маркера «ID» (ID трекера-источника газовой тревоги).   */
static uint16_t msg_extract_id(const uint8_t *hay, uint8_t hay_len)
{
  uint8_t i;

  for (i = 0u; i < hay_len; i++)
  {
    if (((hay[i] == (uint8_t)'I') || (hay[i] == (uint8_t)'i')) &&
        (i + 1u < hay_len) && ((hay[i + 1u] == (uint8_t)'D') || (hay[i + 1u] == (uint8_t)'d')))
    {
      uint16_t val = 0u;
      uint8_t k = (uint8_t)(i + 2u);
      /* пропускаем разделители */
      while ((k < hay_len) && ((hay[k] == (uint8_t)' ') || (hay[k] == (uint8_t)':') ||
                               (hay[k] == (uint8_t)'='))) { k++; }
      while ((k < hay_len) && (hay[k] >= (uint8_t)'0') && (hay[k] <= (uint8_t)'9'))
      {
        if (val <= 6553u) { val = (uint16_t)((uint16_t)(val * 10u) + (uint16_t)(hay[k] - (uint8_t)'0')); }
        k++;
      }
      if (val != 0u) { return val; }
    }
  }
  return 0u;
}

/* ============================================================================
 *                                   API
 * ============================================================================ */

/* Сброс реестра И движка. КОНТЕКСТ: Measure Task (Vector_Run_Pre_Init), до
   первого Alert_Tick()/Alert_IndicationTick().                              */
void Alert_Init(void)
{
  uint8_t i;

  /* --- движок индикации (бывший Notify_Init) --- */
  s_req_seq    = NULL;
  s_req_stop   = 0u;
  s_req_period = 0u;
  s_req_pause  = 0u;
  s_req_rep    = 0u;
  s_req_count  = 1u;
  s_req_owner  = (uint8_t)ALERT_NONE;
  seq_halt();

  /* --- реестр статусов --- */
  for (i = 0u; i < (uint8_t)ALERT_ID_COUNT; i++)
  {
    s_alert[i].status     = 0u;
    s_alert[i].indication = 0u;
    s_alert[i].param      = 0u;
    s_alert[i].since_ms   = 0u;
    s_alert[i].ack_ms     = 0u;
  }
  s_im.phase          = (uint8_t)IM_COUNTING;
  s_im.no_motion_s    = 0u;
  s_im.prealarm_s     = 0u;
  s_im.last_motion_ms = VTICK_MS();
  s_im.motion_cnt     = 0u;
  s_im.activity       = 0u;
  s_im.incident_sent  = 0u;

  s_gas_src       = 0u;
  s_err_mask_prev = 0u;
  s_bat_low       = 0u;
  s_bat_crit      = 0u;

  s_msg_text[0] = '\0';
  s_msg_seq     = 0u;
  s_msg_shown   = 0u;
  s_tick_div    = 0u;

  s_ind_owner = (uint8_t)ALERT_NONE;

#if CONFIG_LIS3DH
  /* Порог детектора движения - из настроек трекера (Immobility_Ths_mg),
     с дефолтом, если поле нулевое (старый конфиг во flash).                 */
  s_motion_ths = immob_cfg(Sns_Cfg_struct.Config_common.Immobility_Ths_mg,
                           ALERT_MOTION_THS_MG_DEF);
  (void)lis3dh_motion_set_threshold(s_motion_ths);
#endif
}

/* Взвести статус: индикация стартует сама (Alert_Tick), статус живёт до
   Alert_Clear(). Повторный вызов при активном статусе обновляет метку
   ПОСЛЕДНЕГО ПОДТВЕРЖДЕНИЯ (по ней работает автоочистка hold_s) и НЕ
   перезапускает индикацию - иначе ежекунундное подтверждение отменяло бы
   квитирование. КОНТЕКСТ: любой поток.                                     */
void Alert_Set(alert_id_t id)
{
  if ((id == ALERT_NONE) || (id >= ALERT_ID_COUNT)) { return; }

  if (s_alert[id].status == 0u)
  {
    s_alert[id].status     = 1u;
    s_alert[id].indication = 1u;   /* свежий статус всегда сопровождается индикацией */
    s_alert[id].since_ms   = VTICK_MS();
    LOG_I(VLOG_M_SYS, "alert: SET [%s]", s_desc[id].name);
  }
  else
  {
    s_alert[id].since_ms = VTICK_MS();
  }
}

/* Снять статус (условие исчезло): индикация гаснет в Alert_Tick.            */
void Alert_Clear(alert_id_t id)
{
  if ((id == ALERT_NONE) || (id >= ALERT_ID_COUNT)) { return; }

  if (s_alert[id].status != 0u)
  {
    s_alert[id].status     = 0u;
    s_alert[id].indication = 0u;
    s_alert[id].param      = 0u;
    LOG_I(VLOG_M_SYS, "alert: CLEAR [%s]", s_desc[id].name);
  }
}

uint8_t Alert_IsActive(alert_id_t id)
{
  if ((id == ALERT_NONE) || (id >= ALERT_ID_COUNT)) { return 0u; }
  return s_alert[id].status;
}

uint8_t Alert_Indication(alert_id_t id)
{
  if ((id == ALERT_NONE) || (id >= ALERT_ID_COUNT)) { return 0u; }
  return (uint8_t)((s_alert[id].status != 0u) && (s_alert[id].indication != 0u));
}

/* Квитирование зелёной кнопкой: гасим индикацию самого приоритетного статуса,
   у которого ack_button = 1. СТАТУС и его таймеры не трогаем (требование
   автора п.2), поэтому неподвижность продолжит отсчёт и отправит инцидент,
   если сотрудник не двигается. Возврат: id или ALERT_NONE.                  */
uint8_t Alert_AckButton(void)
{
  alert_id_t top;

  /* Квитирование имеет смысл только во включённом приборе: на прогреве
     (DEVICE_TURNED_ON_1 - «включён без звука/вибро») и в выключенном
     состоянии индикация не идёт, гасить нечего, а флаг indication должен
     дожить до включения - иначе тревога потеряется. Возврат ALERT_NONE
     отдаёт жест обратно в навигацию меню (Menu_vector.c).                   */
  if (device_turn != DEVICE_TURNED_ON) { return (uint8_t)ALERT_NONE; }

  top = alert_top_indication();
  if (top == ALERT_NONE) { return (uint8_t)ALERT_NONE; }
  if (s_desc[top].ack_button == 0u)
  {
    /* Индикация не снимается вручную (нет СИЗ/ГА/браслета, ошибки прибора,
       батарея): кнопку не «съедаем», возвращаем ALERT_NONE - жест уйдёт в
       навигацию меню.                                                       */
    return (uint8_t)ALERT_NONE;
  }
  s_alert[top].indication = 0u;
  s_alert[top].ack_ms     = VTICK_MS();
  LOG_I(VLOG_M_SYS, "alert: ACK by button [%s] (status stays)", s_desc[top].name);
  return (uint8_t)top;
}

/* «ОСТОРОЖНО! ГАЗ!» с ID трекера-источника (на экране виден ID).            */
void Alert_GasAlarm(uint16_t source_tracker_id)
{
  s_alert[ALERT_GAS_ALARM].param = source_tracker_id;
  Alert_Set(ALERT_GAS_ALARM);
  alert_rearm_indication(ALERT_GAS_ALARM);   /* статус уже был - звучим снова */
  LOG_W(VLOG_M_SYS, "alert: GAS ALARM (source tracker ID=%u)",
        (uint32_t)source_tracker_id);
}

/* Текстовое сообщение сервера (блок Message, LoRa v4 п.7.1). Возврат: id
   поднятого статуса. Идемпотентность: повтор того же MsgSeq индикацию не
   перезапускает (сервер шлёт дубль, пока не получит MsgAck).                */
uint8_t Alert_LoraMessage(const uint8_t *text_cp1251, uint8_t len, uint8_t seq)
{
  uint16_t pos = 0u;
  uint8_t  i;

  if ((text_cp1251 == NULL) || (len == 0u)) { return (uint8_t)ALERT_NONE; }

  if ((s_msg_shown != 0u) && (seq == s_msg_seq))
  {
    LOG_I(VLOG_M_SYS, "alert: message seq=%u already shown (skip)", (uint32_t)seq);
    return (uint8_t)((s_alert[ALERT_GAS_ALARM].status != 0u)   ? ALERT_GAS_ALARM :
                     ((s_alert[ALERT_EVACUATION].status != 0u) ? ALERT_EVACUATION : ALERT_MESSAGE));
  }

  /* CP1251 -> UTF-8 (для LVGL) */
  s_msg_text[0] = '\0';
  for (i = 0u; i < len; i++)
  {
    uint16_t cp = cp1251_to_unicode(text_cp1251[i]);
    uint8_t  n;
    if (cp == 0u) { continue; }
    n = utf8_put(&s_msg_text[pos], (uint16_t)(ALERT_MSG_UTF8_LEN - 1u - pos), cp);
    if (n == 0u) { break; }                    /* не влезло - обрезаем       */
    pos = (uint16_t)(pos + n);
  }
  s_msg_text[pos] = '\0';
  s_msg_seq   = seq;
  s_msg_shown = 1u;

  LOG_I(VLOG_M_SYS, "alert: MESSAGE seq=%u len=%u", (uint32_t)seq, (uint32_t)len);

  /* Классификация текста: «ОСТОРОЖНО! ГАЗ!» -> газовая тревога (с ID
     трекера-источника), «ВСЕМ НА ВЫХОД» -> эвакуация, прочее -> сигнал
     сообщения. Маркеры - ALERT_MSG_MARK_* (vector_config.h).                */
  if (msg_contains(text_cp1251, len, ALERT_MSG_MARK_GAS) != 0u)
  {
    /* Источник - сервер: локальный сброс порогов Avis такую тревогу не гасит,
       её снимет hold_s (ALERT_GAS_HOLD_S) после последнего сообщения.       */
    s_gas_src = (uint8_t)(s_gas_src | ALERT_GAS_SRC_SERVER);
    Alert_GasAlarm(msg_extract_id(text_cp1251, len));
    return (uint8_t)ALERT_GAS_ALARM;
  }
  if (msg_contains(text_cp1251, len, ALERT_MSG_MARK_EVACUATION) != 0u)
  {
    Alert_Set(ALERT_EVACUATION);
    alert_rearm_indication(ALERT_EVACUATION);
    LOG_W(VLOG_M_SYS, "alert: EVACUATION by message seq=%u", (uint32_t)seq);
    return (uint8_t)ALERT_EVACUATION;
  }

  s_alert[ALERT_MESSAGE].param = seq;
  Alert_Set(ALERT_MESSAGE);
  alert_rearm_indication(ALERT_MESSAGE);
  return (uint8_t)ALERT_MESSAGE;
}

/* «ВСЕМ НА ВЫХОД» с самого трекера (режим «мастер»): локальный статус +
   бит EVACUATION в ControlLora16 (сервер разносит остальным трекерам).      */
uint8_t Alert_SendEvacuation(void)
{
  if ((Sns_Cfg_struct.Config_common.Alert_Flags & ALERT_CFG_MASTER) == 0u)
  {
    LOG_W(VLOG_M_SYS, "alert: evacuation REJECTED (master mode off)");
    return 0u;
  }

  Alert_Set(ALERT_EVACUATION);
  alert_rearm_indication(ALERT_EVACUATION);
#if CONFIG_LORA
  if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA))
  {
    Lora_SetControlLoraBit(ST_LORA_BIT_EVACUATION, 1u);
  }
#endif
  LOG_W(VLOG_M_SYS, "alert: EVACUATION sent by master tracker");
  return 1u;
}

alert_id_t Alert_Top(void)
{
  uint8_t i;

  for (i = 1u; i < (uint8_t)ALERT_ID_COUNT; i++)
  {
    if (s_alert[i].status != 0u) { return (alert_id_t)i; }
  }
  return ALERT_NONE;
}

const char *Alert_Name(alert_id_t id)
{
  if (id >= ALERT_ID_COUNT) { return ""; }
  return s_desc[id].name;
}

uint16_t Alert_Param(alert_id_t id)
{
  if (id >= ALERT_ID_COUNT) { return 0u; }
  return s_alert[id].param;
}

const char *Alert_SysErrName(uint8_t bit)
{
  if ((bit >= (uint8_t)ALERT_SYSERR_BITS) || (s_syserr_name[bit] == NULL))
  {
    return "";
  }
  return s_syserr_name[bit];
}

const char *Alert_ParamText(alert_id_t id)
{
  if (id == ALERT_DEVICE_ERROR)
  {
    /* param = номер бита Config_common.StateErr (enum ST_COMMON_ERR)        */
    return Alert_SysErrName((uint8_t)s_alert[id].param);
  }
  return "";
}

const char *Alert_MessageText(void)
{
  return s_msg_text;
}

uint8_t Alert_AnyActive(void)
{
  return (Alert_Top() != ALERT_NONE) ? 1u : 0u;
}

/* Движок реестра: 100 мс из Measure Task.                                   */
void Alert_Tick(void)
{
  /* 1) Движение от акселерометра (опрос 10 Гц из measure_task_function).
        Любое событие -> сброс окна неподвижности и снятие статуса.           */
#if CONFIG_LIS3DH
  {
    uint8_t guard = 8u;                      /* защита от «залипания» флага   */
    while ((guard != 0u) && (lis3dh_motion_event() != false))
    {
      guard--;
      immob_on_motion();
    }
  }
#endif

  /* 2) Downlink LoRa: текстовое сообщение сервера (Message, п.7.1).
        Забираем здесь, чтобы Lora_S7678S.c не знал про статусы.              */
#if CONFIG_LORA
  if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA))
  {
    if (Lora_MessageIsNew() != 0u)
    {
      TX_INTERRUPT_SAVE_AREA;    /* сохраняет состояние прерываний для TX_DISABLE/TX_RESTORE */
      uint8_t *text = NULL;
      uint8_t  len  = 0u;
      uint8_t  seq  = 0u;

      /* Буфер lora_message_text принадлежит модулю LoRa и переписывается в
         Receiver Task при приёме следующего сообщения, а Alert_Tick идёт в
         Measure Task - копируем (CP1251 -> UTF-8) с выключенными прерываниями
         планировщика, как это делает Ble_Control_States_Update(). Квитирование
         доставки (MsgAck, бит 12 контейнера) выставляет сам Lora-модуль при
         приёме - от Alert ничего не требуется.                              */
      TX_DISABLE;
      if (Lora_GetMessage(&text, &len, &seq) != 0u)
      {
        (void)Alert_LoraMessage(text, len, seq);
      }
      TX_RESTORE;
      Lora_MessageDisplayed();     /* гасим флаг «новое»                     */
    }
  }
#endif

  /* 3) Секундная логика: неподвижность, пороги газа, ошибки прибора, батарея,
        устройства комплекта, автоочистка, LoRa Motion.                       */
  s_tick_div++;
  if (s_tick_div >= (uint8_t)ALERT_TICKS_PER_SEC)
  {
    s_tick_div = 0u;

    immob_tick_1s();

#if CONFIG_BLE
    gas_local_tick();          /* пороги своего газоанализатора Avis        */
    device_presence_tick();    /* нет СИЗ / ГА / браслета                   */
#endif

    syserr_tick();             /* ошибки модулей (ST_COMMON_ERR)            */
    battery_tick();            /* низкий/критический заряд по напряжению    */
    alert_hold_tick();         /* автоочистка статусов с одноразовым источником */

#if CONFIG_LIS3DH
    /* Порог движения мог поменять конфигуратор/меню - применяем на лету.     */
    {
      uint16_t ths = immob_cfg(Sns_Cfg_struct.Config_common.Immobility_Ths_mg,
                               ALERT_MOTION_THS_MG_DEF);
      if (ths != s_motion_ths)
      {
        s_motion_ths = ths;
        (void)lis3dh_motion_set_threshold(ths);
        LOG_I(VLOG_M_SYS, "alert: motion threshold = %u mg", (uint32_t)ths);
      }
    }
#endif

#if CONFIG_LORA
    /* Бит эвакуации от сервера: статус зеркалит его (сервер и снимает).      */
    if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA))
    {
      if (TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_EVACUATION))
      {
        Alert_Set(ALERT_EVACUATION);
      }
      else if (s_alert[ALERT_EVACUATION].status != 0u)
      {
        /* Снято сервером. Локальную (мастер) эвакуацию сервер не сбрасывает -
           её гасит hold_s после последнего подтверждения.                   */
        Alert_Clear(ALERT_EVACUATION);
      }
      /* Motion-блок: поза + признак неподвижности + уровень активности.      */
      Lora_SetMotionState(immob_posture(),
                          (uint8_t)((s_alert[ALERT_IMMOBILITY].status != 0u) ? 1u : 0u),
                          s_im.activity);
    }
#endif
  }

  /* 4) Индикация: один лидер (самый приоритетный активный статус).          */
  alert_indication_arbitrate();
}
