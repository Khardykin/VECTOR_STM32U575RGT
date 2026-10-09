#include "Ga_main.h"

#define CALC_CRC_CONFIG(CFG, _CRC_)						\
        {									\
          _CRC_ = 0xFFFF;							\
          for(uint16_t i=0; i < (sizeof(SNS_CFG)); i++)			\
          {									\
            _CRC_ = _CRC_ ^ ((uint8_t*)&CFG)[i];				\
            for(uint8_t k=0; k<8; k++)						\
            {									\
              if((_CRC_ & 0x0001)==1) {_CRC_ >>= 1; _CRC_ ^= 0xA001;}		\
              else {_CRC_ >>= 1;}						\
            }									\
        }									\
        }

EVENT_LOG               Event_log;                      // Структура хранения лога журнала
EVENT_LOG_TYPE_2        Event_log_type_2;               // Структура хранения лога журнала 2
EVENT_LOG				Event_log_read;

uint16_t current_build_type = 0;
uint8_t SaveConfig = 0;                 // Сохранение конфигурации
uint8_t	SaveConfigDefault = 0;          // Сохранение заводской конфигурации
uint8_t	ReadConfigDefault = 0;			// Чтение заводской конфигурации
uint8_t	SaveEventType_2 = 0;			// Сохранение лога второго типа
TYPE_LOG TypeLogEventType_2 = 0;		// Тип лога
//===========================================================================================================================
//Сохранение параметров конфигурации
static uint32_t data_save_flash[4] __attribute__((aligned(16))) = {0, 0, 0, 0};
void Internal_Flash_Write(uint32_t flash_addr, uint8_t *data, uint16_t size)
{
    HAL_FLASH_Unlock();

    // Очищаем флаги прошлых ошибок STM32U5
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGSERR | FLASH_FLAG_WRPERR);

    // Стираем страницу памяти перед записью
    uint32_t PageError;
    FLASH_EraseInitTypeDef pEraseInit;
    pEraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    pEraseInit.Page      = GetPage(flash_addr); // Ваш макрос/функция получения номера страницы
    pEraseInit.Banks     = GetBank(flash_addr);
    pEraseInit.NbPages   = 1;

    if (HAL_FLASHEx_Erase(&pEraseInit, &PageError) != HAL_OK) {
        HAL_FLASH_Lock();
        return; // Ошибка стирания
    }

    uint32_t bytes_written = 0;

    while (bytes_written < size)
    {
    	data_save_flash[0] = 0xFFFFFFFF;
    	data_save_flash[1] = 0xFFFFFFFF;
    	data_save_flash[2] = 0xFFFFFFFF;
    	data_save_flash[3] = 0xFFFFFFFF;// ПРАВИЛЬНО
//    	memcpy((uint8_t*)data_save_quad, (void*)(aligned_Address + bytes_written), 16);

        uint16_t chunk = size - bytes_written;
        if (chunk > 16) {
            chunk = 16;
        }

        memcpy((uint8_t*)data_save_flash, &data[bytes_written], chunk);

        // Записываем блок 128 бит (QUADWORD)
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, flash_addr + bytes_written, (uint32_t)data_save_flash) != HAL_OK)
        {
            break; // Ошибка записи
        }

        bytes_written += 16;
    }

    HAL_FLASH_Lock();
}

//===========================================================================================================================
void Init_Build_Type(void)
{
    // Читаем байт прямо по физическому адресу из Flash
    current_build_type = *(volatile uint16_t*)FLASH_ADDRESS_BUILD_TYPE;

    if (current_build_type == 0xFFFF) {
        current_build_type = MIRAX;
    }
}

void Save_Build_Type_Dynamic(uint16_t new_brand)
{
    // 1. ПРОВЕРКА: Читаем текущий бренд из Flash (байт 8). Если он уже равен новому, выходим!
    uint16_t current_flash_brand = *(volatile uint16_t*)FLASH_ADDRESS_BUILD_TYPE;
    if (current_flash_brand == new_brand) {
        current_build_type = new_brand; // Синхронизируем оперативную память
        return;
    }

    // 2. Создаем временный буфер в RAM на 3 слова (12 байт)
    uint32_t ram_buffer[3];

    // 3. Считываем ТЕКУЩИЕ ЖИВЫЕ калибровки напрямую из Flash-памяти (первые 2 слова / 8 байт)
    ram_buffer[0] = *(volatile uint32_t*)(FLASH_ADDRESS_CALIB_TEMP_T);
    ram_buffer[1] = *(volatile uint32_t*)(FLASH_ADDRESS_CALIB_TEMP_T + 4);

    // 4. Помещаем новый бренд в третье слово (индекс 2)
    ram_buffer[2] = (uint32_t)new_brand;

    // 5. Вызываем вашу функцию записи: она сотрет сектор страницы
    // и запишет обратно старые калибровки + наш новый бренд.
    Internal_Flash_Write(FLASH_ADDRESS_CALIB_TEMP_T, ram_buffer, 3);

    // 6. Обновляем глобальную переменную рантайма
    current_build_type = new_brand;
}

//===========================================================================================================================
void flash_write_calibration_safe(const uint32_t *calib_data_2words)
{
    uint32_t final_payload[3];

    // 1. Копируем исходные 2 слова калибровок (calib_t и calib_v)
    final_payload[0] = calib_data_2words[0];
    final_payload[1] = calib_data_2words[1];

    // 2. Считываем текущий сохраненный бренд из Flash (байт 8)
    uint8_t existing_brand = *(volatile uint16_t*)FLASH_ADDRESS_BUILD_TYPE;

    // Если Flash чистый (0xFFFF), берем бренд из активной переменной рантайма
    if (existing_brand == 0xFFFF) {
        existing_brand = current_build_type;
    }

    // 3. Дописываем бренд третьим элементом
    final_payload[2] = (uint32_t)existing_brand;

    // 4. Вызываем вашу базовую функцию на 3 слова
    Internal_Flash_Write(FLASH_ADDRESS_CALIB_TEMP_T, final_payload, 3);
}

//===========================================================================================================================
// Функция записи AppKey с проверкой изменений
#if CONFIG_LORA
void flash_write_appkey_if_changed(const uint8_t *new_appkey)
{
    // 1. Читаем текущие данные из Flash
    const uint8_t *flash_ptr = (const uint8_t *)FLASH_ADDRESS_APPKEY;
    uint8_t current_appkey[16];
    memcpy(current_appkey, flash_ptr, 16);

    // Проверяем, не стёрта ли Flash (все байты = 0xFF)
    bool flash_empty = true;
    for (int i = 0; i < 16; i++) {
        if (current_appkey[i] != 0xFF) {
            flash_empty = false;
            break;
        }
    }

    // Записываем если Flash пуста ИЛИ данные отличаются
    if (flash_empty || memcmp(current_appkey, new_appkey, 16) != 0) {
    	Internal_Flash_Write(FLASH_ADDRESS_APPKEY, (uint8_t*)new_appkey, 16);
    }
}
#endif

//===========================================================================================================================
//Сохранение параметров конфигурации
void save_param(uint8_t * RamData, uint16_t len, uint16_t addr)
{
  uint16_t CRCcalc = 0;
  SNS_CFG *cfg_ptr = (SNS_CFG*)RamData;

#if CONFIG_LORA
  flash_write_appkey_if_changed(appkey);
#endif
  cfg_ptr->CRC_CONFIG = 0; // Обнуляем перед расчетом
  CALC_CRC_CONFIG((*cfg_ptr), CRCcalc); // Считаем вашим макросом
  cfg_ptr->CRC_CONFIG = CRCcalc; // Записываем обратно
#if FLASHMK
  // Вызываем внутреннюю безопасную функцию записи во Flash
  Internal_Flash_Write(FLASH_ADDRESS_SETTINGS, RamData, len);
#else
  Save_param_memory(addr, RamData, len);
#endif
}

//===========================================================================================================================
//Чтение параметров конфигурации
uint8_t read_param(uint8_t * RamData, uint16_t len, uint16_t addr)
{
#if CONFIG_LORA
  const uint8_t *flash_ptr = (const uint8_t *)FLASH_ADDRESS_APPKEY;
  if(flash_ptr[0] != 0xFF){
	  memcpy(appkey, flash_ptr, 16);
  }
#endif
#if FLASHMK
  // Прямое чтение из адресного пространства Flash МК
  memcpy(RamData, (void*)FLASH_ADDRESS_SETTINGS, len);
#else
  Read_param_memory(addr, RamData, len);
#endif
  uint16_t CRCcalc = 0;
  uint16_t CRCrec = 0;
  SNS_CFG *cfg_ptr = (SNS_CFG*)RamData;
  CRCrec = cfg_ptr->CRC_CONFIG;

  cfg_ptr->CRC_CONFIG = 0;              // Обнуляем для проверки
  CALC_CRC_CONFIG((*cfg_ptr), CRCcalc);
  cfg_ptr->CRC_CONFIG = CRCrec;        // Возвращаем считанный из памяти CRC

  if(CRCrec != CRCcalc){
	  return 0; // Ошибка: Данные повреждены или память пуста
  }

  return 1; // Все успешно прочитано и CRC совпал
}

//===========================================================================================================================
//Сохранение текущего состояния прошивки
void SaveCurrentParamFirmware(uint16_t *status, uint16_t *size)
{
//  uint8_t data[4];
#if FLASHMK

#else
  //----------------------------------------------------------------------------
  data[0] = (*status)>>8;
  data[1] = *status;
  data[2] = (*size)>>8;
  data[3] = *size;
  //----------------------------------------------------------------------------
  //write:
  exit_ultra_deep_power_down_memory();
  Save_param_memory(reg_mem->Page_addr_fw_param, data, 4);
  ultra_deep_power_down_memory();
  //----------------------------------------------------------------------------
#endif
}

//Чтение текущего состояния прошивки
void ReadCurrentParamFirmware(uint16_t *status, uint16_t *size)
{
//  uint8_t data[4] = {0};
#if FLASHMK

#else
  //----------------------------------------------------------------------------
  exit_ultra_deep_power_down_memory();
  Read_param_memory(reg_mem->Page_addr_fw_param, data, 4);
  ultra_deep_power_down_memory();
  //----------------------------------------------------------------------------
  *status = (data[0]<<8) + data[1];
  *size = (data[2]<<8) + data[3];
  //----------------------------------------------------------------------------
#endif
}

//===========================================================================================================================
//Сохранение текущего адресса архива
static uint16_t offset_addr_log = 0;
static uint16_t offset_page_addr_log = 0;
static uint16_t offset_page_addr_log_max = 0;
//static uint32_t clear_addr_file = 0xFFFFFFFF;
static uint32_t preAddrFile = 0xFFFFFFFF;
static uint32_t curAddrFile = 0xFFFFFFFF;
void SaveCurrentAddrFile(void)
{
#if FLASHMK

#else
	if(Sns_Cfg_struct.Config_common.CurrentAddrFile >= MASK_INC_COUNT_LOG){
		Sns_Cfg_struct.Config_common.CurrentAddrFile = 0;
		Sns_Cfg_struct.Config_common.BegginAddrFile = 0;
	}
  //----------------------------------------------------------------------------
  //write:
	if(reg_mem->Type_mem == TYPE_MEM_W25){
		offset_page_addr_log_max = 16;
	}
	else if(reg_mem->Type_mem == TYPE_MEM_AT45){
		offset_page_addr_log_max = 5;
	}
		if((Sns_Cfg_struct.Config_common.CurrentAddrFile == 0) || (Sns_Cfg_struct.Config_common.CurrentAddrFile == 1)){
			 offset_addr_log = 0;
			 offset_page_addr_log = 0;
		}
		if(offset_addr_log >= 256){
			 offset_addr_log = 0;
			 offset_page_addr_log += 1;
			 if(offset_page_addr_log >= offset_page_addr_log_max){
				 offset_page_addr_log = 0;
			 }
		}

		Save_param_addr_log_memory(reg_mem->Page_addr_log + offset_page_addr_log, offset_addr_log, ((uint8_t*)&Sns_Cfg_struct.Config_common.CurrentAddrFile), 4);
//		if(reg_mem->Type_mem == TYPE_MEM_W25){
//			Save_param_addr_log_memory(reg_mem->Page_addr_log + offset_page_addr_log + 8, offset_addr_log, ((uint8_t*)&Sns_Cfg_struct.Config_common.CurrentAddrFile), 4);
//		}
		offset_addr_log += 4;

//		if(reg_mem->Type_mem == TYPE_MEM_AT45){
//			if(offset_addr_log >= 256){
//				offset_addr_log = 0;
//				offset_page_addr_log += 1;
//				if(offset_page_addr_log >= offset_page_addr_log_max){
//					offset_page_addr_log = 0;
//				}
//			}
//			Save_param_addr_log_memory(reg_mem->Page_addr_log + offset_page_addr_log, offset_addr_log, ((uint8_t*)&clear_addr_file), 4);
//		}
//	}
  //----------------------------------------------------------------------------
#endif
}

//Чтение текущего адресса архива
void ReadCurrentAddrFile(void)
{
#if FLASHMK

#else
  //----------------------------------------------------------------------------
	if(reg_mem->Type_mem == TYPE_MEM_W25){
		offset_page_addr_log_max = 16;
	}
	else if(reg_mem->Type_mem == TYPE_MEM_AT45){
		offset_page_addr_log_max = 5;
	}

	// Считаем лимит итераций, чтобы не уйти в бесконечный цикл, если память битая
	uint32_t max_entries = (offset_page_addr_log_max * 256) / 4;

	for(uint32_t i = 0; i < max_entries; i++){
		// Читаем 4 байта
		Read_param_addr_log_memory(reg_mem->Page_addr_log + offset_page_addr_log,
								   offset_addr_log, (uint8_t*)&curAddrFile, 4);

		// 2. Ключевое условие: поиск "стенки"
		if(curAddrFile == 0xFFFFFFFF){
			// Мы нашли чистую ячейку. Значит, запись остановилась на ПРЕДЫДУЩЕЙ.
			break;
		}

        // 2. Нашли старые данные (если стирание прервалось ранее)
        // Так как счетчик 32 бита и только растет, любое число меньше
        // предыдущего — это мусор из прошлого.
        if((preAddrFile != 0xFFFFFFFF) && (curAddrFile < preAddrFile)) {
            break;
        }

		preAddrFile = curAddrFile;

		// 4. Двигаем физические указатели ВМЕСТЕ с поиском
		// Это самое важное: после выхода из цикла они будут смотреть точно на FF
		offset_addr_log += 4;
		if(offset_addr_log >= 256){
			offset_addr_log = 0;
			offset_page_addr_log++;
			if(offset_page_addr_log >= offset_page_addr_log_max){
				offset_page_addr_log = 0;
			}
		}
	}

	// 5. Итог поиска
	if(preAddrFile == 0xFFFFFFFF){
		// Если вообще ничего не нашли (память пуста)
		Sns_Cfg_struct.Config_common.CurrentAddrFile = 0;
	}else{
		Sns_Cfg_struct.Config_common.CurrentAddrFile = preAddrFile;
	}

	// 6. Расчет начала архива (логический)
	if(Sns_Cfg_struct.Config_common.CurrentAddrFile > MASK_COUNT_LOG){
		Sns_Cfg_struct.Config_common.BegginAddrFile = Sns_Cfg_struct.Config_common.CurrentAddrFile - MASK_COUNT_LOG;
	}else{
		Sns_Cfg_struct.Config_common.BegginAddrFile = 0;
	}
//	}
  //----------------------------------------------------------------------------
#endif
}

//===========================================================================================================================
//Сохранение журнала событий
void save_event(SNS_CFG *pSnsCfg)
{
#if CONFIG_SAVE_PARAM_LOG
  uint16_t BegginAddrFile = (uint16_t)pSnsCfg->Config_common.BegginAddrFile&MASK_COUNT_LOG;
  uint16_t CurrentAddrFile = (uint16_t)pSnsCfg->Config_common.CurrentAddrFile&MASK_COUNT_LOG;

  Event_log.Type_log                                            = TYPE_READINGS;
  Event_log.CurrentAddrFile                                     = pSnsCfg->Config_common.CurrentAddrFile;
  Event_log.State_common                                        = pSnsCfg->Config_common.State;
  Event_log.State_commonErr                                     = pSnsCfg->Config_common.StateErr;
  Event_log.working_hours_log                                   = pSnsCfg->Config_common.working_hours;
  Event_log.battery_charge_percent                              = pSnsCfg->Config_common.battery_charge_percent;
  Event_log.temperature                                         = (int16_t)(pSnsCfg->Config_common.Temperature*100);

  for(uint8_t chan = 0; chan < COUNT_CHAN; chan++){

  }

	exit_ultra_deep_power_down_memory();                                      //выход из режима низкого потребления микросхемы памяти
	// Запись событий в архив
	save_log_memory((uint16_t)CurrentAddrFile, ((uint8_t*)&Event_log));
	if(!TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45))
	{pSnsCfg->Config_common.CurrentAddrFile++;}
	// Сохранение текущего адресса архива
	SaveCurrentAddrFile();
	ultra_deep_power_down_memory();
#endif
}

//===========================================================================================================================
//Сохранение журнала событий(второй тип)
void save_event_type_2(SNS_CFG *pSnsCfg, TYPE_LOG type_log)
{
#if CONFIG_SAVE_PARAM_LOG
  uint16_t BegginAddrFile = (uint16_t)pSnsCfg->Config_common.BegginAddrFile&MASK_COUNT_LOG;
  uint16_t CurrentAddrFile = (uint16_t)pSnsCfg->Config_common.CurrentAddrFile&MASK_COUNT_LOG;

  Event_log_type_2.Type_log                                     = type_log;
  Event_log_type_2.Firmware_version								= FIRMWARE_VERSION;
  Event_log_type_2.CurrentAddrFile                              = pSnsCfg->Config_common.CurrentAddrFile;
  Event_log_type_2.State_common                                 = pSnsCfg->Config_common.State;
  Event_log_type_2.State_commonErr                              = pSnsCfg->Config_common.StateErr;
  Event_log_type_2.working_hours_log                            = pSnsCfg->Config_common.working_hours;
  Event_log_type_2.Bump_interval                                = pSnsCfg->Config_common.Bump_interval;
  Event_log_type_2.DataLastBumpTest                             = pSnsCfg->Config_common.DataLastBumpTest;

  for(uint8_t chan = 0; chan < COUNT_CHAN; chan++){

  }

	exit_ultra_deep_power_down_memory();                                      //выход из режима низкого потребления микросхемы памяти
	// Запись событий в архив
	save_log_memory((uint16_t)CurrentAddrFile, ((uint8_t*)&Event_log_type_2));
	if(!TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45))
	{pSnsCfg->Config_common.CurrentAddrFile++;}
	// Сохранение текущего адресса архива
	SaveCurrentAddrFile();
	ultra_deep_power_down_memory();
#endif
}
//===========================================================================================================================
// Сравнение текущих параметров и сохраненных
void compare_param(SNS_CFG *pSnsCfgread, SNS_CFG *pSnsCfgCur)
{
  uint16_t type_log = 0;

  // Чтение конфигурационных параметров
  if(read_param((uint8_t*)pSnsCfgread, sizeof(SNS_CFG), reg_mem->Page_addr_param)){
    for(uint8_t chan = 0; chan < COUNT_CHAN; chan++){

    }
  }
  if(pSnsCfgread->Config_common.Archiveinterval != pSnsCfgCur->Config_common.Archiveinterval){
	  END_TIMER_RTC(TIMER_RTC_LOG);
  }
  if(type_log){
	  save_event_type_2(pSnsCfgCur, (TYPE_LOG)type_log);
  }
}
//===========================================================================================================================
//Чтение журнала событий
uint8_t read_event(uint16_t address, uint8_t * Data, uint16_t len)
{
  uint16_t addr1 = 0;
  if((uint32_t)(address + len) > (MASK_COUNT_LOG+1))
  {
	  addr1 = (MASK_COUNT_LOG+1) - address;
	  read_log_memory(address, Data, addr1*SIZE_LOG);
	  read_log_memory(0, &Data[addr1*SIZE_LOG], (len - addr1)*SIZE_LOG);
  }
  else
  {
	  read_log_memory(address, Data, len*SIZE_LOG);
  }
  return 1;
}

//===========================================================================================================================
//Чтение журнала событий для поиска
uint8_t read_event_for_search(uint16_t address, uint8_t * Data)
{
  read_log_memory(address, Data, 12);

  return 1;
}

//===========================================================================================================================
//Начальная или тестовая конфигурация
void DefaultConfig(SNS_CFG *pSnsCfg)
{
	//----------------------------------------------------------------------------
	// Зеленый светодиод
	pSnsCfg->Config_common.TimeLedStatePulse = TIME_RTC_LED_STATE_RUN_PULSE;
	pSnsCfg->Config_common.TimeLedStatePeriod = TIME_RTC_LED_STATE_RUN_PERIOD;
	//----------------------------------------------------------------------------
	// Пороги срабатывания сигшнализации батареи
	pSnsCfg->Config_common.battery_charge_volt_max = BAT_MAX_DEF;
	pSnsCfg->Config_common.battery_charge_volt_min = BAT_MIN_DEF;
	pSnsCfg->Config_common.battery_charge_volt_lim_1 = BAT_LIM_1_DEF;
	pSnsCfg->Config_common.battery_charge_volt_lim_2 = BAT_LIM_2_DEF;
	//----------------------------------------------------------------------------
	pSnsCfg->Config_common.TimeLimit = 0;
	pSnsCfg->Config_common.TimeLimitRST = 0;
	//----------------------------------------------------------------------------
	//Инициализация текущего положения, начального положения и включение записи логов
	pSnsCfg->Config_common.CurrentAddrFile                                = 0;
	pSnsCfg->Config_common.BegginAddrFile                                 = 0;
	pSnsCfg->Config_common.ArchiveRecording                               = 0;
	pSnsCfg->Config_common.Archiveinterval                                = 0x0500|0x0005;// 1s / 1m
	//----------------------------------------------------------------------------
	// Конфигурация lcd
	pSnsCfg->Config_common.Type_lcd = CONFIG_LCD_VER;
	  //----------------------------------------------------------------------------
	  // Настройка Lora
	#if CONFIG_LORA
	  Lora_Cfg_ApplyDefaults(&Sns_Cfg_struct);
	  pSnsCfg->Config_common.PeriodTimeLora |= TIME_RTC_LORA_DATA_SET;			// Период сообщений
	  pSnsCfg->Config_common.PeriodTimeLora |= (TIME_RTC_LORA_DATA_SET<<8)/3;	// Период аварийный сообщ
	#endif
}
                   
