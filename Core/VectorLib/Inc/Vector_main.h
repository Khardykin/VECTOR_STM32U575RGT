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

/* --- датчики на I2C1: BME280 (0x76), LIS3DH (0x19), MAX17048 (0x36) --------
 * Владелец шины - поток Measure Task: чтение только из sensors_read(), поэтому
 * мьютекс не нужен. Микросхемы выключаются CONFIG_BME / CONFIG_LIS3DH /
 * CONFIG_MAX17048 (тело драйверов закрыто этими флагами).
 * Всё состояние - ОДНА структура sensors_status (как audio_status).          */
typedef struct
{
  volatile uint8_t  bme_ok;             /* 1 = BME280 ответил и настроен     */
  volatile uint8_t  lis3dh_ok;          /* 1 = LIS3DH ответил (WHO_AM_I)     */
  volatile uint8_t  max17048_ok;        /* 1 = MAX17048 ответил (VERSION)    */

  volatile float    temperature_c;      /* BME280, C                         */
  volatile float    humidity_pct;       /* BME280, %                         */
  volatile float    pressure_hpa;       /* BME280, мм рт.ст. (Па/133.3)      */
  volatile float    accel_x_g;          /* LIS3DH, g (шкала +-4g, HR 12 бит) */
  volatile float    accel_y_g;
  volatile float    accel_z_g;
  volatile uint8_t  orientation;        /* lis3dh_orientation_t              */
  volatile uint8_t  screen_rotation;    /* 0 или 2 - аргумент TFT_Rotation() */
  volatile uint16_t battery_percent_x10;/* MAX17048 SOC, % * 10              */
  volatile uint16_t battery_voltage_mv; /* MAX17048 VCELL, мВ                */

  volatile uint32_t cnt_init_ok;        /* сколько модулей поднялось         */
  volatile uint32_t cnt_bme_reads;      /* успешных чтений BME280            */
  volatile uint32_t cnt_accel_reads;    /* обновлений кэша акселерометра     */
  volatile uint32_t cnt_battery_reads;  /* чтений MAX17048                   */
  volatile uint32_t cnt_rotation_events;/* событий смены ориентации          */
  volatile uint32_t cnt_read_ms;        /* длительность последнего чтения, мс*/
  volatile uint32_t last_read_ms;       /* метка последнего sensors_read()   */
} sensors_status_t;

extern volatile sensors_status_t sensors_status;

void    sensors_init(void);             /* из Vector_Run_Pre_Init (один раз) */
void    sensors_read(void);             /* из Vector_Run_Measure (1 с)       */
uint8_t sensors_rotation_changed(void); /* 1 = экран надо повернуть (сброс)  */

#endif /* VECTORLIB_INC_VECTOR_MAIN_H_ */
