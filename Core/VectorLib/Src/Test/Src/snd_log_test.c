/**
  ******************************************************************************
  * @file    snd_log_test.c
  * @brief   [ВРЕМЕННЫЙ ТЕСТ - УДАЛИТЬ ПОСЛЕ ПРОВЕРКИ] Реализация: см. .h.
  *
  *          КОНТЕКСТ: поток measure_task (Vector_main.c, секция flag_1s).
  *          Пишет в журнал НЕ сам: только взводит SaveEvent/TypeLogEvent,
  *          а запись делает штатный Vector_RunFlashMemory() того же потока
  *          (окно тишины, отсрочка стираний и т.д.) - тестируется ИМЕННО
  *          штатный путь "звук + журнал".
  *
  *          Как читать отчёт (строка "snd_log_test ..." раз в 5 с):
  *            snd: trig  - сколько раз звук запускался (1 старт + rearm);
  *                 rearm - перезапуски watchdog'ом (в норме 0);
  *                 busy  - audio_is_busy() сейчас (1 = звучит);
  *                 loop  - индекс звука в цикле (-1 = цикла НЕТ - проблема);
  *                 vol   - громкость % (должно быть 0; если вдруг 100 -
  *                         задета кнопка PB3 демо-режима);
  *                 err   - audio_status.last_error (0 = AUDIO_OK).
  *            log: req     - сколько раз взведён SaveEvent (≈ секунд работы);
  *                 lost    - флаг ещё стоял, когда подошёл срок (окно тишины
  *                           не наступило за 1 с) - в норме редкие единицы,
  *                           устойчивый рост = проблема;
  *                 written - СКОЛЬКО ЗАПИСАНО (дельта CurrentAddrFile):
  *                           в норме written ≈ req - lost;
  *                 defers  - tasks_status.cnt_flash_defers (письмо ждало окно
  *                           тишины - РАСТЁТ ВСЕГДА, это нормальная работа
  *                           отсрочки, не ошибка);
  *                 errs    - cnt_flash_errors (жёсткие ошибки - в норме 0).
  *            ext: busy      - ext_dbg_busy (шина занята при обращении);
  *                 erase_def - ext_dbg_erase_defer (стирания отложены из-за
  *                           звука; стирание сектора ~раз в 64 записи ≈ 64 с).
  *            last: rc   - log_read последней записи (0 = ok, 1 = вне
  *                           диапазона, -1 = EXT_BUSY, -9 = записей ещё нет);
  *                  type - Type_log (8 = TYPE_LOG_PARAM_NOP; 4 = TYPE_READINGS
  *                           если параллельно включён архив ArchiveRecording);
  *                  addr - CurrentAddrFile прочитанной записи;
  *                  wh   - working_hours записи (должно расти ~1/с).
  ******************************************************************************
  */
#include "main.h"              /* типовые определения проекта                  */
#include "Vector_main.h"       /* Sns_Cfg_struct, tasks_status, snd_log_test.h */
#include "Config_save_read.h"  /* SaveEvent, TypeLogEvent, log_read, TYPE_LOG  */
#include "audio_player.h"      /* audio_*, audio_status                        */
#include "audio_ids.h"         /* SND_* (образ sounds.bin)                     */
#include "extstore.h"          /* ext_dbg_busy, ext_dbg_erase_defer            */
#include "vector_log.h"        /* LOG_I / LOG_W                                */
#include "snd_log_test.h"

#if VECTOR_SND_LOG_TEST

/* --------------------------- параметры теста ------------------------------- */
#ifndef SLT_SOUND_IDX
#define SLT_SOUND_IDX        SND_ZVONOK_1  /* самый короткий звук образа (2 с)  */
#endif
#define SLT_LOOP_PAUSE_MS    1000u   /* ПЕРИОД МЕЖДУ ЗВУКАМИ: 1 с (задача)     */
#define SLT_LOG_PERIOD_S     1u      /* журнальное событие каждые N с           */
#define SLT_REPORT_PERIOD_S  5u      /* отчёт в терминал каждые N с             */
#define SLT_REARM_S          4u      /* не звучит N с подряд -> перезапуск      */

/* --------------------------- состояние теста ------------------------------- */
static uint8_t  slt_inited;          /* 1 после первого такта (ленивый init)   */
static uint32_t slt_sec;             /* счётчик секунд теста                   */
static uint32_t slt_snd_trig;        /* запусков звука (старт + rearm)         */
static uint32_t slt_snd_rearm;       /* перезапусков watchdog'ом               */
static uint32_t slt_log_req;         /* взведено SaveEvent                     */
static uint32_t slt_log_lost;        /* взвести не удалось (флаг ещё стоял)    */
static uint32_t slt_addr0;           /* CurrentAddrFile на момент старта       */
static uint32_t slt_silent_s;        /* подряд секунд с audio_is_busy()==0     */

void SndLogTest_Run1s(void)
{
  EVENT_LOG ev = {0};
  uint32_t  written;
  int       rc;

  /* ---- ленивая инициализация (плеер уже поднят: audio_init до потоков) ---- */
  if (slt_inited == 0u)
  {
    slt_inited = 1u;
    slt_addr0  = Sns_Cfg_struct.Config_archive.CurrentAddrFile;

    audio_set_volume(0);                    /* громкость 0 - звук не мешает    */
    audio_set_loop(1);                      /* повторять бесконечно            */
    audio_set_loop_pause(SLT_LOOP_PAUSE_MS);/* пауза между звуками 1 с         */
    if (audio_play_now((uint16_t)SLT_SOUND_IDX) == AUDIO_OK)
    {
      slt_snd_trig++;
    }
    LOG_I(VLOG_M_TASKS,
          "snd_log_test START: vol=0%% loop snd=%d pause=%ums | log every %us | "
          "report every %us | addr0=%u",
          (int)SLT_SOUND_IDX, (unsigned)SLT_LOOP_PAUSE_MS,
          (unsigned)SLT_LOG_PERIOD_S, (unsigned)SLT_REPORT_PERIOD_S,
          (unsigned)slt_addr0);
  }

  slt_sec++;

  /* ---- звук: watchdog цикла (демо-кнопка PB2 может остановить) ---- */
  if (audio_is_busy() == 0u)
  {
    slt_silent_s++;
    if (slt_silent_s >= SLT_REARM_S)
    {
      slt_silent_s = 0u;
      if (audio_play_now((uint16_t)SLT_SOUND_IDX) == AUDIO_OK)
      {
        slt_snd_trig++;
        slt_snd_rearm++;
        LOG_W(VLOG_M_TASKS, "snd_log_test: цикл звука встал - перезапуск (%u)",
              (unsigned)slt_snd_rearm);
      }
    }
  }
  else
  {
    slt_silent_s = 0u;
  }

  /* ---- журнал: событие раз в SLT_LOG_PERIOD_S (штатный отложенный путь) ---- */
  if ((slt_sec % SLT_LOG_PERIOD_S) == 0u)
  {
    if (SaveEvent == 0u)
    {
      TypeLogEvent = TYPE_LOG_PARAM_NOP;   /* "любой" безвредный тип записи    */
      SaveEvent    = 1u;
      slt_log_req++;
    }
    else
    {
      slt_log_lost++;   /* окно тишины не наступило за период - флаг ещё стоит
                           (слот флага один: новое событие перезаписало бы тип,
                           поэтому ждём освобождения)                          */
    }
  }

  /* ---- отчёт + контрольное чтение последней записи ---- */
  if ((slt_sec % SLT_REPORT_PERIOD_S) == 0u)
  {
    written = Sns_Cfg_struct.Config_archive.CurrentAddrFile - slt_addr0;
    rc      = -9;                          /* записей ещё нет                */
    if (written > 0u)
    {
      rc = log_read(&Sns_Cfg_struct,
                    Sns_Cfg_struct.Config_archive.CurrentAddrFile - 1u, &ev);
    }
    LOG_I(VLOG_M_TASKS,
          "snd_log_test t=%us | snd: trig=%u rearm=%u busy=%u loop=%d vol=%u "
          "err=%d | log: req=%u lost=%u written=%u defers=%u errs=%u | "
          "ext: busy=%u erase_def=%u | last: rc=%d type=%u addr=%u wh=%u",
          (unsigned)slt_sec,
          (unsigned)slt_snd_trig, (unsigned)slt_snd_rearm,
          (unsigned)audio_is_busy(), (int)audio_status.loop_index,
          (unsigned)audio_status.volume_percent, (int)audio_status.last_error,
          (unsigned)slt_log_req, (unsigned)slt_log_lost, (unsigned)written,
          (unsigned)tasks_status.cnt_flash_defers,
          (unsigned)tasks_status.cnt_flash_errors,
          (unsigned)ext_dbg_busy, (unsigned)ext_dbg_erase_defer,
          rc,
          (unsigned)((rc == 0) ? ev.Type_log : 0xFFFFu),
          (unsigned)((rc == 0) ? ev.CurrentAddrFile : 0u),
          (unsigned)((rc == 0) ? ev.working_hours : 0u));
  }
}

#endif /* VECTOR_SND_LOG_TEST */
