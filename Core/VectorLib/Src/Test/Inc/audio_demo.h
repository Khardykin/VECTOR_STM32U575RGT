/**
  ******************************************************************************
  * @file    audio_demo.h
  * @brief   ТЕСТОВЫЙ ввод с кнопок для проверки плеера (не продакшн)
  *
  *          Включается макросом VECTOR_AUDIO_DEMO_KEYS в vector_config.h.
  *          В продакшне ставится 0 - и этот модуль исчезает из сборки,
  *          а кнопки обрабатывает ваш рабочий код, вызывая при необходимости
  *          audio_play()/audio_play_now()/audio_stop() напрямую.
  *
  *          Схема обработки - ваша: ловим ОБА фронта EXTI, в обработчике
  *          читаем текущий уровень пина и считаем событием только тот фронт,
  *          на котором пин в активном состоянии (плюс защита от дребезга
  *          по интервалу 200 мс).
  ******************************************************************************
  */
#ifndef AUDIO_DEMO_H
#define AUDIO_DEMO_H

#include "vector_config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if VECTOR_AUDIO_DEMO_KEYS

/** Вызывать из ЛЮБОГО обработчика EXTI кнопки (оба фронта).
    Сам читает уровень пина и решает, было ли нажатие. Контекст: ISR. */
void audio_demo_key_handler(uint16_t GPIO_Pin);

/* --- состояние демо-модуля: ОДНА структура demo_status -------------------
 * Тот же приём, что и в audio_player.h для audio_status: вместо россыпи
 * demo_dbg_* - одна структура, в Expressions/Live Watch добавляете строку
 * `demo_status` и раскрываете её.
 *
 * Порядок диагностики "кнопки не работают":
 *   cnt_edges == 0                  -> EXTI не прилетает вовсе
 *                                      (пины/NVIC/схема кнопки)
 *   cnt_edges > 0, cnt_pressed == 0 -> EXTI есть, но уровень пина не совпал с
 *                                      VECTOR_KEY_PRESSED_LEVEL (не та
 *                                      полярность/подтяжка) либо всё съедает
 *                                      антидребезг (cnt_debounced)
 *   cnt_pressed > 0                 -> события доходят; причину молчания
 *                                      ищем в audio_status (audio_player.h) */
typedef struct
{
  volatile uint32_t cnt_edges;           /* сколько EXTI вообще прилетело   */
  volatile uint32_t cnt_pressed;         /* из них признано нажатием        */
  volatile uint32_t cnt_inactive_level;  /* отброшено: пин в неактивном ур. */
  volatile uint32_t cnt_debounced;       /* отброшено антидребезгом         */
  volatile uint32_t last_pin;            /* последний обработанный пин      */
  volatile uint16_t sound_index;         /* BUTTON1: индекс звука           */
  volatile uint8_t  volume_percent;      /* BUTTON3: громкость, %           */
  volatile uint8_t  playing;             /* BUTTON2: зеркало audio_is_playing() */
} audio_demo_status_t;

extern volatile audio_demo_status_t demo_status;

#endif /* VECTOR_AUDIO_DEMO_KEYS */

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_DEMO_H */
