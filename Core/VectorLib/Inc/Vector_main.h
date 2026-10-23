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
#include "snd_log_test.h"  /* [ВРЕМЕННЫЙ ТЕСТ] звук+журнал - удалить после проверки */
#include "audio_player.h"
#include "vector_tasks.h"
#include "Ble_vector.h"     /* BLE-модуль MK01: мастерская сторона, порт Ble_mk01 (CONFIG_BLE) */
#include "Menu_vector.h"    /* основа слоя меню: экраны + жесты кнопок (CONFIG_DISPLAY) */


/* Карта ВНУТРЕННЕЙ flash. Заводские данные - последняя страница банка 2
   (0x080FE000..0x080FFFFF, 8 КБ): линкерный скрипт ограничивает код 1016K,
   поэтому страница не занята прошивкой (STM32U575RGTX_FLASH.ld - правка
   слетает при регенерации куба, проверить!). Конфигурация SNS_CFG и журнал -
   во ВНЕШНЕЙ flash (extstore/sfmap.h, CONFIG 0x400000 рабочий,
   CONFIG_DEFAULT 0x401000 заводской, LOG 0x402000).
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

  /* Ориентация и поворот экрана - источник истины ЗДЕСЬ (из SNS_CFG
     перенесены сюда); Screen_rotation читает TFT_Rotation() в Measure Task. */
  volatile uint8_t  Orientation;        /* lis3dh_orientation_t              */
  volatile uint8_t  Screen_rotation;    /* 0 или 2 -> TFT_Rotation()         */

  /* Падение (free-fall, генератор INT2 LIS3DH, латч): fall_detected ставит
     sensors_read(), снимает sensors_fall_event(); счётчик - с включения.   */
  volatile uint8_t  fall_detected;
  volatile uint32_t cnt_fall_events;

  /* --- ИЗМЕРЕННЫЕ ДАННЫЕ (решение автора: всё СЧИТАННОЕ живёт здесь,
     в SNS_CFG остаётся только то, что сохраняется в конфигурацию).
     Orientation/Screen_rotation - выше (единственный экземпляр, дубль
     из пула автора убран - член структуры объявлялся дважды).            */
  volatile uint16_t battery_charge_percent; /* MAX17048: заряд, %            */
  volatile uint16_t battery_charge_volt;    /* MAX17048: напряжение, мВ      */
  volatile uint8_t  Sensors_ok;             /* биты: 1=BME280 2=LIS3DH 4=MAX17048 */
  volatile uint8_t  Sensors_reserve;
  volatile float    Temperature_calib_offset; /* поправка калибровки, °C     */
  volatile float    Temperature;            /* BME280 int32-драйвер: °C×100 (Bosch comp_data). LoRa v4 Environment: /10 -> °C×10. Индикация: /100 */
  volatile float    Humidity;               /* BME280 int32-драйвер: %×1024 (Bosch). LoRa v4: /1024 -> %. Индикация: /1024 */
  volatile float    Pressure;               /* BME280 int32 (Па×100)/1000 = гПа×10 - СРАЗУ формат LoRa v4 Environment.Pressure (u16) */

} sensors_status_t;

extern volatile sensors_status_t sensors_status;

/* Биты sensors_status.Sensors_ok (маска переехала из Config_common) -
   какие микросхемы ответили при инициализации (выставляет sensors_init()). */
#define SENSORS_OK_BME        (1u)
#define SENSORS_OK_LIS3DH     (2u)
#define SENSORS_OK_MAX17048   (4u)

void    sensors_init(void);             /* из Vector_Run_Pre_Init (один раз) */
void    Battery_Init(void);             /* MAX17048: quickStart + лимиты напряжения
                                           от BAT_MIN/BAT_MAX конфига; зовёт
                                           Vector_Run_Pre_Init после чтения конфига */
void    sensors_read(void);             /* из Vector_Run_Measure (1 с)       */
uint8_t sensors_rotation_changed(void);
uint8_t sensors_fall_event(void);       /* 1 = было падение, флаг сбрасывается */

/* --- сервис (по образцу Avis) ------------------------------------------
 * SoftResetProgram: 1 = команда мягкой перезагрузки прошивки (взводит COM/
 *   BLE-модуль ВМЕСТЕ с START_TIMER_RTC(TIMER_RTC_SOFT_RESET_PROGRAM, N)); */
extern uint8_t SoftResetProgram;
void Avis_Battery_System(void);

/* --- общие данные модулей обмена -------------------------------------- */
extern uint8_t deveui_com[8];

/* --- такты прибора -------------------------------------------------------- */
/* Блокировка конфигурации (п.65): RMW-секции Sns_Cfg_struct (биты State,
   PeriodTimeLora, маски, downlink LoRa, сброс к заводским, будущий
   конфигуратор COM/BLE, меню) выполнять ПОД этой парой; одиночные чтения
   выровненных u16/u32 атомарны на Cortex-M33 - читатели блокировку НЕ
   берут. Реализация/экземпляр - Vector_main.c (мьютекс создан в
   vector_tasks_init, до потоков; до создания - no-op, как vlog_lock).   */
void Sys_Cfg_Lock(void);
void Sys_Cfg_Unlock(void);

void Timer_Tick_1ms(void);
void Timer_Tick_1s(void);
void Clear_Timer_Rtc(void);           /* сбросить все секундные таймеры      */

/* Бортовая инициализация ДО старта RTOS */
void Vector_Run_Board_Init(void);

/* Таймаут кадра COM-порта (1 мс): weak-заглушка, реализация появится вместе
   с command_message(). Зовётся из Timer_Tick_1ms().                        */
void Uart_Command_Receive_Timer_Inc(void); /* 1 = экран надо повернуть (сброс)  */

#endif /* VECTORLIB_INC_VECTOR_MAIN_H_ */
