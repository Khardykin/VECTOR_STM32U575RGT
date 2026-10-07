/**
  ******************************************************************************
  * @file    rf_thread.h
  * @brief   Поток RF: LoRa + BLE + LTE - приём из колец UART и разбор кадров
  *
  *          ОДИН поток владеет приёмом всех радио-линков прибора. Схема:
  *
  *            ISR приёма UART  --байт-->  InputBuffer[TYPE_LORA/BLE/LTE]
  *                   (add_to_buffer, см. buffer.h - правило SPSC)
  *                              |
  *                              v
  *            поток "RF" (rf_thread_entry):
  *               for each link:  кольцо -> rf_<link>_rx_byte(байт)
  *                                       -> накопитель кадра
  *                                       -> rf_<link>_frame(кадр целиком)
  *               передача: rf_send(link, ...) -> transmit_buffer(...)
  *
  *          ЧТО УЖЕ РАБОТАЕТ (каркас): создание потока, опрос колец всех трёх
  *          линков, сбор кадра по паузе в линии, счётчики и состояния в ОДНОЙ
  *          структуре rf_status, неблокирующая передача rf_send().
  *
  *          ЧТО ПУСТОЕ И ЖДЁТ НАПОЛНЕНИЯ: rf_lora_rx_byte()/rf_lora_frame(),
  *          rf_ble_*(), rf_lte_*() в rf_thread.c - это и есть места под обмен
  *          данными и парсинг (AT-машина S7678S, протокол BLE-модуля, модем
  *          LTE). Каркас их вызывает, поэтому начинать можно с любого линка,
  *          не трогая поток.
  *
  *          ПРАВИЛО ВЛАДЕЛЬЦА КОЛЬЦА: у каждого UART ровно ОДИН потребитель -
  *          этот поток. Существующий драйвер LoRa (Lora_Receive() в
  *          Lora_S7678S.c) читает то же кольцо InputBuffer[TYPE_LORA], поэтому
  *          при CONFIG_LORA 1 вызывать его надо ИЗ rf_lora_poll(), а не из
  *          другого потока/таймера: два потребителя ломают SPSC-кольцо.
  *
  *          UART'ы линков пока не назначены: кто каким портом владеет, решает
  *          transmit_buffer() в buffer.c по типу TYPE_* (макросы USART_BLE /
  *          USART_RF в main.h - см. PLAN.md раздел 7, п.13). Приём байтов
  *          кладут в кольца обработчики USART*_IRQHandler (stm32u5xx_it.c),
  *          они сейчас закрыты флагами CONFIG_LORA / CONFIG_BLE - до их
  *          включения rf_status.link[].cnt_rx_bytes расти не будет.
  *
  *          КОНТЕКСТ ВЫЗОВОВ:
  *            rf_init()            - инициализация (tx_application_define)
  *            rf_notify()          - ISR приёма UART (неблокирующий)
  *            rf_send()            - поток (внутри блокирующий HAL_UART_Transmit)
  *            rf_link_enable()     - поток
  *            чтение rf_status     - любой контекст (поля volatile)
  ******************************************************************************
  */
#ifndef RF_THREAD_H
#define RF_THREAD_H

#include <stdint.h>
#include "vector_config.h"
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#if VECTOR_RF_THREAD

/* Линки потока RF. Порядок задаёт индекс в rf_status.link[] - не менять без
   необходимости (на него завязаны инициализаторы таблицы в rf_thread.c).    */
typedef enum
{
  RF_LINK_LORA = 0,   /* модуль LoRa S7678S (AT-команды)                    */
  RF_LINK_BLE,        /* BLE-модуль (мост данных/статусы)                   */
  RF_LINK_LTE,        /* сотовый модем LTE                                  */
  RF_LINK_COUNT
} rf_link_id_t;

/* Состояние линка. В отладчике видно имя, а не цифру. */
typedef enum
{
  RF_LINK_OFF = 0,    /* выключен (компиляцией или rf_link_enable(0))        */
  RF_LINK_IDLE,       /* включён, приёма нет                                 */
  RF_LINK_RECEIVING,  /* идёт приём: в накопителе есть байты кадра           */
  RF_LINK_ERROR       /* ошибка обмена/парсера (ставит код модуля)           */
} rf_link_state_t;

/* Этап инициализации потока (по нему видно, дошёл ли поток до рабочего цикла). */
typedef enum
{
  RF_BOOT_INIT = 0,   /* rf_init() ещё не отработал                          */
  RF_BOOT_RUNNING     /* поток в рабочем цикле                               */
} rf_boot_t;

/* Состояние и счётчики ОДНОГО линка. Всё, что нужно знать про приём/передачу
   конкретного модуля, - здесь; отдельные глобальные переменные не заводим.  */
typedef struct
{
  volatile rf_link_state_t state;        /* чем линк занят сейчас            */
  volatile uint8_t  enabled;             /* 0 = линк выключен в рантайме     */
  volatile uint32_t cnt_rx_bytes;        /* байт принято из кольца           */
  volatile uint32_t cnt_rx_frames;       /* кадров собрано и отдано парсеру  */
  volatile uint32_t cnt_rx_overruns;     /* потерь при переполнении кольца
                                            (заполнится, когда ISR начнёт
                                            учитывать возврат add_to_buffer:
                                            PLAN.md раздел 7, п.5)            */
  volatile uint32_t cnt_tx_frames;       /* сколько кадров передали          */
  volatile uint32_t cnt_tx_bytes;        /* сколько байт передали            */
  volatile uint32_t cnt_parse_errors;    /* ошибок разбора (ставит парсер)   */
  volatile uint16_t frame_len;           /* длина последнего собранного кадра*/
  volatile uint32_t last_byte_ms;        /* когда пришёл последний байт      */
  volatile uint32_t last_rx_ms;          /* когда приняли последний кадр     */
  volatile uint32_t last_tx_ms;          /* когда передали последний кадр    */
} rf_link_status_t;

/* ЕДИНОЕ состояние модуля RF: в Expressions/Live Watch достаточно одной
   строки `rf_status` (тот же приём, что и audio_status в audio_player.h).   */
typedef struct
{
  volatile rf_boot_t boot_stage;         /* init / рабочий цикл              */
  rf_link_status_t   link[RF_LINK_COUNT];/* по одному блоку на линк          */
  volatile uint32_t  cnt_wakes;          /* пробуждений потока               */
  volatile uint32_t  cnt_wake_timeouts;  /* из них холостых (heartbeat)      */
  volatile uint32_t  cnt_cycles;         /* полных проходов по всем линкам   */
} rf_status_t;

extern volatile rf_status_t rf_status;

/* Создать поток RF и подготовить кольца приёма. Вызывать ОДИН раз из
   tx_application_define() (после vlog_init()). Контекст: инициализация.     */
void rf_init(void);

/* Разбудить поток: на линке есть данные. КОНТЕКСТ: ISR приёма UART - вызов
   неблокирующий (tx_semaphore_put), из прерывания разрешён. Поток за один
   проход обслуживает ВСЕ линки, поэтому аргумент используется только для
   счёта событий.                                                            */
void rf_notify(rf_link_id_t link);

/* Рантайм-выключатель линка (например, по статусу ST_COMMON_BIT_TURN_ON_*):
   on = 0 -> RF_LINK_OFF, поток линк не опрашивает. Контекст: поток.         */
void    rf_link_enable(rf_link_id_t link, uint8_t on);
uint8_t rf_link_enabled(rf_link_id_t link);

/* Короткое имя линка для лога ("lora"/"ble"/"lte"). Возвращает "?" на
   неизвестном id.                                                           */
const char *rf_link_name(rf_link_id_t link);

/* Передать кадр по линку (обёртка над transmit_buffer() со счётчиками).
   КОНТЕКСТ: поток (внутри блокирующая передача HAL).
   Возврат: 0 = передано, -1 = неверные аргументы, -2 = линк выключен.       */
int rf_send(rf_link_id_t link, uint8_t *data, uint16_t len);

#else  /* VECTOR_RF_THREAD == 0: модуль не собирается, вызовы становятся пустыми */

#define rf_init()                       ((void)0)
#define rf_notify(link)                 ((void)(link))
#define rf_link_enable(link, on)        ((void)(link), (void)(on))
#define rf_link_enabled(link)           ((uint8_t)0)
#define rf_link_name(link)              ((const char *)"off")
#define rf_send(link, data, len)        ((void)(link), (void)(data), (void)(len), -1)

#endif /* VECTOR_RF_THREAD */

#ifdef __cplusplus
}
#endif

#endif /* RF_THREAD_H */
