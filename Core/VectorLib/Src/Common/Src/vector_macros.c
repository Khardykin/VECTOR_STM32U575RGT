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
#include "rtc.h"           /* hrtc: RTC_ReadTime/RTC_WriteTime (BKP0, v26) */
#include "time.h"
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

/* ============================================================================
 * ЧАСЫ ПРИБОРА в резервном регистре RTC (v26, замена AT32 rtc_counter_*)
 *
 * В Avis счётчик секунд жил в свободном 32-битном счётчике RTC
 * (rtc_counter_get/rtc_counter_set, AT32). На STM32U5 такого счётчика нет:
 * RTC здесь - календарь (дата/время), а роль "вечного" 32-битного хранилища
 * играет резервный регистр BKP0 (TAMP_BKP0R). Он живёт от VBAT, переживает
 * NVIC_SystemReset() и полный сброс питания при живой батарейке, и НЕ занят
 * кубом (HAL_RTCEx_BKGetRegister/BKSetRegister работают напрямую).
 *
 * Доступ к backup-домену разрешён постоянно: HAL_PWREx_EnableBkUpAccess()
 * вызывается один раз в Vector_Run_Board_Init() (снимает PWR_DBP). Без него
 * запись в BKP0 молча игнорируется - часы не переживут выключение.
 *
 * Единица - секунды: Timer_Tick_1s() (RTC wakeup, ровно 1 Гц) делает +1,
 * Vector_Options_System() раз в секунду сверяет и пишет обратно
 * (flag_start_work_bat_sys). При включении Vector_Run_Pre_Init() читает
 * регистр ПОСЛЕ конфигурации и перекрывает значение из flash.
 * КОНТЕКСТ: поток (обе функции; из ISR не звать - не нужно).
 * ============================================================================ */
uint32_t RTC_ReadTime(void)
{
	time_t tmp_bt = 0;
	struct tm tm_ptr = {0};
	RTC_DateTypeDef sdates = {0};
	RTC_TimeTypeDef stimes = {0};
	HAL_RTC_GetDate(&hrtc, &sdates, RTC_FORMAT_BIN);
	HAL_RTC_GetTime(&hrtc, &stimes, RTC_FORMAT_BIN);

	tm_ptr.tm_year = sdates.Year;
	tm_ptr.tm_mon = sdates.Month - 1;
	tm_ptr.tm_mday = sdates.Date;

	tm_ptr.tm_sec = stimes.Seconds;
	tm_ptr.tm_min = stimes.Minutes;
	tm_ptr.tm_hour = stimes.Hours;

	tmp_bt = mktime(&tm_ptr);
	return (uint32_t)tmp_bt;
//  return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);
}

/* Записать счётчик часов в BKP0. Возврат: 0 = ok, -1 = отказ (нет handle/
   не снята защита backup-домена). Имя/сигнатура - как в Avis
 * (error_status заменён на int: типа error_status в проекте нет).          */
int RTC_WriteTime(uint32_t TimeCounter)
{

	error_status status = SUCCESS;
	time_t tmp = TimeCounter;
	RTC_DateTypeDef sdates = {0};
	RTC_TimeTypeDef stimes = {0};
	struct tm tm_ptr = *gmtime(&tmp);

	sdates.Year = tm_ptr.tm_year;
	sdates.Month = tm_ptr.tm_mon + 1;
	sdates.Date = tm_ptr.tm_mday;

	stimes.Seconds = tm_ptr.tm_sec;
	stimes.Minutes = tm_ptr.tm_min;
	stimes.Hours = tm_ptr.tm_hour;
	stimes.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	stimes.StoreOperation = RTC_STOREOPERATION_RESET;

	HAL_RTC_SetDate(&hrtc, &sdates, RTC_FORMAT_BIN);
	HAL_RTC_SetTime(&hrtc, &stimes, RTC_FORMAT_BIN);

	return status;
//  if (HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, TimeCounter) != HAL_OK)
//  {
//    return -1;
//  }
//  return 0;
}
