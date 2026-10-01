#include "Vector_main.h"

#if (DMA_USART == 1)
//	#include "gpdma.h"
#endif
T_Buffer InputBuffer[TYPE_USART_COUNT] = {0}; //Буфер приема по usart
__attribute__((section(".sram4")))  uint8_t buffer_transmit[TYPE_USART_COUNT][300];
//======================================================================================================================================
//Инициализация меток буфера
void Init_Buffer(T_Buffer *Buffer)
{
  Buffer->begin = 0;
  Buffer->end = 0;
}

//======================================================================================================================================
//добавление принятого байта
uint16_t add_to_buffer(T_Buffer *Buffer, uint8_t ch)
{
  uint16_t res = 0;
  __disable_irq();

  if(Buffer->end == BUFFER_LENGTH) {
    if(Buffer->begin == 0) {
      res = 1;
    } else {
    Buffer->end = 0;
      Buffer->buffer[Buffer->end] = ch;
    };
  } else {
    if(Buffer->begin == Buffer->end + 1) {
      res = 1;
    } else {
      Buffer->buffer[Buffer->end] = ch;
      Buffer->end++;
    };
  };
  __enable_irq();
  return res;
}

//======================================================================================================================================
//передача пакета
void transmit_buffer(uint8_t *pData, uint16_t Size, uint8_t type_transmit)
{
#if CONFIG_UART
	UART_HandleTypeDef *huart_ptr = NULL;
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
//прием пакета
uint16_t receive_buffer(T_Buffer *Buffer, uint8_t * ch)
{
  uint16_t res = 0;
//  __disable_irq();

  if(Buffer->begin >= BUFFER_LENGTH) {
    Buffer->begin = 0;
  }
  if(Buffer->begin < Buffer->end) {
    *ch = (uint8_t)(Buffer->buffer [Buffer->begin]);
    Buffer->begin++;
    res = 1;
  }
  if(Buffer->begin > Buffer->end) {
    *ch = (uint8_t)(Buffer->buffer [Buffer->begin]);
    Buffer->begin++;
    res = 1;
  }
  if(Buffer->begin == Buffer->end) {
     Buffer->begin = 0;
     Buffer->end = 0;
  }
//  __enable_irq();
  return res;
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

