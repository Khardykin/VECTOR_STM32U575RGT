/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_azure_rtos.c
  * @author  MCD Application Team
  * @brief   app_azure_rtos application implementation file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
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
#include "app_azure_rtos.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sai.h"
#include "audio_player.h"
#include "extstore.h"
#include "uart_bridge.h"
#include "vector_config.h"
#include "vector_log.h"
#include "main.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Размеры стека для задач (в байтах) */
#define AUDIO_STACK_SIZE   2048   /* было 1024 - слишком впритык, см. отчёт   */
#define LVGL_STACK_SIZE    4096

/* Дескрипторы потоков ThreadX */
static TX_THREAD audio_thread;
static TX_THREAD lvgl_thread;

/* Выделение памяти под стеки потоков (выравнивание обязательно для ThreadX) */
static uint8_t audio_stack[AUDIO_STACK_SIZE] __attribute__((aligned(8)));
static uint8_t lvgl_stack[LVGL_STACK_SIZE]   __attribute__((aligned(8)));

/* Семафор: взводится из HAL_SAI_TxCpltCallback / HAL_SAI_ErrorCallback (ISR) */
static TX_SEMAPHORE audio_done_sem;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

#if (USE_STATIC_ALLOCATION == 1)

/* USER CODE BEGIN TX_Pool_Buffer */
/* USER CODE END TX_Pool_Buffer */
#if defined ( __ICCARM__ )
#pragma data_alignment=4
#endif
__ALIGN_BEGIN static UCHAR tx_byte_pool_buffer[TX_APP_MEM_POOL_SIZE] __ALIGN_END;
static TX_BYTE_POOL tx_app_byte_pool;

#endif

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
/* ---------------------------------------------------------------------------
 * Счётчики для диагностики (смотреть в отладчике / выводить в UART)
 * -------------------------------------------------------------------------*/
volatile uint32_t audio_start_errors = 0;  /* HAL_SAI_Transmit_DMA != HAL_OK   */
volatile uint32_t audio_timeouts     = 0;  /* DMA не завершилась за 4 с        */
volatile uint32_t audio_sai_errors   = 0;  /* HAL_SAI_ErrorCallback (OVRUDR..) */
volatile uint32_t audio_sai_errcode  = 0;
volatile uint32_t audio_played_ok    = 0;

/* ---------------------------------------------------------------------------
 * Поток воспроизведения звука.
 *
 * Схема: поток запускает one-shot DMA, затем БЛОКИРУЕТСЯ на семафоре, который
 * взводит ISR по факту окончания передачи. Это надёжнее, чем спать
 * "расчётное" время: при малейшем рассинхроне следующий вызов
 * HAL_SAI_Transmit_DMA() вернул бы HAL_BUSY и звук молча пропал.
 * -------------------------------------------------------------------------*/
void audio_thread_entry(ULONG thread_input)
{
  /* Воспроизведением теперь владеет поток из audio_player.c
     (создаётся в audio_init()). Этот поток больше не нужен - усыпляем навсегда.
     Держим объект, чтобы не править tx_thread_create ниже. */
  (void)thread_input;
  tx_thread_sleep(TX_WAIT_FOREVER);
}

/* ---------------------------------------------------------------------------
 * Колбэки HAL SAI. Вызываются из контекста ISR (GPDMA1_Channel11_IRQHandler).
 * Здесь НЕЛЬЗЯ делать ничего блокирующего - только tx_semaphore_put().
 * -------------------------------------------------------------------------*/
/* ---------------------------------------------------------------------------
 * Поток для дисплея и LVGL
 * -------------------------------------------------------------------------*/
void lvgl_thread_entry(ULONG thread_input)
{
  (void)thread_input;
  /* Тут в будущем инициализируем LVGL и дисплей */
  /* lv_init(); */

  while (1)
  {
    /* Вызов периодического обработчика таймеров LVGL */
    /* lv_timer_handler(); */

    /* Пока LVGL нет, поток только жрёт CPU: 100 пробуждений в секунду впустую.
       До подключения LVGL лучше спать подольше. */
    tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 10u);   /* 100 мс */
  }
}
/* USER CODE END PFP */

/**
  * @brief  Define the initial system.
  * @param  first_unused_memory : Pointer to the first unused memory
  * @retval None
  */
VOID tx_application_define(VOID *first_unused_memory)
{
  /* USER CODE BEGIN  tx_application_define_1*/
  /* Порядок важен:
       0) vlog_init - консольный лог (USART1), чтобы шаги ниже были видны
       1) ext_init  - мьютекс шины flash + сканирование журнала (до потоков!)
       2) audio_init- читает sounds.img, создаёт поток плеера
       3) uart_bridge_init - мост UART4<->USART2                       */
  vlog_init();
  ext_init();
  audio_init();
#if VECTOR_UART_BRIDGE_TEST
  uart_bridge_init();
#endif

  /* 0. Семафор "DMA завершила передачу". Начальное состояние 0.
        ВАЖНО: создать ДО потоков - иначе audio_thread дёрнет несуществующий
        объект. tx_semaphore_put() из ISR разрешён. */
  if (tx_semaphore_create(&audio_done_sem, "audio done sem", 0) != TX_SUCCESS)
  {
    /* USER CODE BEGIN Semaphore_Error */
    while (1) { }
    /* USER CODE END Semaphore_Error */
  }

  /* 1. Создаем поток для Аудио (Приоритет 10 - выше графики) */
  if (tx_thread_create(&audio_thread, "Audio Task", audio_thread_entry, 0,
                       audio_stack, AUDIO_STACK_SIZE,
                       10, 10, TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    /* USER CODE BEGIN AudioThread_Error */
    while (1) { }
    /* USER CODE END AudioThread_Error */
  }

  /* 2. Создаем поток для Графики LVGL (Приоритет 15 - средний) */
  if (tx_thread_create(&lvgl_thread, "LVGL Task", lvgl_thread_entry, 0,
                       lvgl_stack, LVGL_STACK_SIZE,
                       15, 15, TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    /* USER CODE BEGIN LvglThread_Error */
    while (1) { }
    /* USER CODE END LvglThread_Error */
  }
  /* USER CODE END  tx_application_define_1 */
#if (USE_STATIC_ALLOCATION == 1)
  UINT status = TX_SUCCESS;
  VOID *memory_ptr;

  if (tx_byte_pool_create(&tx_app_byte_pool, "Tx App memory pool", tx_byte_pool_buffer, TX_APP_MEM_POOL_SIZE) != TX_SUCCESS)
  {
    /* USER CODE BEGIN TX_Byte_Pool_Error */

    /* USER CODE END TX_Byte_Pool_Error */
  }
  else
  {
    /* USER CODE BEGIN TX_Byte_Pool_Success */

    /* USER CODE END TX_Byte_Pool_Success */

    memory_ptr = (VOID *)&tx_app_byte_pool;
    status = App_ThreadX_Init(memory_ptr);
    if (status != TX_SUCCESS)
    {
      /* USER CODE BEGIN  App_ThreadX_Init_Error */
      while(1)
      {
      }
      /* USER CODE END  App_ThreadX_Init_Error */
    }
    /* USER CODE BEGIN  App_ThreadX_Init_Success */

    /* USER CODE END  App_ThreadX_Init_Success */

  }

#else
/*
 * Using dynamic memory allocation requires to apply some changes to the linker file.
 * ThreadX needs to pass a pointer to the first free memory location in RAM to the tx_application_define() function,
 * using the "first_unused_memory" argument.
 * This require changes in the linker files to expose this memory location.
 * For EWARM add the following section into the .icf file:
     place in RAM_region    { last section FREE_MEM };
 * For MDK-ARM
     - either define the RW_IRAM1 region in the ".sct" file
     - or modify the line below in "tx_initialize_low_level.S to match the memory region being used
        LDR r1, =|Image$$RW_IRAM1$$ZI$$Limit|

 * For STM32CubeIDE add the following section into the .ld file:
     ._threadx_heap :
       {
          . = ALIGN(8);
          __RAM_segment_used_end__ = .;
          . = . + 64K;
          . = ALIGN(8);
        } >RAM_D1 AT> RAM_D1
    * The simplest way to provide memory for ThreadX is to define a new section, see ._threadx_heap above.
    * In the example above the ThreadX heap size is set to 64KBytes.
    * The ._threadx_heap must be located between the .bss and the ._user_heap_stack sections in the linker script.
    * Caution: Make sure that ThreadX does not need more than the provided heap memory (64KBytes in this example).
    * Read more in STM32CubeIDE User Guide, chapter: "Linker script".

 * The "tx_initialize_low_level.S" should be also modified to enable the "USE_DYNAMIC_MEMORY_ALLOCATION" flag.
 */

  /* USER CODE BEGIN DYNAMIC_MEM_ALLOC */
  (void)first_unused_memory;
  /* USER CODE END DYNAMIC_MEM_ALLOC */
#endif

}
