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

/* Что сейчас выдаёт плеер в SAI. Раньше это состояние было "зашито" в одно
   знаковое число (audio_dbg_cur_idx: -1 тишина, -2 аварийный писк, >=0 индекс
   звука) - ни в коде, ни в отладчике не прочитать. Теперь состояние описывает
   enum, а индекс звука лежит отдельным полем (audio_status_t).              */
typedef enum
{
  AUDIO_OUT_SILENT = 0,   /* тишина: ничего не играем                       */
  AUDIO_OUT_SOUND,        /* звук из образа sounds.bin (sound_index >= 0)   */
  AUDIO_OUT_ALARM_BEEP    /* аварийный писк из внутренней flash             */
} audio_output_t;

/* Этап инициализации плеера (прежний audio_dbg_boot_stage 0..3). */
typedef enum
{
  AUDIO_BOOT_INIT = 0,    /* audio_init() ещё не отработал                  */
  AUDIO_BOOT_IMAGE_OK,    /* образ во внешней flash валиден                 */
  AUDIO_BOOT_IMAGE_BAD,   /* образа нет/бит/не та частота -> только писк    */
  AUDIO_BOOT_RUNNING      /* поток в рабочем цикле команд                   */
} audio_boot_t;

/* Команды очереди плеера. Номер упаковывается в сообщение tx_queue
   (MSG(cmd, arg) в audio_player.c), поэтому значения менять нельзя.
   3 не используется: номер занимал CMD_STATE удалённого слоя "состояний".
   audio_status.last_command хранится этим типом - в отладчике видно имя.   */
typedef enum
{
  AUDIO_CMD_NONE     = 0,
  AUDIO_CMD_PLAY     = 1,  /* в очередь: сыграет ПОСЛЕ текущего             */
  AUDIO_CMD_STOP     = 2,  /* немедленный стоп + очистка очереди и цикла    */
  AUDIO_CMD_BEEP     = 4,  /* аварийный писк, прерывает текущий звук        */
  AUDIO_CMD_LOOP     = 5,  /* пересчитать повтор (audio_set_loop)           */
  AUDIO_CMD_PLAY_NOW = 6   /* прервать текущий и СРАЗУ играть новый         */
} audio_cmd_t;

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

/* --- ЕДИНОЕ состояние плеера: структура audio_status ----------------------
 * Было: 14 глобальных переменных audio_dbg_*, причём СОСТОЯНИЕ (тишина /
 * звук / аварийный писк) кодировалось одним знаковым числом
 * audio_dbg_cur_idx (-1 / -2 / >=0) - нечитаемо ни в коде, ни в отладчике.
 * Стало:
 *   - состояние описано enum'ами (audio_output_t, audio_boot_t,
 *     audio_cmd_t, audio_err_t) - в отладчике видно имя, а не цифру;
 *   - всё живёт в ОДНОЙ структуре audio_status: в Expressions/Live Watch
 *     достаточно одной строки `audio_status`;
 *   - дублей состояния внутри audio_player.c больше нет: ap_playing_idx,
 *     ap_loop_on/ap_loop_idx/ap_last_idx/ap_loop_pause/ap_next_at и ap_img_ok
 *     удалены, их заменили поля этой структуры (один источник правды).
 *
 * Порядок быстрой диагностики "звука нет":
 *   boot_stage != AUDIO_BOOT_RUNNING  -> поток не дошёл до рабочего цикла
 *   cnt_commands == 0                 -> команды до плеера не доходят
 *                                        (кнопки/EXTI/ваш код)
 *   cnt_commands > 0, cnt_started == 0-> команды есть, но старт DMA не
 *                                        получился (смотрите last_error)
 *   cnt_started > 0, cnt_played == 0  -> DMA стартовала, но колбэк завершения
 *                                        не пришёл (GPDMA1_Channel11_IRQn/SAI)
 *   cnt_wake_timeouts растёт          -> поток жив (heartbeat), событий нет
 *
 * ПОЛЯ "состояние/повтор/ошибки" - РАБОЧИЕ, их читают другие модули
 * (audio_is_playing() в audio_demo.c), а не только отладчик.
 *
 * АТОМАРНОСТЬ: структуру пишут и поток плеера, и ISR завершения/ошибки DMA
 * (output, sound_index, cnt_started, cnt_played, last_error, sai_error_code,
 * cnt_watchdog), поэтому volatile объявлена ПЕРЕМЕННАЯ ЦЕЛИКОМ. Порядок
 * записи состояния фиксирован: при старте звука сначала sound_index, потом
 * output; при остановке - наоборот. Тогда читающий никогда не увидит
 * AUDIO_OUT_SOUND с мусорным индексом.                                       */
typedef struct
{
  /* --- состояние (рабочее, не только для отладки) --- */
  volatile audio_output_t output;           /* что звучит сейчас             */
  volatile int32_t        sound_index;      /* индекс звука в образе;
                                               -1, если звучит не звук образа */
  volatile audio_boot_t   boot_stage;       /* этап инициализации плеера     */
  volatile uint8_t        image_ok;         /* 1 = образ sounds.bin валиден  */
  volatile uint16_t       image_count;      /* звуков в образе (0 = нет)     */
  volatile uint8_t        volume_percent;   /* громкость 0..100              */

  /* --- повтор (цикл) --- */
  volatile uint8_t        loop_enabled;     /* audio_set_loop(1)             */
  volatile int32_t        loop_index;       /* какой звук повторяется;
                                               -1 = цикла нет                */
  volatile int32_t        last_sound_index; /* последний успешно запущенный
                                               звук; -1 = ещё не было        */
  volatile uint32_t       loop_pause_ms;    /* пауза между повторами         */
  volatile uint32_t       loop_next_at_ms;  /* момент повтора (VTICK_MS);
                                               0 = отсчёт паузы не начат     */

  /* --- ошибки --- */
  volatile audio_err_t    last_error;       /* последняя ошибка, audio_err_t */
  volatile uint32_t       sai_error_code;   /* HAL SAI ErrorCode (0 = не было;
                                               раньше смешивалось с кодом
                                               ошибки как 0x1000|ErrorCode)  */

  /* --- счётчики событий (диагностика) --- */
  volatile uint32_t       cnt_commands;      /* принято команд из очереди    */
  volatile audio_cmd_t    last_command;      /* последняя команда            */
  volatile uint32_t       cnt_started;       /* запусков DMA                 */
  volatile uint32_t       cnt_played;        /* успешно доиграно             */
  volatile uint32_t       cnt_errors;        /* ошибки чтения flash/DMA      */
  volatile uint32_t       cnt_dropped;       /* play вытеснил предыдущий play*/
  volatile uint32_t       cnt_underruns;     /* стрим: половина не была
                                                дозагружена вовремя (поток не
                                                успел за CHUNK/44100 = 93 мс)
                                                - слышно как заикание; лечится
                                                увеличением
                                                VECTOR_AUDIO_STREAM_CHUNK     */
  volatile uint32_t       cnt_loops;         /* сколько повторов цикла сыграно*/
  volatile uint32_t       cnt_watchdog;      /* >0: колбэк завершения DMA не
                                                приходил, звук добит
                                                watchdog'ом - искать в
                                                SAI/GPDMA1_Channel11          */
  volatile uint32_t       cnt_wakes;         /* пробуждений потока           */
  volatile uint32_t       cnt_wake_timeouts; /* из них холостых (heartbeat)  */
} audio_status_t;

extern volatile audio_status_t audio_status;

/* --- чтение состояния: безопасно из ЛЮБОГО контекста, включая ISR ----------
 * audio_is_playing() = звучит ЗВУК ОБРАЗА (именно это проверяла BUTTON2 в
 * демо-режиме: аварийный писк "игрой" не считается, как и раньше при
 * audio_dbg_cur_idx >= 0). audio_is_busy() = звучит ЧТО-ЛИБО (звук или писк).
 * audio_status доступна и напрямую - эти функции просто чтобы не раскатывать
 * сравнения с enum по модулям.                                               */
audio_output_t audio_get_output(void);
uint8_t        audio_is_playing(void);
uint8_t        audio_is_busy(void);
int32_t        audio_get_sound_index(void);
audio_boot_t   audio_get_boot_stage(void);
audio_err_t    audio_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */
