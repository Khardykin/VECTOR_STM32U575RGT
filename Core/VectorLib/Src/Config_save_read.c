/*
 * Config_save_read.c - конфигурация прибора и журнал событий (v21).
 */
#include "Vector_main.h"          /* HAL, SNS_CFG, макросы статусов/таймеров, лог */
#include "extstore.h"             /* cfg_save/cfg_load/log_append - внешняя flash */
#include "vector_log.h"           /* LOG_E/LOG_I */
#include "Config_save_read.h"
#include <string.h>

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
// todo то записать водну структуру
uint8_t SaveConfig = 0;                 // Сохранение конфигурации
uint8_t	SaveConfigDefault = 0;          // Сохранение заводской конфигурации
uint8_t	ReadConfigDefault = 0;		// Чтение заводской конфигурации
uint8_t	SaveEvent = 0;			// Отложенная запись события в журнал

TYPE_LOG TypeLogEvent = TYPE_TURN_ON;	// Тип отложенного события

//===========================================================================================================================
// Банк и страница
static uint32_t GetBank(uint32_t addr)
{
	return (addr < 0x08080000u) ? FLASH_BANK_1 : FLASH_BANK_2;
}

static uint32_t GetPage(uint32_t addr)
{
	uint32_t base = (addr < 0x08080000u) ? 0x08000000u : 0x08080000u;
	return (addr - base) / FLASH_PAGE_SIZE;
}

//===========================================================================================================================
// Сохранение параметров конфигурации (внешняя flash, extstore cfg_save)
void save_param(uint8_t * RamData, uint16_t len, uint16_t addr)
{
  (void)addr;   // адресацию слотов ведёт extstore
  uint16_t CRCcalc = 0;
  SNS_CFG *cfg_ptr = (SNS_CFG*)RamData;

  cfg_ptr->CRC_CONFIG = 0; // Обнуляем перед расчетом
  CALC_CRC_CONFIG((*cfg_ptr), CRCcalc);
  cfg_ptr->CRC_CONFIG = CRCcalc; // Записываем обратно

  if (cfg_save(RamData, len) != 0)
  {
	  LOG_E(VLOG_M_SYS, "config: cfg_save FAIL (ext flash)");
  }
}

//===========================================================================================================================
// Чтение параметров конфигурации: 1 = прочитано и CRC верен, 0 = пусто/бито
uint8_t read_param(uint8_t * RamData, uint16_t len, uint16_t addr)
{
  (void)addr;

  uint16_t out_len = 0;
  if ((cfg_load(RamData, len, &out_len) != 0) || (out_len != len))
  {
	  return 0; // конфига ещё нет ИЛИ размер от другой версии прошивки
  }

  uint16_t CRCcalc = 0;
  uint16_t CRCrec = 0;
  SNS_CFG *cfg_ptr = (SNS_CFG*)RamData;
  CRCrec = cfg_ptr->CRC_CONFIG;

  cfg_ptr->CRC_CONFIG = 0;              // Обнуляем для проверки
  CALC_CRC_CONFIG((*cfg_ptr), CRCcalc);
  cfg_ptr->CRC_CONFIG = CRCrec;         // Возвращаем считанный из памяти CRC

  if(CRCrec != CRCcalc){
	  return 0; // Ошибка: Данные повреждены
  }

  return 1; // Все успешно прочитано и CRC совпал
}

//===========================================================================================================================
// Сохранение журнала событий (внешняя flash, кольцевая область LOG)
void save_event(SNS_CFG *pSnsCfg, TYPE_LOG type_log)
{
#if CONFIG_SAVE_PARAM_LOG
  Event_log.Type_log                                            = (uint16_t)type_log;
  Event_log.Firmware_version                                    = FIRMWARE_VERSION;
  Event_log.CurrentAddrFile                                     = pSnsCfg->Config_common.CurrentAddrFile;
  Event_log.State                                               = pSnsCfg->Config_common.State;
  Event_log.StateErr                                            = pSnsCfg->Config_common.StateErr;
  Event_log.working_hours                                       = pSnsCfg->Config_common.working_hours_offset
                                                                + pSnsCfg->Config_common.working_hours;
  Event_log.battery_charge_percent                              = pSnsCfg->Config_common.battery_charge_percent;
  Event_log.battery_charge_volt                                 = pSnsCfg->Config_common.battery_charge_volt;
  Event_log.temperature                                         = (int16_t)(pSnsCfg->Config_common.Temperature*100);

  if (log_append(&Event_log, sizeof(Event_log)) != 0)
  {
	  LOG_E(VLOG_M_SYS, "log: append FAIL");
	  return;
  }
  pSnsCfg->Config_common.CurrentAddrFile++;
#else
  (void)pSnsCfg; (void)type_log;
#endif
}

//===========================================================================================================================
// Сравнение текущих параметров и сохранённых (после приёма новых по COM/BLE)
void compare_param(SNS_CFG *pSnsCfgread, SNS_CFG *pSnsCfgCur)
{
  uint16_t type_log = 0;

  // Чтение конфигурационных параметров
  if(read_param((uint8_t*)pSnsCfgread, sizeof(SNS_CFG), 0)){
    if(pSnsCfgread->Config_common.Archiveinterval != pSnsCfgCur->Config_common.Archiveinterval){
  	  END_TIMER_RTC(TIMER_RTC_LOG);   // форсируем запись архива с новым периодом
  	  type_log |= TYPE_LOG_PARAM_UPDATE_0;
    }
    /* TODO: остальные параметры по битам TYPE_LOG_PARAM_UPDATE_x - когда
       перенесётся COM-модуль (command_message) и каналы измерения.         */
  }
  if(type_log){
	  save_event(pSnsCfgCur, (TYPE_LOG)type_log);
  }
}

//===========================================================================================================================
// Начальная или тестовая конфигурация
void DefaultConfig(SNS_CFG *pSnsCfg)
{
	//----------------------------------------------------------------------------
	// Зеленый светодиод
	pSnsCfg->Config_common.TimeLedStatePulse = TIME_RTC_LED_STATE_RUN_PULSE;
	pSnsCfg->Config_common.TimeLedStatePeriod = TIME_RTC_LED_STATE_RUN_PERIOD;
	//----------------------------------------------------------------------------
	// Пороги срабатывания сигнализации батареи, мВ (Li-ion)
	pSnsCfg->Config_common.battery_charge_volt_max = BAT_MAX_DEF;
	pSnsCfg->Config_common.battery_charge_volt_min = BAT_MIN_DEF;
	pSnsCfg->Config_common.battery_charge_volt_lim_1 = BAT_LIM_1_DEF;
	pSnsCfg->Config_common.battery_charge_volt_lim_2 = BAT_LIM_2_DEF;
	//----------------------------------------------------------------------------
	pSnsCfg->Config_common.TimeLimit = 0;
	pSnsCfg->Config_common.TimeLimitRST = 0;
	//----------------------------------------------------------------------------
	// Инициализация текущего положения, начального положения и запись логов
	pSnsCfg->Config_common.CurrentAddrFile                                = 0;
	pSnsCfg->Config_common.BegginAddrFile                                 = 0;
	pSnsCfg->Config_common.ArchiveRecording                               = 0;
	pSnsCfg->Config_common.Archiveinterval                                = 0x0500|0x0005;// (мин<<8)|с - как в Avis
	//----------------------------------------------------------------------------
	// Конфигурация lcd
	pSnsCfg->Config_common.Type_lcd = CONFIG_MODEL_LCD;
	//----------------------------------------------------------------------------
	// Настройка Lora
#if CONFIG_LORA
	Lora_Cfg_ApplyDefaults(pSnsCfg);
	pSnsCfg->Config_common.PeriodTimeLora |= TIME_RTC_LORA_DATA_SET;			// Период сообщений
	pSnsCfg->Config_common.PeriodTimeLora |= (TIME_RTC_LORA_DATA_SET<<8)/3;	// Период аварийный сообщ
#endif
}
