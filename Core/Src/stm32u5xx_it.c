/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32u5xx_it.c
  * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32u5xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Vector_main.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint8_t 			flag_end_data_exchange = 0;
uint32_t 			timer_end_data_exchange = 0;   			// Таймер автоматического окончания режима передачи данных

Button_variables 	button1 = {0};
Button_variables 	button2 = {0};
Button_variables 	button3 = {0};
Button_variables 	button_sos = {0};
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern I2C_HandleTypeDef hi2c1;
extern RTC_HandleTypeDef hrtc;
extern DMA_NodeTypeDef Node_GPDMA1_Channel11;
extern DMA_QListTypeDef List_GPDMA1_Channel11;
extern DMA_HandleTypeDef handle_GPDMA1_Channel11;
extern DMA_HandleTypeDef handle_GPDMA1_Channel10;
extern DMA_HandleTypeDef handle_GPDMA1_Channel9;
extern DMA_HandleTypeDef handle_GPDMA1_Channel8;
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim3;
extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern TIM_HandleTypeDef htim6;

/* USER CODE BEGIN EV */
//===========================================================================================================================
/* Такты прибора (timer, countdown_time, countdown_time_rtc) и их обслуживание
   (Countdown_Timer, Countdown_Timer_Rtc, Clear_Timer_Rtc, Timer_Tick_1ms/1s)
   живут в Vector_main.c - здесь только вызов из колбэка RTC.                    */
//===========================================================================================================================
// Callback RTC (wakeup, 1 с)
void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc)
{
	Timer_Tick_1s();
}

// Обработчик прерываний TIM
//void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
//{
//	if(htim->Instance == TIM3){
//		SET_TGL(STATE_LED);
//	}
//}
//===========================================================================================================================
// Обработчик внешних прерываний
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
	if(GPIO_Pin == BUTTON_1_Pin){
		//----------------------------------------------------------------------------
		if((READ_PIN_IN(BUTTON_1))){
			button1.button_flag = 0;
			button1.button_pressed = 0;
			button_sos.button_flag = 0;
			LL_EXTI_DisableRisingTrig_0_31(BUTTON_1_EXTI_LINE);
			LL_EXTI_EnableFallingTrig_0_31(BUTTON_1_EXTI_LINE);
		}
	}
	else if(GPIO_Pin == BUTTON_2_Pin){
		//----------------------------------------------------------------------------
		if((READ_PIN_IN(BUTTON_2))){
			button2.button_flag = 0;
			button2.button_pressed = 0;
			LL_EXTI_DisableRisingTrig_0_31(BUTTON_2_EXTI_LINE);
			LL_EXTI_EnableFallingTrig_0_31(BUTTON_2_EXTI_LINE);
		}
	}
	else if(GPIO_Pin == BUTTON_3_Pin){
		//----------------------------------------------------------------------------
		if((READ_PIN_IN(BUTTON_3))){
			button3.button_flag = 0;
			button3.button_pressed = 0;
			LL_EXTI_DisableRisingTrig_0_31(BUTTON_3_EXTI_LINE);
			LL_EXTI_EnableFallingTrig_0_31(BUTTON_3_EXTI_LINE);
		}
	}
#if (CONFIG_LIS3DH)
	else if(GPIO_Pin == ACCEL_INT_Pin){
		if((READ_PIN_IN(ACCEL_INT))){
			lis3dh_irq_handler();
			LL_EXTI_DisableRisingTrig_0_31(ACCEL_INT_EXINT_LINE);
			LL_EXTI_EnableFallingTrig_0_31(ACCEL_INT_EXINT_LINE);
		}
	}
#endif
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
	if(GPIO_Pin == BUTTON_1_Pin){
		//----------------------------------------------------------------------------
		if((!READ_PIN_IN(BUTTON_1))){
			button1.button_count = 0;
			button1.button_flag = 1;
			button1.button_pressed = 1;
			button1.button_pressed_action = 1;
			button_sos.button_count = 0;
			button_sos.button_flag = 1;
			LL_EXTI_EnableRisingTrig_0_31(BUTTON_1_EXTI_LINE);
			LL_EXTI_DisableFallingTrig_0_31(BUTTON_1_EXTI_LINE);
		}
	}
	else if(GPIO_Pin == BUTTON_2_Pin){
		//----------------------------------------------------------------------------
		if((!READ_PIN_IN(BUTTON_2))){
			button2.button_count = 0;
			button2.button_flag = 1;
			button2.button_pressed = 1;
			button2.button_pressed_action = 1;
			LL_EXTI_EnableRisingTrig_0_31(BUTTON_2_EXTI_LINE);
			LL_EXTI_DisableFallingTrig_0_31(BUTTON_2_EXTI_LINE);
		}
	}
	else if(GPIO_Pin == BUTTON_3_Pin){
		//----------------------------------------------------------------------------
		if((!READ_PIN_IN(BUTTON_3))){
			button3.button_count = 0;
			button3.button_flag = 1;
			button3.button_pressed = 1;
			button3.button_pressed_action = 1;
			LL_EXTI_EnableRisingTrig_0_31(BUTTON_3_EXTI_LINE);
			LL_EXTI_DisableFallingTrig_0_31(BUTTON_3_EXTI_LINE);
		}
	}
#if (CONFIG_LIS3DH)
	else if(GPIO_Pin == ACCEL_INT_Pin){
		if((!READ_PIN_IN(ACCEL_INT))){
			LL_EXTI_EnableRisingTrig_0_31(ACCEL_INT_EXINT_LINE);
			LL_EXTI_DisableFallingTrig_0_31(ACCEL_INT_EXINT_LINE);
		}
	}
#endif
#if VECTOR_AUDIO_DEMO_KEYS
	audio_demo_key_handler(GPIO_Pin);
#endif
}

void HAL_UART_TxHalfCpltCallback(UART_HandleTypeDef *huart)
{
	if(huart->Instance == USART_COM.Instance){
//		SET_OFF(RS485_DE);
	}
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	uint8_t data_uart = 0;
	__HAL_UART_CLEAR_IT(huart, UART_CLEAR_PEF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF | UART_CLEAR_IDLEF);
	data_uart = huart->Instance->RDR;
	UNUSED(data_uart);
	__HAL_UART_ENABLE_IT(huart, UART_IT_RXNE);
}
/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32U5xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32u5xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles RTC non-secure interrupt.
  */
void RTC_IRQHandler(void)
{
  /* USER CODE BEGIN RTC_IRQn 0 */

  /* USER CODE END RTC_IRQn 0 */
  HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc);
  /* USER CODE BEGIN RTC_IRQn 1 */

  /* USER CODE END RTC_IRQn 1 */
}

/**
  * @brief This function handles EXTI Line1 interrupt.
  */
void EXTI1_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI1_IRQn 0 */

  /* USER CODE END EXTI1_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(BUTTON_1_Pin);
  /* USER CODE BEGIN EXTI1_IRQn 1 */

  /* USER CODE END EXTI1_IRQn 1 */
}

/**
  * @brief This function handles EXTI Line2 interrupt.
  */
void EXTI2_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI2_IRQn 0 */

  /* USER CODE END EXTI2_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(BUTTON_2_Pin);
  /* USER CODE BEGIN EXTI2_IRQn 1 */

  /* USER CODE END EXTI2_IRQn 1 */
}

/**
  * @brief This function handles EXTI Line3 interrupt.
  */
void EXTI3_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI3_IRQn 0 */

  /* USER CODE END EXTI3_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(BUTTON_3_Pin);
  /* USER CODE BEGIN EXTI3_IRQn 1 */

  /* USER CODE END EXTI3_IRQn 1 */
}

/**
  * @brief This function handles TIM3 global interrupt.
  */
void TIM3_IRQHandler(void)
{
  /* USER CODE BEGIN TIM3_IRQn 0 */

  /* USER CODE END TIM3_IRQn 0 */
  HAL_TIM_IRQHandler(&htim3);
  /* USER CODE BEGIN TIM3_IRQn 1 */

  /* USER CODE END TIM3_IRQn 1 */
}

/**
  * @brief This function handles TIM6 global interrupt.
  */
void TIM6_IRQHandler(void)
{
  /* USER CODE BEGIN TIM6_IRQn 0 */

  /* USER CODE END TIM6_IRQn 0 */
  HAL_TIM_IRQHandler(&htim6);
  /* USER CODE BEGIN TIM6_IRQn 1 */

  /* USER CODE END TIM6_IRQn 1 */
}

/**
  * @brief This function handles I2C1 Error interrupt.
  */
void I2C1_ER_IRQHandler(void)
{
  /* USER CODE BEGIN I2C1_ER_IRQn 0 */

  /* USER CODE END I2C1_ER_IRQn 0 */
  HAL_I2C_ER_IRQHandler(&hi2c1);
  /* USER CODE BEGIN I2C1_ER_IRQn 1 */

  /* USER CODE END I2C1_ER_IRQn 1 */
}

/**
  * @brief This function handles SPI1 global interrupt.
  */
void SPI1_IRQHandler(void)
{
  /* USER CODE BEGIN SPI1_IRQn 0 */

  /* USER CODE END SPI1_IRQn 0 */
  HAL_SPI_IRQHandler(&hspi1);
  /* USER CODE BEGIN SPI1_IRQn 1 */

  /* USER CODE END SPI1_IRQn 1 */
}

/**
  * @brief This function handles SPI2 global interrupt.
  */
void SPI2_IRQHandler(void)
{
  /* USER CODE BEGIN SPI2_IRQn 0 */

  /* USER CODE END SPI2_IRQn 0 */
  HAL_SPI_IRQHandler(&hspi2);
  /* USER CODE BEGIN SPI2_IRQn 1 */

  /* USER CODE END SPI2_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */
#if CONFIG_GPS
	uint8_t data_uart = 0;
	if (UART_IRQReceive(&huart1, &data_uart)){
		Gps_Data_Verification(data_uart);
	}
	else{
#endif
  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */
#if CONFIG_GPS
	}
#endif
  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles USART2 global interrupt.
  */
void USART2_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_IRQn 0 */
	uint8_t data_uart = 0;
	if (UART_IRQReceive(&huart2, &data_uart)){
#if LTE_DEBUG
		USART_DEBUG.Instance->TDR = data_uart;
#endif
#if CONFIG_LORA
			add_to_buffer(&InputBuffer[TYPE_LORA], data_uart);
#endif
	}
	else{
  /* USER CODE END USART2_IRQn 0 */
  HAL_UART_IRQHandler(&huart2);
  /* USER CODE BEGIN USART2_IRQn 1 */
	}
  /* USER CODE END USART2_IRQn 1 */
}

/**
  * @brief This function handles USART3 global interrupt.
  */
void USART3_IRQHandler(void)
{
  /* USER CODE BEGIN USART3_IRQn 0 */
	uint8_t data_uart = 0;
	if (UART_IRQReceive(&huart3, &data_uart)){
#if BLE_DEBUG
		USART_DEBUG.Instance->TDR = data_uart;
#endif
#if CONFIG_BLE
			add_to_buffer(&InputBuffer[TYPE_BLE], data_uart);
#endif
	}
	else{
  /* USER CODE END USART3_IRQn 0 */
  HAL_UART_IRQHandler(&huart3);
  /* USER CODE BEGIN USART3_IRQn 1 */
	}
  /* USER CODE END USART3_IRQn 1 */
}

/**
  * @brief This function handles UART4 global interrupt.
  */
void UART4_IRQHandler(void)
{
  /* USER CODE BEGIN UART4_IRQn 0 */
	uint8_t data_uart = 0;
	if (UART_IRQReceive(&huart4, &data_uart)){
		add_to_buffer(&InputBuffer[TYPE_USART], data_uart);
#if BLE_DEBUG
		huart3.Instance->TDR = data_uart;
#endif
#if LTE_DEBUG
		huart2.Instance->TDR = data_uart;
#endif
//		buffer_uart[TYPE_USART].TimeRX = TIME_OUT_UART;
#if CONFIG_UART
//		SET_STATUS_COMMON_BIT(ST_COMMON_BIT_DATA_EXCHANGE);
//		timer_end_data_exchange = TIME_END_DATA_EXCHANGE;                       // Запуск таймера автоматического окончания режима передачи данных
//		START_TIMER_RTC(TIMER_RTC_UART_RX_WAKE_UP, TIME_RTC_UART_RX_WAKE_UP);	// Запуск таймера на вход в сон, когда передача не активна, но включена
#endif
	}
	else{
  /* USER CODE END UART4_IRQn 0 */
  HAL_UART_IRQHandler(&huart4);
  /* USER CODE BEGIN UART4_IRQn 1 */
	}
  /* USER CODE END UART4_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 8 global interrupt.
  */
void GPDMA1_Channel8_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel8_IRQn 0 */

  /* USER CODE END GPDMA1_Channel8_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel8);
  /* USER CODE BEGIN GPDMA1_Channel8_IRQn 1 */

  /* USER CODE END GPDMA1_Channel8_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 9 global interrupt.
  */
void GPDMA1_Channel9_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel9_IRQn 0 */

  /* USER CODE END GPDMA1_Channel9_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel9);
  /* USER CODE BEGIN GPDMA1_Channel9_IRQn 1 */

  /* USER CODE END GPDMA1_Channel9_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 10 global interrupt.
  */
void GPDMA1_Channel10_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel10_IRQn 0 */

  /* USER CODE END GPDMA1_Channel10_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel10);
  /* USER CODE BEGIN GPDMA1_Channel10_IRQn 1 */

  /* USER CODE END GPDMA1_Channel10_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 11 global interrupt.
  */
void GPDMA1_Channel11_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel11_IRQn 0 */

  /* USER CODE END GPDMA1_Channel11_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel11);
  /* USER CODE BEGIN GPDMA1_Channel11_IRQn 1 */

  /* USER CODE END GPDMA1_Channel11_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
