/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32u5xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LTE_LED_Pin GPIO_PIN_0
#define LTE_LED_GPIO_Port GPIOC
#define LTE_EN_Pin GPIO_PIN_1
#define LTE_EN_GPIO_Port GPIOC
#define LTE_STATUS_Pin GPIO_PIN_2
#define LTE_STATUS_GPIO_Port GPIOC
#define LTE_RESET_Pin GPIO_PIN_3
#define LTE_RESET_GPIO_Port GPIOC
#define USART2_TX_LTE_Pin GPIO_PIN_2
#define USART2_TX_LTE_GPIO_Port GPIOA
#define USART2_RX_LTE_Pin GPIO_PIN_3
#define USART2_RX_LTE_GPIO_Port GPIOA
#define CS_FLASH_Pin GPIO_PIN_4
#define CS_FLASH_GPIO_Port GPIOA
#define USART3_TX_BLE_Pin GPIO_PIN_4
#define USART3_TX_BLE_GPIO_Port GPIOC
#define USART3_RX_BLE_Pin GPIO_PIN_5
#define USART3_RX_BLE_GPIO_Port GPIOC
#define STATE_LED_Pin GPIO_PIN_0
#define STATE_LED_GPIO_Port GPIOB
#define BUTTON1_Pin GPIO_PIN_1
#define BUTTON1_GPIO_Port GPIOB
#define BUTTON1_EXTI_IRQn EXTI1_IRQn
#define BUTTON2_Pin GPIO_PIN_2
#define BUTTON2_GPIO_Port GPIOB
#define BUTTON2_EXTI_IRQn EXTI2_IRQn
#define LCD_DC_Pin GPIO_PIN_10
#define LCD_DC_GPIO_Port GPIOB
#define LCD_RST_Pin GPIO_PIN_12
#define LCD_RST_GPIO_Port GPIOB
#define LCD_CS_Pin GPIO_PIN_14
#define LCD_CS_GPIO_Port GPIOB
#define LCD_LED_Pin GPIO_PIN_6
#define LCD_LED_GPIO_Port GPIOC
#define ALRT_Pin GPIO_PIN_7
#define ALRT_GPIO_Port GPIOC
#define CHARGE_STATE_Pin GPIO_PIN_8
#define CHARGE_STATE_GPIO_Port GPIOC
#define SD_MODE_Pin GPIO_PIN_9
#define SD_MODE_GPIO_Port GPIOC
#define ALARM_LED_1_Pin GPIO_PIN_11
#define ALARM_LED_1_GPIO_Port GPIOA
#define ALARM_LED_2_Pin GPIO_PIN_12
#define ALARM_LED_2_GPIO_Port GPIOA
#define VIBRO_Pin GPIO_PIN_15
#define VIBRO_GPIO_Port GPIOA
#define ACCEL_INT_Pin GPIO_PIN_11
#define ACCEL_INT_GPIO_Port GPIOC
#define BLE_RESET_Pin GPIO_PIN_12
#define BLE_RESET_GPIO_Port GPIOC
#define BUTTON3_Pin GPIO_PIN_3
#define BUTTON3_GPIO_Port GPIOB
#define BUTTON3_EXTI_IRQn EXTI3_IRQn
#define GNSS_MODE_Pin GPIO_PIN_4
#define GNSS_MODE_GPIO_Port GPIOB
#define GNSS_RST_Pin GPIO_PIN_5
#define GNSS_RST_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
