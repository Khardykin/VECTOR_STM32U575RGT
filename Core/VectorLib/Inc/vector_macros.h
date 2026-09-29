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

/* Смена скорости UART на ходу - см. макрос usart_init() и прототип
   Uart_SetBaudrate() в main.h (реализация в vector_macros.c).                */

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_MACROS_H */
