/**
  ******************************************************************************
  * @file    Ble_vector.h
  * @brief   BLE: ОТВЕТНАЯ сторона протокола Avis (v26) - мы периферийное
  *          устройство, мастер (Avis) задаёт вопросы по радио
  *
  *          Направление ОБРАТНОЕ относительно Ble_mk01.c (docs/): там MCU
  *          был мастером своего модуля и пушил данные (AT+ADVMSDDATASET=);
  *          здесь мастер - Avis: он шлёт нам запрос AT+ADVMSDDATAGET, а мы
  *          ОТВЕЧАЕМ своей структурой; его AT+ADVMSDDATASET=<hex> несёт
  *          данные Avis-прибора и раскладывается в Connected_device
  *          (SNS_Dev_Type, shared_types.h). Протокол будет дополняться
  *          (автор) - команды добавляются в таблицу ble_cmds[] в .c.
  *
  *          ФОРМАТ ЛИНИИ (как в Ble_mk01): "AT" | "AT+<CMD>[=<payload>]",
  *          терминатор 0x0D; payload данных - hex-строка "%02x" побайтно.
  *          НАШ ОТВЕТ (предварительно, до уточнения автора):
  *            команда без данных -> "OK" + 0x0D  (или "ERROR" + 0x0D);
  *            команда с данными  -> "<payload>" + 0x0D, затем "OK" + 0x0D.
  *
  *          ТРАНСПОРТ: USART3 (PC4/PC5, 115200 8N1) -> ISR кладёт байты в
  *          InputBuffer[TYPE_BLE] (stm32u5xx_it.c, под CONFIG_BLE) ->
  *          Ble_Receive() в Receiver Task (каждые 10 мс, НЕБЛОКИРУЮЩЕ:
  *          побайтовая машина строки, ответ сразу из того же прохода) ->
  *          transmit_buffer(..., TYPE_BLE). Ble_Run() (Measure Task, 1 с) -
  *          таймаут незавершённой строки и хаускипинг. Счётчики - ble_status.
  *
  *          Модуль 컴илируется под CONFIG_BLE (config_device.h) и замещает
  *          weak-заглушки Ble_Receive()/Ble_Run() из Vector_main.c без его
  *          правки. До проверки на живом модуле CONFIG_BLE = 0.
  ******************************************************************************
  */
#ifndef BLE_VECTOR_H
#define BLE_VECTOR_H

#include <stdint.h>
#include "config_device.h"
#include "shared_types.h"    /* SNS_Dev_Type (Connected_device), COUNT_CHAN */

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_BLE

/* --- формат данных стороны Avis (приём) ------------------------------------
 * Повторяет str_ble_data_set_t из Ble_mk01.h (docs/) байт-в-байт: мастер
 * заполняет её на своей стороне и шлёт hex-строкой в AT+ADVMSDDATASET=.
 * COUNT_CHAN_BLE = 7 (их каналы); у нас COUNT_CHAN = 10 - раскладка в
 * Connected_device идёт по минимуму из двух.                                */
#define DATA_STRUCT_VER     (2u)
#define COUNT_CHAN_BLE      (7u)

typedef struct
{
  uint8_t Type_gas;          /* номер формулы газа (num_formula_gas)          */
  uint8_t Concentration[2];  /* концентрация, LE u16 (raw; дискретность на
                                стороне мастера - TODO: уточнить в протоколе) */
  uint8_t State_channel;     /* ошибки/пороги канала (StateErr & 0x7F)        */
  uint8_t Setting_channel;   /* настройки канала (MEASURE_MASK|MEASURE_DISK)  */
} sensor_data_t;

typedef struct
{
  uint8_t data_struct_ver;   /* = DATA_STRUCT_VER                             */
  uint8_t dev_id;            /* DIVICE_ID_BLE мастера (1/2 в Avis)            */
  uint8_t dev_hw_ver;
  uint8_t dev_fw_ver;
  uint8_t charge;            /* заряд батареи мастера, %                      */
  uint8_t sensors_enabled:5; /* число заполненных sensors[]                   */
  uint8_t rfu:3;
  sensor_data_t sensors[COUNT_CHAN_BLE];
} str_ble_data_set_t;

#define SIZE_STR_SENSOR_DATA   (sizeof(sensor_data_t))
#define SIZE_STR_BLE_DATA_SET  (sizeof(str_ble_data_set_t) - (SIZE_STR_SENSOR_DATA * COUNT_CHAN_BLE))

/* --- формат НАШИХ данных (ответ на ADVMSDDATAGET) ----------------------
 * Заголовок байт-в-байт как у Avis (их парсер примет), наполнение - наше;
 * расширения протокола (T/H/P, ориентация, время...) - по согласованию с
 * автором, добавляются ПОСЛЕ базового заголовка с bump'ом data_struct_ver.  */
typedef struct
{
  uint8_t data_struct_ver;   /* = DATA_STRUCT_VER                             */
  uint8_t dev_id;            /* = VECTOR_BLE_DEV_ID (задаёт автор)            */
  uint8_t dev_hw_ver;        /* Sns_Cfg_struct.Config_common.HardwareVersion  */
  uint8_t dev_fw_ver;        /* FIRMWARE_VERSION                              */
  uint8_t charge;            /* sensors_status.battery_charge_percent         */
  uint8_t sensors_enabled:5; /* 0 - каналов измерения пока нет (SNS_Dev_Type
                                в резерве); когда появятся - заполним         */
  uint8_t rfu:3;
  /* TODO(автор): дополнения протокола - объявить здесь и в ble_send_dataset() */
} str_ble_vector_data_t;

/* --- идентификаторы команд (таблица ble_cmds[] в .c) ----------------------- */
typedef enum
{
  BLE_CMD_NONE = 0,
  BLE_CMD_AT,               /* "AT" - пинг                                    */
  BLE_CMD_ADVMSDDATAGET,    /* отдать наши данные (ответ hex+OK)              */
  BLE_CMD_ADVMSDDATASET,    /* принять данные мастера -> Connected_device     */
  BLE_CMD_MODINFO,          /* "AT+MODINFO?" - информация о нашем модуле/MCU  */
  BLE_CMD_OTHER             /* распознано как AT+..., но не в таблице         */
} ble_cmd_t;

/* ЕДИНОЕ состояние модуля (стиль проекта: одна volatile-структура в
   Expressions - `ble_status`).                                              */
typedef struct
{
  volatile uint16_t busy_line;        /* длина набираемой строки (0 = ждём;
                                         u16: VECTOR_BLE_MAX_LINE > 255)      */
  volatile uint8_t  last_cmd;         /* ble_cmd_t последней команды          */
  volatile uint32_t cnt_rx_lines;     /* принято полных строк                 */
  volatile uint32_t cnt_tx_ok;        /* ответов "OK"                         */
  volatile uint32_t cnt_tx_error;     /* ответов "ERROR"                      */
  volatile uint32_t cnt_unknown_cmd;  /* AT+... вне таблицы                   */
  volatile uint32_t cnt_decode_fail;  /* hex-payload не декодировался         */
  volatile uint32_t cnt_frame_tmo;    /* строка сброшена по таймауту тишины   */
  volatile uint32_t cnt_datasets;     /* успешно принято DATASET -> Connected */
  volatile uint32_t last_rx_ms;       /* метка последнего байта (VTICK_MS)    */
} ble_vector_status_t;

extern volatile ble_vector_status_t ble_status;
extern str_ble_data_set_t  ble_rx_data_set;   /* последний принятый набор мастера */

/* Приём: побайтовая машина строки из InputBuffer[TYPE_BLE] + немедленный
   ответ. КОНТЕКСТ: Receiver Task (каждый проход, ~10 мс), НЕ блокируется.   */
void Ble_Receive(void);

/* Периодика: таймаут незавершённой строки (VECTOR_BLE_FRAME_TMO_MS тишины),
   счётчики. КОНТЕКСТ: Measure Task (1 с).                                   */
void Ble_Run(void);

/* Разобрать принятый набор мастера в Connected_device (SNS_Dev_Type).
   Возврат: число разложенных каналов. КОНТЕКСТ: Receiver Task.              */
uint8_t Ble_ApplyConnectedDevice(const str_ble_data_set_t *set);

#endif /* CONFIG_BLE */

#ifdef __cplusplus
}
#endif

#endif /* BLE_VECTOR_H */
