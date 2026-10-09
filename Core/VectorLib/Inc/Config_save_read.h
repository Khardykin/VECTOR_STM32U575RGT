#ifndef CONFIG_SAVE_READ_H
#define CONFIG_SAVE_READ_H

#ifdef __cplusplus
extern "C" {
#endif

#define FLASHMK (1)

#if FLASHMK
#define FLASH_ADDRESS_SETTINGS      (0x0803C000)
#endif

// Тип лога 2
typedef enum {
	TYPE_TURN_ON = 0x00,
	TYPE_TURN_OFF = 0x01,
	TYPE_FW_UPDATE = 0x02,
	TYPE_READINGS = 0x04,
	TYPE_LOG_PARAM_NOP = 0x08,
	TYPE_LOG_PARAM_UPDATE_0 = 0x10,
	TYPE_LOG_PARAM_UPDATE_1 = 0x20,
	TYPE_LOG_PARAM_UPDATE_2 = 0x40,
	TYPE_LOG_PARAM_UPDATE_3 = 0x80,
	TYPE_LOG_PARAM_UPDATE_4 = 0x100,
	TYPE_LOG_PARAM_UPDATE_5 = 0x200,
	TYPE_LOG_PARAM_UPDATE_6 = 0x400,
	TYPE_LOG_PARAM_UPDATE_7 = 0x800,
	TYPE_LOG_PARAM_UPDATE_8 = 0x1000,
	TYPE_LOG_PARAM_UPDATE_9 = 0x2000,
}TYPE_LOG;

#define MASK_COUNT_LOG 		(0x7FFF)//Объем хранения логов(количество логов
#define MASK_INC_COUNT_LOG 	(2000000)//Объем хранения логов(количество inc логов
//===========================================================================================================================
extern void 	Internal_Flash_Write(uint32_t flash_addr, uint8_t *data, uint16_t size);
//===========================================================================================================================
extern void 	Init_Build_Type(void);
extern void 	Save_Build_Type_Dynamic(uint16_t new_brand);
//===========================================================================================================================
extern void 	flash_write_appkey_if_changed(const uint8_t *new_appkey);
extern void 	flash_write_calibration_safe(const uint32_t *calib_data_2words);
//===========================================================================================================================
extern void     save_param(uint8_t * RamData, uint16_t len, uint16_t addr);     // Сохранение параметров
extern uint8_t  read_param(uint8_t * RamData, uint16_t len, uint16_t addr);     // Чтение параметров
//--------------------------------------------------------------------------------------------------------------
extern void     SaveCurrentAddrFile(void);
extern void     ReadCurrentAddrFile(void);
extern void 	ReadAddrFileAll(void);
extern uint8_t  read_event(uint16_t address, uint8_t * Data, uint16_t len);     // Чтение журнала событий
extern uint8_t 	read_event_for_search(uint16_t address, uint8_t * Data);		//Чтение журнала событий для поиска
extern void     save_event(SNS_CFG *pSnsCfg);                                               // Сохранение журнала событий
extern void     save_event_type_2(SNS_CFG *pSnsCfg, TYPE_LOG type_log);
//--------------------------------------------------------------------------------------------------------------
extern void     SaveCurrentParamFirmware(uint16_t *status, uint16_t *size);
extern void     ReadCurrentParamFirmware(uint16_t *status, uint16_t *size);
//--------------------------------------------------------------------------------------------------------------
extern void     DefaultConfig(SNS_CFG *pSnsCfg);                                // Начальная или тестовая конфигурация
//===========================================================================================================================  
extern uint16_t	current_build_type;
extern uint8_t  SaveConfig;               	// Сохранение конфигурации
extern uint8_t  SaveConfigDefault;        	// Сохранение заводской конфигурации
extern uint8_t  ReadConfigDefault;			// Чтение заводской конфигурации
extern uint8_t	SaveEventType_2;
extern TYPE_LOG	TypeLogEventType_2;
//===========================================================================================================================
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_SAVE_READ_H */
