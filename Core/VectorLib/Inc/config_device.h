/*
 * config_device.h
 *
 *  Created on: 1 окт. 2026 г.
 *      Author: Dmitriy
 */

#ifndef VECTORLIB_INC_CONFIG_DEVICE_H_
#define VECTORLIB_INC_CONFIG_DEVICE_H_

#define MIRAX 0
#define BACS 1
#define MIRAX_BACS_BUILD	current_build_type		// 0-Mirax, 1- Bacs

//Конфигурация
#define Dev1     		(6) //
#define DEVICE_NUMBER 	(Dev1)

#define DEVICE_NUMBER_MODIF	0

#if (DEVICE_NUMBER == Dev1)
	#define FIRMWARE_VERSION                (1)     		// Версия прошивки
#else
	#define FIRMWARE_VERSION                (0)     		// Версия прошивки
#endif
//===========================================================================================================================
#define DEVICE_NUMBER_COM				(DEVICE_NUMBER + DEVICE_NUMBER_MODIF)
#define BLE_DEBUG 1
#define LORA_DEBUG 0
#define TEST_PROG						1

#define CONFIG_BATTERY                  0               // Включение конфигурации с батареей
#define CONFIG_UART                   	1               // Включение конфигурации с USART
#define CONFIG_CALIB_RTC				0
#define CONFIG_BLE						0				// Конфигурация включающая BLE
#define CONFIG_LORA						0				// Конфигурация включающая LORA
#define CONFIG_G4						0				// Конфигурация включающая G4
#define CONFIG_GPS						1				// Конфигурация включающая GPS
#define CONFIG_SLEEP                    1               // Включение конфигурации со сном
#define CONFIG_SAVE_PARAM_LOG           1               // Включение конфигурации с использованием микросхемы памяти
#define CONFIG_DISPLAY                  1               // Включение конфигурации с использованием управления дисплеем
#define CONFIG_TYPE_LCD_TFT				1
#define CONFIG_MODEL_LCD				0				// 0 = ST7789P3 172x320 (портрет, подсветка вкл/выкл PC6), 1 = 320x240 ILI9341 (ШИМ)

#define CONFIG_LIS3DH					1
#define CONFIG_BME						1
#define CONFIG_MAX17048					1

#define CONFIG_HW						0				// 0 -
#if (CONFIG_HW == 0)

#elif (CONFIG_HW == 99)

#endif

#endif /* VECTORLIB_INC_CONFIG_DEVICE_H_ */
