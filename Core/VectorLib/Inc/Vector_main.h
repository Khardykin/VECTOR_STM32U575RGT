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

/* Общие типы прибора: SNS_CFG, биты статусов ST_COMMON_*, DEVICE_TURNED.
   Макросы доступа к статусам - vector_status.h (SET/CLEAR/TEST_*).
   vector_compat.h - Delay/DelayInt/GetTick/Search_text для кода,
   перенесённого с другого прибора (Avis/AT32).                        */
#include "shared_types.h"
#include "vector_status.h"
#include "vector_compat.h"
#include "shared_macros.h"   /* общие макросы перенесённого кода (пины/статусы/время) */

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

#include "vector_sensors.h"

#include "buffer.h"

#include "audio_demo.h"
#include "audio_player.h"
#include "vector_tasks.h"

#endif /* VECTORLIB_INC_VECTOR_MAIN_H_ */
