/**
  ******************************************************************************
  * @file    audio_player.c
  * @brief   Плеер звуков из внешней SPI flash (очередь, состояния, громкость)
  *
  *          Поток-владелец воспроизведения: только он трогает SAI/DMA.
  *          Команды приходят через tx_queue, пробуждение - через семафор,
  *          который взводят и команды, и ISR завершения DMA.
  *
  *          Тестовый вход - кнопки:
  *            BUTTON1 (PB1) - цикл состояний 0 -> 1 -> 2 -> 0
  *                            st0 = звук #0, st1 = звук #1, st2 = ТИШИНА
  *            BUTTON2 (PB2) - проиграть следующий звук образа по очереди
  *            BUTTON3 (PB3) - стоп
  *
  *          ПОРЯДОК СТАРТА (важно, см. также docs/CODE_MAP.md):
  *            main()                    - периферия, sf_probe(), кнопки
  *            tx_application_define()   - ext_init() -> audio_init()
  *            audio_init()              - RTOS-объекты, ПОТОК, load_image()
  *            ap_thread_entry()         - factory-запись (только если образа
  *                                        нет), затем вечный цикл команд
  *
  *          ДИАГНОСТИКА "ничего не происходит": audio_dbg_boot_stage,
  *          audio_dbg_wakes, audio_dbg_timeouts, audio_dbg_keys,
  *          audio_dbg_started/played/errors - см. audio_player.h.
  ******************************************************************************
  */
#include "audio_player.h"
#include "vector_config.h"
#include "audio_factory.h"
#include "audio_ids.h"
#include "audio_image.h"
#include "spiflash.h"
#include "extstore.h"
#include "audio_beep.h"
#include "sai.h"
#include "main.h"
#include "tx_api.h"
#include "vector_log.h"   /* консольный лог: VECTOR_LOG_ENABLE в vector_config.h */

/* ------------------------------------------------------------------ RTOS -- */
#define AP_STACK_SIZE   4096u
#define AP_PRIORITY     10u

/* Сколько тиков ThreadX ждать события, если VECTOR_AUDIO_WAKE_TIMEOUT_MS > 0.
   TX_TIMER_TICKS_PER_SECOND = 100 (10 мс/тик), округление вверх.            */
#if (VECTOR_AUDIO_WAKE_TIMEOUT_MS > 0u)
#define AP_WAKE_TICKS   (((VECTOR_AUDIO_WAKE_TIMEOUT_MS * TX_TIMER_TICKS_PER_SECOND) + 999u) / 1000u)
#define AP_WAKE_TMO     ((ULONG)AP_WAKE_TICKS)
#else
#define AP_WAKE_TMO     TX_WAIT_FOREVER
#endif

static TX_THREAD    ap_thread;
static uint8_t      ap_stack[AP_STACK_SIZE] __attribute__((aligned(8)));
static TX_QUEUE     ap_queue;
static ULONG        ap_queue_mem[AUDIO_QUEUE_LEN];
static TX_SEMAPHORE ap_wake;

/* Команды */
#define CMD_PLAY    1u
#define CMD_STOP    2u
#define CMD_STATE   3u
#define CMD_BEEP    4u
#define MSG(cmd, arg)   ((((ULONG)(cmd)) << 16) | ((ULONG)(arg) & 0xFFFFu))

/* ---------------------------------------------------------------- образ --- */
#define AP_MAX_SOUNDS   32u
static audio_img_header_t ap_hdr;
static audio_img_entry_t  ap_tab[AP_MAX_SOUNDS];
static uint8_t            ap_img_ok = 0;
/* factory-запись делается РОВНО ОДИН раз за старт и только если образа нет:
   иначе каждая перезагрузка стирала и писала 195 КБ во внешнюю flash.        */
static uint8_t            ap_factory_tried = 0;

/* ---------------------------------------------------------------- буфер ---
 * ap_buf - ОДИН на всё устройство: звук читается из flash ЦЕЛИКОМ сюда и
 * одной DMA-транзакцией уходит в SAI. 131070 байт (65535 сэмплов = 8.19 с
 * при 8 кГц). Второй звук в это же место не читается, поэтому одновременное
 * воспроизведение двух дорожек невозможно by design.                        */
static int16_t ap_buf[AUDIO_BUF_SAMPLES];

/* -------------------------------------------------------------- состояние -
 * Таблица "состояние -> звук". По умолчанию берётся из СГЕНЕРИРОВАННОГО
 * audio_ids.h (первые два звука образа), поэтому при добавлении звуков
 * править здесь ничего не нужно: enum и SND_STATE_DEFAULT_* пересоздаются
 * скриптом pack_sounds.py вместе с образом.
 *
 * Хотите другую привязку - подставьте имена SND_* из audio_ids.h, например:
 *     { SND_D_MYVOICE, SND_A_GAS, AUDIO_STATE_SILENT }
 * Кнопка BUTTON1 крутит состояния по кругу (см. HAL_GPIO_EXTI_Rising_Callback).
 */
static volatile uint8_t  ap_state = 0;
static const uint16_t    ap_state_map[3] = {
  SND_STATE_DEFAULT_0,      /* состояние 0 */
  SND_STATE_DEFAULT_1,      /* состояние 1 */
  AUDIO_STATE_SILENT        /* состояние 2 = тишина */
};

/* Защита на этапе компиляции: индексы из таблицы обязаны существовать
   в данном составе образа (0xFFFF = тишина, проверяется отдельно). */
typedef char ap_state_assert0
    [((SND_STATE_DEFAULT_0 == 0xFFFFu) || (SND_STATE_DEFAULT_0 < SND_COUNT)) ? 1 : -1];
typedef char ap_state_assert1
    [((SND_STATE_DEFAULT_1 == 0xFFFFu) || (SND_STATE_DEFAULT_1 < SND_COUNT)) ? 1 : -1];

/* --------------------------------------------------------------- громкость */
static volatile int32_t ap_vol_q15 = 32767;   /* 100% */

/* ---------------------------------------------------------- воспроизведение
 * ap_playing_idx: -1 = ничего не играет, -2 = играет аварийный писк,
 *                 >=0 = индекс звука в образе.
 * ap_pending_idx: -1 = нет отложенного звука, >=0 = ждёт окончания текущего.
 * Оба volatile: ap_playing_idx меняется и из ISR завершения DMA.            */
static volatile int32_t ap_playing_idx = -1;
static volatile int32_t ap_pending_idx = -1;

/* Дедлайн текущего звучания (HAL_GetTick() + длительность + запас). Нужен
   только watchdog'у ap_check_stuck(): если колбэк завершения DMA так и не
   пришёл, поток не встаёт навсегда, а гасит "залипший" звук сам.            */
#define AP_STUCK_MARGIN_MS  500u
static volatile uint32_t ap_play_deadline = 0;
static uint32_t ap_seen_played = 0;   /* для лога "звук доигран" (событие из ISR) */

/* ---------------------------------------------------------------- отладка --
 * Все счётчики смотрятся в отладчике (Expressions), ничего печатать не нужно.
 * Типичная проверка "почему молчит":
 *   boot_stage = 5 и keys = 0        -> кнопки не доезжают (EXTI/уровень пина)
 *   boot_stage = 5 и keys > 0,
 *   started = 0                      -> команды есть, но старт DMA не удался
 *                                      (смотрите last_err и errors)
 *   boot_stage = 2/4                 -> во внешней flash нет валидного образа
 *   wakes растёт, timeouts растёт    -> поток жив, событий просто нет
 */
#define AP_BOOT_INIT        0u  /* audio_init() ещё не отработал             */
#define AP_BOOT_IMG_OK      1u  /* образ во внешней flash валиден            */
#define AP_BOOT_IMG_BAD     2u  /* образа нет/бит -> уходим в factory-запись */
#define AP_BOOT_FACTORY_OK  3u  /* factory-запись прошла, образ перечитан    */
#define AP_BOOT_FACTORY_ERR 4u  /* factory-запись НЕ удалась, образа нет     */
#define AP_BOOT_RUN         5u  /* поток в рабочем цикле команд              */

volatile uint32_t audio_dbg_played    = 0;
volatile uint32_t audio_dbg_started   = 0;
volatile uint32_t audio_dbg_errors    = 0;
volatile int32_t  audio_dbg_cur_idx   = -1;
volatile uint32_t audio_dbg_last_err  = 0;
volatile uint32_t audio_dbg_boot_stage= AP_BOOT_INIT;
volatile uint32_t audio_dbg_wakes     = 0;  /* сколько раз проснулись        */
volatile uint32_t audio_dbg_timeouts  = 0;  /* проснулись по таймауту, не по
                                               событию (heartbeat)           */
volatile uint32_t audio_dbg_keys      = 0;  /* команд принято из очереди     */
volatile uint32_t audio_dbg_last_cmd  = 0;  /* последняя команда (CMD_*)     */
volatile uint32_t audio_dbg_dropped   = 0;  /* play вытеснил предыдущий play */
volatile uint32_t audio_dbg_stuck     = 0;  /* звук добит watchdog'ом: колбэк
                                               завершения DMA не пришёл      */



/* ============================================================ внутреннее == */
/* Watchdog звучания. КОНТЕКСТ: поток плеера, вызывается только на
   heartbeat-проходе (событий не было). Если звук "играет" дольше расчётного
   времени + запас, значит колбэк HAL_SAI_TxCpltCallback не пришёл (не включён
   GPDMA1_Channel11_IRQn, ошибка SAI, сбой DMA) - принудительно гасим передачу,
   иначе очередь встала бы навсегда. Рост audio_dbg_stuck = искать проблему в
   цепочке SAI/GPDMA, а не в кнопках.                                        */
static void ap_check_stuck(void)
{
  uint32_t dl = ap_play_deadline;

  if ((ap_playing_idx != -1) && (dl != 0u))
  {
    /* сравнение с учётом переполнения HAL_GetTick() (~49.7 суток) */
    if ((int32_t)(HAL_GetTick() - dl) > 0)
    {
      (void)HAL_SAI_Abort(&hsai_BlockA1);
      ap_playing_idx    = -1;
      audio_dbg_cur_idx = -1;
      ap_play_deadline  = 0;
      audio_dbg_stuck++;
    }
  }
}

/* Применяет программную громкость (Q15) к буферу PCM in-place.
   Контекст: поток плеера, после чтения из flash и ДО старта DMA.
   При 100% (g >= 32767) выходит сразу - копирования не делает.              */
static void apply_volume(int16_t *p, uint32_t n)
{
  int32_t g = ap_vol_q15;
  uint32_t i;
  if (g >= 32767)
  {
    return;
  }
  for (i = 0; i < n; i++)
  {
    int32_t v = ((int32_t)p[i] * g) >> 15;
    if (v > 32767)      { v = 32767;  }
    else if (v < -32768){ v = -32768; }
    p[i] = (int16_t)v;
  }
}

/* Аварийный писк: DMA прямо из const-массива во внутренней flash.
   Контекст: только поток плеера. Внешняя flash при этом НЕ трогается,
   поэтому писк работает даже при мёртвой SPI-шине/битом образе.
   Возврат: AUDIO_OK или AUDIO_ERR_IO (DMA не запустилась).                  */
static audio_err_t beep_now(void)
{
  if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audio_beep_pcm,
                           (uint16_t)audio_beep_samples) != HAL_OK)
  {
    audio_dbg_errors++;
    audio_dbg_last_err = (uint32_t)AUDIO_ERR_IO;
    return AUDIO_ERR_IO;
  }
  ap_playing_idx    = -2;      /* маркер: играет писк */
  audio_dbg_cur_idx = -2;
  audio_dbg_started++;
  ap_play_deadline  = HAL_GetTick() +
                      ((audio_beep_samples * 1000u) / 8000u) + AP_STUCK_MARGIN_MS;
  return AUDIO_OK;
}

/* Главный "запуск звука": таблица -> чтение PCM из внешней flash в ap_buf ->
   громкость -> одна DMA-транзакция в SAI. Контекст: только поток плеера.
   idx - индекс звука в образе (не идентификатор из audio_ids.h напрямую,
   а его значение - они совпадают по построению образа).
   Возврат: AUDIO_OK, иначе код ошибки (и audio_dbg_last_err/++errors).
   НЕ блокирует: окончание звучания придёт из HAL_SAI_TxCpltCallback.        */
static audio_err_t start_now(uint16_t idx)
{
  const audio_img_entry_t *e;
  audio_err_t rc = AUDIO_OK;

  if (!ap_img_ok)
  {
    /* Внешняя flash мертва или образ битый: аварийный писк из внутренней
       flash, чтобы устройство не молчало совсем. */
    audio_dbg_last_err = (uint32_t)AUDIO_ERR_NO_IMAGE;
    return beep_now();
  }
  else if (idx >= ap_hdr.count)      { rc = AUDIO_ERR_BAD_INDEX; }
  else
  {
    e = &ap_tab[idx];
    if ((e->length / 2u) > AUDIO_BUF_SAMPLES)
    {
      rc = AUDIO_ERR_TOO_LONG;
    }
    else if (ext_read(AUDIO_IMG_BASE_ADDR + e->offset, (uint8_t *)ap_buf, e->length) != 0)
    {
      rc = AUDIO_ERR_IO;
    }
    else
    {
      apply_volume(ap_buf, e->length / 2u);
      if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)ap_buf,
                               (uint16_t)(e->length / 2u)) != HAL_OK)
      {
        rc = AUDIO_ERR_IO;
      }
      else
      {
        LOG_I(VLOG_M_AUDIO, "play #%u '%s' %u samples @%u Hz",
              (uint32_t)idx, (const char *)e->name, (e->length / 2u), (uint32_t)e->rate);
        uint32_t rate = (e->rate != 0u) ? e->rate : 8000u;
        ap_playing_idx     = (int32_t)idx;
        audio_dbg_cur_idx  = (int32_t)idx;
        audio_dbg_started++;
        /* сколько звук должен играть + запас: порог для ap_check_stuck() */
        ap_play_deadline   = HAL_GetTick() +
                             (((e->length / 2u) * 1000u) / rate) + AP_STUCK_MARGIN_MS;
      }
    }
  }

  if (rc != AUDIO_OK)
  {
    audio_dbg_errors++;
    audio_dbg_last_err = (uint32_t)rc;
    LOG_E(VLOG_M_AUDIO, "start #%u failed, err=%d (2=no image 3=bad idx 4=too long 5=io)",
          (uint32_t)idx, (int32_t)rc);
  }
  return rc;
}

/* Немедленный стоп: гасим DMA/SAI и помечаем "не играет".
   Контекст: только поток плеера. Очередь команд НЕ чистит - это делает
   вызывающий (queue_clear), чтобы stop_now можно было звать и просто так.   */
static void stop_now(void)
{
  if (ap_playing_idx != -1)
  {
    (void)HAL_SAI_Abort(&hsai_BlockA1);
    ap_playing_idx    = -1;
    audio_dbg_cur_idx = -1;
  }
  ap_pending_idx   = -1;      /* отложенный звук тоже отменяем */
  ap_play_deadline = 0;       /* watchdog больше не нужен */
}

/* Выбрасывает ВСЕ накопившиеся команды из очереди (flush) И отложенный звук.
   Контекст: только поток плеера. Семафор ap_wake при этом может остаться
   взведённым - это безопасно: лишний проход цикла просто ничего не найдёт.  */
static void queue_clear(void)
{
  (void)tx_queue_flush(&ap_queue);
  ap_pending_idx = -1;
}

/* --------------------------------------------------------------- поток ----
 * Тело потока плеера - ЕДИНСТВЕННЫЙ владелец SAI/DMA и ap_buf.
 *
 * Цикл: проснулись (событие или heartbeat-таймаут) -> разобрали ВСЕ команды
 * из очереди -> если свободно и в очереди что-то лежит, взяли следующий звук.
 * Команды кладут audio_play()/audio_stop()/audio_set_state()/audio_beep(),
 * а пробуждение даёт либо они же, либо ISR завершения DMA (см. конец файла).
 *
 * Перед циклом - одноразовый factory-режим: если audio_init() не нашёл во
 * внешней flash валидного образа, зашиваем туда образ из прошивки и
 * перечитываем таблицу. Именно "если не нашёл": иначе каждая перезагрузка
 * стирала и писала 195 КБ заново (долгий "зависон" на старте + износ flash). */
static void ap_thread_entry(ULONG arg)
{
  (void)arg;

#if VECTOR_AUDIO_FACTORY_EMBED
  if ((!ap_img_ok) && (!ap_factory_tried))
  {
    ap_factory_tried = 1;
    audio_dbg_boot_stage = AP_BOOT_IMG_BAD;
    LOG_W(VLOG_M_AUDIO, "образа нет -> factory-запись (это секунды)");
    if (audio_factory_program() == 0)
    {
      audio_reload_image();
      audio_dbg_boot_stage = ap_img_ok ? AP_BOOT_FACTORY_OK : AP_BOOT_FACTORY_ERR;
      LOG_I(VLOG_M_AUDIO, "factory ok, sounds=%u", (uint32_t)ap_hdr.count);
    }
    else
    {
      /* Образ так и не появился: дальше на любую команду будет аварийный
         писк из внутренней flash (см. start_now). Причина - в sf_probe_rc,
         factory_dbg_sector и audio_dbg_last_err. */
      audio_dbg_boot_stage = AP_BOOT_FACTORY_ERR;
      LOG_E(VLOG_M_AUDIO, "factory FAILED -> только аварийный писк");
    }
  }
#endif

  if (ap_img_ok)
  {
    audio_dbg_boot_stage = AP_BOOT_RUN;   /* образ есть - рабочий режим */
  }
  /* иначе оставляем AP_BOOT_IMG_BAD / AP_BOOT_FACTORY_ERR: по boot_stage
     сразу видно, что плеер работает без образа (только аварийный писк) */

  /* Стартовая проверка тракта (VECTOR_AUDIO_BOOT_PLAY): до первого нажатия
     кнопки слышно, жив ли звук вообще. В боевом режиме ставится 0.          */
#if (VECTOR_AUDIO_BOOT_PLAY == 1)
  {
    uint16_t snd = ap_state_map[0];
    if (snd != AUDIO_STATE_SILENT)
    {
      LOG_I(VLOG_M_AUDIO, "boot play: state 0 sound #%u", (uint32_t)snd);
      (void)start_now(snd);
    }
  }
#elif (VECTOR_AUDIO_BOOT_PLAY == 2)
  LOG_I(VLOG_M_AUDIO, "boot play: sound #0");
  (void)start_now(0u);
#elif (VECTOR_AUDIO_BOOT_PLAY == 3)
  LOG_I(VLOG_M_AUDIO, "boot beep (проверка SAI/усилителя, без внешней flash)");
  (void)beep_now();
#endif

  for (;;)
  {
    ULONG msg = 0;

    /* Ждём любого события: команда ИЛИ завершение DMA.
       AP_WAKE_TMO != TX_WAIT_FOREVER (VECTOR_AUDIO_WAKE_TIMEOUT_MS > 0) -
       тогда это heartbeat: раз в секунду поток просыпается сам, и потерянное
       событие превращается в задержку, а не в вечный "завис".               */
    audio_dbg_wakes++;
    if (tx_semaphore_get(&ap_wake, AP_WAKE_TMO) != TX_SUCCESS)
    {
      audio_dbg_timeouts++;      /* событий не было - холостой проход */
      ap_check_stuck();          /* страховка от потерянного колбэка DMA */
    }

    /* Окончание звука приходит из ISR, а лог из ISR не печатается - поэтому
       показываем событие здесь, заметив изменение счётчика.                  */
    if (audio_dbg_played != ap_seen_played)
    {
      ap_seen_played = audio_dbg_played;
      LOG_D(VLOG_M_AUDIO, "sound finished (всего доиграно %u)", ap_seen_played);
    }
    if (audio_dbg_stuck != 0u)
    {
      LOG_W(VLOG_M_AUDIO, "watchdog: звук добит %u раз(а) - нет колбэка SAI DMA",
            audio_dbg_stuck);
      audio_dbg_stuck = 0;         /* не спамим: сообщение один раз на случай */
    }

    /* 1. Разбираем ВСЕ накопившиеся команды.
          Цикл НЕ прерывается на "занято": PLAY откладывается в ap_pending_idx,
          а STOP/STATE/BEEP обрабатываются сразу. Раньше здесь был break -
          из-за него стоп, пришедший сразу за play, ждал окончания длинного
          звука (до 8 с). */
    while (tx_queue_receive(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
    {
      ULONG cmd = (msg >> 16) & 0xFFu;
      ULONG a   = msg & 0xFFFFu;

      audio_dbg_keys++;
      audio_dbg_last_cmd = cmd;
      LOG_D(VLOG_M_AUDIO, "cmd=%u arg=%u playing=%d", cmd, a, (int32_t)ap_playing_idx);

      if (cmd == CMD_STOP)
      {
        stop_now();
        queue_clear();
      }
      else if (cmd == CMD_STATE)
      {
        uint16_t snd;
        stop_now();
        queue_clear();
        ap_state = (uint8_t)a;
        snd = (a < (sizeof ap_state_map / sizeof ap_state_map[0]))
              ? ap_state_map[a] : AUDIO_STATE_SILENT;
        if (snd != AUDIO_STATE_SILENT)
        {
          (void)start_now(snd);       /* смена режима прерывает текущий звук */
        }
      }
      else if (cmd == CMD_BEEP)
      {
        stop_now();
        queue_clear();
        (void)beep_now();
      }
      else if (cmd == CMD_PLAY)
      {
        if (ap_playing_idx == -1)
        {
          (void)start_now((uint16_t)a);   /* свободно - играем сразу        */
        }
        else
        {
          /* занято: текущий звук доигрывает, новый откладывается в ОДИН
             слот ожидания (последняя команда побеждает). Так стоп остаётся
             мгновенным, а "нажал три раза" не превращается в очередь
             устаревших звуков. */
          if (ap_pending_idx != -1)
          {
            audio_dbg_dropped++;
          }
          ap_pending_idx = (int32_t)a;
        }
      }
      else
      {
        /* неизвестная команда - игнорируем */
      }
    }

    /* 2. Если освободились и есть отложенный звук - запускаем его */
    if (ap_playing_idx == -1)
    {
      if (ap_pending_idx != -1)
      {
        int32_t nxt = ap_pending_idx;
        ap_pending_idx = -1;
        (void)start_now((uint16_t)nxt);
      }
    }
  }
}

/* Читает заголовок и таблицу образа sounds.img из внешней flash в RAM
   (ap_hdr + ap_tab[]) и проверяет magic/version/CRC32 таблицы.
   Контекст: audio_init() (ДО планировщика) и audio_reload_image() (поток).
   Мьютекс не берёт намеренно: оба вызова происходят, когда шину больше никто
   не трогает; обращение идёт напрямую через sf_read().
   Результат: ap_img_ok = 1 только если образ валиден, иначе 0 (и плеер
   переходит на аварийный писк). Скратч-буфер static, а не в стеке: вызов из
   audio_init() идёт на стеке MSP до старта планировщика, 1.2 КБ кадра там
   не нужны.                                                                 */
static void load_image(void)
{
  static uint8_t raw[AUDIO_IMG_HEADER_SIZE + AP_MAX_SOUNDS * AUDIO_IMG_ENTRY_SIZE];
  uint32_t tbl_bytes;

  ap_img_ok = 0;
  if (sf_read(AUDIO_IMG_BASE_ADDR, raw, sizeof ap_hdr) == HAL_OK)
  {
    const audio_img_header_t *h = (const audio_img_header_t *)raw;
    if ((h->magic == AUDIO_IMG_MAGIC) && (h->version == AUDIO_IMG_VERSION) &&
        (h->count > 0u) && (h->count <= AP_MAX_SOUNDS))
    {
      tbl_bytes = (uint32_t)h->count * AUDIO_IMG_ENTRY_SIZE;
      if (sf_read(AUDIO_IMG_BASE_ADDR + AUDIO_IMG_HEADER_SIZE, raw, tbl_bytes) == HAL_OK)
      {
        uint32_t crc = 0;
        uint32_t i;
        /* crc32 таблицы считаем тем же алгоритмом, что и pack_sounds.py
           (zlib.crc32); здесь - простая побайтовая реализация без таблицы */
        crc = 0xFFFFFFFFu;
        for (i = 0; i < tbl_bytes; i++)
        {
          uint32_t b = raw[i];
          int k;
          crc ^= b;
          for (k = 0; k < 8; k++)
          {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
          }
        }
        crc ^= 0xFFFFFFFFu;

        if (crc == h->table_crc32)
        {
          ap_hdr = *h;
          for (i = 0; i < h->count; i++)
          {
            const uint8_t *p = raw + i * AUDIO_IMG_ENTRY_SIZE;
            audio_img_entry_t *e = &ap_tab[i];
            e->offset   = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            e->length   = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
                          ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            e->samples  = (uint32_t)p[8] | ((uint32_t)p[9] << 8) |
                          ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
            e->rate     = (uint16_t)((uint32_t)p[12] | ((uint32_t)p[13] << 8));
            e->channels = p[14];
            e->format   = p[15];
            e->crc32    = (uint32_t)p[16] | ((uint32_t)p[17] << 8) |
                          ((uint32_t)p[18] << 16) | ((uint32_t)p[19] << 24);
            for (uint32_t j = 0; j < AUDIO_IMG_NAME_LEN; j++)
            {
              ((char *)&e->name)[j] = (char)p[20 + j];
            }
          }
          ap_img_ok = 1;   /* таблица разобрана ЦЕЛИКОМ - образ годен */
        }
      }
    }
  }

  LOG_I(VLOG_M_AUDIO, "image ok=%u sounds=%u", (uint32_t)ap_img_ok,
        ap_img_ok ? (uint32_t)ap_hdr.count : 0u);
}

/* =============================================================== публичное */
/* Инициализация плеера. Вызывать ОДИН раз из tx_application_define(), ПОСЛЕ
   ext_init() (нужен мьютекс шины flash и отсканированный журнал).
   Порядок внутри важен:
     1) создаём семафор/очередь - до потока, иначе поток дёрнет несуществующие
        объекты;
     2) создаём поток (TX_AUTO_START - реально побежит после tx_kernel_enter);
     3) load_image() - читаем таблицу образа из внешней flash. Без этого
        шага ap_img_ok остаётся 0, плеер считает образ отсутствующим и на
        каждом старте уходит в factory-запись 195 КБ (долго) либо навсегда
        остаётся на аварийном писке.
   Контекст: инициализация (стек MSP), планировщик ещё не запущен.          */
void audio_init(void)
{
  LOG_I(VLOG_M_AUDIO, "audio init");

  /* 1-2. RTOS-объекты и поток */
  (void)tx_semaphore_create(&ap_wake, "audio wake", 0);
  (void)tx_queue_create(&ap_queue, "audio cmd", TX_1_ULONG,
                        ap_queue_mem, sizeof ap_queue_mem);
  (void)tx_thread_create(&ap_thread, "Audio Player", ap_thread_entry, 0,
                         ap_stack, AP_STACK_SIZE,
                         AP_PRIORITY, AP_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);

  /* 3. читаем образ (именно этот вызов терялся - см. docs/FIX_REPORT.md) */
  load_image();
  audio_dbg_boot_stage = ap_img_ok ? AP_BOOT_IMG_OK : AP_BOOT_IMG_BAD;
}

/* Поставить звук в очередь. Контекст: ЛЮБОЙ (поток или ISR - только
   tx_queue_send(TX_NO_WAIT) и tx_semaphore_put, оба из прерывания разрешены).
   Звук заиграет ПОСЛЕ текущего; если ничего не играет - сразу.
   Возврат: AUDIO_OK или AUDIO_ERR_QUEUE_FULL (очередь на AUDIO_QUEUE_LEN).  */
audio_err_t audio_play(uint16_t idx)
{
  ULONG msg = MSG(CMD_PLAY, idx);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) != TX_SUCCESS)
  {
    return AUDIO_ERR_QUEUE_FULL;
  }
  (void)tx_semaphore_put(&ap_wake);
  return AUDIO_OK;
}

/* То же, что audio_play(), но по имени из таблицы образа (поле name[16]).
   Контекст: только поток (читает ap_tab без блокировки).
   Возврат: AUDIO_OK / AUDIO_ERR_NO_IMAGE / AUDIO_ERR_BAD_INDEX.            */
audio_err_t audio_play_name(const char *name)
{
  uint32_t i;
  if (!ap_img_ok)
  {
    return AUDIO_ERR_NO_IMAGE;
  }
  for (i = 0; i < ap_hdr.count; i++)
  {
    uint32_t j;
    int match = 1;
    for (j = 0; j < AUDIO_IMG_NAME_LEN; j++)
    {
      char c = ap_tab[i].name[j];
      char n = name[j];
      if (c == '\0' && n == '\0') { break; }
      if (c != n) { match = 0; break; }
    }
    if (match)
    {
      return audio_play((uint16_t)i);
    }
  }
  return AUDIO_ERR_BAD_INDEX;
}

/* Аварийный писк из const-массива во ВНУТРЕННЕЙ flash (не зависит от
   внешней). Контекст: любой. Прерывает текущий звук и чистит очередь.      */
void audio_beep(void)
{
  ULONG msg = MSG(CMD_BEEP, 0);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
  {
    (void)tx_semaphore_put(&ap_wake);
  }
}

/* Немедленный стоп + очистка очереди и отложенного звука. Контекст: любой. */
void audio_stop(void)
{
  ULONG msg = MSG(CMD_STOP, 0);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
  {
    (void)tx_semaphore_put(&ap_wake);
  }
}

/* Смена РЕЖИМА: текущий звук прерывается, играется звук нового состояния из
   ap_state_map[] (или тишина, если состоянию назначен AUDIO_STATE_SILENT).
   Контекст: любой. st < 3, иначе берётся тишина.                          */
void audio_set_state(uint8_t st)
{
  ULONG msg = MSG(CMD_STATE, st);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
  {
    (void)tx_semaphore_put(&ap_wake);
  }
}

/* Текущий режим (0..2). Контекст: любой, чтение одного байта. */
uint8_t audio_get_state(void) { return ap_state; }

/* Громкость 0..100 -> Q15 коэффициент. Контекст: любой.
   Применяется НЕ сразу, а при следующем чтении звука в ap_buf (см.
   apply_volume): уже играющая дорожка не меняется.                         */
void audio_set_volume(uint8_t percent)
{
  uint32_t p = (percent > 100u) ? 100u : percent;
  ap_vol_q15 = (int32_t)((p * 32767u) / 100u);
}

/* Обратное преобразование Q15 -> проценты (0..100). Контекст: любой. */
uint8_t audio_get_volume(void)
{
  return (uint8_t)(((uint32_t)ap_vol_q15 * 100u) / 32767u);
}

/* Перечитать таблицу образа из внешней flash (нужно после factory-записи или
   после обновления образа по OTA). Контекст: поток плеера.                 */
void audio_reload_image(void)
{
  load_image();
}

/* Справка по образу: валиден ли, сколько звуков. Контекст: любой. */
uint8_t  audio_image_ok(void) { return ap_img_ok; }
uint16_t audio_count(void)    { return ap_img_ok ? ap_hdr.count : 0u; }

/* Имя звука по индексу ("" если образа нет или индекс вне таблицы).
   Контекст: поток. Возвращает указатель ВНУТРЬ ap_tab - не освобождать.    */
const char *audio_name(uint16_t idx)
{
  if (!ap_img_ok || (idx >= ap_hdr.count))
  {
    return "";
  }
  return ap_tab[idx].name;
}

/* ================================================== колбэки HAL SAI (ISR) ==
 * КОНТЕКСТ: прерывание GPDMA1_Channel11 (завершение DMA звука). Здесь нельзя
 * ничего блокирующего - только флаги и tx_semaphore_put(), который будит
 * поток, и тот уже запускает следующий звук из очереди.                     */

/* DMA доиграла звук до конца: снимаем флаг "играет" и будим поток. */
void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    if (ap_playing_idx != -1)
    {
      audio_dbg_played++;
      ap_playing_idx    = -1;
      audio_dbg_cur_idx = -1;
      ap_play_deadline  = 0;
      (void)tx_semaphore_put(&ap_wake);
    }
  }
}

/* Ошибка SAI/DMA (OVRUDR и т.п.): тоже снимаем флаг "играет" и будим поток,
   иначе очередь встала бы навсегда. Код ошибки - в audio_dbg_last_err
   (0x1000 | hsai->ErrorCode).                                              */
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    audio_dbg_errors++;
    audio_dbg_last_err = 0x1000u | hsai->ErrorCode;
    if (ap_playing_idx != -1)
    {
      ap_playing_idx    = -1;
      audio_dbg_cur_idx = -1;
      ap_play_deadline  = 0;
      (void)tx_semaphore_put(&ap_wake);
    }
  }
}
