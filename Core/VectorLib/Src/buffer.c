#include "Vector_main.h"

#include <string.h>   /* memcpy: каждый .c несёт свои include сам */

#if (DMA_USART == 1)
//	#include "gpdma.h"
#endif
T_Buffer InputBuffer[TYPE_USART_COUNT] = {0}; //Буфер приема по usart
/* Секция .sram4 объявлена в STM32U575RGTX_FLASH.ld / _RAM.ld (>SRAM4, 16 КБ
   @0x28000000). Такт SRAM4 включает vector_board_init(): __HAL_RCC_SRAM4_CLK_ENABLE().
   Пока DMA_USART == 0 массив не используется (передача опросом).             */
__attribute__((section(".sram4")))  uint8_t buffer_transmit[TYPE_USART_COUNT][300];
//======================================================================================================================================
//Инициализация меток буфера
void Init_Buffer(T_Buffer *Buffer)
{
  Buffer->begin = 0;
  Buffer->end = 0;
}

//======================================================================================================================================
/* Добавление принятого байта. КОНТЕКСТ: ISR приёма UART (единственный
   производитель - правило SPSC, см. buffer.h).
   Маскирование прерываний здесь НЕ НУЖНО и даже вредно: прежние безусловные
   __disable_irq()/__enable_irq() из потока внутри критической секции RTOS
   сорвали бы маску. Писатель трогает только свою метку end, читатель - только
   свою begin; на Cortex-M33 выровненное 16-битное чтение/запись атомарно.
   Возврат: 0 = байт добавлен, 1 = буфер полон (байт ПОТЕРЯН).               */
uint16_t add_to_buffer(T_Buffer *Buffer, uint8_t ch)
{
  uint16_t end  = Buffer->end;                 /* своя метка - можно в локаль */
  uint16_t next = (uint16_t)(end + 1u);

  if (next >= BUFFER_LENGTH)
  {
    next = 0;
  }
  if (next == Buffer->begin)
  {
    return 1;                                  /* полно: один слот всегда пуст */
  }

  Buffer->buffer[end] = ch;
  __DMB();                                 /* сначала данные, ПОТОМ метка:
                                              барьер не даёт компилятору/ядру
                                              опубликовать end раньше байта */
  Buffer->end = next;                      /* публикуем байт ПОСЛЕ записи */
  return 0;
}

//======================================================================================================================================
//передача пакета
void transmit_buffer(uint8_t *pData, uint16_t Size, uint8_t type_transmit)
{
#if CONFIG_UART
	UART_HandleTypeDef *huart_ptr = NULL;

	/* Проверки границ: неизвестный тип или пустой/нулевой указатель раньше
	   уводили бы в memcpy за пределы buffer_transmit (PLAN.md §7 п.6).      */
	if ((pData == NULL) || (Size == 0u) || (type_transmit >= TYPE_USART_COUNT))
	{
		return;
	}

	if(type_transmit == TYPE_USART){
		huart_ptr  = &USART_COM;
	}
#if CONFIG_BLE
    else if (type_transmit == TYPE_BLE) {
        huart_ptr = &USART_BLE; // Ваш USART_BLE
    }
#endif
#if (CONFIG_LORA || CONFIG_LORA_G || CONFIG_G4 || CONFIG_G2)
    else if (type_transmit == TYPE_RF) {
        huart_ptr = &USART_RF; // Ваш USART_LORA
    }
#endif
    else if (type_transmit == TYPE_DEBUG) {
        huart_ptr = &USART_DEBUG; // Ваш SENSOR_UART (LPUART)
    }

	if (huart_ptr != NULL)
	{
#if (DMA_USART == 1)
		// 2. ОЖИДАНИЕ ГОТОВНОСТИ (Замена вашего цикла ожидания флага AT32)
		// В HAL STM32U5 состояние huart->gState автоматически переходит в BUSY во время DMA,
		// и возвращается в READY, когда GPDMA полностью завершит транзакцию.
		uint32_t start_time = GetTick();
		const uint32_t timeout_ms = 500;

		while (huart_ptr->gState != HAL_UART_STATE_READY)
		{
			if ((GetTick() - start_time) >= timeout_ms)
			{
				// Таймаут передачи, прерываем отправку нового пакета
				return;
			}
		}

		// 3. ПОДГОТОВКА ДАННЫХ И КОПИРОВАНИЕ В БУФЕР
		if (type_transmit == TYPE_USART)
		{
			// Если pData указывает на глобальный статический буфер, можно слать напрямую:
			// HAL_UART_Transmit_DMA сама сбросит старую конфигурацию GPDMA и запишет новые адреса
			HAL_UART_Transmit_DMA(huart_ptr, pData, Size);
		}
		else
		{
			// Пакет длиннее буфера передачи - не урезаем (обрезанный пакет
			// протокол всё равно не разберёт), отбрасываем целиком.
			if (Size > (uint16_t)sizeof(buffer_transmit[0]))
			{
				return;
			}

			// Для остальных типов копируем в ваш массив buffer_transmit
			memcpy(&buffer_transmit[type_transmit], pData, Size);

			// Запускаем передачу по DMA
			HAL_UART_Transmit_DMA(huart_ptr, &buffer_transmit[type_transmit][0], Size);
		}
#else
		// Обычный блокирующий режим (Polling)
		HAL_UART_Transmit(huart_ptr, pData, Size, 100);
#endif
	}
#endif
}

//======================================================================================================================================
/* Чтение ОДНОГО байта. КОНТЕКСТ: поток (единственный потребитель - SPSC).
   Метка end читается ОДИН раз в локальную копию: если ISR добавит байт между
   проверкой и чтением - не страшно, его заберёт следующий вызов. Прежняя
   версия сравнивала begin с end трижды и при begin == end обнуляла ОБЕ
   метки, включая чужую end, - байт, принятый ISR в этот момент, терялся
   (гонка, PLAN.md §7 п.2). Возврат: 1 = байт прочитан, 0 = буфер пуст.      */
uint16_t receive_buffer(T_Buffer *Buffer, uint8_t * ch)
{
  uint16_t end   = Buffer->end;                /* один volatile-снимок */
  uint16_t begin = Buffer->begin;

  if (begin >= BUFFER_LENGTH)                  /* страховка, в норме не бывает */
  {
    begin = 0;
  }
  if (begin == end)
  {
    Buffer->begin = begin;
    return 0;                                  /* пусто */
  }

  *ch = Buffer->buffer[begin];
  begin++;
  if (begin >= BUFFER_LENGTH)
  {
    begin = 0;
  }
  __DMB();                                 /* сначала прочитать байт, ПОТОМ
                                              освобождать слот (publish begin) */
  Buffer->begin = begin;                       /* своя метка - публикуем */
  return 1;
}

//======================================================================================================================================
//проверка на наличие необработаного байта в буфере
uint8_t check_buffer(T_Buffer *Buffer)
{
  if(Buffer->begin != Buffer->end) {
    return 1;
  } else
    return 0;
}

