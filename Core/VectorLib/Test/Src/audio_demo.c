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
  *          видно, на каком звене обрывается цепочка (см. docs/archive/FIX_REPORT.md).
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

/* Позиция в цикле BUTTON1: 0/1/2 = состояния плеера, 3 = СТОП. Старт с 3,
   чтобы ПЕРВОЕ нажатие дало состояние 0, а не 1.                           */
static uint8_t  demo_state_idx = 3u;

/* ТЕСТ громкости (BUTTON3): значения по кругу. demo_dbg_volume видно в Live
   Watch / Expressions - из ISR лог не печатается (vlog такие вызовы
   отбрасывает в vlog_dbg_isr_skipped), а подтверждение, что громкость реально
   применена к звучащим данным, печатает сам плеер из потока: строка
   строка "volume NN% (q15=...)" из apply_volume().                                */
static const uint8_t demo_vol_tbl[] = { 100u, 75u, 50u, 25u, 0u };
static uint8_t       demo_vol_idx   = 0;
volatile uint8_t     demo_dbg_volume = 100u;   /* текущая громкость, %      */

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
 * Кнопки: BUTTON1 - цикл состояний 0 -> 1 -> 2 -> СТОП -> 0,
 *         BUTTON2 - не используется, BUTTON3 - тест громкости.              */
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
    /* Цикл: состояние 0 -> 1 -> 2 -> СТОП -> 0 ...
       Звук состояния берётся из ap_state_map[] (audio_player.c):
         0 = SND_STATE_DEFAULT_0 = kolokol_1
         1 = SND_STATE_DEFAULT_1 = kolokol_2
         2 = ТРЕТИЙ звук образа (phone_1), пока SND_COUNT > 2; тишиной оно
             бывает только если в образе два звука или меньше (AP_STATE_SND2).
       audio_set_state() прерывает текущий звук и чистит очередь, поэтому
       переключение слышно сразу; если включён цикл (audio_set_loop(1)),
       звук состояния повторяется.
       Позиция 3 - audio_stop(): полный стоп + очистка очереди + снятие цикла,
       ap_state при этом НЕ меняется (в лог уйдёт "stop: loop off (state=N)").
       BUTTON2 больше не используется: audio_play(idx) отличался тем, что НЕ
       прерывал текущий звук, а откладывал следующий в один слот ожидания, и
       позволял прослушать все звуки образа (phone_2, zvonok_1 состояниями
       0..2 недостижимы). Вернуть - см. историю, это 4 строки.             */
    demo_state_idx = (uint8_t)((demo_state_idx + 1u) % 4u);
    if (demo_state_idx < 3u)
    {
      audio_set_state(demo_state_idx);
    }
    else
    {
      audio_stop();
    }
  }
  else if (GPIO_Pin == BUTTON2_Pin)
  {
	  audio_stop();
  }
  else if (GPIO_Pin == BUTTON3_Pin)
  {
    /* BUTTON3: ТЕСТ audio_set_volume(). Цикл 100 -> 75 -> 50 -> 25 -> 0 -> 100.
       audio_stop() убран НАМЕРЕННО: вместе с ним проверить громкость нельзя -
       звук обрывается в тот же миг, слушать нечего. В рабочем (стриминговом)
       режиме новая громкость применяется к следующему дозагружаемому куску,
       то есть слышна уже через ~93 мс (AP_CHUNK/44100) прямо во время
       звучания. Как только тест не нужен - верните строку audio_stop().    */
    demo_vol_idx = (uint8_t)((demo_vol_idx + 1u) % (uint8_t)sizeof demo_vol_tbl);
    audio_set_volume(demo_vol_tbl[demo_vol_idx]);
    demo_dbg_volume = demo_vol_tbl[demo_vol_idx];
    /* audio_stop(); */
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
