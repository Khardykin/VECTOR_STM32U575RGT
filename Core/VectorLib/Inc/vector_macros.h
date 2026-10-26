/**
  ******************************************************************************
  * @file    vector_macros.h
  * @brief   Общие макросы и мелкие хелперы проекта (ОДИН файл вместо прежних
  *          vector_status.h + vector_compat.h)
  *
  *          Что где лежит:
  *            main.h           - абстракция периферии (I2C_xxx, SPI_xxx,
  *                               GPIO_xxx, Uart_xxx) - библиотеки HAL не знают;
  *            shared_macros.h  - биты статусов прибора (SET/CLEAR/TEST_STATUS_
  *                               COMMON_BIT), SETBIT/CLRBIT/TESTBIT, таймеры;
  *            vector_macros.h  - ЭТОТ ФАЙЛ: время/паузы для перенесённого кода
  *                               (Delay, DelayInt, GetTick, Search_text),
  *                               тики таймеров приёма (TIME_DEL_1,
  *                               TIME_OUT_LORA), VECTOR_DEVICE_IS_ON().
  *
  *          ВРЕМЯ: только тик RTOS (vector_tick.h). GetTick() = VTICK_MS(),
  *          Delay() в потоке спит, до планировщика - крутит цикл ядра.
  ******************************************************************************
  */
#ifndef VECTOR_MACROS_H
#define VECTOR_MACROS_H

#include <stdint.h>
#include "vector_tick.h"
#include "shared_types.h"    /* DEVICE_TURNED, SNS_CFG */

#ifdef __cplusplus
extern "C" {
#endif

/* Прибор включён (не в режиме TURN_OFF). */
#define VECTOR_DEVICE_IS_ON()   (device_turn != DEVICE_TURNED_OFF)

/* --- тики таймеров приёма модулей ------------------------------------------
 * TIME_DEL_1 - период, с которым вызываются Uart_Gps_Receive_Timer_Inc() и
 * Uart_Lora_Receive_Timer_Inc(): они висят на TIM3, а TIM3 в кубе настроен на
 * 1 кГц (Prescaler 159, Period 1000 при 160 МГц) -> 1 мс. Отсюда
 * TIME_OUT_GPS (Gps.h) = 0.05/0.001 = 50 тиков = 50 мс тишины в линии.
 * Поменяете период TIM3 в кубе - правьте и эту константу.                    */
#define TIME_DEL_1     (0.001)

/* Таймаут кадра AT-ответа LoRa в тех же тиках (Lora_Receive_wait() продлевает
   ожидание на TIME_OUT_LORA + 2 мс после каждого принятого куска).           */
#define TIME_OUT_LORA  ((uint32_t)((0.05) / TIME_DEL_1 + 0.5))

/* --- busy-wait: итераций на 1 мс (~4 такта на итерацию при 160 МГц) ---------
 * Нужно только ДО планировщика; в потоке Delay() спит по тику RTOS.          */
#define COMPAT_BUSY_UNITS_PER_MS   40000u

/* Пауза, мс. В потоке - сон (CPU свободен), до планировщика - цикл ядра. */
void Delay(uint32_t ms);

/* Пауза, мс, ВСЕГДА циклом ядра: для инициализации до RTOS и последовательностей,
   где сон недопустим (сброс LCD/GNSS). В потоке занимает CPU.                */
void DelayInt(uint32_t ms);

/* Текущее время, мс = тик RTOS (до планировщика 0). */
uint32_t GetTick(void);

/* Поиск data2 в буфере data1 (буфер заканчивается '\0'). Возврат: смещение
   найденного места + 1 (семантика Avis: Gps.c складывает эти смещения и читает
   поле через atof). Не найдено -> 0. КОНТЕКСТ: любой.                        */
uint16_t Search_text(uint8_t *data1, const char *data2);

/* --- hex-хелперы: ОБЩИЕ для BLE и LoRa (разбор hex-ответов AT-модулей) ------
 * Оба модуля получают по UART hex-ASCII (ADVMSDDATAGET у BLE, RX/MAC-команды
 * у LoRa) и разбирают его в сырые байты. Раньше было две реализации
 * (Ble_HexToBin на strtol и связка Lora_IsHexChar/HexToByte/Lora_HexStringToBytes);
 * теперь одна общая - здесь. Разбор ТОЛЕРАНТНЫЙ: останавливается на первом
 * не-hex символе, поэтому хвост ответа ("OK"/CR/LF) просто отсекается.
 * Функции чистые (без HAL/RTOS), КОНТЕКСТ: любой.                              */

/* Символ - hex-цифра ([0-9A-Fa-f]) -> 1, иначе 0. */
uint8_t Vector_IsHexChar(char c);

/* Два hex-символа (старший, младший) -> байт. Не-hex символ трактуется как 0;
   вызывающий (Vector_HexStringToBytes) такие не пропускает - заранее проверяет
   пару через Vector_IsHexChar.                                                */
uint8_t Vector_HexToByte(char high, char low);

/* Hex-строка (до первого не-hex символа, '\0'-terminated) -> массив out.
 * Пишет не более out_size байт. Возврат: число разобранных байт.
 * hex_str/out == NULL -> 0.                                                    */
uint16_t Vector_HexStringToBytes(const char *hex_str, uint8_t *out, uint16_t out_size);

/* Смена скорости UART на ходу - см. макрос usart_init() и прототип
   Uart_SetBaudrate() в main.h (реализация в vector_macros.c).                */

/* --- SAI: вывод звука (платформенный слой библиотеки audio_player) ----------
 * Библиотеки модулей не должны звать HAL напрямую (кроссплатформенность,
 * решение автора): старт/стоп DMA SAI1_A и его параметры - здесь, HAL внутри
 * vector_macros.c. Обратная сторона: входные колбэки HAL
 * (HAL_SAI_TxHalfCplt/TxCplt/ErrorCallback) тоже в vector_macros.c - они зовут
 * хуки Audio_Out_On*() из audio_player.h (реализация в audio_player.c).
 * Возврат Sai_StartDMA: 0 = стартовало; ненуль = код ошибки платформы (в лог).
 * КОНТЕКСТ: поток плеера / инициализация до планировщика.                     */
int      Sai_StartDMA(uint8_t *buf, uint32_t samples);
void     Sai_StopDMA(void);
uint32_t Sai_GetClockFreq(void);    /* частота ядра SAI1 (PLL3P), Гц          */
uint32_t Sai_GetErrorCode(void);    /* ErrorCode SAI - диагностика            */
uint32_t Sai_GetMckdiv(void);       /* Init.Mckdiv (0 трактуется как 1)       */
int      Sai_DmaIsCircular(void);   /* 1 = DMA circular (linked-list, U5)     */

/* --- ЧАСЫ прибора: календарный RTC <-> Unix-секунды (подход автора) ----
 * Замена AT32 rtc_counter_get/set из Avis: на U5 RTC - календарь (LSE,
 * backup-домен, идёт от VBAT при снятом VDD). RTC_ReadTime() преобразует
 * календарь в Unix-секунды (mktime, AN4759), RTC_WriteTime() - обратно
 * (gmtime). Единица - секунды; +1 делает Timer_Tick_1s() (RTC wakeup 1 Гц),
 * сверку и запись раз в секунду - Vector_Options_System(), чтение при
 * включении - Vector_Run_Pre_Init() ПОСЛЕ конфигурации. Требуется
 * HAL_PWR_EnableBkUpAccess() один раз при старте (Vector_Run_Board_Init;
 * на U5 функция в PWR-модуле, НЕ Ex). База года: HAL 0..99 от 2000,
 * tm_year от 1900 - внутри функций +/-100. TZ newlib не трогать (UTC).      */
uint32_t RTC_ReadTime(void);              /* Unix-секунды из календаря RTC    */
int      RTC_WriteTime(uint32_t TimeCounter);  /* 0 = ok, -1 = отказ          */

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_MACROS_H */
