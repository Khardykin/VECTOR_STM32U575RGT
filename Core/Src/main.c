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
#include "spiflash.h"
#include "vector_config.h"
#include "vector_log.h"
#include "vector_time.h"
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
/* ------------------------------------------------------------------------
 * ПРИБОРЫ для проверки системного тика. Тайм-база HAL сейчас - TIM6
 * (NVIC.TimeBase=TIM6_IRQn в .ioc, stm32u5xx_hal_timebase_tim.c), 1 кГц:
 * PCLK1 160 МГц / PSC 159 -> 1 МГц, ARR 999 -> прерывание раз в 1 мс.
 *
 *   sys_dbg_ticks            сколько раз сработало прерывание тайм-базы
 *   sys_dbg_tick_min_us      минимальный период между ними (норма 1000)
 *   sys_dbg_tick_max_us      максимальный период (норма ~1000; если сильно
 *                            больше - прерывание тика задерживают: приоритет
 *                            TIM6_IRQn = 15, самый низкий, а ядро ThreadX
 *                            закрывает прерывания через PRIMASK внутри
 *                            каждого своего вызова)
 *   sys_dbg_hal_tick         HAL_GetTick() на момент последнего замера -
 *                            сверяем счётчик прерываний с uwTick: они должны
 *                            совпадать, иначе тики теряются
 * Период между замерами меряется по DWT->CYCCNT (vector_time.h, 6.25 нс),
 * а НЕ по HAL_GetTick - иначе прибор измерял бы сам себя.
 * Раз в VECTOR_LOG_TICK_REPORT_S секунд всё это печатается в лог из потока
 * (см. lvgl_thread_entry): из прерывания лог не печатается принципиально.
 * ------------------------------------------------------------------------ */
volatile uint32_t sys_dbg_ticks       = 0;
volatile uint32_t sys_dbg_tick_min_us = 0xFFFFFFFFu;
volatile uint32_t sys_dbg_tick_max_us = 0;
volatile uint32_t sys_dbg_hal_tick    = 0;
static   uint32_t sys_tick_last_us    = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
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
  /* USER CODE BEGIN 2 */
  /* ==========================================================================
   * Включение оконечного усилителя на I2S (PC9 = SD_MODE, MAX98357A-подобный).
   *
   * MX_GPIO_Init() пишет в SD_MODE уровень RESET, а у MAX98357A
   * "Drive SD_MODE low to put the IC into shutdown" -> усилитель выключен и
   * звука НЕ БУДЕТ ВООБЩЕ, даже если SAI/DMA работают идеально.
   *
   * После подачи 1 на SD_MODE нужно выдержать время выхода из shutdown
   * (порядка единиц мс по datasheet), иначе первые сэмплы проглатываются.
   * ========================================================================== */
  HAL_GPIO_WritePin(SD_MODE_GPIO_Port, SD_MODE_Pin, GPIO_PIN_SET);
  HAL_Delay(5);

  /* Проба внешней SPI flash (MX25K6435F, DD2). Результат смотреть в отладчике:
       sf_jedec[3] - должно быть { 0xC2, .., 0x17 }  (Macronix, 64 Мбит)
       sf_rdsr     - статус-регистр
       sf_probe_rc - 0 (HAL_OK), если чип ответил; иначе смотри SPI1/CS/питание */
  (void)sf_probe();

  /* Лог старта ещё до RTOS: USART1 уже инициализирован, а vlog до vlog_init()
     просто работает без мьютекса (исполнитель здесь один). */
  LOG_I(VLOG_M_SYS, "probe rc=%d jedec=%x %x %x", (int32_t)sf_probe_rc,
        (uint32_t)sf_jedec[0], (uint32_t)sf_jedec[1], (uint32_t)sf_jedec[2]);

  /* Чем реально тикает система. Регистры TIM6 читаем напрямую, чтобы не
     зависеть от имени хендла в сгенерированном файле (оно уже менялось
     TIM1 -> TIM7 -> TIM6). Ожидание при PCLK1 = 160 МГц: PSC = 159,
     ARR = 999, период = 1000 us.
     Другие источники времени в проекте: SysTick = тик ThreadX, 100 Гц (10 мс,
     настраивает tx_initialize_low_level.S, SYSTEM_CLOCK = 160000000), и
     RTC WakeUp = CK_SPRE 1 Гц, то есть его счётчик в СЕКУНДАХ.
     Подробно - docs/CODE_MAP.md, раздел 7.                                  */
  {
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint64_t us    = ((uint64_t)(TIM6->PSC + 1u) * (uint64_t)(TIM6->ARR + 1u)
                      * 1000000ull) / pclk1;
    LOG_I(VLOG_M_SYS, "tick: PCLK1=%u Hz TIM6 PSC=%u ARR=%u -> %u us (core %u Hz)",
          pclk1, (uint32_t)TIM6->PSC, (uint32_t)TIM6->ARR, (uint32_t)us,
          vtime_clock_hz());
  }
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
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.LSIDiv = RCC_LSI_DIV1;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 1;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

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

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
  if (htim->Instance == TIM6)
  {
    /* КОНТЕКСТ: прерывание тайм-базы, 1 кГц. Здесь можно только читать
       регистр, инкрементить счётчики и дёргать пин - НИКАКОГО лога и ничего
       блокирующего (лог из ISR отбрасывается намеренно, см. vector_log.c).
       Мигалку отсюда убрали: если нужен ровный меандр, его даёт аппаратный
       выход таймера (PWM/OC), а не переключение пина в прерывании - у ISR
       приоритет 15 (ниже всех) и его сдвигает любое другое прерывание.      */
    uint32_t now = vtime_us();

    if (sys_dbg_ticks != 0u)
    {
      uint32_t d = (uint32_t)(now - sys_tick_last_us);
      if (d < sys_dbg_tick_min_us) { sys_dbg_tick_min_us = d; }
      if (d > sys_dbg_tick_max_us) { sys_dbg_tick_max_us = d; }
    }
    sys_tick_last_us = now;
    sys_dbg_ticks++;
    sys_dbg_hal_tick = HAL_GetTick();
  }
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
