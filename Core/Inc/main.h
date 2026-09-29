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

#include "stm32u5xx_ll_crs.h"
#include "stm32u5xx_ll_rcc.h"
#include "stm32u5xx_ll_bus.h"
#include "stm32u5xx_ll_system.h"
#include "stm32u5xx_ll_exti.h"
#include "stm32u5xx_ll_cortex.h"
#include "stm32u5xx_ll_utils.h"
#include "stm32u5xx_ll_pwr.h"
#include "stm32u5xx_ll_dma.h"
#include "stm32u5xx_ll_gpio.h"

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
#define I2C_Mem_Write(x1,x2,x3,x4,x5,x6,x7)		HAL_I2C_Mem_Write(x1,x2,x3,x4,x5,x6,x7)
#define I2C_Mem_Read(x1,x2,x3,x4,x5,x6,x7)		HAL_I2C_Mem_Read(x1,x2,x3,x4,x5,x6,x7)
#define I2C_Master_Transmit(x1,x2,x3,x4,x5)		HAL_I2C_Master_Transmit(x1,x2,x3,x4,x5)
#define I2C_Master_Receive(x1,x2,x3,x4,x5)		HAL_I2C_Master_Receive(x1,x2,x3,x4,x5)
#define i2c_status_type						HAL_StatusTypeDef
#define I2C_IsBusy(x1)						(HAL_I2C_GetState(x1) != HAL_I2C_STATE_READY)
#define I2C_ReConfig(x1)					do { (void)HAL_I2C_DeInit(x1); (void)HAL_I2C_Init(x1); } while (0)

/* --- SPI: экран (SPI2+DMA) и внешняя flash (SPI1) --- */
#define SPI_OK							HAL_OK
#define SPI_Transmit(x1,x2,x3,x4)			HAL_SPI_Transmit(x1,x2,x3,x4)
#define SPI_Transmit_DMA(x1,x2,x3)			HAL_SPI_Transmit_DMA(x1,x2,x3)
#define SPI_Abort(x1)						HAL_SPI_Abort(x1)
#define SPI_GetErrorCode(x1)				((x1)->ErrorCode)
#define SPI_GetDataSize(x1)					((x1)->Init.DataSize)
#define SPI_GetDMA(x1)						((x1)->hdmatx)
#define SPI_INSTANCE(x1)					((x1)->Instance)
#define SPI_DATA_SIZE_8BIT				SPI_DATASIZE_8BIT
#define MAX_DELAY						HAL_MAX_DELAY

/* --- GPIO: пины экрана/модулей --- */
#define GPIO_WritePin(x1,x2,x3)				HAL_GPIO_WritePin(x1,x2,x3)
#define GPIO_ReadPin(x1,x2)					HAL_GPIO_ReadPin(x1,x2)
#define PIN_SET							GPIO_PIN_SET
#define PIN_RESET						GPIO_PIN_RESET

/* --- UART: перенос скорости на ходу (Gps.c зовёт usart_init из AT32) ---
   Разрядность и стоп-биты не меняем: их задаёт CubeMX (8N1), поэтому
   аргументы databits/stopbits макросом игнорируются. Реализация
   Uart_SetBaudrate() - в vector_macros.c.                                  */
#define UART_OK							HAL_OK
#define Uart_Init(x1)						HAL_UART_Init(x1)
#define Uart_Enable_IT(x1,x2)				__HAL_UART_ENABLE_IT(x1,x2)
#define UART_IT_RX_ERR					UART_IT_ERR
#define UART_IT_RX_BYTE					UART_IT_RXNE
#define USART_DATA_8BITS				(8u)
#define USART_STOP_1_BIT				(1u)
#define usart_init(port,baud,databits,stopbits)	Uart_SetBaudrate(&(port),(baud))

void Uart_SetBaudrate(UART_HandleTypeDef *huart, uint32_t baud);

#define READ_PIN_IN(x)  	(LL_GPIO_IsInputPinSet(x##_GPIO_Port, x##_Pin))
#define READ_PIN_OUT(x)  	(LL_GPIO_IsOutputPinSet(x##_GPIO_Port, x##_Pin))
#define SET_OFF(x)  		LL_GPIO_ResetOutputPin(x##_GPIO_Port, x##_Pin);
#define SET_ON(x)   		LL_GPIO_SetOutputPin(x##_GPIO_Port, x##_Pin);
#define SET_TGL(x)			LL_GPIO_TogglePin(x##_GPIO_Port, x##_Pin);

#define USART_COM			(huart4)
#define USART_DEBUG			(huart4)


#define BUTTON_1_EXTI_LINE 	LL_EXTI_LINE_1
#define BUTTON_2_EXTI_LINE 	LL_EXTI_LINE_2
#define BUTTON_3_EXTI_LINE 	LL_EXTI_LINE_3
#define ACCEL_INT_EXINT_LINE LL_EXTI_LINE_11
#define error_status 		ErrorStatus
#define I2C_OK				HAL_OK
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
#define BUTTON_1_Pin GPIO_PIN_1
#define BUTTON_1_GPIO_Port GPIOB
#define BUTTON_1_EXTI_IRQn EXTI1_IRQn
#define BUTTON_2_Pin GPIO_PIN_2
#define BUTTON_2_GPIO_Port GPIOB
#define BUTTON_2_EXTI_IRQn EXTI2_IRQn
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
#define BUTTON_3_Pin GPIO_PIN_3
#define BUTTON_3_GPIO_Port GPIOB
#define BUTTON_3_EXTI_IRQn EXTI3_IRQn
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
