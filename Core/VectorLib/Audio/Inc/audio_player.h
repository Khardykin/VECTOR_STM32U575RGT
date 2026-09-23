/**
  ******************************************************************************
  * @file    audio_player.h
  * @brief   Плеер звуков из внешней SPI flash: очередь, состояния, громкость
  *
  *          Звуки лежат образом sounds.img во внешней MX25K6435F (см.
  *          audio_image.h). Плеер читает нужный звук целиком в RAM-буфер,
  *          применяет громкость и отдаёт в SAI одной DMA-транзакцией -
  *          без щелей и без участия CPU во время звучания.
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
  *              транзакцией. Предел 65535 сэмплов = 4.09 с при 16 кГц, RAM
  *              131 КБ. Плеер сам определит режим и напечатает подсказку.
  *          На U5 circular для GPDMA - это связный список (linked-list), и
  *          только в режиме DMA_LINKEDLIST_CIRCULAR HAL НЕ гасит SAI по
  *          завершении блока, поэтому стыки кусков идут без щелей.
  *
  *          Семантика:
  *            audio_play(id)      - в очередь; играется ПОСЛЕ текущего
  *                                  (текущий доигрывает до конца)
  *            audio_stop()        - стоп сейчас + очистка очереди
  *            audio_set_state(st) - СМЕНА РЕЖИМА: текущий звук прерывается,
  *                                  играется звук нового состояния
  *                                  (или тишина, если состоянию звук не назначен)
  *            audio_set_volume(%) - программная громкость 0..100
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
     1) CubeMX -> SAI1 -> Audio Frequency (сейчас SAI_AUDIO_FREQUENCY_16K);
     2) tools/pack_sounds.py --rate (сборка sounds.img);
     3) tools/gen_beep.py --rate (аварийный писк во внутренней flash).       */
#define AUDIO_SAMPLE_RATE     16000u

/* RAM-буфер под один звук. 65535 сэмплов - это ещё и предел uint16_t Size в
   HAL_SAI_Transmit_DMA, поэтому одна DMA-транзакция длиннее не бывает:
   при 16 кГц максимум 4.09 с на звук (при 8 кГц было 8.19 с).               */
#define AUDIO_BUF_SAMPLES     65535u
#define AUDIO_QUEUE_LEN       8u              /* глубина очереди play()      */
#define AUDIO_STATE_SILENT    0xFFFFu         /* состоянию звук не назначен  */

typedef enum
{
  AUDIO_OK = 0,
  AUDIO_ERR_NO_IMAGE,     /* образ sounds.img не найден/битый                */
  AUDIO_ERR_BAD_INDEX,    /* нет такого звука в таблице                    */
  AUDIO_ERR_TOO_LONG,     /* звук длиннее AUDIO_BUF_SAMPLES                */
  AUDIO_ERR_IO,           /* ошибка чтения флеш или запуска DMA            */
  AUDIO_ERR_QUEUE_FULL,
} audio_err_t;

/* Вызывать один раз из tx_application_define(). Создаёт поток и читает образ. */
void audio_init(void);

/* ПРЯМАЯ проверка звукового тракта без очереди/потока/внешней flash:
   играет аварийный писк из const-массива во внутренней flash. Можно звать из
   main() до старта RTOS. Возврат: 0 = тракт работает (DMA стартовала и
   завершилась), 1 = DMA не стартовала, 2 = стартовала, но не завершилась.
   Заодно показывает, не врёт ли системный тик (печатает затраченные мс при
   ожидаемых ~240).                                                          */
int audio_selftest(void);

audio_err_t audio_play(uint16_t idx);
audio_err_t audio_play_name(const char *name);
void        audio_stop(void);
void        audio_beep(void);      /* аварийный писк из внутренней flash */
void        audio_set_state(uint8_t st);
uint8_t     audio_get_state(void);

/* --- ЦИКЛ: звук состояния повторяется с паузой ---------------------------
 * audio_set_state(st) включает звук состояния st и, если цикл включён,
 * повторяет его с паузой audio_set_loop_pause() БЕСКОНЕЧНО, пока не будет:
 *   - audio_stop()            (полный стоп: звук + очередь + цикл)
 *   - audio_set_state(2)      (состояние "тишина" = пустая ячейка таблицы)
 *   - audio_set_loop(0)       (цикл снять, текущий повтор доиграет)
 * Одноразовый audio_play() встраивается в цикл: звучит вместо повтора,
 * после чего цикл продолжается.                                             */
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
                                                   2 образа нет, 3 factory ок,
                                                   4 factory провален,
                                                   5 рабочий цикл            */
extern volatile uint32_t audio_dbg_wakes;       /* пробуждений потока        */
extern volatile uint32_t audio_dbg_timeouts;    /* из них по heartbeat       */
extern volatile uint32_t audio_dbg_keys;        /* принято команд            */
extern volatile uint32_t audio_dbg_last_cmd;    /* 1 play 2 stop 3 state 4 beep */
extern volatile uint32_t audio_dbg_dropped;     /* play вытеснил play        */
extern volatile uint32_t audio_dbg_underrun;    /* стрим: половина не была
                                                   дозагружена вовремя (поток
           не успел за 256 мс) - слышно как заикание, лечится увеличением
           VECTOR_AUDIO_STREAM_CHUNK или разборкой, кто держит ext_mtx       */
extern volatile uint32_t audio_dbg_loops;       /* сколько повторов цикла сыграно */
extern volatile uint32_t audio_dbg_stuck;       /* >0: колбэк завершения DMA не
                                                   приходил, звук добит
                                                   watchdog'ом - искать в
                                                   SAI/GPDMA1_Channel11        */

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */
