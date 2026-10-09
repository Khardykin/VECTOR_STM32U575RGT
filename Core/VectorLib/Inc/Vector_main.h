/*
 * Vector_main.h
 *
 *  Created on: 1 окт. 2026 г.
 *      Author: Dmitriy
 */

#ifndef VECTORLIB_INC_VECTOR_MAIN_H_
#define VECTORLIB_INC_VECTOR_MAIN_H_

//===========================================================================================================================
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "rtc.h"
//===========================================================================================================================
#include "config_device.h"
#include "vector_config.h"

#include <stdint.h>

/* Общие типы и макросы прибора:
     shared_types.h   SNS_CFG, биты ST_COMMON_*, DEVICE_TURNED, таймеры;
     shared_macros.h  SET/CLEAR/TEST_STATUS_COMMON_BIT, SETBIT/CLRBIT/TESTBIT;
     vector_macros.h  Delay/DelayInt/GetTick/Search_text, TIME_DEL_1,
                      VECTOR_DEVICE_IS_ON - для кода, перенесённого с Avis;
     main.h           абстракция периферии (I2C_xxx, SPI_xxx, GPIO_xxx).    */
#include "shared_types.h"
#include "shared_macros.h"
#include "vector_macros.h"

#if CONFIG_LORA
	#include "Lora_S7678S.h"
#endif

#if CONFIG_GPS
	#include "Gps.h"
#endif

#if CONFIG_LIS3DH
	#include "lis3dh.h"
#endif

#if CONFIG_BME
	#include "bme280_com.h"
#endif

#if CONFIG_MAX17048
	#include "MAX17048.h"
#endif

#include "buffer.h"

#include "audio_demo.h"
#include "audio_player.h"
#include "vector_tasks.h"


#define FLASH_ADDRESS_START_BOOTLOADER  (0x08000000)	// Адресс основной программы
#define FLASH_ADDRESS_STOP_BOOTLOADER   (0x08005000)  	// Конец памяти stm
#define FLASH_PAGE_SIZE                 (2048)

#define FLASH_ADDRESS_APPKEY    		(0x0803A000)
#define FLASH_ADDRESS_CALIB_TEMP_T     	(0x080FF800)
#define FLASH_ADDRESS_CALIB_TEMP_V     	(FLASH_ADDRESS_CALIB_TEMP_T + 4)
#define FLASH_ADDRESS_BUILD_TYPE    	(FLASH_ADDRESS_CALIB_TEMP_T + 8)

#define TEMPSENSOR_CALIB_TEMP_T     	(*(float*) FLASH_ADDRESS_CALIB_TEMP_T)
#define TEMPSENSOR_CALIB_TEMP_V     	(*(uint32_t*) FLASH_ADDRESS_CALIB_TEMP_V)

/* --- датчики на I2C1: BME280 (0x76), LIS3DH (0x19), MAX17048 (0x36) --------
 * Владелец шины - поток Measure Task: чтение только из sensors_read(), поэтому
 * мьютекс не нужен. Микросхемы выключаются CONFIG_BME / CONFIG_LIS3DH /
 * CONFIG_MAX17048 (тело драйверов закрыто этими флагами).
 * Всё состояние - ОДНА структура sensors_status (как audio_status).          */
typedef struct
{
  volatile uint32_t cnt_init_ok;        /* сколько модулей поднялось         */
  volatile uint32_t cnt_bme_reads;      /* успешных чтений BME280            */
  volatile uint32_t cnt_accel_reads;    /* обновлений кэша акселерометра     */
  volatile uint32_t cnt_battery_reads;  /* чтений MAX17048                   */
  volatile uint32_t cnt_rotation_events;/* событий смены ориентации          */
  volatile uint32_t cnt_read_ms;        /* длительность последнего чтения, мс*/
  volatile uint32_t last_read_ms;       /* метка последнего sensors_read()   */
} sensors_status_t;

extern volatile sensors_status_t sensors_status;

/* Биты Sns_Cfg_struct.Config_common.Sensors_ok - какие микросхемы ответили
   при инициализации (выставляет sensors_init()).                          */
#define SENSORS_OK_BME        (1u)
#define SENSORS_OK_LIS3DH     (2u)
#define SENSORS_OK_MAX17048   (4u)

void    sensors_init(void);             /* из Vector_Run_Pre_Init (один раз) */
void    sensors_read(void);             /* из Vector_Run_Measure (1 с)       */
uint8_t sensors_rotation_changed(void);

/* --- такты прибора --------------------------------------------------------
 * Timer_Tick_1ms() - из TIM3 (1 кГц, HAL_TIM_PeriodElapsedCallback в main.c):
 *   декремент countdown_time[], флаги timer.flag_1ms/10ms/100ms/1s, счётчики
 *   кнопок, таймауты кадров UART (COM/GPS/LoRa).
 * Timer_Tick_1s()  - из RTC wakeup (1 с, HAL_RTCEx_WakeUpTimerEventCallback в
 *   stm32u5xx_it.c): декремент countdown_time_rtc[], working_hours,
 *   flag_start_work, таймер окончания режима обмена.
 * Обе - КОНТЕКСТ ПРЕРЫВАНИЯ: только декремент и флаги, никакого I2C/лога.
 * Переменные (timer, countdown_time, countdown_time_rtc) - volatile, объявлены
 * в shared_types.h; макросы START/TEST/RESET/END_TIMER - в shared_macros.h.  */
void Timer_Tick_1ms(void);
void Timer_Tick_1s(void);
void Clear_Timer_Rtc(void);           /* сбросить все секундные таймеры      */

/* Бортовая инициализация ДО старта RTOS (бывший vector_board_init): такт
   SRAM4, заморозка TIM6 под отладчиком, SD_MODE (усилитель), sf_probe(),
   audio_selftest(). Звать из main() (USER CODE 2).                         */
void Vector_Run_Board_Init(void);

/* Таймаут кадра COM-порта (1 мс): weak-заглушка, реализация появится вместе
   с command_message(). Зовётся из Timer_Tick_1ms().                        */
void Uart_Command_Receive_Timer_Inc(void); /* 1 = экран надо повернуть (сброс)  */

#endif /* VECTORLIB_INC_VECTOR_MAIN_H_ */
