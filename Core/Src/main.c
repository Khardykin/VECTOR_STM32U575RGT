/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "app_threadx.h"
#include "main.h"
#include "gpdma.h"
#include "i2c.h"
#include "icache.h"
#include "rtc.h"
#include "sai.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Vector_main.h"    /* CONFIG_*, Uart_Gps/Lora_Receive_Timer_Inc */
#include "vector_board.h"   /* вся бортовая инициализация до RTOS - один вызов */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

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
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPDMA1_Init();
  MX_I2C1_Init();
  MX_SAI1_Init();
  MX_SPI1_Init();
  MX_SPI2_Init();
  MX_TIM2_Init();
  MX_UART4_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_USART2_UART_Init();
  MX_ICACHE_Init();
  MX_RTC_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  /* Вся бортовая инициализация до RTOS: усилитель (SD_MODE), проба внешней
     flash и selftest звука. Что именно делает и что из этого надо перенести
     в CubeMX - см. vector_board.h и docs/SYSTEM.md (раздел 9).              */
  vector_board_init();
  /* USER CODE END 2 */

  MX_ThreadX_Init();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_4);
  while(LL_FLASH_GetLatency()!= LL_FLASH_LATENCY_4)
  {
  }

  LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
  while (LL_PWR_IsActiveFlag_VOS() == 0)
  {
  }
  LL_RCC_HSE_Enable();

   /* Wait till HSE is ready */
  while(LL_RCC_HSE_IsReady() != 1)
  {
  }

  LL_PWR_EnableBkUpAccess();
  while (LL_PWR_IsEnabledBkUpAccess () == 0U)
  {
  }

  LL_RCC_LSE_SetDriveCapability(LL_RCC_LSEDRIVE_LOW);
  LL_RCC_LSE_EnablePropagation();
  LL_RCC_LSE_Enable();

   /* Wait till LSE is ready */
  while(LL_RCC_LSE_IsReady() != 1)
  {
  }

  LL_RCC_PLL1_ConfigDomain_SYS(LL_RCC_PLL1SOURCE_HSE, 1, 20, 1);
  LL_RCC_PLL1_EnableDomain_SYS();
  LL_RCC_SetPll1EPodPrescaler(LL_RCC_PLL1MBOOST_DIV_1);
  LL_RCC_PLL1_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_8_16);
  LL_RCC_PLL1_Enable();

   /* Wait till PLL is ready */
  while(LL_RCC_PLL1_IsReady() != 1)
  {
  }

   /* Intermediate AHB prescaler 2 when target frequency clock is higher than 80 MHz */
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_2);

  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);

   /* Wait till System clock is ready */
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL1)
  {
  }

  /* Insure 1us transition state at intermediate medium speed clock*/
  for (__IO uint32_t i = (160 >> 1); i !=0; i--);

  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
  LL_RCC_SetAPB3Prescaler(LL_RCC_APB3_DIV_1);
  LL_SetSystemCoreClock(160000000);

   /* Update the time base */
  if (HAL_InitTick (TICK_INT_PRIORITY) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  LL_RCC_PLL3_ConfigDomain_SAI(LL_RCC_PLL3SOURCE_HSE, 2, 56, 20);
  LL_RCC_PLL3_EnableDomain_SAI();
  LL_RCC_PLL3_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_4_8);
  LL_RCC_PLL3_Enable();

   /* Wait till PLL3 is ready */
  while(LL_RCC_PLL3_IsReady() != 1)
  {
  }
  LL_RCC_PLL3FRACN_Enable();
  LL_RCC_PLL3_SetFRACN(3671);
}

/* USER CODE BEGIN 4 */
//===========================================================================================================================
// Обработчик прерываний таймера 2
// Обработчик таймера интервал вызова (1mS)
void Countdown_Timer_Chan(void)
{
	uint8_t i = 0;
	for(uint8_t chan = 0; chan < COUNT_CHAN; chan++){
		for(i=0; i < COUNT_TIMERS_CHAN; i++)
		{
			if(countdown_time_chan[chan].Timers[i])
			{
				countdown_time_chan[chan].Timers[i] --;
				if(!countdown_time_chan[chan].Timers[i])
					END_TIMER_CH(i, chan);
			}
		}
	}
}

void Countdown_Timer(void)
{
	uint8_t i = 0;
	for(i=0; i < COUNT_TIMERS; i++)
	{
		if(countdown_time.Timers[i])
		{
			countdown_time.Timers[i] --;
			if(!countdown_time.Timers[i])
				END_TIMER(i);
		}
	}
}
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */
//	if (htim->Instance == TIM6){
//		SET_TGL(ALARM_LED_1);
//	}
  /* пусто: мигалку/счётчики сюда ставить не нужно, всё ниже в hook */
  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
  else if(htim->Instance == TIM3){
	  Countdown_Timer();
	  Countdown_Timer_Chan();
	  timer.flag_1ms = 1;
	  timer.count_1ms++;
	  if(timer.count_1ms >= 10){
		  timer.count_1ms = 0;
		  timer.flag_10ms = 1;
		  timer.count_10ms++;
		  if((timer.count_10ms%10) == 0){
			  timer.flag_100ms = 1;
			  timer.count_100ms++;
			  if(timer.count_100ms >= 10){
				  timer.count_10ms = 0;
				  timer.count_100ms = 0;
				  timer.flag_1s = 1;
				  timer.count_1s++;
			  }
		  }
	  }
	  if(button.button_flag){
		  button.button_count++;
	  }
	  if(button2.button_flag){
		  button2.button_count++;
	  }
	  if(button3.button_flag){
		  button3.button_count++;
	  }
	  //--------------------------------------------------------------------------
#if CONFIG_UART
	  Uart_Command_Receive_Timer_Inc();
#endif
#if CONFIG_GPS
	  Uart_Gps_Receive_Timer_Inc();    /* таймаут кадра NMEA (TIME_OUT_GPS)  */
#endif
#if CONFIG_LORA
	  Uart_Lora_Receive_Timer_Inc();   /* таймаут AT-ответа LoRa             */
#endif
//	  SET_TGL(STATE_LED);
  }
  /* пусто: время приложения считается только от тика ThreadX (vector_tick.h),
     TIM6 обслуживает исключительно внутреннюю тайм-базу HAL.               */
  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
