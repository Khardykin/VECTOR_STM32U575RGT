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
  *          Как читать отчёт (РАЗ 5 С ДВЕ строки: «snd_log_test t=... | snd:
  *          ...» и «snd_log_test log: ...»; одной строкой нельзя - строка
  *          длиннее 160 символов (VLOG_LINE) обрезается форматтером vlog и
  *          теряет перевод строки, следующая строка приклеивается к обрезку):
  *            snd: trig  - сколько раз звук запускался (1 старт + rearm);
  *                 rearm - перезапуски watchdog'ом (в норме 0; >0 = повтор
  *                         плеера не работает - смотреть loop/err);
  *                 busy  - audio_is_busy() сейчас (1 = звучит);
  *                 loop  - индекс звука в цикле (в норме 5 = SND_ZVONOK_1;
  *                         -1 = цикла НЕТ - проблема);
  *                 vol   - громкость % (должно быть 0; если вдруг 100 -
  *                         задета кнопка PB3 демо-режима);
  *                 err   - audio_status.last_error (0 = AUDIO_OK);
  *                 loops - audio_status.cnt_loops: сколько повторов цикла
  *                         сыграно (растёт ~1 раз в 3 с, если цикл жив);
  *                 und   - cnt_underruns с момента последнего старта
  *                         (заикания стрима; в норме 0; >0 = flash не
  *                         успевает под звуком - искать конфликт на шине);
  *                 wdog  - cnt_watchdog (звук убит за пропажей колбэка DMA;
  *                         в норме 0).
  *            log: req     - сколько событий поставлено в RAM-очередь (v2,
  *                           LOG_FORMAT §6: снимок на момент события, 1/с);
  *                 q       - глубина очереди на момент отчёта (в норме 0-1:
  *                           писатель дренирует каждые 50 мс, program под
  *                           звуком разрешён; больше - ждётся окно для erase);
  *                 drop    - logq_dropped: потери при ПЕРЕПОЛНЕНИИ очереди
  *                           (16 слотов; в норме 0 - прежних «lost ~50%»
  *                           однослотового SaveEvent больше нет);
  *                 written - СКОЛЬКО ЗАПИСАНО (дельта CurrentAddrFile):
  *                           в норме written = req - q (всё, что не в очереди,
  *                           уже во flash);
  *                 defers  - tasks_status.cnt_flash_defers (запись/стирание
  *                           наткнулись на EXT_BUSY: шина занята стримом или
  *                           стирание ждёт окно тишины - норма, не ошибка);
  *                 errs    - cnt_flash_errors (жёсткие ошибки - в норме 0).
  *            ext: busy      - ext_dbg_busy (шина занята при обращении);
  *                 erase_def - ext_dbg_erase_defer (стирание сектора ОТЛОЖЕНО
  *                           из-за звука/шины - теперь ВОЗМОЖНО >0: стирания
  *                           кольца v2 идут каждые 102 записи (~110 с при
  *                           1 событии/с; после переинициализации кольца
  *                           flash нечистая - каждый сектор стирается), и
  *                           попытка под звук даёт отсрочку в окно тишины).
  *            last: rc   - log_read последней записи (0 = ok, 1 = вне
  *                           диапазона, 2 = запись невалидна, -1 = EXT_BUSY,
  *                           -9 = записей ещё нет);
  *                 type - H.Type_log (8 = TYPE_LOG_PARAM_NOP; 0x40/4/0x30 =
  *                          SNAPSHOT/READINGS/PPE если включён архив);
  *                 addr - H.CurrentAddrFile прочитанной записи;
  *                 wh   - H.working_hours = ВРЕМЯ СОБЫТИЯ (снимок в момент
  *                        постановки в очередь, а не момент записи): при
  *                        SLT_LOG_PERIOD_S=1 соседние записи ровно +1 с,
  *                        дельта wh между соседними ОТЧЁТАМИ ровно 5 с
  *                        (прежнее «то 5, то 6» - штамп по моменту записи -
  *                        ушло вместе с однослотовой схемой, PLAN п.58).
  *                        Такт 1 с: прогон 4 подтвердил +1/с (п.56-57).
  ******************************************************************************
  */
#include "main.h"              /* типовые определения проекта                  */
#include "Vector_main.h"       /* Sns_Cfg_struct, tasks_status, snd_log_test.h */
#include "Config_save_read.h"  /* SaveEvent, TypeLogEvent, log_read, TYPE_LOG  */
#include "audio_player.h"      /* audio_*, audio_status                        */
#include "audio_ids.h"         /* SND_* (образ sounds.bin)                     */
#include "extstore.h"          /* ext_dbg_busy, ext_dbg_erase_defer            */
#include "rtc.h"               /* hrtc - диагностика RTC WakeUp (прогон 2)     */
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

/* Одноразовая КОРРЕКЦИЯ ЧАСОВ (прогон 5: календарь RTC снова «2066-01-01» -
   стартовое отравление старым wh из конфига; гонка закрыта состоянием
   прибора DEVICE_TURNED_OFF_PRE_INIT, PLAN п.59-60).
   0 = выключено; иначе Unix-секунды: на t=2 с пишутся в working_hours, а
   штатная сверка Vector_Options_System() за <=1 с переносит их в календарь
   RTC (живёт от VBAT; следующие старты читают уже его). Примеры:
   946684800 = 2000-01-01 00:00:00 UTC (эпоха прибора),
   1792540800 = 2026-10-21 00:00:00 UTC (реальная дата).
   После коррекционного прогона вернуть 0. */
#ifndef SLT_SET_WH_UNIX
#define SLT_SET_WH_UNIX      0u
#endif

/* --------------------------- состояние теста ------------------------------- */
static uint8_t  slt_inited;          /* 1 после первого такта (ленивый init)   */
static uint32_t slt_sec;             /* счётчик секунд теста                   */
static uint32_t slt_snd_trig;        /* запусков звука (старт + rearm)         */
static uint32_t slt_snd_rearm;       /* перезапусков watchdog'ом               */
static uint32_t slt_log_req;         /* событий поставлено в RAM-очередь       */
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
        /* Намеренно по-английски: терминал автора в CP1251, UTF-8 кириллица
           в логе выглядит кракозябрами (прогон 1, PLAN.md п.54).            */
        LOG_W(VLOG_M_TASKS, "snd_log_test: sound loop stalled - restart (%u)",
              (unsigned)slt_snd_rearm);
      }
    }
  }
  else
  {
    slt_silent_s = 0u;
  }

  /* ---- журнал: событие раз в SLT_LOG_PERIOD_S -> RAM-очередь (LOG_FORMAT
         §6): снимок на момент события, писатель (Vector_RunFlashMemory,
         50 мс) переносит во flash даже ПОД ЗВУКОМ. Прежних «lost» больше
         нет - очередь (16 слотов) вбирает всплески; потери только при
         переполнении (logq_dropped). Ожидание: written = req - q.       ---- */
  if ((slt_sec % SLT_LOG_PERIOD_S) == 0u)
  {
    (void)log_post(TYPE_LOG_PARAM_NOP);   /* "любой" безвредный тип записи    */
    slt_log_req++;
  }

#if (SLT_SET_WH_UNIX != 0u)
  if (slt_sec == 2u)
  {
    /* В календарь напрямую НЕ пишем: wh перенесёт в него штатная сверка
       (device_turn = DEVICE_TURNED_ON к этому моменту - Pre_Init завершён,
       receiver работает). Гонка с RTC-ISR (+1) допустима - точность
       секундная.                                                          */
    Sns_Cfg_struct.Working_data.working_hours = (uint32_t)SLT_SET_WH_UNIX;
    LOG_I(VLOG_M_TASKS, "clock set: wh=%u (cal sync <=1 s, kept by VBAT)",
          (unsigned)SLT_SET_WH_UNIX);
  }
#endif

  /* ---- диагностика RTC-такта 1 с (прогоны 3-4 ЗАКРЫЛИ вопрос, PLAN.md
     п.56-57). Корень замера wh в прогоне 2: WakeUp в rtc.c/.ioc был настроен
     CK_SPRE_17BITS (WUCKSEL=110, период 2^17+WUT+1 с ≈ 36,4 ч). Правка
     АВТОРА: 16BITS (WUCKSEL=100, ck_spre -> ровно 1 с); прогон 4 подтвердил:
     wh +981 за ровно 981 с. Реарм v7 переставлял таймер ТЕМ ЖЕ 17BITS (и
     снова убивал такт), поэтому удалён автором (bb55896) - t=10 с теперь
     только сверяет CR до/после, таймер не трогая. Норма дампа (расшифровка
     по stm32u575xx.h): CR=0x40004404 - WUCKSEL=100, WUTE бит10, WUTIE
     бит14, бит30 TAMPALRM_TYPE (дефолт куба, безвреден); WUTR=0; ICSR -
     RSF+ALR*WF (WUTWF=0 при работе - норма); BDCR - LSEON+LSERDY, RTCSEL=01
     (LSE), RTCEN; ISER0 бит2 (RTC_IRQn=2)=1. DBP на U5 живёт в PWR->DBPR
     (бит0), НЕ в PWR_CR1: CR1=0x80 в дампах - это ULPMEN (бит7). Календарь
     после сброса backup-домена - аппаратный дефолт 2000-01-01 (INITS=0 в
     ICSR - норма): wh = 946684800 + аптайм; реальное время позже поставит
     LoRa TimeRef (или отладчик: рабочая wh за <=1 с уедет в календарь через
     сверку Vector_Options_System и закрепится от VBAT).
     Строки намеренно короткие (<160, VLOG_LINE) и по-английски (терминал
     CP1251). */
  if (slt_sec == 5u)
  {
    RTC_TimeTypeDef st = {0};
    RTC_DateTypeDef sd = {0};
    int rct = (int)HAL_RTC_GetTime(&hrtc, &st, RTC_FORMAT_BIN);
    int rcd = (int)HAL_RTC_GetDate(&hrtc, &sd, RTC_FORMAT_BIN);

    LOG_I(VLOG_M_TASKS, "rtc dbg: CR=%X WUTR=%u ICSR=%X",
          (unsigned)RTC->CR, (unsigned)RTC->WUTR, (unsigned)RTC->ICSR);
    LOG_I(VLOG_M_TASKS, "rtc dbg: BDCR=%X DBPR=%X ISER0=%X rtcie=%u",
          (unsigned)RCC->BDCR, (unsigned)PWR->DBPR, (unsigned)NVIC->ISER[0],
          (unsigned)((NVIC->ISER[0] >> (uint32_t)RTC_IRQn) & 1u));
    /* Порядок Time->Date (теневые регистры); "20%u" - год HAL 0..99 от 2000;
       %u БЕЗ флагов ширины (форматтер vlog их не знает, п.55). rc=2/2 -
       HAL_BUSY (конкурентный доступ к hrtc) - не страшно, чисто дамп.    */
    LOG_I(VLOG_M_TASKS, "rtc dbg: cal=20%u-%u-%u %u:%u:%u rc=%d/%d",
          (unsigned)sd.Year, (unsigned)sd.Month, (unsigned)sd.Date,
          (unsigned)st.Hours, (unsigned)st.Minutes, (unsigned)st.Seconds,
          rct, rcd);
  }
  if (slt_sec == 10u)
  {
    unsigned cr_before = (unsigned)RTC->CR;

    LOG_I(VLOG_M_TASKS, "rtc dbg: CRb=%X rearm CRa=%X WUTR=%u",
          cr_before, (unsigned)RTC->CR, (unsigned)RTC->WUTR);
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
    /* Отчёт - ДВЕ короткие строки: одна длинная (>160 символов, VLOG_LINE)
       обрезается форматтером vlog и теряет '\r\n' - в прогоне 1 хвост
       «last: rc=...» так и не было видно (PLAN.md п.54).                   */
    LOG_I(VLOG_M_TASKS,
          "snd_log_test t=%us | snd: trig=%u rearm=%u busy=%u loop=%d vol=%u "
          "err=%d loops=%u und=%u wdog=%u",
          (unsigned)slt_sec,
          (unsigned)slt_snd_trig, (unsigned)slt_snd_rearm,
          (unsigned)audio_is_busy(), (int)audio_status.loop_index,
          (unsigned)audio_status.volume_percent, (int)audio_status.last_error,
          (unsigned)audio_status.cnt_loops,
          (unsigned)audio_status.cnt_underruns,
          (unsigned)audio_status.cnt_watchdog);
    LOG_I(VLOG_M_TASKS,
          "snd_log_test log: req=%u q=%u drop=%u written=%u defers=%u errs=%u "
          "| ext: busy=%u erase_def=%u | last: rc=%d type=%u addr=%u wh=%u",
          (unsigned)slt_log_req, (unsigned)logq_depth(),
          (unsigned)logq_dropped, (unsigned)written,
          (unsigned)tasks_status.cnt_flash_defers,
          (unsigned)tasks_status.cnt_flash_errors,
          (unsigned)ext_dbg_busy, (unsigned)ext_dbg_erase_defer,
          rc,
          (unsigned)((rc == 0) ? ev.H.Type_log : 0xFFFFu),
          (unsigned)((rc == 0) ? ev.H.CurrentAddrFile : 0u),
          (unsigned)((rc == 0) ? ev.H.working_hours : 0u));
  }
}

#endif /* VECTOR_SND_LOG_TEST */
