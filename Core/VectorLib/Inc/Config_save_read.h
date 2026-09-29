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

/* Запись журнала событий - то, что save_event() кладёт в log_append().
   Поля берутся из SNS_CFG и sensors_status на момент записи. Reserve - под
   каналы измерения, когда будут перенесены (как в Avis).                    */
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
	int16_t  temperature;			// Temperature * 100
	uint16_t Reserve[8];
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
// Журнал событий (внешняя flash, log_append). save_event - немедленная запись;
// отложенную делает Vector_RunFlashMemory() по флагам SaveEvent/TypeLogEvent.
extern int      save_event(SNS_CFG *pSnsCfg, TYPE_LOG type_log);
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
