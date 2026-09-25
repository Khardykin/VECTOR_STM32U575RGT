/**
  ******************************************************************************
  * @file    vector_board.c
  * @brief   Бортовая инициализация до RTOS (см. vector_board.h)
  ******************************************************************************
  */
#include "vector_board.h"
#include "vector_config.h"
#include "vector_log.h"
#include "vector_sys.h"
#include "spiflash.h"
#include "audio_player.h"
#include "main.h"

/* Грубая задержка циклом для кода ДО планировщика: tx_thread_sleep() там ещё
   не работает, а HAL_Delay() мы сознательно не используем. При 160 МГц и -O2
   одна итерация volatile-цикла с __NOP() это ~4-5 тактов, то есть
   n = 200000 -> примерно 5 мс. Точность здесь не нужна.                     */
static void board_delay_cycles(uint32_t n)
{
  volatile uint32_t d = n;
  while (d > 0u)
  {
    d--;
    __NOP();
  }
}

/* Всё, что нужно сделать в main() до MX_ThreadX_Init(). См. vector_board.h. */
void vector_board_init(void)
{
#if VECTOR_AUDIO_SELFTEST
  uint32_t i;
#endif

  /* 1. Оконечный усилитель. MAX98357A-подобные: "Drive SD_MODE low to put the
        IC into shutdown" - пока на PC9 ноль, звука не будет при любом рабочем
        SAI/DMA. Правильное место для этого - CubeMX (PC9 -> GPIO output level
        = High), тогда шаг убирается целиком.                                 */
  HAL_GPIO_WritePin(SD_MODE_GPIO_Port, SD_MODE_Pin, GPIO_PIN_SET);
  board_delay_cycles(200000u);          /* ~5 мс на выход из shutdown */

  /* 2. Приборы времени (и заморозка тайм-базы под отладчиком) */
  vector_sys_init();

  /* 3. Проба внешней SPI flash: sf_jedec должно быть { C2 28 17 }
        (Macronix MX25R6435F, 64 Мбит), sf_probe_rc = 0. Чистый опрос, RTOS не
        нужен. Работает до планировщика, поэтому DMA здесь не используется.   */
  (void)sf_probe();
  LOG_I(VLOG_M_SYS, "probe rc=%d jedec=%x %x %x", (int32_t)sf_probe_rc,
        (uint32_t)sf_jedec[0], (uint32_t)sf_jedec[1], (uint32_t)sf_jedec[2]);

#if VECTOR_AUDIO_SELFTEST
  /* 4. ПРЯМАЯ проверка звукового тракта: const-PCM из внутренней flash ->
        SAI DMA -> усилитель. Без очереди, потока, состояний и внешней памяти.
        Возврат: 0 = DMA стартовала И завершилась, 1 = не стартовала,
        2 = стартовала, но не завершилась (нет GPDMA1_Channel11_IRQn).
        Если 0, а звука нет - проблема аналоговая: SD_MODE / питание /
        обвязка / динамик (чек-лист в docs/AUDIO.md, раздел 7.4).      */
  for (i = 0; i < (uint32_t)VECTOR_AUDIO_SELFTEST; i++)
  {
    if (audio_selftest() != 0)
    {
      break;                            /* тракт не работает - повторять нечего */
    }
    board_delay_cycles(12000000u);      /* ~300 мс между писками */
  }
#endif
}
