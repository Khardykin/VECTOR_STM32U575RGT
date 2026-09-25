/**
  ******************************************************************************
  * @file    audio_demo.c
  * @brief   ТЕСТОВЫЙ ввод с кнопок (см. audio_demo.h)
  *
  *          Что здесь происходит и ОТКУДА берутся события:
  *            EXTI1/2/3 (PB1/PB2/PB3)
  *              -> EXTI1_IRQHandler (stm32u5xx_it.c, генерится кубом)
  *              -> HAL_GPIO_EXTI_IRQHandler (stm32u5xx_hal_gpio.c)
  *              -> HAL_GPIO_EXTI_Rising_Callback / _Falling_ (ниже)
  *              -> audio_demo_key_handler()
  *              -> audio_set_state()/audio_play()/audio_stop()  (audio_player.c)
  *              -> tx_queue_send + tx_semaphore_put -> поток "Audio Player"
  *
  *          КОНТЕКСТ: прерывание EXTI. Поэтому здесь только чтение пина,
  *          счётчики и неблокирующие вызовы очереди/семафора.
  *
  *          Пины PB1/PB2/PB3 настраивает CubeMX (gpio.c): оба фронта
  *          (GPIO_MODE_IT_RISING_FALLING) + GPIO_PULLUP. Активный уровень
  *          должен соответствовать подтяжке: VECTOR_KEY_PRESSED_LEVEL.
  *
  *          ЕСЛИ ЗВУКА НЕТ И поток плеера спит: смотрите demo_dbg_* - по ним
  *          видно, на каком звене обрывается цепочка (см. docs/FIX_REPORT.md).
  ******************************************************************************
  */
#include "audio_demo.h"

#if VECTOR_AUDIO_DEMO_KEYS

#include "audio_player.h"
#include "main.h"
#include "vector_tick.h"   /* VTICK_MS(): источник времени выбирается в vector_config.h */

#define DEMO_DEBOUNCE_MS   VECTOR_KEY_DEBOUNCE_MS
#define DEMO_KEYS          3u

/* Активный уровень пина берём из vector_config.h. Он ОБЯЗАН соответствовать
   подтяжке, которую задаёт CubeMX в gpio.c:
     GPIO_PULLUP   (сейчас у PB1/PB2/PB3) -> пин в покое 1, кнопка на GND,
                                             активный уровень = 0;
     GPIO_PULLDOWN                        -> наоборот, активный уровень = 1.
   Если перепутать, звук будет стартовать по ОТПУСКАНИЮ кнопки, а не по
   нажатию (демо ловит оба фронта и выбирает из них "активный").            */
#define DEMO_PRESSED_LEVEL (VECTOR_KEY_PRESSED_LEVEL & 1u)

/* Отдельный отсчёт антидребезга НА КАЖДУЮ кнопку: общий интервал на все три
   приводил к тому, что второе нажатие другой кнопкой в течение 200 мс съедалось. */
static uint32_t demo_last_ms[DEMO_KEYS] = { 0, 0, 0 };
static uint8_t  demo_next_idx = 0;

/* Номер кнопки (0..2) по пину; 0xFF - не наша. */
static uint8_t demo_key_index(uint16_t pin)
{
  if (pin == BUTTON1_Pin) { return 0u; }
  if (pin == BUTTON2_Pin) { return 1u; }
  if (pin == BUTTON3_Pin) { return 2u; }
  return 0xFFu;
}

/* ---------------------------------------------------------------- отладка --
 * Счётчики для быстрой диагностики "кнопки не работают":
 *   edges == 0            -> EXTI не приходит вовсе (пины/NVIC/схема)
 *   edges > 0, press == 0  -> EXTI есть, но VECTOR_KEY_PRESSED_LEVEL не
 *                             совпадает с подтяжкой из gpio.c (или всё
 *                             съедает антидребезг - смотрите debounce)
 *   press > 0              -> события доходят, причину молчания ищем уже в
 *                             audio_dbg_* (audio_player.c)                  */
volatile uint32_t demo_dbg_edges   = 0;   /* сколько EXTI вообще прилетело   */
volatile uint32_t demo_dbg_press   = 0;   /* из них признано нажатием        */
volatile uint32_t demo_dbg_level0  = 0;   /* отброшено: пин в неактивном ур. */
volatile uint32_t demo_dbg_debounce= 0;   /* отброшено антидребезгом         */
volatile uint32_t demo_dbg_last_pin= 0;   /* последний обработанный пин      */

/* Обработчик события кнопки. КОНТЕКСТ: ISR EXTI.
 * Логика: фронт уже случился - читаем ТЕКУЩИЙ уровень пина и событием считаем
 * только тот фронт, на котором пин в активном состоянии (то есть нажатие, а не
 * отбой). Плюс защита от дребезга по интервалу DEMO_DEBOUNCE_MS.
 * Кнопки: BUTTON1 - цикл состояний, BUTTON2 - следующий звук, BUTTON3 - стоп. */
void audio_demo_key_handler(uint16_t GPIO_Pin)
{
  uint32_t now = VTICK_MS();
  GPIO_PinState lv;
  uint8_t level, pressed, key;

  demo_dbg_edges++;
  demo_dbg_last_pin = GPIO_Pin;

  key = demo_key_index(GPIO_Pin);
  if (key == 0xFFu)
  {
    return;            /* не наша кнопка */
  }

  /* защита от дребезга: интервал свой для каждой кнопки.
     VTICK_MS() в ISR читать можно - это обычная переменная, которую
     инкрементит прерывание тайм-базы; разрешение 1 мс, для антидребезга
     этого достаточно.                                                      */
  if ((now - demo_last_ms[key]) < DEMO_DEBOUNCE_MS)
  {
    demo_dbg_debounce++;
    return;
  }

  lv = HAL_GPIO_ReadPin(GPIOB, GPIO_Pin);   /* все три кнопки на порту B */

  level   = (lv == GPIO_PIN_SET) ? 1u : 0u;
  pressed = (level == DEMO_PRESSED_LEVEL);
  if (!pressed)
  {
    demo_dbg_level0++;
    return;            /* этот фронт - отбой, событие не считаем */
  }
  demo_last_ms[key] = now;
  demo_dbg_press++;

  if (GPIO_Pin == BUTTON1_Pin)
  {
    /* цикл состояний: 0 (звук#0) -> 1 (звук#1) -> 2 (ТИШИНА) -> 0 */
    audio_set_state((uint8_t)((audio_get_state() + 1u) % 3u));
  }
  else if (GPIO_Pin == BUTTON2_Pin)
  {
    uint16_t n = audio_count();
    if (n > 0u)
    {
      (void)audio_play(demo_next_idx);
      demo_next_idx = (uint8_t)((demo_next_idx + 1u) % n);
    }
  }
  else
  {
    audio_stop();
  }
}

/* Колбэки HAL GPIO. КОНТЕКСТ: ISR EXTI1/2/3. Куб настроил пины на ОБА фронта
   (RISING_FALLING), поэтому ловим оба, а было ли это нажатие - решает чтение
   уровня пина в audio_demo_key_handler().                                  */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
  audio_demo_key_handler(GPIO_Pin);
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
  audio_demo_key_handler(GPIO_Pin);
}

#endif /* VECTOR_AUDIO_DEMO_KEYS */
