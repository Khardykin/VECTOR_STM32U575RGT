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
#include "rtc.h"           /* hrtc: RTC_ReadTime/RTC_WriteTime (календарь) */
#include <time.h>          /* mktime/gmtime: календарь RTC <-> Unix-секунды  */
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

/* --- hex-хелперы (общие для BLE и LoRa): см. объявления в vector_macros.h ----
 * Чистые функции, HAL/RTOS не трогают. Заменяют прежние Ble_HexToBin (strtol)
 * и Lora_IsHexChar/HexToByte/Lora_HexStringToBytes одной общей реализацией.   */

uint8_t Vector_IsHexChar(char c)
{
  if (((c >= '0') && (c <= '9')) ||
      ((c >= 'A') && (c <= 'F')) ||
      ((c >= 'a') && (c <= 'f')))
  {
    return 1U;
  }

  return 0U;
}

uint8_t Vector_HexToByte(char high, char low)
{
#define VECTOR_HEX_VAL(c) (((c) >= '0' && (c) <= '9') ? ((c) - '0') : \
                           ((c) >= 'a' && (c) <= 'f') ? ((c) - 'a' + 10) : \
                           ((c) >= 'A' && (c) <= 'F') ? ((c) - 'A' + 10) : 0)

  return (uint8_t)((VECTOR_HEX_VAL(high) << 4) | VECTOR_HEX_VAL(low));

#undef VECTOR_HEX_VAL
}

uint16_t Vector_HexStringToBytes(const char *hex_str, uint8_t *out, uint16_t out_size)
{
  uint16_t count = 0U;

  if ((hex_str == (const char *)0) || (out == (uint8_t *)0))
  {
    return 0U;
  }

  /* Читаем по паре, пока обе цифры hex и пока есть место в out. Первый не-hex
     символ (в т.ч. '\0' или хвост "OK"/CR) останавливает разбор.             */
  while ((count < out_size) &&
         (Vector_IsHexChar(hex_str[0]) != 0U) &&
         (Vector_IsHexChar(hex_str[1]) != 0U))
  {
    out[count] = Vector_HexToByte(hex_str[0], hex_str[1]);
    count++;

    hex_str += 2;
  }

  return count;
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
 * ЧАСЫ ПРИБОРА: календарный RTC -> Unix-секунды (подход автора, коммит "6";
 * исправлены база года, порядок Time/Date и контроль ошибок HAL)
 *
 * В Avis счётчик секунд жил в свободном 32-битном счётчике RTC
 * (rtc_counter_get/rtc_counter_set, AT32). У STM32U5 RTC - КАЛЕНДАРЬ
 * (TR/DR + прескалеры от LSE 32.768 кГц), весь календарь живёт в backup-
 * домене и идёт от VBAT при снятом VDD - т.е. "вечность" обеспечивает сам
 * календарь, BKP-регистр под счётчик больше не нужен (подход с BKP0
 * заменён автором на преобразование календарь <-> time_t, как в AN4759).
 *
 * Доступ к backup-домену разрешён постоянно: HAL_PWR_EnableBkUpAccess()
 * (на U5 функция именно в PWR-модуле, не Ex - правка автора верна)
 * вызывается один раз в Vector_Run_Board_Init() (снимает PWR DBP). Без него
 * запись в календарь молча игнорируется.
 *
 * ВАЖНО - база года: HAL хранит Year = 0..99 ОТ 2000-го, struct tm - tm_year
 * ОТ 1900-го, поэтому при чтении +100, при записи -100. Без этого mktime()
 * даёт 1926-й вместо 2026-го (абсолютное время неверно; LoRa v4 TimeRef и
 * журнал используют именно Unix-секунды).
 * ВАЖНО - порядок: читать Time ПЕРЕД Date (чтение TR блокирует теневые
 * регистры до чтения DR - календарь не "рассинхронится" на границе суток),
 * писать SetTime ПЕРЕД SetDate (AN4759, все примеры ST).
 * ВАЖНО - часовой пояс: mktime() трактует struct tm как ЛОКАЛЬНОЕ время;
 * в newlib без setenv("TZ",...) локальное = UTC, т.е. симметрично gmtime().
 * setenv("TZ") в проекте НЕ вызывать. Ограничение: time_t в newlib 32-битный
 * знаковый - переполнение в 2038-м (как и у AT32-счётчика в Avis).
 *
 * Единица - секунды: Timer_Tick_1s() (RTC wakeup, ровно 1 Гц) делает +1,
 * Vector_Options_System() раз в секунду сверяет и пишет обратно
 * (flag_start_work_bat_sys). При включении Vector_Run_Pre_Init() читает
 * календарь ПОСЛЕ MX_RTC_Init() и перекрывает значение из flash.
 * КОНТЕКСТ: поток (обе функции; из ISR не звать - не нужно).
 * ============================================================================ */

/* Прочитать часы прибора (Unix-секунды из календаря RTC). Возврат: секунды;
   0 - календарь не читается (RTC не инициализирован). */
uint32_t RTC_ReadTime(void)
{
	time_t tmp_bt = 0;
	struct tm tm_ptr = {0};
	RTC_DateTypeDef sdates = {0};
	RTC_TimeTypeDef stimes = {0};

	/* Порядок: Time -> Date (чтение TR фиксирует теневой DR). */
	if (HAL_RTC_GetTime(&hrtc, &stimes, RTC_FORMAT_BIN) != HAL_OK)
	{
		return 0;
	}
	if (HAL_RTC_GetDate(&hrtc, &sdates, RTC_FORMAT_BIN) != HAL_OK)
	{
		return 0;
	}

	tm_ptr.tm_year = (int)sdates.Year + 100;  /* HAL: 0..99 от 2000 -> tm: от 1900 */
	tm_ptr.tm_mon  = (int)sdates.Month - 1;   /* HAL: 1..12 -> tm: 0..11          */
	tm_ptr.tm_mday = (int)sdates.Date;

	tm_ptr.tm_sec  = (int)stimes.Seconds;
	tm_ptr.tm_min  = (int)stimes.Minutes;
	tm_ptr.tm_hour = (int)stimes.Hours;
	tm_ptr.tm_isdst = 0;                      /* без DST: newlib TZ = UTC         */

	tmp_bt = mktime(&tm_ptr);
	if (tmp_bt == (time_t)-1)
	{
		return 0;
	}
	return (uint32_t)tmp_bt;
}

/* Записать часы прибора (Unix-секунды) в календарь RTC. Возврат: 0 = ok,
   -1 = отказ (HAL не принял дату/время - напр. календарь не инициализирован
   или не снят DBP). Имя/сигнатура - как в Avis.                            */
int RTC_WriteTime(uint32_t TimeCounter)
{
	error_status status = SUCCESS;
	time_t tmp = (time_t)TimeCounter;
	RTC_DateTypeDef sdates = {0};
	RTC_TimeTypeDef stimes = {0};
	struct tm *tm_p = gmtime(&tmp);           /* UTC; newlib: статический буфер */

	if (tm_p == NULL)
	{
		return -1;
	}
	struct tm tm_ptr = *tm_p;                 /* копия: буфер gmtime общий      */

	sdates.Year  = (uint8_t)(tm_ptr.tm_year - 100); /* tm: от 1900 -> HAL: 0..99 от 2000 */
	sdates.Month = (uint8_t)(tm_ptr.tm_mon + 1);
	sdates.Date  = (uint8_t)tm_ptr.tm_mday;

	stimes.Seconds = (uint8_t)tm_ptr.tm_sec;
	stimes.Minutes = (uint8_t)tm_ptr.tm_min;
	stimes.Hours   = (uint8_t)tm_ptr.tm_hour;
	stimes.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	stimes.StoreOperation = RTC_STOREOPERATION_RESET;

	/* Порядок: SetTime -> SetDate (AN4759, примеры ST). */
	if (HAL_RTC_SetTime(&hrtc, &stimes, RTC_FORMAT_BIN) != HAL_OK)
	{
		status = ERROR;
	}
	if (HAL_RTC_SetDate(&hrtc, &sdates, RTC_FORMAT_BIN) != HAL_OK)
	{
		status = ERROR;
	}

	return (status == SUCCESS) ? 0 : -1;
}
