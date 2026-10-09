#ifndef __SHARED_TYPES_H
#define __SHARED_TYPES_H

//===========================================================================================================================
//Таймера в 1mS
enum{
	TIMER_MAIN_SCREEN,                              // Таймер возврата на главный экран
	TIMER_SHUTDOWN,                                 // Таймер выключения прибора
	TIMER_CHARGE_SOUND_VIBRO,						// Таймер включение звука и вибро при зарядке
	TIMER_END,
};
//===========================================================================================================================
//Таймера в 1S
enum{
	TIMER_RTC_LOG = 0,	                        	// Таймер на старта записи лога
	TIMER_RTC_UART_RX_WAKE_UP,                      // Таймер на вход в сон, когда передача не активна, но включена
	TIMER_RTC_MAIN_RESET,                           // Таймер возврата на главный экран
	TIMER_RTC_WARM,	                                // Таймер
	TIMER_RTC_LOW_BAT,	                            // Таймер индикации низкого уровня батареи
	TIMER_RTC_TWA,	                                // Таймер twa
	TIMER_RTC_DISP_FLASHING_ERR,                    // Таймер переключения ошибок на экране
	TIMER_RTC_DISP_FLASHING_CHARGE,                 // Таймер мигания зарядки
	TIMER_RTC_TIME_WARM_SENSOR,	                    // Таймер прогрев сенсоров при сбросе питания на часах
	TIMER_RTC_BUMP_TEST_L,                          // Таймер BUMP TEST L
	TIMER_RTC_DEPAS_SET,                        	// Таймер start depas или старт измерений батареи
	TIMER_RTC_BLE,                         			// Таймер
	TIMER_RTC_BLE_DATA_SLEEP,                       // Таймер sleep ble
	TIMER_RTC_LORA_DATA_INIT,                       // Таймер init ble
	TIMER_RTC_LORA_DATA_SET,                        // Таймер начала отправки данных по ble
	TIMER_RTC_LORA_DATA_SLEEP,                      // Таймер sleep ble
	TIMER_RTC_LED_STATE_RUN_PERIOD,					// Период индикации зеленого светодиода в режиме работы
	TIMER_RTC_LED_STATE_RUN_PULSE,					// Пульс индикации зеленого светодиода в режиме работы
	TIMER_RTC_LIGHT_RESET,                          // Таймер на выключение подсветки
	TIMER_RTC_SOFT_RESET_PROGRAM, 					// Сброс программы
	TIMER_RTC_TURN_ON_BAT, 							// Таймер на выключение при низком заряде батареи
	TIMER_RTC_OFF_BAT_CRITICAL_LOW, 				// Таймер на включение индикации разряженной батареи при попытке включения(22)
	TIMER_RTC_BLE_DATA_INIT,                        // таймер инициализации BLE
	TIMER_RTC_BLE_DATA_SET,                         // таймер передачи данных BLE
	TIMER_RTC_GPS_DATA_INIT,                        // таймер инициализации GPS
	TIMER_RTC_LORA_DATA_SET_ALARM,                  // таймер аварийного пакета LoRa
	TIMER_RTC_COUNT_PRESS_SOS,                      // Таймер сброса помощи(23)
	TIMER_RTC_END,
};

//===========================================================================================================================
// Язык прибора
typedef enum {
	RUS = 0,
	EN,
}APPLICATIONLANGUAGE;
  
// Состояние прибора
typedef enum {
	DEVICE_TURNED_ON = 0,	             			// Включен
	DEVICE_TURNED_OFF,		                        // Выключен
	DEVICE_TURNED_ON_1,		                        // Включен, без индикации(звук, вибро..)Используется для прогрева и авт выкл.
//    DEVICE_TURNED_SAVE_BATTERY_POWER,             // Включен, Используется для режима сохранения энергии
//  DEVICE_TURNED_END,
}DEVICE_TURNED;
  
extern DEVICE_TURNED device_turn;

//===========================================================================================================================
//Для статусного байта ошибок, биты ошибок
enum ST_GA_ERR
{
  ST_BIT_LIMIT1 = 0,		                        // бит 0  - Порог 1 +
  ST_BIT_LIMIT2, 		                            // бит 1  - Порог 2 +
  ST_BIT_LIMIT3, 		                            // бит 2  - Порог 3 +
  ST_BIT_LIMIT_STEL, 		                        // бит 3  - Порог STEL +
  ST_BIT_LIMIT_TWA, 		                        // бит 4  - Порог TWA +
  ST_BIT_EXCEEDED_THE_RANGE,                        // бит 5  - Превышение диапазона +
  ST_BIT_SENSOR_FAILED,                             // бит 6  - Сенсор вышел из строя или не подключен
  ST_BIT_AUTO_ZERO_ERR,								// бит 7  - Ошибка при калибровке нуля(выставляется на третий раз как в ТЗ) +
  ST_BIT_AUTO_SPAN_ERR,                             // бит 8  - Ошибка при калибровке диапазона(выставляется на третий раз как в ТЗ) +
  ST_BIT_CALIB_INTERVAL,                            // бит 9  - Время калибровки(err) +
  ST_BIT_ERR_ADC,                           	 	// бит 10 - Ошибка микросхемы ADC
  ST_BIT_ERR_MCP4652,                           	// бит 11 - Ошибка микросхемы mcp4652
  ST_BIT_ERR_MCP47,                           	 	// бит 12 - Ошибка микросхемы mcp47c
};

//--------------------------------------------------------------------------------------------------------------
//Для общего статусного байта ошибок, биты статуса ошибок
enum ST_COMMON_ERR
{
  ST_COMMON_BIT_ERR_CALIB_INTERVAL = 0,                                         // бит 0 - Время калибровки(err) +
  ST_COMMON_BIT_ERR_BUMP_INTERVAL,                                              // бит 1 - Время BUMP TEST(err) +
  ST_COMMON_BIT_CRITICAL_LOW_BATTERY,                                           // бит 2 - Критично низкий заряд аккумулятора(err) +
  ST_COMMON_BIT_ERR_NOP_1,                                                      // бит 3 -
  ST_COMMON_BIT_ERR_STS4,                                                       // бит 4 - Ошибка микросхемы температуры
  ST_COMMON_BIT_ERR_BLE,                                                      	// бит 5 - Ошибка микросхемы BLE
  ST_COMMON_BIT_ERR_GPS,                                                      	// бит 6 - Ошибка микросхемы GPS
  ST_COMMON_BIT_ERR_GSM,                                                      	// бит 7 - Ошибка микросхемы GSM
  ST_COMMON_BIT_ERR_LORA,                                                      	// бит 8 - Ошибка микросхемы LORA
  ST_COMMON_BIT_ERR_LCD, 														// бит 9 - Ошибкак LCD
  ST_COMMON_BIT_CRITICAL_BATTERY,												// бит 10 - Критическое состояние батареи +
  ST_COMMON_BIT_DEPAS,															// бит 11 - Статус депасивации
  ST_COMMON_BIT_ERR_NOP_2,		           										// бит 12 -
};
//--------------------------------------------------------------------------------------------------------------
enum ST_COMMON
{
  ST_COMMON_BIT_BLOCK_SOUND = 0,                                                // бит 0  - Блокировка звука +
  ST_COMMON_BIT_BLOCK_SOUND_LIMIT_CALIB,                                        // бит 1  - Блокировка звука до следующего включения прибора/перезагрузки
  ST_COMMON_BIT_BLOCK_TURN_OFF,                                                 // бит 2  - Блокировка выключения прибора вне док станции +
  ST_COMMON_BIT_BLOCK_CALIB,                                                    // бит 3  - Блокировка калибровки через меню +
  ST_COMMON_BIT_NOP_1,                                                   		// бит 4  -
  ST_COMMON_BIT_DATA_EXCHANGE,                                                  // бит 5  - Режим обмена данными +
  ST_COMMON_BIT_BAT_CHARGE,                                                     // бит 6  - Режим зарядки устройства +
  ST_COMMON_BIT_BUMP_TEST,                                                      // бит 7  - BUMP TEST(статус) +
  ST_COMMON_BIT_BUMP_TEST_RUN,                                                  // бит 8  - Старт BUMP TEST +
  ST_COMMON_BIT_TURN_LCD_UNIT,                                                  // бит 9  - Отображение единицы измерения
  ST_COMMON_BIT_TURN_ON_BLE,                                                    // бит 10 - Включение BLE
  ST_COMMON_BIT_TURN_STATE_RUN_LED,                                             // бит 11 - Включение green run led
  ST_COMMON_BIT_TURN_TEST_SOUND, 												// бит 12 - Тест звука
  ST_COMMON_BIT_TURN_ON_GPS,                                                    // бит 13 - Включение GPS
  ST_COMMON_BIT_TURN_ON_GSM,                                                    // бит 14 - Включение GSM
  ST_COMMON_BIT_TURN_ON_LORA,                                                   // бит 15 - Включение LORA
  ST_COMMON_BIT_TURN_OFF = 31,                                                  // бит 31 - Прибор выключен(Не ведутся логи в режиме выключения)+
};

//===========================================================================================================================
//--------------------------------------------------------------------------------------------------------------
//Колличество таймеров------------------------------------------------------------------------------------------
#define COUNT_TIMERS			(TIMER_END)
#define COUNT_TIMERS_RTC        (TIMER_RTC_END)
//Колличество каналов прибора. LoRa-драйвер строит по нему COUNT_CHAN_LORA и массив sensors[] (Lora_S7678S.h)
#define COUNT_CHAN				(10)
//--------------------------------------------------------------------------------------------------------------
typedef struct
{
  uint16_t	        Timers[COUNT_TIMERS];
  uint16_t	        TimerIsEnd;		          		// Тест на конец таймера (побитно)
} DOWN_TIMER;

//--------------------------------------------------------------------------------------------------------------
typedef struct
{
  uint16_t	        Timers[COUNT_TIMERS_RTC];
  uint32_t	        TimerIsEnd;		              // Тест на конец таймера (побитно)
} DOWN_TIMER_RTC;
//--------------------------------------------------------------------------------------------------------------
typedef struct
{
	uint32_t 	count_1ms;
	uint32_t 	count_10ms;
	uint32_t 	count_100ms;
	uint32_t 	count_1s;
	uint8_t	 	flag_1ms;
	uint8_t		flag_10ms;
	uint8_t		flag_100ms;
	uint8_t 	flag_1s;
	uint8_t    	flag_start_work;			// флаг старта работы
	uint8_t 	flag_start_work_bat_sys;
}Timer_variables;

extern volatile Timer_variables 	timer;
extern volatile DOWN_TIMER 			countdown_time;		 /* миллисекундные (TIM3 1 кГц)     */
extern volatile DOWN_TIMER_RTC 		countdown_time_rtc;	 /* секундные (RTC wakeup 1 с)      */
//--------------------------------------------------------------------------------------------------------------
/* Длительности секундных таймеров, с: START_TIMER_RTC(TIMER_RTC_x, TIME_RTC_x).
   Значения стартовые - подберите под свои модули.                                  */
#define TIME_RTC_BLE_DATA_INIT		(2u)
#define TIME_RTC_BLE_DATA_SET			(60u)
#define TIME_RTC_BLE_DATA_SLEEP		(30u)
#define TIME_RTC_LORA_DATA_INIT		(3u)
#define TIME_RTC_LORA_DATA_SET		(120u)
#define TIME_RTC_LORA_DATA_SLEEP	(60u)
#define TIME_RTC_LED_STATE_RUN_PULSE	(1u)	// пульс зеленого светодиода, с
#define TIME_RTC_LED_STATE_RUN_PERIOD	(5u)	// период зеленого светодиода, с
//--------------------------------------------------------------------------------------------------------------
/* Пороги сигнализации батареи, мВ (Li-ion) - заводские значения DefaultConfig(). */
#define BAT_MAX_DEF				(4200u)
#define BAT_MIN_DEF				(3300u)
#define BAT_LIM_1_DEF			(3500u)
#define BAT_LIM_2_DEF			(3400u)
//--------------------------------------------------------------------------------------------------------------
typedef struct//
{   
  uint32_t              State;                                                  // Состояние системы(Общий)
  uint32_t              StateErr;                                               // Ошибки

  uint16_t              TimeLedStatePulse;
  uint16_t              TimeLedStatePeriod;

  uint16_t              battery_charge_percent;                                 // Заряд батареи в процентах
  uint16_t              battery_charge_volt;                                    // Заряд батареи в вольтах
  uint16_t				battery_charge_volt_max;
  uint16_t				battery_charge_volt_min;
  uint16_t				battery_charge_volt_lim_1;
  uint16_t				battery_charge_volt_lim_2;
  
  volatile  uint32_t    working_hours;
  uint32_t              working_hours_offset;// сдвиг часового пояса
  
  float					Temperature_calib_offset;
  float                 Temperature;                                            // Температура
  float                 Humidity;
  float					Pressure;
  uint16_t	        	SerialLo;                                               // Заводской номер
  uint16_t	        	SerialHi;                                               // Заводской номер
  
  uint16_t              HardwareVersion;                                        // Аппаратная версия
  uint32_t              ProductionDate;                                         // Дата производства

  uint32_t				Gps_type_nav;
  uint16_t				PeriodTimeLora;
  uint16_t				Lora_Config_Flags;
  uint32_t				Lora_freq_rx2;
  uint16_t				Lora_dr_rx2;
  /* --- данные датчиков --------------------------------------------------------
     BME280   -> Temperature / Humidity / Pressure (выше),
     MAX17048 -> battery_charge_percent / battery_charge_volt,
     LIS3DH   -> Orientation, Screen_rotation.
     Оси акселерометра (Accel_x/y/z) и статусы падения живут в sensors_status_t
     (Vector_main.h) - runtime-данные, в конфиг и во flash не сохраняются.     */
  uint8_t               Orientation;                                            // lis3dh_orientation_t
  uint8_t               Screen_rotation;                                        // 0 или 2 -> TFT_Rotation()
  uint8_t               Sensors_ok;                                             // биты: 1=BME280 2=LIS3DH 4=MAX17048
  uint8_t               Sensors_reserve;

  /* --- журнал и режимы работы (использует Config_save_read.c) ---------------
     Поля добавлены ДО Reserve[], поэтому sizeof(SNS_CFG) вырос - extstore
     cfg_save/cfg_load хранят длину записи, совместимость проверяет CRC.        */
  uint16_t              TimeLimit;                                              // лимит времени (Avis)
  uint16_t              TimeLimitRST;                                           // лимит времени RST (Avis)
  uint16_t              Bump_interval;                                          // интервал Bump test
  uint32_t              DataLastBumpTest;                                       // дата последнего Bump test
  uint32_t              CurrentAddrFile;                                        // журнал: логический счётчик записей
  uint32_t              BegginAddrFile;                                         // журнал: начало доступного диапазона
  uint8_t               ArchiveRecording;                                       // 1 = запись архива разрешена
  uint8_t               Type_lcd;                                               // CONFIG_MODEL_LCD
  uint16_t              Archiveinterval;                                        // (мин<<8)|с - период архива, как в Avis
  uint16_t				current_build_type;
  uint16_t              Reserve[10];
}SNS_CFG_Type_common; //Общая струкрутра для сенсоров

typedef struct
{   
  SNS_CFG_Type_common   Config_common;
  APPLICATIONLANGUAGE   application_language;
  uint16_t	        	CRC_CONFIG;
}SNS_CFG; //Общая струкрутра для сенсоров
extern SNS_CFG Sns_Cfg_struct;
extern SNS_CFG Cfg_structdef_read;

//===========================================================================================================================
typedef struct//
{
	float		temperature;
	uint16_t 	flag;
}TEMPSENSOR_CALIB;
extern TEMPSENSOR_CALIB tempsensor_calib;
//===========================================================================================================================


#endif /* __SHARED_TYPES_H */
