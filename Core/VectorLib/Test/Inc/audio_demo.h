/**
  ******************************************************************************
  * @file    audio_demo.h
  * @brief   ТЕСТОВЫЙ ввод с кнопок для проверки плеера (не продакшн)
  *
  *          Включается макросом VECTOR_AUDIO_DEMO_KEYS в vector_config.h.
  *          В продакшне ставится 0 - и этот модуль исчезает из сборки,
  *          а кнопки обрабатывает ваш рабочий код, вызывая при необходимости
  *          audio_play()/audio_set_state()/audio_stop() напрямую.
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

/* --- счётчики диагностики (Expressions в отладчике) ----------------------
 * edges == 0            -> EXTI не прилетает вовсе (пины/NVIC/схема кнопки)
 * edges > 0, press == 0  -> EXTI есть, но уровень пина не совпал с
 *                           VECTOR_KEY_PRESSED_LEVEL (не та полярность/подтяжка)
 *                           либо всё съедает антидребезг (debounce)
 * press > 0              -> события доходят; причину молчания ищем в audio_dbg_* */
extern volatile uint32_t demo_dbg_edges;
extern volatile uint32_t demo_dbg_press;
extern volatile uint32_t demo_dbg_level0;
extern volatile uint32_t demo_dbg_debounce;
extern volatile uint32_t demo_dbg_last_pin;

#endif /* VECTOR_AUDIO_DEMO_KEYS */

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_DEMO_H */
