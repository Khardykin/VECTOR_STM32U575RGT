/**
  ******************************************************************************
  * @file    vector_tasks.h
  * @brief   Потоки прибора: receiver_task (приём/парсинг UART) и measure_task
  *          (периодика прибора -> Vector_Run_Measure() -> модули BLE/LoRa/LTE)
  *
  *          Структура - как в Avis_main.c на другом приборе, только вместо
  *          FreeRTOS здесь ThreadX/Azure RTOS:
  *
  *            receiver_task_function()  приём и парсинг по КАЖДОМУ UART:
  *                                        command_message()  - COM/терминал
  *                                        Lora_Receive()     - LoRa S7678S
  *                                        Ble_Receive()      - BLE-модуль
  *                                        Lte_Receive()      - LTE-модем
  *                                        Gps_Receive()      - GPS/GNSS (Gps.c)
  *                                        Uart_Channel_Receive() - сенсоры
  *                                        Vector_Options_System() - сервис
  *            measure_task_function()   периодика (1 с):
  *                                        Vector_Run_Measure()
  *                                          -> измерения прибора (наполнить)
  *                                          -> Ble_Run(); Lora_Run(); Lte_Run(); Gps_Run();
  *                                        Vector_RunFlashMemory()
  *
  *          Отличия API ThreadX от FreeRTOS, которые надо держать в голове при
  *          переносе кода с того прибора:
  *            void task(void *pvParameters)  ->  void task(ULONG thread_input)
  *            vTaskDelay(N) [тики FreeRTOS]  ->  VTICK_SLEEP_MS(ms) (тик 10 мс)
  *            xTaskCreate(..., usStackDepth) [слова] -> tx_thread_create(...,
  *                                             stack_size) [БАЙТЫ]
  *            xSemaphoreTake/Give            ->  tx_mutex_get/put
  *            Приоритеты: в FreeRTOS больше = ниже приоритет, в ThreadX
  *            наоборот (0 - самый высокий). Audio Player = 10, LVGL = 15.
  *
  *          ВРЕМЯ: только тик RTOS через vector_tick.h (VTICK_MS/VTICK_SLEEP_MS),
  *          HAL_GetTick/HAL_Delay в модули не тащим. Таймеры прибора
  *          (TIMER_RTC_* из shared_types.h) сюда ещё не перенесены - см. PLAN.md.
  ******************************************************************************
  */
#ifndef VECTOR_TASKS_H
#define VECTOR_TASKS_H

#include <stdint.h>
#include "vector_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if VECTOR_TASKS_ENABLE

/* Модули, которые обслуживают потоки. Порядок = индекс в tasks_status.mod[]. */
typedef enum
{
  TASK_MOD_COM = 0,    /* COM/терминал (UART4): command_message()            */
  TASK_MOD_LORA,       /* LoRa S7678S                                        */
  TASK_MOD_BLE,        /* BLE-модуль                                         */
  TASK_MOD_LTE,        /* сотовый модем LTE/GSM                              */
  TASK_MOD_GPS,        /* GPS/GNSS-модуль (Gps.c)                            */
  TASK_MOD_SENSOR,     /* сенсорный UART (каналы измерения)                  */
  TASK_MOD_COUNT
} task_module_t;

/* Счётчики ОДНОГО модуля: что вызывали, сколько приняли/передали, когда. */
typedef struct
{
  volatile uint32_t cnt_receive_calls;  /* вызовов *_Receive() из потока приёма */
  volatile uint32_t cnt_run_calls;      /* вызовов *_Run() из потока измерений  */
  volatile uint32_t cnt_rx_bytes;       /* байт вынуто из кольца приёма         */
  volatile uint32_t cnt_rx_frames;      /* кадров/строк разобрано               */
  volatile uint32_t cnt_tx_frames;      /* кадров передано                      */
  volatile uint32_t cnt_errors;         /* ошибок обмена/парсинга               */
  volatile uint32_t last_receive_ms;    /* когда последний раз принимали        */
  volatile uint32_t last_run_ms;        /* когда последний раз вызывали Run     */
} task_module_status_t;

/* ЕДИНОЕ состояние потоков прибора (как audio_status в плеере): в
   Expressions/Live Watch достаточно одной строки `tasks_status`.             */
typedef struct
{
  volatile uint8_t  receiver_running;    /* поток приёма создан и крутится     */
  volatile uint8_t  measure_running;     /* поток измерений создан и крутится  */
  volatile uint32_t cnt_receiver_passes; /* проходов цикла потока приёма       */
  volatile uint32_t cnt_measure_passes;  /* проходов цикла потока измерений    */
  volatile uint32_t cnt_measure_runs;    /* сколько раз отработал Run_Measure  */
  volatile uint32_t cnt_flash_runs;      /* сколько раз отработал RunFlashMemory*/
  volatile uint32_t cnt_pre_init;        /* однократная инициализация выполнена*/
  volatile uint32_t last_measure_ms;     /* метка последнего Run_Measure       */
  task_module_status_t mod[TASK_MOD_COUNT];
} tasks_status_t;

extern volatile tasks_status_t tasks_status;

/* Создать потоки receiver_task и measure_task. Вызывать ОДИН раз из
   tx_application_define() (в проекте - рядом с audio_init()).
   Контекст: инициализация, до старта планировщика.                          */
void vector_tasks_init(void);

/* --- наполнение прибора (аналоги Avis_Run_* из Avis_main.c) -----------------
 * Vector_Run_Pre_Init()    - однократно при старте потока измерений: включить
 *                            модули, прочитать конфиг из внешней flash.
 * Vector_Run_Measure()     - периодика 1 с: измерения + вызов модулей обмена
 *                            (Ble_Run/Lora_Run/Lte_Run).
 * Vector_RunFlashMemory()  - журнал/конфиг во внешней flash (extstore).
 * Vector_Options_System()  - сервис: мягкая перезагрузка, стирание, окончание
 *                            режима обмена данными.                          */
void Vector_Run_Pre_Init(void);
void Vector_Run_Measure(void);
void Vector_RunFlashMemory(void);
void Vector_Options_System(void);

/* Реакция на серию ошибок BME280 (в Avis - Avis_Search_Temp_Start):
   переинициализация/поиск температурного датчика. Зовётся из bme280_com.c,
   weak-заглушка определена в Vector_main.c. data - код причины (2 = ошибка
   инициализации, 0 = 10 незавершённых измерений подряд).                 */
void Vector_Search_Temp_Start(uint16_t data);

/* --- МОДУЛИ: приём (поток receiver_task) ------------------------------------
 * Слабые (__attribute__((weak))) заглушки в Vector_main.c: когда перенесёте
 * свою реализацию в файл модуля, она автоматически заменит заглушку, править
 * Vector_main.c не придётся. Lora_Receive() уже есть в Lora_S7678S.c - она
 * вызывается напрямую под #if CONFIG_LORA.                                    */
void command_message(void);        /* COM/UART4: разбор команд обмена         */
void Ble_Receive(void);            /* USART3: InputBuffer[TYPE_BLE] -> парсер */
void Lte_Receive(void);            /* модем LTE: InputBuffer[TYPE_LTE] -> парсер */
void Uart_Channel_Receive(void);   /* сенсорный UART: InputBuffer[TYPE_SENSOR] */

/* --- МОДУЛИ: обмен (поток measure_task, из Vector_Run_Measure) -------------- */
void Ble_Run(void);
void Lora_Run(void);
void Lte_Run(void);
void Gps_Run(void);   /* Gps_Receive() объявлен в Gps.h (под CONFIG_GPS) */

/* Короткое имя модуля для лога ("com"/"lora"/"ble"/"lte"/"sensor"), "?" если
   id вне диапазона. Возвращает статическую строку, не освобождать.           */
const char *task_module_name(task_module_t mod);

#else  /* VECTOR_TASKS_ENABLE == 0: потоков прибора нет, вызовы пустые */

#define vector_tasks_init()        ((void)0)
#define Vector_Run_Pre_Init()      ((void)0)
#define Vector_Run_Measure()       ((void)0)
#define Vector_RunFlashMemory()    ((void)0)
#define Vector_Options_System()    ((void)0)

#endif /* VECTOR_TASKS_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_TASKS_H */
