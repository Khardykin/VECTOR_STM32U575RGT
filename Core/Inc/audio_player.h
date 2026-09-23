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
  *          Ограничение: длина звука <= AUDIO_BUF_SAMPLES сэмплов
  *          (65535 = 8.19 с при 8 кГц). Для более длинных нужен стриминг
  *          с circular DMA - это следующий этап.
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

#define AUDIO_BUF_SAMPLES     65535u          /* RAM-буфер, = 8.19 с @ 8 кГц  */
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

audio_err_t audio_play(uint16_t idx);
audio_err_t audio_play_name(const char *name);
void        audio_stop(void);
void        audio_set_state(uint8_t st);
uint8_t     audio_get_state(void);

/* 0..100. Применяется при загрузке звука в RAM (до DMA), стоимость < 1 мс. */
void        audio_set_volume(uint8_t percent);
uint8_t     audio_get_volume(void);

/* Справка по образу */
uint8_t     audio_image_ok(void);
uint16_t    audio_count(void);
const char *audio_name(uint16_t idx);

/* --- отладочные счётчики (смотреть в Expressions) ------------------------ */
extern volatile uint32_t audio_dbg_played;      /* успешно доиграно          */
extern volatile uint32_t audio_dbg_started;     /* запусков DMA              */
extern volatile uint32_t audio_dbg_errors;      /* ошибки чтения/DMA         */
extern volatile int32_t  audio_dbg_cur_idx;     /* -1 если не играет         */
extern volatile uint32_t audio_dbg_last_err;    /* код audio_err_t           */

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */
