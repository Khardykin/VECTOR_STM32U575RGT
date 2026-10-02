/*
 * config_device.h
 *
 *  Created on: 1 окт. 2026 г.
 *      Author: Dmitriy
 */

#ifndef VECTORLIB_INC_CONFIG_DEVICE_H_
#define VECTORLIB_INC_CONFIG_DEVICE_H_

#define MIRAX 1
#define BACS 2
#define MIRAX_BACS_BUILD	MIRAX		// 1-Mirax, 2- Bacs

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
#define CONFIG_G2						0				// Конфигурация включающая G2
#define CONFIG_SLEEP                    1               // Включение конфигурации со сном
#define CONFIG_SAVE_PARAM_LOG           1               // Включение конфигурации с использованием микросхемы памяти
#define CONFIG_DISPLAY                  1               // Включение конфигурации с использованием управления дисплеем
#define CONFIG_INDICATION               0               // Выбор между индикациями
#define CONFIG_HW						0				// 0 -
#if (CONFIG_HW == 0)

#elif (CONFIG_HW == 99)

#endif

#endif /* VECTORLIB_INC_CONFIG_DEVICE_H_ */
