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
/* Инклуды проекта - В ОДНОМ МЕСТЕ (агрегатор Vector_main.h): audio_player,
   vector_tasks, extstore, vector_config/log, main.h, Menu_vector.h
   (Vector_Run_Indication), Notify_vector.h (Notify_Tick: фазы свет/звук/
   вибро ~10 мс), TFT.h/lvgl.h (TFT_Initialized, lv_timer_handler - при
   CONFIG_TYPE_LCD_TFT && VECTOR_LCD_USE_LVGL).                              */
#include "Vector_main.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Размеры стека для задач (в байтах). */
#define LVGL_STACK_SIZE    4096

/* Дескриптор потока ThreadX */
static TX_THREAD lvgl_thread;

/* Выделение памяти под стек потока (выравнивание обязательно для ThreadX) */
static uint8_t lvgl_stack[LVGL_STACK_SIZE]   __attribute__((aligned(8)));
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
 * Поток дисплея/индикации (LVGL Task).
 * -------------------------------------------------------------------------*/
void lvgl_thread_entry(ULONG thread_input)
{
  (void)thread_input;

  while (1)
  {
    if (tasks_status.cnt_pre_init != 0u)     /* Pre_Init завершён */
    {
      if (timer.flag_100ms != 0u)
      {
        timer.flag_100ms = 0u;
#if CONFIG_DISPLAY
        Vector_Run_Indication();
#endif
      }
      /* Секвенсор оповещений (Notify_vector): тик движка ~каждые 10 мс.
         Вне гейта flag_100ms и вне CONFIG_DISPLAY: работает и при выключенном
         приборе - сценарий включения (Button_Run_Start -> NotifySeq_PowerOn)
         стартует как раз в состоянии OFF. */
      Notify_Tick();
#if (CONFIG_TYPE_LCD_TFT && VECTOR_LCD_USE_LVGL)
      if (TFT_Initialized == 1u)
      {
        lv_timer_handler();
      }
#endif
    }
    tx_thread_sleep(1u);                     /* 10 мс */
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
       2) audio_init- читает sounds.bin, создаёт поток плеера
       3) vector_tasks_init - потоки "Receiver Task" (приём/парсинг UART)
                          и "Measure Task" (периодика прибора)      */
  vlog_init();
  ext_init();
  audio_init();
  vector_tasks_init();

  /* Создаем поток для Графики LVGL (Приоритет 15 - средний). Звук целиком
     живёт в потоке "Audio Player" из audio_player.c (создан в audio_init). */
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
