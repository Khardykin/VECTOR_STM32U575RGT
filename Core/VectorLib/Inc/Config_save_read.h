/*
 * Config_save_read.h
 */
#ifndef CONFIG_SAVE_READ_H
#define CONFIG_SAVE_READ_H

#include <stdint.h>
#include "shared_types.h"   /* SNS_CFG */
#include "extstore.h"       /* EXT_BUSY - коды возврата save_param/save_event */

#ifdef __cplusplus
extern "C" {
#endif

// Тип лога
typedef enum {
	TYPE_TURN_ON = 0x00,
	TYPE_TURN_OFF = 0x01,
	TYPE_FW_UPDATE = 0x02,
	TYPE_READINGS = 0x04,
	TYPE_LOG_PARAM_NOP = 0x08,
}TYPE_LOG;

/* Запись журнала событий = СЛОТ кольца LOG фиксированной длины 64 байта
   (схема автора, v25: длина одинаковая у всех записей, поэтому per-record
   заголовок LOG_REC_HDR не нужен; assert sizeof == 64 в Config_save_read.c).
   Поля берутся из SNS_CFG и sensors_status на момент записи; working_hours -
   ВРЕМЯ записи (ключ поиска log_find_by_time). Reserve - под каналы
   измерения, когда будут перенесены (как в Avis).                           */
typedef struct
{
	uint16_t Type_log;				// TYPE_LOG
	uint16_t Firmware_version;		// FIRMWARE_VERSION (config_device.h)
	uint32_t CurrentAddrFile;		// номер записи (логический счётчик журнала)
	uint32_t State;					// Config_common.State
	uint32_t StateErr;				// Config_common.StateErr
	uint32_t working_hours;			// суммарное время, с (offset + working_hours конфига, v24)
	uint16_t battery_charge_percent;
	uint16_t battery_charge_volt;	// мВ
	int16_t  temperature;			// sensors_status.Temperature * 100
	uint16_t Reserve[19];			// добивает слот до РОВНО 64 байт (v25)
}EVENT_LOG;

//===========================================================================================================================
// Коды возврата save_param/save_event: 0 = записано, EXT_BUSY (-2) = шина
// занята или стирание отложено (звучит звук) - вызывающий НЕ гасит флаг и
// повторяет позже, -1 = ошибка flash (LOG_E уже напечатан).
// addr - область внешней flash (sfmap.h): SF_CONFIG_BASE = рабочий конфиг
// (SaveConfig, загрузка при старте), SF_CONFIG_DEFAULT = заводской конфиг
// (SaveConfigDefault записывает туда текущий; ReadConfigDefault - СБРОС К
// ЗАВОДСКИМ: читает область через буфер Cfg_structdef_read и копирует его
// в рабочий Sns_Cfg_struct, v23).
extern int      save_param(uint8_t * RamData, uint16_t len, uint32_t addr);     // Сохранение параметров
extern uint8_t  read_param(uint8_t * RamData, uint16_t len, uint32_t addr);     // Чтение параметров (1 = CRC верен)
//--------------------------------------------------------------------------------------------------------------
// КОЛЬЦЕВОЙ ЖУРНАЛ (схема автора, v25): область LOG (sfmap.h) поделена на
// слоты по 64 байта (= sizeof(EVENT_LOG), assert в .c); кольцо 0x8000 (32768)
// слотов, позиция в памяти = указатель & LOG_RING_MASK (0x7FFF). Указатели -
// в SNS_CFG.Config_archive: CurrentAddrFile (куда писать следующую) и
// BegginAddrFile (самая старая доступная), оба монотонно растут и
// сохраняются вместе с конфигом. Переполнение: сектор под новую запись
// стирается, BegginAddrFile сдвигается на число записей в секторе (64 -
// чётное, сектор делится нацело). Ёмкость кольца = 32768 - 64 = 32704 записи.
// save_event - запись слота; отложенную делает Vector_RunFlashMemory() по
// флагам SaveEvent/TypeLogEvent и по архивному таймеру (TIMER_RTC_LOG).
extern int      save_event(SNS_CFG *pSnsCfg, TYPE_LOG type_log);
// Чтение записи ПО НОМЕРУ УКАЗАТЕЛЯ: ptr в диапазоне [BegginAddrFile,
// CurrentAddrFile). Возврат: 0 = ok, 1 = ptr вне доступного диапазона,
// -1 = ошибка шины (EXT_BUSY транзитом). КОНТЕКСТ: поток.
extern int      log_read(SNS_CFG *pSnsCfg, uint32_t ptr, EVENT_LOG *out);
// Поиск ближайших записей ПО ДИАПАЗОНУ ВРЕМЕНИ (Event_log.working_hours, с;
// журнал упорядочен по времени -> бинпоиск): *ptr_from = первая запись с
// временем >= t_from (если таких нет - последняя), *ptr_to = последняя с
// временем <= t_to (если таких нет - первая). Возврат: 0 = диапазон найден
// (ptr_from <= ptr_to), 1 = точного попадания нет (указатели = ближайшие),
// 2 = журнал пуст, -1 = ошибка шины (EXT_BUSY транзитом). КОНТЕКСТ: поток.
extern int      log_find_by_time(SNS_CFG *pSnsCfg, uint32_t t_from, uint32_t t_to,
                                 uint32_t *ptr_from, uint32_t *ptr_to);
//--------------------------------------------------------------------------------------------------------------
// Сравнение текущего конфига с сохранённым (после приёма новых параметров)
extern void     compare_param(SNS_CFG *pSnsCfgread, SNS_CFG *pSnsCfgCur);
//--------------------------------------------------------------------------------------------------------------
extern void     DefaultConfig(SNS_CFG *pSnsCfg);                                // Начальная или тестовая конфигурация
//===========================================================================================================================
extern uint8_t  SaveConfig;              	// 1 = сохранить конфигурацию (снимает Vector_RunFlashMemory)
extern uint8_t  SaveConfigDefault;       	// 1 = записать ТЕКУЩИЙ конфиг в заводскую область SF_CONFIG_DEFAULT (v23)
extern uint8_t  ReadConfigDefault;		// 1 = СБРОС К ЗАВОДСКИМ: прочитать рабочий Sns_Cfg_struct из области SF_CONFIG_DEFAULT (v23)
extern uint8_t	SaveEvent;			// 1 = отложенная запись события в журнал
extern TYPE_LOG	TypeLogEvent;			// тип отложенного события
//===========================================================================================================================
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_SAVE_READ_H */
