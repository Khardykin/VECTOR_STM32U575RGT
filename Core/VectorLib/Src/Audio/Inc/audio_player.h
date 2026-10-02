/**
  ******************************************************************************
  * @file    audio_player.h
  * @brief   Плеер звуков из внешней SPI flash: очередь, состояния, громкость
  *
  *          Звуки лежат образом sounds.bin во внешней MX25R6435F (см.
  *          audio_image.h); записывает его туда ПРОГРАММАТОР через external
  *          loader (Tools/ExtLoader_MX25R64, docs/FLASHING.md) - прошивка
  *          внешнюю flash никогда не программирует. Плеер читает звук из
  *          flash и выдаёт в SAI через DMA - без щелей и без участия CPU.
  *
  *          ДВА РЕЖИМА ВЫДАЧИ (выбираются автоматически по конфигурации DMA):
  *            СТРИМИНГ (VECTOR_AUDIO_STREAM=1 И в CubeMX для SAI1_A DMA
  *              выбран Mode = Circular, то есть hdmatx->Mode ==
  *              DMA_LINKEDLIST_CIRCULAR): звук читается из flash КУСКАМИ в две
  *              половины небольшого буфера, DMA крутится по кругу, поток
  *              дозагружает освободившуюся половину. Длина звука НЕ ограничена
  *              (d_myvoice 8.10 с играется целиком), RAM занимает
  *              2 x VECTOR_AUDIO_STREAM_CHUNK сэмплов (16 КБ при 4096).
  *            ONE-SHOT (канал в Normal mode или стриминг выключен): звук
  *              читается ЦЕЛИКОМ в ap_buf[AUDIO_BUF_SAMPLES] и уходит одной
  *              транзакцией. Предел 65535 сэмплов = 1.49 с при 44.1 кГц, RAM
  *              131 КБ (при VECTOR_AUDIO_STREAM_SMALLBUF 1 - только 16 КБ).
  *              Плеер сам определит режим и напечатает подсказку.
  *          На U5 circular для GPDMA - это связный список (linked-list), и
  *          только в режиме DMA_LINKEDLIST_CIRCULAR HAL НЕ гасит SAI по
  *          завершении блока, поэтому стыки кусков идут без щелей.
  *
  *          Семантика:
  *            audio_play(idx)     - в очередь; играется ПОСЛЕ текущего
  *                                  (текущий доигрывает до конца)
  *            audio_play_now(idx) - прервать текущий и СРАЗУ играть idx
  *            audio_stop()        - стоп сейчас + очистка очереди и повтора
  *            audio_set_loop(on)  - повторять последний запущенный звук
  *            audio_set_volume(%) - программная громкость 0..100
  *
  *          Слоя "состояний" (audio_set_state/ap_state_map) больше нет:
  *          плеер играет звуки ПО ИНДЕКСУ из образа, индекс -> смещение во
  *          внешней flash берётся из таблицы ap_tab[], загруженной один раз
  *          в load_image(). Порядок воспроизведения значения не имеет.
  ******************************************************************************
  */
#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Частота дискретизации ВСЕЙ аудио-подсистемы. Обязана совпадать с тремя
   местами, иначе звук пойдёт с другой скоростью и высотой тона:
     1) CubeMX -> SAI1 -> Audio Frequency (SAI_AUDIO_FREQUENCY_44K; PLL3
        пересчитается сам, проверьте ErrorAudioFreq ~ 0.0 %);
     2) tools/pack_sounds.py --rate (сборка sounds.bin);
     3) tools/gen_beep.py --rate (аварийный писк во внутренней flash).
   44.1 кГц/16 бит/моно выбраны как максимум качества тракта: усилитель
   MAX98357A поддерживает 8-96 кГц и 16/24/32 бита, а исходники звуков -
   16-битные, поэтому большая разрядность дала бы только объём, не качество. */
#define AUDIO_SAMPLE_RATE     44100u

/* RAM-буфер под один звук в запасном режиме "одной транзакцией". 65535
   сэмплов - предел uint16_t Size в HAL_SAI_Transmit_DMA: при 44.1 кГц это
   максимум 1.49 с на звук. Рабочий режим - стриминг (длина не ограничена),
   см. VECTOR_AUDIO_STREAM в vector_config.h.                               */
#define AUDIO_BUF_SAMPLES     65535u
#define AUDIO_QUEUE_LEN       8u              /* глубина очереди команд      */

typedef enum
{
  AUDIO_OK = 0,
  AUDIO_ERR_NO_IMAGE,     /* образ sounds.bin не найден/битый                */
  AUDIO_ERR_BAD_INDEX,    /* нет такого звука в таблице                    */
  AUDIO_ERR_TOO_LONG,     /* звук длиннее AUDIO_BUF_SAMPLES                */
  AUDIO_ERR_IO,           /* ошибка чтения флеш или запуска DMA            */
  AUDIO_ERR_QUEUE_FULL,
} audio_err_t;

/* Вызывать один раз из tx_application_define(). Создаёт поток и читает образ. */
void audio_init(void);

/* ПРЯМАЯ проверка звукового тракта без очереди/потока/внешней flash:
   играет аварийный писк из const-массива во внутренней flash. Можно звать из
   main() до старта RTOS. Возврат: 0 = тракт работает (DMA стартовала И
   завершилась), 1 = DMA не стартовала, 2 = стартовала, но не завершилась (в
   NVIC нет GPDMA1_Channel11_IRQn). Сколько раз пискнуть - VECTOR_AUDIO_SELFTEST.
   В лог печатаются число сэмплов, ожидаемая длительность (~240 мс) и guard -
   число проходов ожидания флага завершения. РЕАЛЬНОЕ время здесь не
   измеряется: до планировщика тика RTOS нет, поэтому "затраченные мс"
   напечатать неоткуда. Расшифровка писков - в комментарии VECTOR_AUDIO_SELFTEST. */
int audio_selftest(void);

audio_err_t audio_play(uint16_t idx);      /* в очередь, после текущего   */
audio_err_t audio_play_now(uint16_t idx);  /* прервать текущий и играть   */
audio_err_t audio_play_name(const char *name);
void        audio_stop(void);
void        audio_beep(void);      /* аварийный писк из внутренней flash */

/* --- ЦИКЛ: повтор последнего запущенного звука ---------------------------
 * audio_set_loop(1) включает повтор: звук, запущенный audio_play()/
 * audio_play_now(), после окончания играется снова с паузой
 * audio_set_loop_pause() БЕСКОНЕЧНО, пока не будет:
 *   - audio_stop()            (полный стоп: звук + очередь + текущий повтор)
 *   - audio_set_loop(0)       (цикл снять, текущий повтор доиграет)
 * Повтор взводится для последнего успешно стартовавшего звука, поэтому
 * audio_set_loop(1) можно дать и заранее - зациклится то, что сыграют первым.
 * Одноразовые audio_play() встраиваются в цикл: звучат вместо повтора,
 * после чего повтор продолжается. По умолчанию цикл ВЫКЛЮЧЕН.               */
void        audio_set_loop(uint8_t on);
uint8_t     audio_get_loop(void);
void        audio_set_loop_pause(uint16_t ms);
uint32_t    audio_get_loop_pause(void);

/* 0..100. Применяется при загрузке звука в RAM (до DMA), стоимость < 1 мс. */
void        audio_set_volume(uint8_t percent);
uint8_t     audio_get_volume(void);

/* Справка по образу */
void        audio_reload_image(void);   /* перечитать таблицу образа */
uint8_t     audio_image_ok(void);
uint16_t    audio_count(void);
const char *audio_name(uint16_t idx);

/* --- отладочные счётчики (смотреть в Expressions) ------------------------
 * Порядок быстрой диагностики "звука нет":
 *   boot_stage != 5  -> поток не дошёл до рабочего цикла (см. значения ниже)
 *   keys == 0        -> команды до плеера не доходят (кнопки/EXTI/ваш код)
 *   keys > 0,
 *   started == 0     -> команды есть, но старт DMA не получился (last_err)
 *   started > 0,
 *   played == 0      -> DMA стартовала, но колбэк завершения не пришёл
 *                      (GPDMA1_Channel11_IRQn / SAI)
 *   timeouts растёт  -> поток жив (heartbeat), просто событий нет           */
extern volatile uint32_t audio_dbg_played;      /* успешно доиграно          */
extern volatile uint32_t audio_dbg_started;     /* запусков DMA              */
extern volatile uint32_t audio_dbg_errors;      /* ошибки чтения/DMA         */
extern volatile int32_t  audio_dbg_cur_idx;     /* -1 если не играет         */
extern volatile uint32_t audio_dbg_last_err;    /* код audio_err_t           */
extern volatile uint32_t audio_dbg_boot_stage;  /* 0 init, 1 образ ок,
                                                   2 образа нет/невалиден,
                                                   3 рабочий цикл            */
extern volatile uint32_t audio_dbg_wakes;       /* пробуждений потока        */
extern volatile uint32_t audio_dbg_timeouts;    /* из них по heartbeat       */
extern volatile uint32_t audio_dbg_keys;        /* принято команд            */
extern volatile uint32_t audio_dbg_last_cmd;    /* 1 play 2 stop 3 state 4 beep */
extern volatile uint32_t audio_dbg_dropped;     /* play вытеснил play        */
extern volatile uint32_t audio_dbg_underrun;    /* стрим: половина не была
                                                   дозагружена вовремя (поток
           не успел за CHUNK/AUDIO_SAMPLE_RATE = 93 мс) - слышно как
           заикание, лечится увеличением VECTOR_AUDIO_STREAM_CHUNK или
           разборкой, кто держит ext_mtx */
extern volatile uint32_t audio_dbg_loops;       /* сколько повторов цикла сыграно */
extern volatile uint32_t audio_dbg_stuck;       /* >0: колбэк завершения DMA не
                                                   приходил, звук добит
                                                   watchdog'ом - искать в
                                                   SAI/GPDMA1_Channel11        */

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */
