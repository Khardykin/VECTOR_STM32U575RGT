/**
  ******************************************************************************
  * @file    snd_log_test.h
  * @brief   [ВРЕМЕННЫЙ ТЕСТ - УДАЛИТЬ ПОСЛЕ ПРОВЕРКИ]
  *          Совместная работа звука и записи журнала на реальной шине.
  *
  *          Что делает (см. snd_log_test.c):
  *            - громкость 0% (звук не мешает, но плеер и шина SPI2 работают
  *              ПО-НАСТОЯЩЕМУ: стриминг, W25Q128, window-логика);
  *            - один звук зациклен с паузой 1 с (audio_set_loop +
  *              audio_set_loop_pause) -> период "звук ~2 с / тишина 1 с";
  *            - каждую секунду журнальное событие (TYPE_LOG_PARAM_NOP)
  *              ставится в RAM-очередь (log_post, LOG_FORMAT §6: снимок на
  *              момент события); во flash переносит штатный писатель
  *              Vector_RunFlashMemory() (50 мс, program под звуком);
  *            - раз в 5 с - отчёт в терминал (маска VLOG_M_TASKS): звук,
  *              счётчики req/q/drop/written/defers/errors, ext_dbg_*, и
  *              контрольное чтение последней записи log_read();
  *            - watchdog: если звук не звучит 4 с подряд (цикл убит, например
  *              кнопкой PB2 демо-режима) - перезапуск.
  *
  *          Включение: VECTOR_SND_LOG_TEST в vector_config.h (1 = вкл).
  *          Хуки (помечены "[ВРЕМЕННЫЙ ТЕСТ]"): include в Vector_main.h,
  *          вызов SndLogTest_Run1s() в Vector_main.c (поток measure,
  *          секция timer.flag_1s).
  *          УДАЛЕНИЕ ПОСЛЕ ПРОВЕРКИ: снять макрос (или =0), затем удалить
  *          snd_log_test.c/.h и оба помеченных места.
  ******************************************************************************
  */
#ifndef SND_LOG_TEST_H
#define SND_LOG_TEST_H

#include "vector_config.h"

#if VECTOR_SND_LOG_TEST

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Один такт теста. Вызывать РОВНО раз в секунду из потока measure.
 *        Первый вызов сам инициализирует тест (плеер к этому моменту уже
 *        поднят: audio_init() в app_azure_rtos.c выполняется до потоков).
 */
void SndLogTest_Run1s(void);

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_SND_LOG_TEST */
#endif /* SND_LOG_TEST_H */
