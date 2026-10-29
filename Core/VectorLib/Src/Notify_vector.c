/**
  ******************************************************************************
  * @file    Notify_vector.c
  * @brief   Движок НЕБЛОКИРУЮЩИХ оповещений: фазы свет/звук/вибро по дедлайнам
  *          VTICK. Подробное описание модели - в Notify_vector.h.
  *
  *          КОНТЕКСТ: Notify_Tick() - поток LVGL Task (app_azure_rtos.c,
  *          цикл ~10 мс). Notify_Start()/Notify_Stop() - любой поток (пишут
  *          только volatile-запрос, движок разбирает его в свой Tick).
  *
  *          Машина фазы: ON (канал включён; ждём t_on_ms или конец звука) ->
  *          OFF (канал выключен; пауза t_off_ms) -> повтор фазы (repeats) /
  *          следующая фаза / loop / СТОП. Вытеснение и Stop всегда гасят
  *          активный канал (светодиод не залипнет, вибро не зависнет).
  ******************************************************************************
  */
/* Инклуды проекта - В ОДНОМ МЕСТЕ (агрегатор Vector_main.h): Notify_vector.h,
   main.h (SET_ON/SET_OFF(ALARM_LED_1/2)), vector_tick.h (тик RTOS),
   vector_macros.h (Vibro_On/Off), vector_log.h, audio_player.h (audio_beep/
   audio_play_now/audio_stop/is_busy), audio_ids.h (SND_*).                  */
#include "Vector_main.h"

/* Защитная выдержка фазы NOTIFY_WAIT_END: очередь плеера может стартовать не
   мгновенно (audio_is_busy()==0 ещё до начала) - раньше неё конец не решать. */
#define NOTIFY_WAIT_GRACE_MS   (80u)

/* Состояния движка.                                                         */
typedef enum
{
  NS_IDLE = 0,   /* ничего не идёт                                           */
  NS_ON,         /* канал фазы включён                                       */
  NS_OFF         /* пауза после фазы                                         */
} notify_state_t;

/* Запросы из других потоков (только volatile-записи, разбирает Tick).        */
static volatile const notify_seq_t *s_req_seq  = NULL;  /* старт сценария    */
static volatile uint8_t             s_req_stop = 0u;    /* 1 = стоп запрос   */

/* Состояние движка (владелец - поток LVGL Task).                            */
static const notify_seq_t *s_seq      = NULL;  /* текущий сценарий           */
static uint8_t  s_idx      = 0u;               /* номер фазы                 */
static uint8_t  s_rep      = 0u;               /* осталось повторов фазы     */
static uint8_t  s_state    = (uint8_t)NS_IDLE;
static uint8_t  s_wait_end = 0u;               /* 1 = ждём конец звука       */
static uint32_t s_t0       = 0u;               /* VTICK старта окна фазы     */
static uint32_t s_t_ms     = 0u;               /* длительность окна, мс      */

/* ---------------------------------------------------------------------------
 * Готовые сценарии
 * -------------------------------------------------------------------------*/

/* ВКЛЮЧЕНИЕ ПРИБОРА (Button_Run_Start): 3 писка из внутренней flash.
   Писк = клип audio_beep_pcm[] 240 мс; ждём его конец (WAIT_END), пауза
   300 мс - ритм как в примере Avis (короткие сигналы через 200..300 мс).    */
static const notify_phase_t s_poweron_phases[] =
{
  /*  канал           вкл              пауза повт  звук */
  { NOTIFY_CH_BEEP,  NOTIFY_WAIT_END, 300u, 3u, 0u },
};
const notify_seq_t NotifySeq_PowerOn =
{
  s_poweron_phases,
  (uint8_t)(sizeof(s_poweron_phases) / sizeof(s_poweron_phases[0])),
  NOTIFY_PRIO_UI,
  0u
};

/* ПРИМЕР будущей тревоги (раскомментируйте и дайте имя, когда понадобится
   "свет, потом звук, потом вибро" - порядок фаз = порядок оповещения):
static const notify_phase_t s_alarm_phases[] =
{
  { NOTIFY_CH_LED1,  200u, 100u, 2u, 0u            },  // 2 вспышки
  { NOTIFY_CH_SOUND, NOTIFY_WAIT_END, 0u, 1u, SND_KOLOKOL_1 }, // голос/звук
  { NOTIFY_CH_VIBRO, 500u, 200u, 1u, 0u            },  // вибро 0.5 с
};
const notify_seq_t NotifySeq_AlarmGas =
  { s_alarm_phases, 3u, NOTIFY_PRIO_ALARM, 1u };        // loop до подтверждения
   Голосовое сообщение БЕЗ фаз - одна фаза SOUND и всё:
static const notify_phase_t s_voice_phases[] =
{
  { NOTIFY_CH_SOUND, NOTIFY_WAIT_END, 0u, 1u, SND_ГОЛОСОВОЕ_ИМЯ },
};                                                       */

/* ---------------------------------------------------------------------------
 * Каналы: включить/выключить фазу
 * -------------------------------------------------------------------------*/
static void phase_on(const notify_phase_t *p)
{
  switch (p->ch)
  {
  case NOTIFY_CH_LED1:  SET_ON(ALARM_LED_1);                  break;
  case NOTIFY_CH_LED2:  SET_ON(ALARM_LED_2);                  break;
  case NOTIFY_CH_VIBRO: Vibro_On();                           break;
  case NOTIFY_CH_BEEP:  audio_beep();                         break; /* 240 мс, внутр. flash */
  case NOTIFY_CH_SOUND: (void)audio_play_now(p->sound_idx);   break; /* клип образа */
  default:                                                    break;
  }

  s_wait_end = (((p->ch == (uint8_t)NOTIFY_CH_BEEP) ||
                 (p->ch == (uint8_t)NOTIFY_CH_SOUND)) &&
                (p->t_on_ms == NOTIFY_WAIT_END)) ? 1u : 0u;
  s_t0    = VTICK_MS();
  s_t_ms  = s_wait_end ? (uint32_t)NOTIFY_WAIT_GRACE_MS : (uint32_t)p->t_on_ms;
  s_state = (uint8_t)NS_ON;
}

static void phase_off(const notify_phase_t *p)
{
  switch (p->ch)
  {
  case NOTIFY_CH_LED1:  SET_OFF(ALARM_LED_1);  break;
  case NOTIFY_CH_LED2:  SET_OFF(ALARM_LED_2);  break;
  case NOTIFY_CH_VIBRO: Vibro_Off();           break;
  case NOTIFY_CH_SOUND: audio_stop();          break; /* обрыв, если ON < клипа */
  case NOTIFY_CH_BEEP:                         break; /* писк доигрывает сам    */
  default:                                     break;
  }
}

/* Полный останов: погасить активный канал и сбросить машину.                */
static void seq_halt(void)
{
  if ((s_seq != NULL) && (s_state != (uint8_t)NS_IDLE) && (s_idx < s_seq->n))
  {
    phase_off(&s_seq->ph[s_idx]);
  }
  s_seq      = NULL;
  s_state    = (uint8_t)NS_IDLE;
  s_idx      = 0u;
  s_rep      = 0u;
  s_wait_end = 0u;
}

/* Старт фазы s_idx текущего s_seq (повторы инициализируются).               */
static void seq_begin_phase(void)
{
  const notify_phase_t *p = &s_seq->ph[s_idx];
  s_rep = (p->repeats > 0u) ? p->repeats : 1u;
  phase_on(p);
}

/* ---------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Сброс движка. КОНТЕКСТ: Measure Task (Menu_Init), до первого Tick.        */
void Notify_Init(void)
{
  s_req_seq  = NULL;
  s_req_stop = 0u;
  seq_halt();
}

/* Запрос старта сценария. КОНТЕКСТ: ЛЮБОЙ поток (только volatile-запись;
   применится в ближайший Notify_Tick, ~10 мс). Отказ по приоритету - в лог
   из Tick (владелец движка).                                                */
void Notify_Start(const notify_seq_t *seq)
{
  if ((seq == NULL) || (seq->ph == NULL) || (seq->n == 0u))
  {
    return;
  }
  s_req_stop = 0u;      /* свежий старт отменяет прежний запрос стопа        */
  s_req_seq  = seq;
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

/* Движок. КОНТЕКСТ: ТОЛЬКО поток LVGL Task, вызов ~каждые 10 мс
   (app_azure_rtos.c). Неблокирующий: вся работа - сравнение дедлайна и,
   по событию, включение/выключение канала.                                  */
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
    const notify_seq_t *ns = s_req_seq;
    s_req_seq = NULL;
    if ((s_seq == NULL) || (ns->prio >= s_seq->prio))
    {
      seq_halt();          /* вытеснение: погасить текущий сценарий          */
      s_seq = ns;
      s_idx = 0u;
      seq_begin_phase();
      LOG_I(VLOG_M_SYS,  "notify: seq start prio=%u n=%u loop=%u",
            ns->prio, ns->n, ns->loop);
    }
    else
    {
      LOG_W(VLOG_M_SYS,  "notify: seq REJECT prio=%u < cur %u",
            ns->prio, s_seq->prio);
    }
  }

  if ((s_seq == NULL) || (s_state == (uint8_t)NS_IDLE))
  {
    return;
  }
  p = &s_seq->ph[s_idx];

  /* 3) Фаза ON: ждём окно t_on (или защитную выдержку при WAIT_END), затем
        для звука - ещё и фактический конец воспроизведения.                  */
  if (s_state == (uint8_t)NS_ON)
  {
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

  /* 4) Фаза OFF: пауза кончилась - повтор / следующая фаза / loop / стоп.   */
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
  if (s_seq->loop != 0u)
  {
    s_idx = 0u;
    seq_begin_phase();     /* тревога: крутим до Notify_Stop()/вытеснения     */
    return;
  }
  seq_halt();              /* сценарий завершён                               */
}
