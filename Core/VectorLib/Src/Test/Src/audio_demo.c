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
  *              -> audio_play_now()/audio_play()/audio_stop()   (audio_player.c)
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

/* Индекс звука для BUTTON1: перебор ВСЕХ звуков образа по кругу
   (0 .. audio_count()-1). Старт с 0xFFFF, чтобы первое нажатие дало звук #0. */
static uint16_t demo_snd_idx = 0xFFFFu;

/* ТЕСТ громкости (BUTTON3): значения по кругу. demo_dbg_volume видно в Live
   Watch / Expressions - из ISR лог не печатается (vlog такие вызовы
   отбрасывает в vlog_dbg_isr_skipped), а подтверждение, что громкость реально
   применена к звучащим данным, печатает сам плеер из потока: строка
   строка "volume NN% (q15=...)" из apply_volume().                                */
static const uint8_t demo_vol_tbl[] = { 100u, 75u, 50u, 25u, 0u };
static uint8_t       demo_vol_idx   = 0;
volatile uint8_t     demo_dbg_volume = 100u;   /* текущая громкость, %      */
volatile uint16_t    demo_dbg_sound  = 0xFFFFu;/* BUTTON1: индекс звука      */

/* Номер кнопки (0..2) по пину; 0xFF - не наша. */
static uint8_t demo_key_index(uint16_t pin)
{
  if (pin == BUTTON_1_Pin) { return 0u; }
  if (pin == BUTTON_2_Pin) { return 1u; }
  if (pin == BUTTON_3_Pin) { return 2u; }
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

uint8_t audio_play_now_flag = 0;
/* Обработчик события кнопки. КОНТЕКСТ: ISR EXTI.
 * Логика: фронт уже случился - читаем ТЕКУЩИЙ уровень пина и событием считаем
 * только тот фронт, на котором пин в активном состоянии (то есть нажатие, а не
 * отбой). Плюс защита от дребезга по интервалу DEMO_DEBOUNCE_MS.
 * Кнопки: BUTTON1 - перебор всех звуков образа по кругу (прерывая текущий),
 *         BUTTON2 - стоп, BUTTON3 - тест громкости.                         */
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

  if (GPIO_Pin == BUTTON_1_Pin)
  {
    /* BUTTON1: перебор ВСЕХ звуков образа по кругу - 0, 1, 2, 3, 4, снова 0.

       audio_play_now(idx) прерывает текущий звук и сразу запускает новый -
       это ОДНА команда очереди (CMD_PLAY_NOW), а не пара audio_stop() +
       audio_play(). Индекс берётся по модулю audio_count(), то есть состав
       образа может быть любым: пересоберёте sounds.bin на три или семь
       звуков - кнопка продолжит листать их все.

       Звуки лежат во внешней flash не "по порядку воспроизведения", а там,
       куда их положил pack_sounds.py: load_image() один раз читает таблицу
       образа в RAM (ap_tab[]: смещение, длина, частота, имя), и дальше
       обращение к звуку N - это ap_tab[N].offset, никакой перебор не нужен.
       Поэтому играть звуки в произвольном порядке можно так же быстро,
       как и по порядку.                                                    */
    uint16_t n = audio_count();
    if (n > 0u)
    {
      /* 0xFFFF на старте -> первое нажатие даёт звук #0, дальше по кругу */
      if (demo_snd_idx >= n)
      {
        demo_snd_idx = 0u;
      }
      else
      {
        demo_snd_idx = (uint16_t)((demo_snd_idx + 1u) % n);
      }
      (void)audio_play_now(demo_snd_idx);
      demo_dbg_sound = demo_snd_idx;
      audio_play_now_flag = 0;
    }
  }
  else if (GPIO_Pin == BUTTON_2_Pin)
  {
	  if(audio_play_now_flag == 1){
		  audio_play_now(demo_snd_idx);
		  audio_play_now_flag = 0;
	  }
	  else{
		  audio_stop();
		  audio_play_now_flag = 1;
	  }
  }
  else if (GPIO_Pin == BUTTON_3_Pin)
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

#endif /* VECTOR_AUDIO_DEMO_KEYS */
