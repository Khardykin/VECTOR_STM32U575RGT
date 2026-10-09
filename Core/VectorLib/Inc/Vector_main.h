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

#include "Config_save_read.h"

#include "audio_demo.h"
#include "audio_player.h"
#include "vector_tasks.h"


/* Карта ВНУТРЕННЕЙ flash. Заводские данные - последняя страница банка 2
   (0x080FE000..0x080FFFFF, 8 КБ): линкерный скрипт ограничивает код 1016K,
   поэтому страница не занята прошивкой (STM32U575RGTX_FLASH.ld - правка
   слетает при регенерации куба, проверить!). Конфигурация SNS_CFG и журнал -
   во ВНЕШНЕЙ flash (extstore/sfmap.h: CONFIG 0x400000, LOG 0x401000).
   Страницу 8 КБ NOR стирает целиком, пишет по 16 байт, поэтому запись
   заводских полей идёт через RAM-окно 64 Б (Internal_Flash_Write).        */
#define FLASH_ADDRESS_START_BOOTLOADER  (0x08000000)	// Начало основной программы (бутлоадера пока нет)
#define FLASH_ADDRESS_STOP_BOOTLOADER   (0x08005000)  	// Резерв: граница бутлоадера, когда появится

/* --- датчики на I2C1: BME280 (0x76), LIS3DH (0x19), MAX17048 (0x36) --------
 * Владелец шины - поток Measure Task: чтение только из sensors_read(), поэтому
 * мьютекс не нужен. Микросхемы выключаются CONFIG_BME / CONFIG_LIS3DH /
 * CONFIG_MAX17048 (тело драйверов закрыто этими флагами).
 * Всё состояние - ОДНА структура sensors_status (как audio_status):
 * счётчики диагностики шины + runtime-данные датчиков, которые НЕ хранятся
 * в конфигурации (оси акселерометра, статусы падения, копия ориентации/
 * поворота экрана). Измеренные значения, входящие в конфиг/журнал (T/H/P,
 * батарея), - в Sns_Cfg_struct.Config_common.                               */
typedef struct
{
  volatile uint32_t cnt_init_ok;        /* сколько модулей поднялось         */
  volatile uint32_t cnt_bme_reads;      /* успешных чтений BME280            */
  volatile uint32_t cnt_accel_reads;    /* обновлений кэша акселерометра     */
  volatile uint32_t cnt_battery_reads;  /* чтений MAX17048                   */
  volatile uint32_t cnt_rotation_events;/* событий смены ориентации          */
  volatile uint32_t cnt_read_ms;        /* длительность последнего чтения, мс*/
  volatile uint32_t last_read_ms;       /* метка последнего sensors_read()   */

  /* LIS3DH: последние оси, g (+-4g, HR 12 бит). Runtime-данные: во flash не
     сохраняются, читаются LVGL/COM-модулем напрямую, без I2C.              */
  volatile float    Accel_x;
  volatile float    Accel_y;
  volatile float    Accel_z;

  /* Ориентация и поворот экрана - копия Config_common (источник истины там,
     поле сохраняется в конфиге); здесь - быстрый доступ для индикации.     */
  volatile uint8_t  Orientation;        /* lis3dh_orientation_t              */
  volatile uint8_t  Screen_rotation;    /* 0 или 2 -> TFT_Rotation()         */

  /* Падение (free-fall, генератор INT2 LIS3DH, латч): fall_detected ставит
     sensors_read(), снимает sensors_fall_event(); счётчик - с включения.   */
  volatile uint8_t  fall_detected;
  volatile uint32_t cnt_fall_events;
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
uint8_t sensors_fall_event(void);       /* 1 = было падение, флаг сбрасывается */

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
