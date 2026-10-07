/**
  ******************************************************************************
  * @file    audio_player.c
  * @brief   Плеер звуков из внешней SPI flash (очередь, цикл, громкость)
  *
  *          Поток-владелец воспроизведения: только он трогает SAI/DMA.
  *          Команды приходят через tx_queue, пробуждение - через семафор,
  *          который взводят и команды, и ISR завершения DMA.
  *
  *          Тестовый вход - кнопки (audio_demo.c):
  *            BUTTON1 (PB1) - перебор всех звуков образа по кругу
  *                            (audio_play_now: прервать текущий и играть новый)
  *            BUTTON2 (PB2) - стоп
  *            BUTTON3 (PB3) - тест громкости
  *
  *          ПОРЯДОК СТАРТА (важно, см. также docs/SYSTEM.md):
  *            main()                    - периферия (CubeMX), sf_probe(), кнопки
  *            tx_application_define()   - ext_init() -> audio_init()
  *            audio_init()              - RTOS-объекты, ПОТОК, load_image()
  *            ap_thread_entry()         - вечный цикл команд
  *
  *          Прошивка НЕ пишет внешнюю flash никогда: образ sounds.bin кладёт
  *          туда программатор ST-LINK через external loader (см.
  *          Tools/ExtLoader_MX25R64 и docs/FLASHING.md). Если образа нет или
  *          он невалиден - играет аварийный писк из внутренней flash.
  *
  *          ДИАГНОСТИКА "ничего не происходит": всё состояние и все счётчики
  *          плеера - в ОДНОЙ структуре audio_status (boot_stage, cnt_commands,
  *          cnt_started/cnt_played/cnt_errors) - см. audio_player.h.
  ******************************************************************************
  */
#include "audio_player.h"
#include "vector_config.h"
#include "audio_ids.h"
#include "audio_image.h"
#include "spiflash.h"
#include "extstore.h"
#include "audio_beep.h"
#include "sai.h"
#include "main.h"
#include "tx_api.h"
#include "vector_log.h"   /* консольный лог: VECTOR_LOG_ENABLE в vector_config.h */
#include "vector_tick.h"   /* VTICK_MS() - время только от тика RTOS */
#include <string.h>

/* ------------------------------------------------------------------ RTOS -- */
#define AP_STACK_SIZE   4096u
#define AP_PRIORITY     10u

/* Сколько ждать события, если heartbeat включён (VECTOR_AUDIO_WAKE_TIMEOUT_MS).
   0 = честное TX_WAIT_FOREVER. Пауза цикла считает свой таймаут отдельно и
   просто уменьшает это значение (см. цикл потока). Перевод мс -> тики делает
   VTICK_MS2TICKS (vector_tick.h), всё время в проекте - от тика RTOS.        */
#if (VECTOR_AUDIO_WAKE_TIMEOUT_MS > 0u)
#define AP_WAKE_TMO     ((ULONG)VTICK_MS2TICKS(VECTOR_AUDIO_WAKE_TIMEOUT_MS))
#else
#define AP_WAKE_TMO     TX_WAIT_FOREVER
#endif

static TX_THREAD    ap_thread;
static uint8_t      ap_stack[AP_STACK_SIZE] __attribute__((aligned(8)));
static TX_QUEUE     ap_queue;
static ULONG        ap_queue_mem[AUDIO_QUEUE_LEN];
static TX_SEMAPHORE ap_wake;

/* Команды очереди - enum AUDIO_CMD_* (audio_player.h): в audio_status.
   last_command видно имя команды, а не цифру. Номера НЕ МЕНЯТЬ: они зашиты
   в сообщение очереди. 3 не используется - номер занимал CMD_STATE удалённого
   слоя "состояний".                                                        */
#define MSG(cmd, arg)   ((((ULONG)(cmd)) << 16) | ((ULONG)(arg) & 0xFFFFu))

/* ---------------------------------------------------------------- образ --- */
#define AP_MAX_SOUNDS   32u
static audio_img_header_t ap_hdr;
static audio_img_entry_t  ap_tab[AP_MAX_SOUNDS];
/* Валиден ли образ sounds.bin и сколько в нём звуков - поля audio_status
   (image_ok / image_count): отдельной переменной ap_img_ok больше нет.     */

/* ---------------------------------------------------------------- буфер ---
 * ap_buf - ОДИН на всё устройство. В режиме стриминга (рабочем) используются
 * только первые 2*AP_CHUNK сэмплов: DMA крутится по кругу, поток дозагружает
 * освободившуюся половину. Полный буфер AUDIO_BUF_SAMPLES (65535 сэмплов) -
 * это предел uint16_t Size у HAL_SAI_Transmit_DMA, он нужен только запасному
 * пути "одной транзакцией" (если в CubeMX сброшен Mode = Circular у SAI DMA):
 * 65535 сэмплов = 1.49 с при 44.1 кГц. VECTOR_AUDIO_STREAM_SMALLBUF 1
 * (теперь по умолчанию) урезает буфер до двух кусков: 16 КБ вместо 131 КБ.  */
#if VECTOR_AUDIO_STREAM
#define AP_CHUNK   ((uint32_t)VECTOR_AUDIO_STREAM_CHUNK)
#endif

/* Размер буфера в сэмплах (см. комментарий выше). */
#if VECTOR_AUDIO_STREAM && VECTOR_AUDIO_STREAM_SMALLBUF
#define AP_BUF_SAMPLES  (2u * VECTOR_AUDIO_STREAM_CHUNK)
#else
#define AP_BUF_SAMPLES  AUDIO_BUF_SAMPLES
#endif
static int16_t ap_buf[AP_BUF_SAMPLES];

/* --------------------------------------------------------------- громкость */
static volatile int32_t ap_vol_q15 = 32767;   /* 100% */

/* ---------------------------------------------------------- воспроизведение
 * ЧТО СЕЙЧАС ЗВУЧИТ хранится в audio_status (output + sound_index):
 * отдельной переменной ap_playing_idx больше нет, состояние не дублируется
 * и не может разъехаться. Пишут его ap_mark_sound()/ap_mark_beep()/
 * ap_mark_silent() - и поток плеера, и ISR завершения DMA.
 * ap_pending_idx: -1 = нет отложенного звука, >=0 = ждёт окончания текущего. */
static volatile int32_t ap_pending_idx = -1;

/* ----------------------------------------------------------------- ЦИКЛ ---
 * Режим "один звук по кругу": повторяется ПОСЛЕДНИЙ ЗАПУЩЕННЫЙ звук образа
 * с паузой. Включается audio_set_loop(1), снимается audio_set_loop(0) или
 * audio_stop(). Одноразовые audio_play() встраиваются в цикл: звучат вместо
 * повтора, после чего цикл продолжается.
 *
 * Всё состояние цикла - поля audio_status (отдельных переменных больше нет):
 *   loop_enabled      0/1 - режим включён (audio_set_loop)
 *   last_sound_index  индекс последнего успешно запущенного звука; -1 = не было
 *   loop_index        звук, который повторяется; -1 = цикла нет
 *   loop_pause_ms     пауза между повторами, мс (audio_set_loop_pause)
 *   loop_next_at_ms   момент (VTICK_MS) следующего повтора; 0 = ещё не задан,
 *                     то есть звук только что закончился и пауза не началась */

/* Дедлайн текущего звучания (VTICK_MS() + длительность + запас). Нужен
   только watchdog'у ap_check_stuck(): если колбэк завершения DMA так и не
   пришёл, поток не встаёт навсегда, а гасит "залипший" звук сам.            */
#define AP_STUCK_MARGIN_MS  500u
static volatile uint32_t ap_play_deadline = 0;

/* Флаг "SAI выдала блок до конца". Ставится из ISR, а ждёт его audio_selftest()
   - единственное место, где окончание передачи проверяется до планировщика
   (спать там негде, а в circular-режиме SAI сама в READY не возвращается).   */
static volatile uint8_t ap_sai_blk_done = 0;

#if VECTOR_AUDIO_STREAM
typedef struct
{
  volatile uint8_t  active;      /* стриминг идёт                            */
  uint8_t           use_flash;   /* 1 = PCM во внешней flash, 0 = const      */
  int32_t           idx;         /* индекс звука (-2 = писк)                 */
  const int16_t    *src;         /* const-источник (писк)                    */
  uint32_t          src_off;     /* адрес PCM во внешней flash               */
  uint32_t          total;       /* всего сэмплов в источнике                */
  uint32_t          loaded;      /* сколько сэмплов уже уложено в буфер      */
  volatile uint32_t played;      /* сколько сэмплов DMA выдала (пишет ISR)   */
  volatile uint8_t  req0;        /* ISR просит дозагрузить первую половину   */
  volatile uint8_t  req1;        /* ISR просит дозагрузить вторую половину   */
} ap_stream_t;

static ap_stream_t ap_st;
#endif


/* --------------------------------------------------- состояние и счётчики --
 * Всё состояние и все счётчики - в ОДНОЙ структуре audio_status (описание
 * полей и порядок диагностики - audio_player.h). Начальные значения задаются
 * здесь; дальше поля правят start_now()/stop_now()/ISR и поток плеера.       */
volatile audio_status_t audio_status =
{
  .output            = AUDIO_OUT_SILENT,
  .sound_index       = -1,
  .boot_stage        = AUDIO_BOOT_INIT,
  .image_ok          = 0u,
  .image_count       = 0u,
  .volume_percent    = 100u,
  .loop_enabled      = 0u,
  .loop_index        = -1,
  .last_sound_index  = -1,
  .loop_pause_ms     = (uint32_t)VECTOR_AUDIO_LOOP_PAUSE_MS,
  .loop_next_at_ms   = 0u,
  .last_error        = AUDIO_OK,
  .sai_error_code    = 0u,
  .last_command      = AUDIO_CMD_NONE
};

/* ------------------------------------------------------------ состояние ---
 * Единственный источник правды о том, ЧТО СЕЙЧАС ЗВУЧИТ, - audio_status
 * (output + sound_index). Отдельной переменной ap_playing_idx больше нет:
 * состояние не дублируется и не может разъехаться.
 *
 * Все три функции пишут ОБА поля в фиксированном порядке (см. комментарий к
 * audio_status_t в audio_player.h), чтобы читающий из другого контекста
 * (поток/ISR/отладчик) не увидел AUDIO_OUT_SOUND с чужим индексом.
 * КОНТЕКСТ: поток плеера и ISR завершения/ошибки DMA - как и раньше.         */
static void ap_mark_sound(int32_t idx)
{
  audio_status.sound_index = idx;
  audio_status.output      = AUDIO_OUT_SOUND;
}

static void ap_mark_beep(void)
{
  audio_status.sound_index = -1;
  audio_status.output      = AUDIO_OUT_ALARM_BEEP;
}

static void ap_mark_silent(void)
{
  audio_status.output      = AUDIO_OUT_SILENT;
  audio_status.sound_index = -1;
}

/* 1 = тишина (ничего не играем), 0 = звучит звук образа или аварийный писк. */
static int ap_is_silent(void)
{
  return (audio_status.output == AUDIO_OUT_SILENT) ? 1 : 0;
}

/* Сколько событий уже напечатано в лог: события приходят из ISR, а лог из ISR
   отбрасывается, поэтому поток показывает их, заметив изменение счётчика.    */
static uint32_t ap_seen_played   = 0;
static uint32_t ap_seen_watchdog = 0;



/* ============================================================ внутреннее == */
/* Пересчитать audio_status.loop_index. КОНТЕКСТ: поток плеера. Цикл повторяет
   ПОСЛЕДНИЙ ЗАПУЩЕННЫЙ звук образа (last_sound_index), поэтому audio_set_loop(1)
   можно вызвать и во время звучания (повтор начнётся после текущего звука) и в
   тишине (цикл взведётся, как только что-то сыграют). Вызывается из
   AUDIO_CMD_LOOP; при старте звука цикл взводится прямо в start_now().

   image_ok обязателен: без образа start_now() каждый раз уходил бы в аварийный
   писк, и цикл превратился бы в бесконечную пищалку.                        */
static void ap_loop_update(void)
{
  if ((audio_status.loop_enabled != 0u) && (audio_status.image_ok != 0u) &&
      (audio_status.last_sound_index >= 0))
  {
    audio_status.loop_index = audio_status.last_sound_index;
  }
  else
  {
    audio_status.loop_index = -1;
    audio_status.loop_next_at_ms  = 0;
  }
}

/* Watchdog звучания. КОНТЕКСТ: поток плеера, вызывается только на
   heartbeat-проходе (событий не было). Если звук "играет" дольше расчётного
   времени + запас, значит колбэк HAL_SAI_TxCpltCallback не пришёл (не включён
   GPDMA1_Channel11_IRQn, ошибка SAI, сбой DMA) - принудительно гасим передачу,
   иначе очередь встала бы навсегда. Рост audio_status.cnt_watchdog = искать проблему в
   цепочке SAI/GPDMA, а не в кнопках.                                        */
static void ap_check_stuck(void)
{
  uint32_t dl = ap_play_deadline;

  if ((ap_is_silent() == 0) && (dl != 0u))
  {
    /* сравнение с учётом переполнения VTICK_MS() (~49.7 суток) */
    if ((int32_t)(VTICK_MS() - dl) > 0)
    {
#if VECTOR_AUDIO_STREAM
      ap_st.active = 0;
#endif
      (void)HAL_SAI_Abort(&hsai_BlockA1);
      ap_mark_silent();
      ap_play_deadline  = 0;
      audio_status.cnt_watchdog++;
    }
  }
}

static void apply_volume(int16_t *p, uint32_t n);   /* объявление: стриминг
                                                       использует её раньше */

/* ------------------------------------------------------------- СТРИМИНГ ---
 * Один звук = поток кусков. Источник: PCM во внешней flash (ap_tab[idx].offset)
 * или const-массив во внутренней flash (аварийный писк).
 *
 * Разделение ответственности:
 *   ISR (HAL_SAI_TxHalfCplt / TxCplt) - только счётчик выданных сэмплов,
 *        флаг "половина освободилась" и tx_semaphore_put. Читать flash из ISR
 *        нельзя (там ожидание на мьютексе и семафоре).
 *   Поток (stream_service)            - дозагружает освободившуюся половину:
 *        ext_read куском, громкость, хвост добивает тишиной.
 *
 * Запас времени на дозагрузку = AP_CHUNK / AUDIO_SAMPLE_RATE (4096/44100 =
 * 93 мс), чтение 8 КБ из flash занимает ~3 мс, то есть запас ~30 раз.
 * Если поток всё же не успеет, DMA повторит предыдущий кусок (слышно как
 * заикание) - счётчик audio_status.cnt_underruns это покажет.               */

/* Признак того, что SAI-DMA сконфигурирована как circular linked-list.
   Это делает CubeMX (SAI1_A -> DMA -> GPDMA1 Channel11 -> Mode = Circular):
   на U5 circular для GPDMA реализуется связным списком, и только в режиме
   DMA_LINKEDLIST_CIRCULAR HAL НЕ гасит SAI по завершении блока - поэтому
   стыки кусков проходят без щелей и щелчков.                               */
static int stream_dma_circular(void)
{
#if VECTOR_AUDIO_STREAM
  DMA_HandleTypeDef *h = hsai_BlockA1.hdmatx;
  if (h != (DMA_HandleTypeDef *)0)
  {
    return (h->Mode == DMA_LINKEDLIST_CIRCULAR) ? 1 : 0;
  }
#endif
  return 0;
}

#if VECTOR_AUDIO_STREAM
/* Дозагрузить одну половину буфера (half = 0 или 1). КОНТЕКСТ: только поток.
   Если источник кончился - кладёт тишину, поэтому DMA никогда не читает
   мусор и конец звука не щёлкает.                                           */
static void stream_fill(uint8_t half)
{
  int16_t *dst = (half != 0u) ? &ap_buf[AP_CHUNK] : &ap_buf[0];
  uint32_t want = AP_CHUNK;
  uint32_t got  = 0;

  if (ap_st.loaded < ap_st.total)
  {
    uint32_t left = ap_st.total - ap_st.loaded;
    if (want > left)
    {
      want = left;
    }
    if (ap_st.use_flash != 0u)
    {
      if (ext_read(ap_st.src_off + (ap_st.loaded * 2u), (uint8_t *)dst, want * 2u) == 0)
      {
        got = want;
      }
      else
      {
        audio_status.cnt_errors++;
        audio_status.last_error = AUDIO_ERR_IO;
        LOG_E(VLOG_M_AUDIO, "stream: read fail at sample %u of %u",
              ap_st.loaded, ap_st.total);
      }
    }
    else
    {
      (void)memcpy(dst, ap_st.src + ap_st.loaded, want * 2u);
      got = want;
    }
    ap_st.loaded += got;
    if (got != 0u)
    {
      apply_volume(dst, got);
    }
  }

  /* хвост добиваем тишиной */
  while (got < AP_CHUNK)
  {
    dst[got++] = 0;
  }
}

/* Обслуживание стрима: дозагрузить половины, о которых попросил ISR, и
   заметить конец звука. КОНТЕКСТ: только поток плеера.                      */
static void stream_service(void)
{
  if (ap_st.active == 0u)
  {
    return;
  }

  if (ap_st.req0 != 0u)
  {
    ap_st.req0 = 0;
    stream_fill(0);
  }
  if (ap_st.req1 != 0u)
  {
    ap_st.req1 = 0;
    stream_fill(1);
  }

  /* Конец: DMA выдала не меньше total сэмплов (хвост - тишина). Точность
     окончания = половина буфера (AP_CHUNK/AUDIO_SAMPLE_RATE), дальше звучит
     тишина, поэтому щелчка нет.                                            */
  if (ap_st.played >= ap_st.total)
  {
    ap_st.active = 0;
    (void)HAL_SAI_Abort(&hsai_BlockA1);
    audio_status.cnt_played++;
    ap_mark_silent();
    ap_play_deadline  = 0;
    LOG_D(VLOG_M_AUDIO, "stream done: %u samples (%u underrun)",
          ap_st.total, audio_status.cnt_underruns);
  }
}

/* Запустить звук кусками. Возврат: AUDIO_OK, либо ошибка (тогда вызывающий
   пробует путь одной транзакцией). КОНТЕКСТ: только поток плеера.           */
static audio_err_t stream_start_src(int32_t idx, uint8_t use_flash,
                                    const int16_t *src, uint32_t src_off,
                                    uint32_t total)
{
  HAL_StatusTypeDef hs;

  if (!stream_dma_circular())
  {
    return AUDIO_ERR_IO;     /* нет circular в кубе -> пусть работает one-shot */
  }
  if ((total == 0u) || ((2u * AP_CHUNK) > 65535u))
  {
    return AUDIO_ERR_BAD_INDEX;
  }

  ap_st.idx       = idx;
  ap_st.use_flash = use_flash;
  ap_st.src       = src;
  ap_st.src_off   = src_off;
  ap_st.total     = total;
  ap_st.loaded    = 0;
  ap_st.played    = 0;
  ap_st.req0      = 0;
  ap_st.req1      = 0;

  /* первые две половины грузим ДО старта DMA, чтобы в эфир не ушёл мусор */
  stream_fill(0);
  stream_fill(1);

  hs = HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)ap_buf,
                            (uint16_t)(2u * AP_CHUNK));
  if (hs != HAL_OK)
  {
    audio_status.cnt_errors++;
    audio_status.last_error = AUDIO_ERR_IO;
    LOG_E(VLOG_M_AUDIO, "stream: SAI DMA start fail hal=%d (1=err 2=busy)", (int32_t)hs);
    return AUDIO_ERR_IO;
  }

  ap_st.active       = 1;
  ap_mark_sound(idx);
  audio_status.cnt_started++;
  audio_status.cnt_underruns = 0;
  ap_play_deadline  = VTICK_MS() + ((total * 1000u) / AUDIO_SAMPLE_RATE)
                      + AP_STUCK_MARGIN_MS;
  return AUDIO_OK;
}
#endif /* VECTOR_AUDIO_STREAM */

/* Применяет программную громкость (Q15) к буферу PCM in-place.
   Контекст: поток плеера, после чтения из flash и ДО старта DMA.
   При 100% (g >= 32767) выходит сразу - копирования не делает.              */
static void apply_volume(int16_t *p, uint32_t n)
{
  int32_t g = ap_vol_q15;
  uint32_t i;

  /* Один раз на изменение громкости. Контекст - поток плеера (stream_fill и
     одноразовый путь), поэтому лог печатается: из ISR vlog вызовы
     отбрасывает. Строка подтверждает, что audio_set_volume() дошёл и
     коэффициент применён к данным, которые реально уходят в SAI-DMA.     */
  {
    static int32_t logged = -1;
    if (g != logged)
    {
      logged = g;
      LOG_I(VLOG_M_AUDIO, "volume %u%% (q15=%d)",
            (uint32_t)(((uint32_t)g * 100u) / 32767u), (int32_t)g);
    }
  }

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
#if VECTOR_AUDIO_STREAM
  if (stream_start_src(-2, 0, audio_beep_pcm, 0, audio_beep_samples) == AUDIO_OK)
  {
    LOG_I(VLOG_M_AUDIO, "beep started (stream), %u samples", audio_beep_samples);
    return AUDIO_OK;
  }
#endif
  {
    HAL_StatusTypeDef hs = HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audio_beep_pcm,
                                                (uint16_t)audio_beep_samples);
    if (hs != HAL_OK)
    {
      audio_status.cnt_errors++;
      audio_status.last_error = AUDIO_ERR_IO;
      LOG_E(VLOG_M_AUDIO, "beep DMA fail: hal=%d (1=err 2=busy) sai_err=%x SD_MODE?",
            (int32_t)hs, (uint32_t)hsai_BlockA1.ErrorCode);
      return AUDIO_ERR_IO;
    }
    LOG_I(VLOG_M_AUDIO, "beep started, %u samples", audio_beep_samples);
    ap_mark_beep();              /* маркер: играет аварийный писк */
    audio_status.cnt_started++;
    ap_play_deadline  = VTICK_MS() +
                        ((audio_beep_samples * 1000u) / AUDIO_SAMPLE_RATE) + AP_STUCK_MARGIN_MS;
    return AUDIO_OK;
  }
}

/* Главный "запуск звука": таблица -> чтение PCM из внешней flash в ap_buf ->
   громкость -> одна DMA-транзакция в SAI. Контекст: только поток плеера.
   idx - индекс звука в образе (не идентификатор из audio_ids.h напрямую,
   а его значение - они совпадают по построению образа).
   Возврат: AUDIO_OK, иначе код ошибки (audio_status.last_error
   и счётчик audio_status.cnt_errors).
   НЕ блокирует: окончание звучания придёт из HAL_SAI_TxCpltCallback.        */
static audio_err_t start_now(uint16_t idx)
{
  const audio_img_entry_t *e;
  audio_err_t rc = AUDIO_OK;

  if (!audio_status.image_ok)
  {
    /* Внешняя flash мертва или образ битый: аварийный писк из внутренней
       flash, чтобы устройство не молчало совсем. */
    audio_status.last_error = AUDIO_ERR_NO_IMAGE;
    LOG_W(VLOG_M_AUDIO, "no valid image -> beep instead of sound #%u", (uint32_t)idx);
    return beep_now();
  }
  else if (idx >= ap_hdr.count)      { rc = AUDIO_ERR_BAD_INDEX; }
  else
  {
    e = &ap_tab[idx];
#if VECTOR_AUDIO_STREAM
    if (stream_dma_circular())
    {
      /* Стриминг: длина звука НЕ ограничена ни буфером, ни uint16_t Size.
         Если не получилось (DMA занята и т.п.) - падаем в путь одной
         транзакцией ниже.                                                    */
      rc = stream_start_src((int32_t)idx, 1u, (const int16_t *)0,
                            AUDIO_IMG_BASE_ADDR + e->offset, e->length / 2u);
      if (rc == AUDIO_OK)
      {
        LOG_I(VLOG_M_AUDIO, "play #%u '%s' stream %u samples @%u Hz (chunk %u)",
              (uint32_t)idx, (const char *)e->name, (e->length / 2u),
              (uint32_t)e->rate, (uint32_t)AP_CHUNK);
        if ((e->rate != 0u) && (e->rate != AUDIO_SAMPLE_RATE))
        {
          LOG_W(VLOG_M_AUDIO, "RATE MISMATCH: sound %u Hz, AUDIO_SAMPLE_RATE %u Hz",
                (uint32_t)e->rate, (uint32_t)AUDIO_SAMPLE_RATE);
        }
        return AUDIO_OK;
      }
      rc = AUDIO_OK;      /* пробуем one-shot, ошибку стрима уже залогировали */
    }
    else
    {
      static uint8_t hinted = 0;
      if (!hinted)
      {
        hinted = 1;
        LOG_W(VLOG_M_AUDIO,
              "SAI DMA is not circular -> one-shot mode, max %.2f s per sound. "
              "Set in CubeMX: SAI1_A -> DMA -> GPDMA1 Channel11 -> Mode = Circular",
              (uint32_t)AP_BUF_SAMPLES / (AUDIO_SAMPLE_RATE / 100u) / 10u);
      }
    }
#endif
    if ((e->length / 2u) > AP_BUF_SAMPLES)
    {
      rc = AUDIO_ERR_TOO_LONG;
      LOG_E(VLOG_M_AUDIO, "sound #%u too long: %u samples > %u (limit %.2f s @%u Hz)",
            (uint32_t)idx, (e->length / 2u), (uint32_t)AP_BUF_SAMPLES,
            (uint32_t)AP_BUF_SAMPLES / (AUDIO_SAMPLE_RATE / 100u) / 10u,
            (uint32_t)AUDIO_SAMPLE_RATE);
    }
    else if (ext_read(AUDIO_IMG_BASE_ADDR + e->offset, (uint8_t *)ap_buf, e->length) != 0)
    {
      rc = AUDIO_ERR_IO;
    }
    else
    {
      apply_volume(ap_buf, e->length / 2u);
      {
        HAL_StatusTypeDef hs = HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)ap_buf,
                                                    (uint16_t)(e->length / 2u));
        if (hs != HAL_OK)
        {
          rc = AUDIO_ERR_IO;
          LOG_E(VLOG_M_AUDIO, "SAI DMA fail: hal=%d (1=err 2=busy) sai_err=%x",
                (int32_t)hs, (uint32_t)hsai_BlockA1.ErrorCode);
        }
        else
        {
          LOG_I(VLOG_M_AUDIO, "play #%u '%s' %u samples @%u Hz",
              (uint32_t)idx, (const char *)e->name, (e->length / 2u), (uint32_t)e->rate);
          uint32_t rate = (e->rate != 0u) ? e->rate : AUDIO_SAMPLE_RATE;
          ap_mark_sound((int32_t)idx);
          audio_status.cnt_started++;
          /* сколько звук должен играть + запас: порог для ap_check_stuck() */
          ap_play_deadline   = VTICK_MS() +
                               (((e->length / 2u) * 1000u) / rate) + AP_STUCK_MARGIN_MS;
        }
      }
    }
  }

  if (rc == AUDIO_OK)
  {
    /* Звук пошёл: запоминаем его для цикла и сразу взводим повтор, если режим
       включён. Поэтому audio_set_loop(1) работает и до старта звука, и во
       время него, а одноразовые audio_play()/audio_play_now() встраиваются
       в цикл: звучат вместо повтора, после чего повтор продолжается.      */
    audio_status.last_sound_index = (int32_t)idx;
    if ((audio_status.loop_enabled != 0u) && (audio_status.image_ok != 0u))
    {
      audio_status.loop_index = (int32_t)idx;
    }
  }
  else
  {
    audio_status.cnt_errors++;
    audio_status.last_error = rc;
    LOG_E(VLOG_M_AUDIO, "start #%u failed, err=%d (1=no image 2=bad idx 3=too long 4=io)",
          (uint32_t)idx, (int32_t)rc);
  }
  return rc;
}

/* Немедленный стоп: гасим DMA/SAI и помечаем "не играет".
   Контекст: только поток плеера. Очередь команд НЕ чистит - это делает
   вызывающий (queue_clear), чтобы stop_now можно было звать и просто так.   */
static void stop_now(void)
{
#if VECTOR_AUDIO_STREAM
  ap_st.active = 0;      /* раньше Abort, чтобы ISR не попросил дозагрузку */
  ap_st.req0   = 0;
  ap_st.req1   = 0;
#endif
  if (ap_is_silent() == 0)
  {
    int32_t was = audio_status.sound_index;
    (void)HAL_SAI_Abort(&hsai_BlockA1);
    ap_mark_silent();
    LOG_I(VLOG_M_AUDIO, "stop (aborted idx=%d)", was);
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
 * Команды кладут audio_play()/audio_play_now()/audio_stop()/audio_beep(),
 * а пробуждение даёт либо они же, либо ISR завершения DMA (см. конец файла).
 *
 * Внешнюю flash поток НЕ программирует никогда: образ sounds.bin записывает
 * программатор через external loader (Tools/ExtLoader_MX25R64,
 * docs/FLASHING.md). Если audio_init() образа не нашёл - на любую команду
 * звучит аварийный писк из внутренней flash.                                  */
static void ap_thread_entry(ULONG arg)
{
  (void)arg;

  if (!audio_status.image_ok)
  {
    /* Образа во внешней flash нет (или он битый/с другой частотой): причина
       уже напечатана load_image(). Дальше - только аварийный писк. Лечится
       записью sounds.bin программатором: docs/FLASHING.md.                   */
    LOG_E(VLOG_M_AUDIO,
          "no valid image -> beep only; program sounds.bin via ST-LINK "
          "(docs/FLASHING.md)");
  }

  if (audio_status.image_ok)
  {
    audio_status.boot_stage = AUDIO_BOOT_RUNNING;   /* образ есть - рабочий режим */
  }
  /* иначе оставляем AUDIO_BOOT_IMAGE_BAD: по boot_stage сразу видно, что плеер
     работает без образа (только аварийный писк) */

#if (VECTOR_SPI_SELFTEST > 0u)
  /* Диагностика линии SPI ДО начала звучания: каждый блок 8 КБ читается
     дважды и сравнивается побайтно. sf_dbg_selftest_bad != 0 означает, что
     данные при чтении портятся (наводки/провода/SCK 20 МГц на пределе) -
     это одна из причин "хрипа" при заведомо чистом образе. Контекст здесь -
     поток, поэтому чтение идёт тем же DMA-путём, что и подкачка звука.
     Подробности: VECTOR_SPI_SELFTEST в vector_config.h, sf_selftest() в
     spiflash.c. На боевой прошивке выставить VECTOR_SPI_SELFTEST 0.          */
  if (sf_selftest(AUDIO_IMG_BASE_ADDR, 8192u, VECTOR_SPI_SELFTEST) != HAL_OK)
  {
    LOG_E(VLOG_M_AUDIO, "SPI selftest: two reads DIFFER -> noisy SPI line (see sf_dbg_selftest_bad)");
  }
#endif

  /* Стартовая проверка тракта (VECTOR_AUDIO_BOOT_PLAY): до первого нажатия
     кнопки слышно, жив ли звук вообще. В боевом режиме ставится 0.          */
#if (VECTOR_AUDIO_BOOT_PLAY == 1)
  {
    /* звук #0 ПО КРУГУ: взводим цикл и стартуем; last_sound_index и
       loop_index выставит сам start_now(), дальше поток повторяет звук
       с паузой.                                                           */
    audio_status.loop_enabled = 1u;
    audio_status.loop_next_at_ms = 0;
    LOG_I(VLOG_M_AUDIO, "boot: sound #0 in loop, pause=%u ms",
          audio_status.loop_pause_ms);
    (void)start_now(0u);
  }
#elif (VECTOR_AUDIO_BOOT_PLAY == 2)
  LOG_I(VLOG_M_AUDIO, "boot play: sound #0");
  (void)start_now(0u);
#elif (VECTOR_AUDIO_BOOT_PLAY == 3)
  LOG_I(VLOG_M_AUDIO, "boot beep (SAI/amp check, no ext flash)");
  (void)beep_now();
#endif

  for (;;)
  {
    ULONG msg = 0;

    /* Сколько спать. База - heartbeat (или TX_WAIT_FOREVER), но если идёт
       пауза цикла, будильник ставим ровно на её конец. Любая команда
       (tx_semaphore_put) прерывает ожидание СРАЗУ, поэтому стоп во время
       паузы отрабатывает мгновенно, а не через всю паузу.                    */
    ULONG tmo = AP_WAKE_TMO;
    if ((audio_status.loop_index != -1) &&
        (audio_status.loop_next_at_ms != 0u) && (ap_is_silent() != 0))
    {
      int32_t left = (int32_t)(audio_status.loop_next_at_ms - VTICK_MS());
      if (left <= 0)
      {
        tmo = 1u;                          /* пора повторять */
      }
      else
      {
        ULONG t = VTICK_MS2TICKS((uint32_t)left) + 1u;
        if (t < tmo) { tmo = t; }
      }
    }

    audio_status.cnt_wakes++;
    if (tx_semaphore_get(&ap_wake, tmo) != TX_SUCCESS)
    {
      audio_status.cnt_wake_timeouts++;      /* событий не было - холостой проход */
      ap_check_stuck();          /* страховка от потерянного колбэка DMA */
    }

    /* Окончание звука приходит из ISR, а лог из ISR не печатается - поэтому
       показываем событие здесь, заметив изменение счётчика.                  */
    if (audio_status.cnt_played != ap_seen_played)
    {
      ap_seen_played = audio_status.cnt_played;
      LOG_D(VLOG_M_AUDIO, "sound finished (total %u)", ap_seen_played);
    }
    if (audio_status.cnt_watchdog != ap_seen_watchdog)
    {
      ap_seen_watchdog = audio_status.cnt_watchdog;
      LOG_W(VLOG_M_AUDIO, "watchdog: stuck sound killed %u time(s), no SAI DMA callback",
            ap_seen_watchdog);
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

      audio_status.cnt_commands++;
      audio_status.last_command = (audio_cmd_t)cmd;
      LOG_D(VLOG_M_AUDIO, "cmd=%u arg=%u out=%u idx=%d", cmd, a,
            (uint32_t)audio_status.output, (int32_t)audio_status.sound_index);

      if (cmd == AUDIO_CMD_STOP)
      {
        /* ПОЛНЫЙ стоп: гасим звук, чистим очередь, отложенный звук И текущий
           повтор. loop_enabled и last_sound_index сохраняются: следующий
           запущенный звук снова пойдёт по кругу, если режим цикла включён. */
        stop_now();
        queue_clear();
        ap_pending_idx = -1;
        audio_status.loop_index = -1;
        audio_status.loop_next_at_ms  = 0;
        LOG_I(VLOG_M_AUDIO, "stop: queue cleared, loop off (loop=%u)",
              (uint32_t)audio_status.loop_enabled);
      }
      else if (cmd == AUDIO_CMD_PLAY_NOW)
      {
        /* Прервать текущий и СРАЗУ запустить новый - одна команда вместо пары
           audio_stop() + audio_play(). Отложенный звук сбрасываем, иначе он
           сыграл бы следом за новым. Цикл НЕ снимаем: loop_enabled остаётся,
           и start_now() взведёт повтор уже для нового звука.               */
        stop_now();
        queue_clear();
        ap_pending_idx = -1;
        audio_status.loop_next_at_ms = 0;
        (void)start_now((uint16_t)a);
      }
      else if (cmd == AUDIO_CMD_BEEP)
      {
        stop_now();
        queue_clear();
        (void)beep_now();
      }
      else if (cmd == AUDIO_CMD_PLAY)
      {
        if (ap_is_silent() != 0)
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
            audio_status.cnt_dropped++;
          }
          ap_pending_idx = (int32_t)a;
        }
      }
      else if (cmd == AUDIO_CMD_LOOP)
      {
        /* audio_set_loop() изменил loop_enabled: пересчитываем loop_index.
           Текущий звук не прерываем - цикл подхватится после него.          */
        ap_loop_update();
        LOG_I(VLOG_M_AUDIO, "loop=%u (idx=%d pause=%u ms)",
              (uint32_t)audio_status.loop_enabled,
              (int32_t)audio_status.loop_index, audio_status.loop_pause_ms);
      }
      else
      {
        /* неизвестная команда - игнорируем */
      }
    }

#if VECTOR_AUDIO_STREAM
    /* 1.5 Обслужить стриминг: дозагрузить половины, о которых попросил ISR,
           и заметить конец звука. Делается ДО разбора "что играть дальше",
           чтобы окончание текущего звука успело сняться в этом же проходе.   */
    stream_service();
#endif

    /* 2. Освободились - решаем, что играть дальше.
          приоритет: одноразовый audio_play() -> повтор цикла -> тишина.      */
    if (ap_is_silent() != 0)
    {
      if (ap_pending_idx != -1)
      {
        int32_t nxt = ap_pending_idx;
        ap_pending_idx = -1;
        (void)start_now((uint16_t)nxt);      /* цикл продолжится после него */
      }
      else if (audio_status.loop_index != -1)
      {
        if (audio_status.loop_next_at_ms == 0u)
        {
          /* звук только что закончился - стартуем отсчёт паузы. На следующем
             проходе (по будильнику) начнём повтор. */
          audio_status.loop_next_at_ms = VTICK_MS() + audio_status.loop_pause_ms;
          if (audio_status.loop_next_at_ms == 0u)
          {
            audio_status.loop_next_at_ms = 1u;   /* 0 = "не задано" */
          }
        }
        else if ((int32_t)(VTICK_MS() - audio_status.loop_next_at_ms) >= 0)
        {
          audio_status.loop_next_at_ms = 0;
          if (start_now((uint16_t)audio_status.loop_index) == AUDIO_OK)
          {
            audio_status.cnt_loops++;
          }
        }
        else
        {
          /* пауза ещё идёт: вернёмся в ожидание, таймаут посчитаем сверху */
        }
      }
      else
      {
        /* ничего не назначено - тишина */
      }
    }
  }
}

/* Сколько раз повторять сбойное чтение образа (VECTOR_SF_READ_RETRY).
   Под отладчиком один SPI-обмен изредка возвращается по таймауту HAL
   (st=3, 0 байт) - это сбой ЧТЕНИЯ, а не порча flash, поэтому повторяем. */
#if (VECTOR_SF_READ_RETRY < 1)
#define SF_READ_TRIES  1u
#elif (VECTOR_SF_READ_RETRY > 16)
#define SF_READ_TRIES  16u
#else
#define SF_READ_TRIES  ((uint32_t)VECTOR_SF_READ_RETRY)
#endif
#define SF_READ_RETRY_DELAY_MS  2u   /* дать halt'у отладчика пройти */

/* Чтение блока образа с повторами. КОНТЕКСТ: audio_init() (до планировщика -
   просто повтор без сна) и audio_reload_image() (поток - сон между попытками
   допустим и полезен).                                                        */
static HAL_StatusTypeDef img_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
  HAL_StatusTypeDef st = HAL_ERROR;
  uint32_t t;

  for (t = 0; t < SF_READ_TRIES; t++)
  {
    st = sf_read(addr, dst, len);
    if (st == HAL_OK)
    {
      break;
    }
    if (VTICK_IN_THREAD())
    {
      VTICK_SLEEP_MS(SF_READ_RETRY_DELAY_MS);
    }
  }
  return st;
}

/* Читает заголовок и таблицу образа sounds.bin из внешней flash в RAM
   (ap_hdr + ap_tab[]) и проверяет magic/version/CRC32 таблицы.
   Контекст: audio_init() (ДО планировщика) и audio_reload_image() (поток).
   Мьютекс не берёт намеренно: оба вызова происходят, когда шину больше никто
   не трогает; обращение идёт напрямую через sf_read().
   Результат: audio_status.image_ok = 1 только если образ валиден, иначе 0 (и плеер
   переходит на аварийный писк). Скратч-буфер static, а не в стеке: вызов из
   audio_init() идёт на стеке MSP до старта планировщика, 1.2 КБ кадра там
   не нужны.                                                                 */
static void load_image(void)
{
  static uint8_t raw[AUDIO_IMG_HEADER_SIZE + AP_MAX_SOUNDS * AUDIO_IMG_ENTRY_SIZE];
  uint32_t tbl_bytes;

  audio_status.image_ok = 0;
  audio_status.image_count = 0u;

  /* КРИТИЧНО: заголовок читаем СРАЗУ в ap_hdr, а не в raw: иначе второе
     чтение (таблица) перезатрёт его, CRC не сойдётся никогда.               */
  if (img_read(AUDIO_IMG_BASE_ADDR, (uint8_t *)&ap_hdr, sizeof ap_hdr) == HAL_OK)
  {
    if ((ap_hdr.magic == AUDIO_IMG_MAGIC) && (ap_hdr.version == AUDIO_IMG_VERSION) &&
        (ap_hdr.count > 0u) && (ap_hdr.count <= AP_MAX_SOUNDS))
    {
      tbl_bytes = (uint32_t)ap_hdr.count * AUDIO_IMG_ENTRY_SIZE;
      if (img_read(AUDIO_IMG_BASE_ADDR + AUDIO_IMG_HEADER_SIZE, raw, tbl_bytes) == HAL_OK)
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

        if (crc == ap_hdr.table_crc32)
        {
          for (i = 0; i < ap_hdr.count; i++)
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
          /* Частота образа обязана совпадать с настройкой SAI, иначе звук
             пойдёт с другой скоростью и тоном (при 44.1 кГц в SAI образ
             16 кГц даст ускорение почти втрое). Старый образ при этом
             ВАЛИДЕН по CRC, поэтому без этой проверки он молча использовался
             бы дальше. При несовпадении audio_status.image_ok остаётся 0: пересоберите
             образ с --rate 44100 и перезапишите flash программатором
             (docs/FLASHING.md).                                             */
          {
            uint32_t bad = 0;
            for (i = 0; i < ap_hdr.count; i++)
            {
              if ((ap_tab[i].rate != 0u) && (ap_tab[i].rate != AUDIO_SAMPLE_RATE))
              {
                bad++;
              }
            }
            if (bad != 0u)
            {
              LOG_E(VLOG_M_AUDIO,
                    "image rate mismatch: %u of %u sounds are not %u Hz -> "
                    "rebuild image and reprogram flash (docs/FLASHING.md)",
                    bad, (uint32_t)ap_hdr.count, (uint32_t)AUDIO_SAMPLE_RATE);
            }
            else
            {
              audio_status.image_ok = 1;   /* таблица разобрана ЦЕЛИКОМ - образ годен */
              audio_status.image_count = ap_hdr.count;
            }
          }
        }
        else
        {
          LOG_E(VLOG_M_AUDIO, "image CRC mismatch: got %x want %x (table %u b)",
                crc, ap_hdr.table_crc32, tbl_bytes);
        }
      }
      else
      {
        LOG_E(VLOG_M_AUDIO, "image: table read fail (%u b)", tbl_bytes);
      }
    }
    else
    {
      LOG_E(VLOG_M_AUDIO,
            "image header bad: magic=%x want %x, ver=%u want %u, count=%u max %u",
            ap_hdr.magic, (uint32_t)AUDIO_IMG_MAGIC, (uint32_t)ap_hdr.version,
            (uint32_t)AUDIO_IMG_VERSION, (uint32_t)ap_hdr.count, (uint32_t)AP_MAX_SOUNDS);
    }
  }
  else
  {
    LOG_E(VLOG_M_AUDIO, "image: header read fail (sf_probe_rc=%d)", (int32_t)sf_probe_rc);
  }

  LOG_I(VLOG_M_AUDIO, "image ok=%u sounds=%u", (uint32_t)audio_status.image_ok,
        (uint32_t)audio_status.image_count);
}

/* =============================================================== публичное */
/* ПРЯМАЯ проверка звукового тракта: БЕЗ очереди, БЕЗ потока плеера, БЕЗ
   состояний и БЕЗ внешней flash. Const-массив писка из внутренней flash ->
   HAL_SAI_Transmit_DMA -> усилитель.
   КОНТЕКСТ: любой, включая main() до старта планировщика (поэтому completion
   ждём активным опросом состояния SAI с ограничением по числу проходов, а не
   по тику: до планировщика тика RTOS нет вовсе, см. vector_tick.h).
   Возврат: 0 = DMA стартовала и завершилась; 1 = не стартовала (код HAL в
   логе); 2 = стартовала, но не завершилась (нет прерывания GPDMA1_Channel11).
   Писк длится 240 мс; guard показывает, сколько проходов заняло ожидание
   завершения DMA (время в проекте - только тик RTOS, см. vector_tick.h).   */
int audio_selftest(void)
{
  HAL_StatusTypeDef hs;
  uint32_t t0 = VTICK_MS();          /* до планировщика = 0 */
  uint32_t guard = 0;

  ap_sai_blk_done = 0;

  hs = HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audio_beep_pcm,
                            (uint16_t)audio_beep_samples);
  if (hs != HAL_OK)
  {
    audio_status.cnt_errors++;
    audio_status.last_error = AUDIO_ERR_IO;
    LOG_E(VLOG_M_AUDIO, "selftest: SAI DMA start FAIL hal=%d (1=err 2=busy)",
          (int32_t)hs);
    return 1;
  }
  LOG_I(VLOG_M_AUDIO, "selftest: beep %u samples via SAI DMA (no queue, no ext flash)",
        audio_beep_samples);

  /* Ждём флаг из ISR завершения блока. В circular-режиме SAI не возвращается
     в READY сама (HAL её не гасит), поэтому проверять состояние бесполезно -
     нужен именно флаг. Ограничение по числу проходов (~1 с на 160 МГц), чтобы
     не зависеть от исправности системного тика. Сразу после флага гасим
     передачу: иначе circular DMA прокрутит писк по второму кругу.            */
  while ((ap_sai_blk_done == 0u) && (guard < 40000000u))
  {
    guard++;
  }
  (void)HAL_SAI_Abort(&hsai_BlockA1);

  if (ap_sai_blk_done == 0u)
  {
    audio_status.cnt_errors++;
    LOG_E(VLOG_M_AUDIO, "selftest: DMA started but NOT finished (GPDMA1_Channel11_IRQn?)");
    return 2;
  }

  audio_status.cnt_played++;
  /* guard - число проходов ожидания: по нему видно, насколько быстро пришла
     DMA (selftest выполняется до планировщика, где тика RTOS ещё нет, поэтому
     длительность оцениваем только так, время в проекте - см. vector_tick.h). */
  LOG_I(VLOG_M_AUDIO, "selftest: done, guard=%u (%u samples, expected ~%u ms)",
        guard, audio_beep_samples,
        (audio_beep_samples * 1000u) / AUDIO_SAMPLE_RATE);
  (void)t0;
  return 0;
}
/* Инициализация плеера. Вызывать ОДИН раз из tx_application_define(), ПОСЛЕ
   ext_init() (нужен мьютекс шины flash и отсканированный журнал).
   Порядок внутри важен:
     1) создаём семафор/очередь - до потока, иначе поток дёрнет несуществующие
        объекты;
     2) создаём поток (TX_AUTO_START - реально побежит после tx_kernel_enter);
     3) load_image() - читаем таблицу образа из внешней flash. Без этого
        шага audio_status.image_ok остаётся 0 и плеер навсегда остаётся на аварийном
        писке, хотя образ во flash может быть валидным.
   Контекст: инициализация (стек MSP), планировщик ещё не запущен.          */
void audio_init(void)
{
  LOG_I(VLOG_M_AUDIO, "audio init");

  /* 0. Контроль тактирования SAI1: реальная частота кадров обязана совпадать
     с AUDIO_SAMPLE_RATE. FS = SAI_CK / (256 * MCKDIV), где SAI_CK - ядро SAI1
     (у нас PLL3P), а MCKDIV HAL считает сам из Init.AudioFrequency. SAI_CK
     должен быть кратен 256 * 44100 = 11.2896 МГц, иначе частота уезжает на
     проценты: при прежнем SAI_CK = 44.1 МГц HAL брал MCKDIV = 4 и выдавал
     43066 Гц (-2.34%: звук ниже на 41 цент и длиннее на 2.4%).
     Расчёт и таблица PLL3 - docs/AUDIO.md, раздел 2 «Тактирование SAI1».   */
  {
    uint32_t sai_ck = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI1);
    uint32_t mckdiv = (hsai_BlockA1.Init.Mckdiv != 0u) ? hsai_BlockA1.Init.Mckdiv : 1u;
    uint32_t fs_mhz = (uint32_t)(((uint64_t)sai_ck * 1000u) / (256u * mckdiv));
    uint32_t need   = AUDIO_SAMPLE_RATE * 1000u;      /* то же в миллигерцах  */
    uint32_t tol    = need / 1000u;                   /* допуск 0.1% (~17 центов) */

    LOG_I(VLOG_M_AUDIO, "sai ck=%u Hz mckdiv=%u fs=%u mHz (need %u mHz)",
          sai_ck, mckdiv, fs_mhz, need);
    if ((fs_mhz + tol < need) || (fs_mhz > need + tol))
    {
      LOG_W(VLOG_M_AUDIO, "SAI fs off >0.1%% -> check PLL3 (docs/AUDIO.md)");
    }
    (void)sai_ck; (void)mckdiv; (void)fs_mhz; (void)need; (void)tol;
  }

  /* 1-2. RTOS-объекты и поток */
  (void)tx_semaphore_create(&ap_wake, "audio wake", 0);
  (void)tx_queue_create(&ap_queue, "audio cmd", TX_1_ULONG,
                        ap_queue_mem, sizeof ap_queue_mem);
  (void)tx_thread_create(&ap_thread, "Audio Player", ap_thread_entry, 0,
                         ap_stack, AP_STACK_SIZE,
                         AP_PRIORITY, AP_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);

  /* 3. читаем образ (именно этот вызов терялся - см. docs/archive/FIX_REPORT.md) */
  load_image();
  audio_status.boot_stage = audio_status.image_ok ? AUDIO_BOOT_IMAGE_OK
                                                  : AUDIO_BOOT_IMAGE_BAD;
}

/* Поставить звук в очередь. Контекст: ЛЮБОЙ (поток или ISR - только
   tx_queue_send(TX_NO_WAIT) и tx_semaphore_put, оба из прерывания разрешены).
   Звук заиграет ПОСЛЕ текущего; если ничего не играет - сразу.
   Возврат: AUDIO_OK или AUDIO_ERR_QUEUE_FULL (очередь на AUDIO_QUEUE_LEN).  */
audio_err_t audio_play(uint16_t idx)
{
  ULONG msg = MSG(AUDIO_CMD_PLAY, idx);
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
  if (!audio_status.image_ok)
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
  ULONG msg = MSG(AUDIO_CMD_BEEP, 0);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
  {
    (void)tx_semaphore_put(&ap_wake);
  }
}

/* Немедленный стоп + очистка очереди и отложенного звука. Контекст: любой. */
void audio_stop(void)
{
  ULONG msg = MSG(AUDIO_CMD_STOP, 0);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
  {
    (void)tx_semaphore_put(&ap_wake);
  }
}

/* Прервать текущий звук и СРАЗУ запустить idx - одна команда очереди вместо
   пары audio_stop() + audio_play(). Контекст: любой (поток или ISR).
   Возврат: AUDIO_OK или AUDIO_ERR_QUEUE_FULL (очередь на AUDIO_QUEUE_LEN).
   Цикл не снимается: при audio_set_loop(1) новый звук дальше пойдёт по кругу
   с паузой audio_set_loop_pause().                                           */
audio_err_t audio_play_now(uint16_t idx)
{
  ULONG msg = MSG(AUDIO_CMD_PLAY_NOW, idx);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) != TX_SUCCESS)
  {
    return AUDIO_ERR_QUEUE_FULL;
  }
  (void)tx_semaphore_put(&ap_wake);
  return AUDIO_OK;
}

/* Включить/выключить цикл ПОСЛЕДНЕГО ЗАПУЩЕННОГО звука. КОНТЕКСТ: любой.
   При включении уже звучащий (или следующий запущенный) звук пойдёт по кругу;
   при выключении текущий повтор доиграет и наступит тишина.                 */
void audio_set_loop(uint8_t on)
{
  ULONG msg;
  audio_status.loop_enabled = (on != 0u) ? 1u : 0u;
  msg = MSG(AUDIO_CMD_LOOP, on);           /* пересчитать loop_index в потоке */
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
  {
    (void)tx_semaphore_put(&ap_wake);
  }
}

uint8_t audio_get_loop(void) { return audio_status.loop_enabled; }

/* Пауза между повторами цикла, мс. Применяется со следующего повтора. */
void audio_set_loop_pause(uint16_t ms) { audio_status.loop_pause_ms = (uint32_t)ms; }
uint32_t audio_get_loop_pause(void)    { return audio_status.loop_pause_ms; }

/* Громкость 0..100 -> Q15 коэффициент. Контекст: любой (можно из ISR: пишет
   одно volatile int32_t). Когда эффект становится слышен - зависит от пути:
     * стриминг (VECTOR_AUDIO_STREAM=1, рабочий режим): apply_volume() вызывается
       для каждого дозагружаемого куска, поэтому уже играющий звук меняется
       в пределах одного куска - ~93 мс при AP_CHUNK=4096 и 44.1 кГц;
     * одноразовый режим (SAI-DMA не circular): звук читается и обрабатывается
       ЦЕЛИКОМ до старта DMA, поэтому подействует только на следующий звук;
     * аварийный писк: в стриминге громкость применяется (кусок копируется в
       ap_buf и обрабатывается), в одноразовом режиме - НЕТ, там DMA идёт
       напрямую из const-массива во внутренней flash, его не изменить.
   Текущее значение - audio_get_volume(); в демо-режиме ещё demo_status.volume_percent.  */
void audio_set_volume(uint8_t percent)
{
  uint32_t p = (percent > 100u) ? 100u : percent;
  ap_vol_q15 = (int32_t)((p * 32767u) / 100u);  /* коэффициент для DSP (Q15) */
  audio_status.volume_percent = (uint8_t)p;     /* для кода и отладчика      */
}

/* Обратное преобразование Q15 -> проценты (0..100). Контекст: любой. */
uint8_t audio_get_volume(void)
{
  return audio_status.volume_percent;
}

/* --- чтение состояния плеера. КОНТЕКСТ: любой (поток, init, ISR) - читается
   одно volatile-поле структуры audio_status.
   audio_is_playing(): звучит ЗВУК ОБРАЗА (аварийный писк - уже "не играет":
   так же вело себя прежнее поле "текущий индекс >= 0").
   audio_is_busy(): звучит ЧТО-ЛИБО, канал SAI занят.                       */
audio_output_t audio_get_output(void)      { return audio_status.output; }
int32_t        audio_get_sound_index(void) { return audio_status.sound_index; }
audio_boot_t   audio_get_boot_stage(void)  { return audio_status.boot_stage; }
audio_err_t    audio_get_last_error(void)  { return audio_status.last_error; }

uint8_t audio_is_playing(void)
{
  return (audio_status.output == AUDIO_OUT_SOUND) ? 1u : 0u;
}

uint8_t audio_is_busy(void)
{
  return (audio_status.output != AUDIO_OUT_SILENT) ? 1u : 0u;
}

/* Перечитать таблицу образа из внешней flash (нужно, если образ перезаписали
   программатором прямо при включённом устройстве, или после будущей OTA-
   загрузки). Контекст: поток плеера.                                       */
void audio_reload_image(void)
{
  load_image();
}

/* Справка по образу: валиден ли, сколько звуков. Контекст: любой. */
uint8_t  audio_image_ok(void) { return audio_status.image_ok; }
uint16_t audio_count(void)    { return audio_status.image_count; }

/* Имя звука по индексу ("" если образа нет или индекс вне таблицы).
   Контекст: поток. Возвращает указатель ВНУТРЬ ap_tab - не освобождать.    */
const char *audio_name(uint16_t idx)
{
  if (!audio_status.image_ok || (idx >= ap_hdr.count))
  {
    return "";
  }
  return ap_tab[idx].name;
}

/* ================================================== колбэки HAL SAI (ISR) ==
 * КОНТЕКСТ: прерывание GPDMA1_Channel11. Здесь нельзя ничего блокирующего -
 * только счётчики, флаги и tx_semaphore_put(), который будит поток.
 *
 * В режиме СТРИМИНГА (circular DMA) эти колбэки приходят постоянно, по разу на
 * каждую половину буфера:
 *   HAL_SAI_TxHalfCpltCallback - первая половина ушла в SAI, её можно
 *                                перезаписать (поток сделает stream_fill(0));
 *   HAL_SAI_TxCpltCallback     - вторая половина ушла, DMA пошла по кругу,
 *                                перезаписывать можно вторую (stream_fill(1)).
 * Окончание звука определяет поток по ap_st.played >= ap_st.total, а НЕ приход
 * TxCplt: в circular-режиме передача не завершается никогда.
 *
 * В режиме ОДНОЙ транзакции (Normal DMA) TxCplt и есть конец звука.          */

/* Половина (или весь блок в Normal-режиме) выдана в SAI. */
void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
#if VECTOR_AUDIO_STREAM
  if (hsai->Instance == SAI1_Block_A)
  {
    ap_sai_blk_done = 1;
    if (ap_st.active != 0u)
    {
      /* первая половина отыграна: разрешаем потоку её перезаписать.
         Если флаг ЕЩЁ стоит - поток не успел обслужить прошлый запрос, DMA
         прокрутила половину по второму кругу (на слух - заикание).          */
      ap_st.played += AP_CHUNK;
      if (ap_st.req0 != 0u) { audio_status.cnt_underruns++; }
      ap_st.req0    = 1;
      (void)tx_semaphore_put(&ap_wake);
    }
  }
#else
  (void)hsai;
#endif
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    ap_sai_blk_done = 1;
#if VECTOR_AUDIO_STREAM
    if (ap_st.active != 0u)
    {
      /* вторая половина отыграна, DMA пошла по кругу */
      ap_st.played += AP_CHUNK;
      if (ap_st.req1 != 0u) { audio_status.cnt_underruns++; }
      ap_st.req1    = 1;
      (void)tx_semaphore_put(&ap_wake);
      return;
    }
#endif
    if (ap_is_silent() == 0)
    {
      audio_status.cnt_played++;
      ap_mark_silent();
      ap_play_deadline  = 0;
      (void)tx_semaphore_put(&ap_wake);
    }
  }
}

/* Ошибка SAI/DMA (OVRUDR и т.п.): снимаем флаг "играет" и будим поток, иначе
   очередь встала бы навсегда. Сырой код HAL - в audio_status.sai_error_code,
   туда же ставится last_error = AUDIO_ERR_IO (раньше оба числа смешивались
   в одном поле как 0x1000 | ErrorCode).                                    */
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    audio_status.cnt_errors++;
    audio_status.last_error     = AUDIO_ERR_IO;
    audio_status.sai_error_code = (uint32_t)hsai->ErrorCode;
    LOG_E(VLOG_M_AUDIO, "SAI error callback, code=%x", (uint32_t)hsai->ErrorCode);
#if VECTOR_AUDIO_STREAM
    ap_st.active = 0;
#endif
    if (ap_is_silent() == 0)
    {
      ap_mark_silent();
      ap_play_deadline  = 0;
      (void)tx_semaphore_put(&ap_wake);
    }
  }
}
