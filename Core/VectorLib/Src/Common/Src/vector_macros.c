/**
  ******************************************************************************
  * @file    vector_macros.c
  * @brief   Реализация хелперов из vector_macros.h + Uart_SetBaudrate()
  *
  *          Это ЕДИНСТВЕННОЕ место вне main.h, где перенесённый код касается
  *          HAL: библиотеки (Gps, BME280, LIS3DH, MAX17048, LCD_platform) зовут
  *          макросы main.h и функции отсюда.
  ******************************************************************************
  */
#include "vector_macros.h"
#include "main.h"          /* Uart_SetBaudrate, usart-макросы, HAL */
#include "tx_api.h"
#include <string.h>

/* Busy-wait в условных итерациях (volatile - чтобы компилятор не свернул). */
static void compat_busy(uint32_t units)
{
  volatile uint32_t d = units;
  while (d != 0u)
  {
    d--;
  }
}

/* В потоке - сон (CPU свободен), до планировщика - цикл ядра (тика RTOS нет). */
void Delay(uint32_t ms)
{
  if (VTICK_IN_THREAD())
  {
    VTICK_SLEEP_MS(ms);
  }
  else
  {
    compat_busy(ms * COMPAT_BUSY_UNITS_PER_MS);
  }
}

/* Всегда циклом ядра - для инициализации до RTOS и жёстких последовательностей. */
void DelayInt(uint32_t ms)
{
  compat_busy(ms * COMPAT_BUSY_UNITS_PER_MS);
}

uint32_t GetTick(void)
{
  return VTICK_MS();
}

/* Поиск подстроки. Семантика Avis: возвращается смещение найденного места + 1,
   поэтому в Gps.c работает цепочка
     pos  = Search_text(buf, "GGA");       // встали за начало метки
     pos += Search_text(&buf[pos], ",");   // встали за разделителем
     atof((char *)&buf[pos]);              // поле между разделителями
   Не найдено -> 0 (без этого strstr вернул бы NULL и арифметика дала бы мусор). */
uint16_t Search_text(uint8_t *data1, const char *data2)
{
  uint8_t *index;

  if ((data1 == (uint8_t *)0) || (data2 == (const char *)0))
  {
    return 0u;
  }

  index = (uint8_t *)strstr((char *)data1, data2);
  if (index == (uint8_t *)0)
  {
    return 0u;
  }

  return (uint16_t)((index + 1) - data1);
}

/* Смена скорости UART на ходу (Gps.c: автоопределение чипа GNSS на 115200/9600).
   Разрядность и стоп-биты не трогаем - их задаёт CubeMX (8N1). После
   HAL_UART_Init прерывания приёма сброшены, поэтому возвращаем их: ровно те,
   что CubeMX включает в MX_USARTx_UART_Init (USER CODE ... Init 2).
   КОНТЕКСТ: поток.                                                           */
void Uart_SetBaudrate(UART_HandleTypeDef *huart, uint32_t baud)
{
  if ((huart == (UART_HandleTypeDef *)0) || (baud == 0u))
  {
    return;
  }

  huart->Init.BaudRate = baud;
  (void)Uart_Init(huart);
  Uart_Enable_IT(huart, UART_IT_RX_ERR);
  Uart_Enable_IT(huart, UART_IT_RX_BYTE);
}
