/**
  ******************************************************************************
  * @file    Alert_vector.c
  * @brief   Реестр статусов/тревог и их индикация: неподвижность, отсутствие
  *          устройств комплекта (СИЗ/ГА/браслет), «ОСТОРОЖНО! ГАЗ!»,
  *          «ВСЕМ НА ВЫХОД», сообщение сервера. Модель и требования - в
  *          Alert_vector.h.
  *
  *          ГЛАВНОЕ РАЗЛИЧЕНИЕ (требование автора п.2):
  *            СТАТУС  - признак («сотрудник неподвижен», «нет СИЗ», «сервер
  *                      поднял эвакуацию»). Живёт, пока живо условие.
  *            ИНДИКАЦИЯ - звук/свет/вибро этого статуса. Кнопка (квитирование)
  *                      гасит ТОЛЬКО индикацию: статус, его таймеры и отправка
  *                      инцидента на сервер сохраняются.
  *          Есть статусы, у которых индикация НЕ снимается вручную
  *          (ack_button = 0: нет СИЗ/ГА/браслета) - она прекращается, когда
  *          связь восстановилась или устройство удалено из настроек трекера.
  *
  *          КОНТЕКСТ: Alert_Tick() - поток Measure Task, период
  *          ALERT_TICK_MS (100 мс). Alert_Set/Clear/AckButton - любой поток
  *          (только volatile-записи, движок разберёт их в свой тик).
  *          Индикацию ведёт Notify_vector (один владелец - этот модуль).
  ******************************************************************************
  */
/* Инклуды проекта - В ОДНОМ МЕСТЕ (агрегатор main_vector.h): Alert_vector.h,
   vector_config.h (ALERT_*), Notify_vector.h, shared_types.h
   (Sns_Cfg_struct, Ble_telemetry, sensors_status), vector_tick.h, vector_log.h,
   lis3dh.h (CONFIG_LIS3DH), Lora_S7678S.h (CONFIG_LORA).                    */
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

/* Делитель тика 100 мс -> 1 с (счётчики неподвижности, свежесть устройств). */
#define ALERT_TICKS_PER_SEC       (1000u / ALERT_TICK_MS)

/* Маска каналов индикации: светодиоды + вибро + писк. Голосовой клип
   добавляется отдельно, если для статуса задан ALERT_SND_* != NONE.          */
#define ALERT_CH_ALARM            ((uint8_t)(NOTIFY_CH_LED1 | NOTIFY_CH_LED2 | \
                                             NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP))
#define ALERT_CH_DEVICE           ((uint8_t)(NOTIFY_CH_LED1 | NOTIFY_CH_LED2 | \
                                             NOTIFY_CH_VIBRO | NOTIFY_CH_BEEP))
#define ALERT_CH_MESSAGE          ((uint8_t)(NOTIFY_CH_LED1 | NOTIFY_CH_BEEP))

/* ============================================================================
 *                                ПЕРЕМЕННЫЕ
 * ============================================================================ */

/* Описание статуса (const-таблица во flash).                                */
typedef struct
{
  const char *name;           /* ключ i18n для экрана/лога                   */
  uint8_t  prio;              /* NOTIFY_PRIO_* (тревоги вытесняют сообщения) */
  uint8_t  ch;                /* маска notify_ch_t (без бита SOUND)          */
  uint16_t period_ms;         /* период индикации (сколько включено)         */
  uint16_t pause_ms;          /* пауза                                       */
  uint8_t  blink;             /* 1 = мигать LED (100 мс)                     */
  uint8_t  ack_button;        /* 1 = индикация снимается зелёной кнопкой     */
  uint16_t voice;             /* ALERT_SND_* (ALERT_SND_NONE = без голоса)   */
  uint8_t  container_event;   /* код EventLog LoRa (0 = не отправлять)       */
  uint8_t  severity;          /* ALERT_SEV_* для EventLog                    */
  uint16_t hold_s;            /* автоочистка: 0 = статус снимает только его
                                 источник; иначе гаснет через hold_s после
                                 последнего подтверждения (Alert_Set)         */
} alert_desc_t;

/* Таблица статусов. Порядок строк = alert_id_t = приоритет показа на экране и
   перехвата индикации (чем меньше id, тем важнее): угроза жизни (газ,
   эвакуация) -> неподвижность -> комплект (нет ГА/СИЗ/браслета) -> сообщение.
   Тайминги - ALERT_*_MS из vector_config.h; у «нет СИЗ/ГА/браслета»
   ack_button = 0 (индикация не снимается вручную - требование автора п.3).   */
static const alert_desc_t s_desc[ALERT_ID_COUNT] =
{
  /* ALERT_NONE - заглушка, не используется                                  */
  { "",                NOTIFY_PRIO_SERVICE, 0u,               0u,                     0u,                    0u, 0u, ALERT_SND_NONE,       0u,                   ALERT_SEV_INFO,     0u                   },
  /* «ОСТОРОЖНО! ГАЗ!» - пороги своего газоанализатора Avis ИЛИ команда
     сервера; на экране виден ID трекера-источника (требование автора п.4)   */
  { "ОСТОРОЖНО! ГАЗ!",
		  NOTIFY_PRIO_ALARM,
		  ALERT_CH_ALARM,
		  ALERT_PERIOD_GAS_MS,
		  ALERT_PAUSE_GAS_MS,
		  0u,
		  1u,
		  ALERT_SND_GAS,
		  0u,
		  ALERT_SEV_CRITICAL,
		  ALERT_GAS_HOLD_S     },
  /* «ВСЕМ НА ВЫХОД» - бит сервера (пока поднят) или трекер-«мастер»         */
  { "ВСЕМ НА ВЫХОД",   NOTIFY_PRIO_ALARM,   ALERT_CH_ALARM,   ALERT_PERIOD_EVAC_MS,   ALERT_PAUSE_EVAC_MS,   1u, 1u, ALERT_SND_EVACUATION, 0u,                   ALERT_SEV_CRITICAL, ALERT_EVAC_HOLD_S    },
  /* Неподвижность: инцидент «Отсутствие активности» (EventLog 0x10).
     Снимается ТОЛЬКО появлением движения - автоочистки нет (hold_s = 0)     */
  { "Неподвижность",   NOTIFY_PRIO_ALARM,   ALERT_CH_ALARM,   ALERT_PERIOD_IMMOB_MS,  ALERT_PAUSE_IMMOB_MS,  0u, 1u, ALERT_SND_ALARM,      ALERT_CEV_INACTIVITY, ALERT_SEV_CRITICAL, 0u                   },
  /* Комплект: нет газоанализатора / СИЗ / браслета. Индикация НЕ снимается
     вручную (ack_button = 0), статус снимает восстановление связи или
     удаление устройства из настроек - автоочистки тоже нет                  */
  { "Нет ГА",          NOTIFY_PRIO_ALARM,   ALERT_CH_DEVICE,  ALERT_PERIOD_DEVICE_MS, ALERT_PAUSE_DEVICE_MS, 0u, 0u, ALERT_SND_NONE,       0u,                   ALERT_SEV_WARNING,  0u                   },
  { "Нет СИЗ",         NOTIFY_PRIO_ALARM,   ALERT_CH_DEVICE,  ALERT_PERIOD_DEVICE_MS, ALERT_PAUSE_DEVICE_MS, 0u, 0u, ALERT_SND_NONE,       0u,                   ALERT_SEV_WARNING,  0u                   },
  { "Нет браслета",    NOTIFY_PRIO_ALARM,   ALERT_CH_DEVICE,  ALERT_PERIOD_DEVICE_MS, ALERT_PAUSE_DEVICE_MS, 0u, 0u, ALERT_SND_NONE,       0u,                   ALERT_SEV_WARNING,  0u                   },
  /* Текстовое сообщение сервера (сигнал сообщения)                           */
  { "Сообщение",       NOTIFY_PRIO_UI,      ALERT_CH_MESSAGE, ALERT_PERIOD_MSG_MS,    ALERT_PAUSE_MSG_MS,    0u, 1u, ALERT_SND_MESSAGE,    0u,                   ALERT_SEV_INFO,     ALERT_MSG_HOLD_S     },
};

/* Состояние одного статуса.                                                 */
typedef struct
{
  volatile uint8_t  status;       /* 1 = статус активен (признак)            */
  volatile uint8_t  indication;   /* 1 = индикация идёт (кнопка гасит её)     */
  volatile uint16_t param;        /* GAS: ID трекера; MESSAGE: MsgSeq        */
  volatile uint32_t since_ms;     /* когда статус взведён                    */
  volatile uint32_t ack_ms;       /* когда квитировали                       */
} alert_state_t;

static alert_state_t s_alert[ALERT_ID_COUNT];

/* Слоты запроса к Notify: фаза/сценарий должны жить до применения в
   Notify_Tick, поэтому держим их здесь (два чередующихся слота - как в
   Notify_AlarmEx: следующий старт не портит тот, что разбирает движок).     */
static notify_phase_t s_notify_phase[2];
static notify_seq_t   s_notify_seq[2];
static volatile uint8_t s_notify_slot = 0u;
static uint8_t  s_notify_owner = (uint8_t)ALERT_NONE;  /* чья индикация идёт */

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

/* --- сообщение сервера ---------------------------------------------------- */
static char     s_msg_text[ALERT_MSG_UTF8_LEN];  /* UTF-8 для LVGL           */
static uint8_t  s_msg_seq;                       /* MsgSeq последнего        */
static uint8_t  s_msg_shown;                     /* идемпотентность повторов  */

/* Источники газовой тревоги (их ДВА, статус один): пороги своего
   газоанализатора Avis и команда сервера. Пока жив хотя бы один бит - статус
   активен; локальный сброс порогов не гасит серверную тревогу и наоборот.  */
#define ALERT_GAS_SRC_LOCAL   (0x01u)   /* Connected_device.StateErr & маска  */
#define ALERT_GAS_SRC_SERVER  (0x02u)   /* сообщение/бит от сервера           */
static uint8_t  s_gas_src;

/* Тактовик: делитель 100 мс -> 1 с.                                        */
static uint8_t  s_tick_div;

/* Применённый порог детектора движения, мг (0 = ещё не применяли).          */
#if CONFIG_LIS3DH
static uint16_t s_motion_ths;
#endif

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
 *                     ИНДИКАЦИЯ: арбитраж через Notify_vector
 * ============================================================================ */

/* Приоритетный статус, у которого статус активен И индикация не квитирована.
   Порядок перечисления alert_id_t и есть приоритет (меньше id = важнее).    */
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

/* Запустить индикацию статуса id (бесконечно: count = 0, пока статус жив или
   не появится более приоритетный). Голосовой клип добавляется битом SOUND,
   если для статуса задан ALERT_SND_* (звуки автор добавит позже).           */
static void alert_indication_start(alert_id_t id)
{
  const alert_desc_t *d = &s_desc[id];
  uint8_t slot = (uint8_t)(s_notify_slot ^ 1u);
  notify_phase_t ph;

  ph.ch        = d->ch;
  ph.t_on_ms   = d->period_ms;
  ph.t_off_ms  = d->pause_ms;
  ph.repeats   = 1u;      /* «пачка» = 1 ВКЛ/ВЫКЛ, повторы задаёт count = 0   */
  ph.blink     = d->blink;
  ph.sound_idx = 0u;

  if (d->voice != (uint16_t)ALERT_SND_NONE)
  {
    ph.ch        = (uint8_t)(ph.ch | (uint8_t)NOTIFY_CH_SOUND);
    ph.sound_idx = d->voice;
  }
  /* Звук при неподвижности можно выключить настройкой (Alert_Flags).        */
  if (((id == (uint8_t)ALERT_IMMOBILITY) != 0) &&
      ((Sns_Cfg_struct.Config_common.Alert_Flags & ALERT_CFG_IMMOB_SOUND) == 0u))
  {
    ph.ch = (uint8_t)(ph.ch & (uint8_t)~(NOTIFY_CH_BEEP | NOTIFY_CH_SOUND));
  }

  s_notify_phase[slot]    = ph;
  s_notify_seq[slot].ph    = &s_notify_phase[slot];
  s_notify_seq[slot].n     = 1u;
  s_notify_seq[slot].prio  = d->prio;
  s_notify_seq[slot].count = (uint8_t)NOTIFY_REPEAT_FOREVER;  /* до Alert_Tick */
  s_notify_slot = slot;

  Notify_StartEx(&s_notify_seq[slot], 0u, 0u, 0u, (uint8_t)NOTIFY_REPEAT_FOREVER);
}

/* Арбитраж: индикацию ведёт ОДИН статус (самый приоритетный). При смене
   лидера прежняя последовательность гасится, новая стартует.                */
static void alert_indication_arbitrate(void)
{
  alert_id_t top = alert_top_indication();

  /* Прибор выключен или идёт прогрев (DEVICE_TURNED_ON_1 - «включён без
     звука/вибро», как в Avis): индикацию не ведём, но СТАТУСЫ считаем
     (неподвижность, свежесть устройств от экрана и звука не зависят).
     Флаг indication у статусов не трогаем - после включения прибор снова
     начнёт оповещать, а квитирование не «проглатывается» втихую.           */
  if (device_turn != DEVICE_TURNED_ON)
  {
    if (s_notify_owner != (uint8_t)ALERT_NONE)
    {
      Notify_Stop();
      s_notify_owner = (uint8_t)ALERT_NONE;
    }
    return;
  }

  if (top == (alert_id_t)s_notify_owner)
  {
    /* Лидер не сменился. Если движок оповещений при этом пуст (сценарий
       кто-то погасил извне: Notify_Stop из меню/выключения, либо чужая
       тревога того же приоритета отработала и завершилась) - поднимаем свою
       индикацию снова, иначе статус активен, а звука/света нет.             */
    if ((top != ALERT_NONE) && (Notify_Busy() == 0u))
    {
      alert_indication_start(top);
    }
    return;
  }

  if (s_notify_owner != (uint8_t)ALERT_NONE)
  {
    Notify_Stop();
  }
  s_notify_owner = (uint8_t)top;
  if (top != ALERT_NONE)
  {
    alert_indication_start(top);
    LOG_I(VLOG_M_SYS, "alert: indication start [%s] period=%u pause=%u voice=%u",
          s_desc[top].name, (uint32_t)s_desc[top].period_ms,
          (uint32_t)s_desc[top].pause_ms, (uint32_t)s_desc[top].voice);
  }
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
   предаварийного таймера).                                                  */
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

/* Инцидент на сервер: Motion.MOTION_BIT_INACTIVE + EventLog «Отсутствие
   активности» (0x10). Отправляем ОДИН раз на эпизод (incident_sent).        */
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
    /* Код события и severity берём из таблицы статусов (s_desc), а не
       литералом: описание статуса - единственный источник правды.
       Value = 0x8000 - флаг «Отсутствие активности» (LoRa v4 п.4.8).       */
    const alert_desc_t *d = &s_desc[ALERT_IMMOBILITY];
    if (d->container_event != 0u)
    {
      Lora_AddContainerEvent(d->container_event, d->severity, (int16_t)0x8000);
    }
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
   залипания; у неподвижности и «нет СИЗ/ГА/браслета» hold_s = 0 - их снимает
   только их источник (движение / восстановление связи / настройка).         */
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
/* Статус «нет X»: взводится, если устройство ОЖИДАЕТСЯ в настройках трекера
   (Devices_Expected) и его данные не свежие; снимается, когда связь
   восстановилась (пакеты снова приходят) ИЛИ устройство удалено из настроек
   (бит сброшен) - требование автора п.3. Индикация у таких статусов не
   снимается кнопкой (ack_button = 0).                                      */
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

/* Сброс реестра. КОНТЕКСТ: Measure Task (Vector_Run_Pre_Init), до первого
   Alert_Tick().                                                            */
void Alert_Init(void)
{
  uint8_t i;

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

  s_msg_text[0] = '\0';
  s_msg_seq     = 0u;
  s_msg_shown   = 0u;
  s_tick_div    = 0u;

  s_notify_owner = (uint8_t)ALERT_NONE;
  Notify_Stop();

#if CONFIG_LIS3DH
  /* Порог детектора движения - из настроек трекера (Immobility_Ths_mg),
     с дефолтом, если поле нулевое (старый конфиг во flash).                 */
  s_motion_ths = immob_cfg(Sns_Cfg_struct.Config_common.Immobility_Ths_mg,
                           ALERT_MOTION_THS_MG_DEF);
  (void)lis3dh_motion_set_threshold(s_motion_ths);
#endif
}

/* Заново взвести ИНДИКАЦИЮ активного статуса (статус не трогаем). Нужно для
   повторяющихся команд сервера: сотрудник квитировал газовую тревогу, а сервер
   прислал её ещё раз (новый MsgSeq) - прибор обязан снова оповестить.
   Дубли с тем же MsgSeq отсекает Alert_LoraMessage (идемпотентность).       */
static void alert_rearm_indication(alert_id_t id)
{
  if (s_alert[id].status != 0u)
  {
    s_alert[id].indication = 1u;
    s_alert[id].since_ms   = VTICK_MS();
    LOG_I(VLOG_M_SYS, "alert: indication REARM [%s]", s_desc[id].name);
  }
}

/* Взвести статус: индикация стартует сама (Alert_Tick), статус живёт до
   Alert_Clear(). КОНТЕКСТ: любой поток.                                    */
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
    /* Статус уже активен: обновляем метку ПОСЛЕДНЕГО ПОДТВЕРЖДЕНИЯ условия
       (по ней работает автоочистка hold_s) - ни индикацию, ни лог не трогаем,
       иначе ежекунундное подтверждение отменяло бы квитирование.            */
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
    /* Индикация не снимается вручную (нет СИЗ/ГА/браслета): кнопку не
       «съедаем», возвращаем ALERT_NONE - жест уйдёт в навигацию меню.       */
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
       её снимет hold_s (ALERT_GAS_HOLD_S) после последнего сообщения.        */
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

const char *Alert_MessageText(void)
{
  return s_msg_text;
}

uint8_t Alert_AnyActive(void)
{
  return (Alert_Top() != ALERT_NONE) ? 1u : 0u;
}

/* Движок: 100 мс из Measure Task.                                           */
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
        Забираем здесь, чтобы Lora_S7678S.c не знал про тревоги.              */
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
         приёме - от Alert ничего не требуется.                             */
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

  /* 3) Секундная логика: неподвижность, устройства комплекта, LoRa Motion.   */
  s_tick_div++;
  if (s_tick_div >= (uint8_t)ALERT_TICKS_PER_SEC)
  {
    s_tick_div = 0u;

    immob_tick_1s();

#if CONFIG_BLE
    gas_local_tick();          /* пороги своего газоанализатора Avis        */
#endif

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

#if CONFIG_BLE
    device_presence_tick();
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
           её гасит повторное нажатие/квитирование + снятие бита.            */
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
