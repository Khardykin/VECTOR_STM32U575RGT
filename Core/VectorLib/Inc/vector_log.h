/**
  ******************************************************************************
  * @file    vector_log.h
  * @brief   Консольный лог: ITM/SWO (консоль внутри CubeIDE) и/или UART
  *
  *          ЗАЧЕМ: без него не видно, что реально происходит с внешней flash
  *          (когда прочиталось/записалось/стёрлось), с мостом UART и с
  *          плеером. Счётчики audio_dbg_* и sf_dbg_* дают то же самое, но только
  *          в отладчике, а лог виден сразу в терминале.
  *
  *          ДВА ВЫКЛЮЧАТЕЛЯ:
  *            1) КОМПИЛЯЦИЯ: VECTOR_LOG_ENABLE = 0 в vector_config.h - все
  *               макросы LOG*() превращаются в ((void)0), вектор_log.c
  *               компилируется в пустой файл, в прошивке не остаётся ни
  *               строк, ни кода.
  *            2) RUNTIME: vlog_set_level()/vlog_set_mask() - можно глушить
  *               отдельные модули или понижать подробность без пересборки
  *               (например, из команды по UART или из отладчика:
  *               вызов vlog_set_level(VLOG_WARN) в Expression/Watch).
  *
  *          ФОРМАТ: только %s %d %u %x %X %c %% (своя печать без stdio, чтобы
  *          не тянуть в прошивку vsnprintf - это десятки КБ). Числа - 32 бита.
  *
  *          ТЕКСТ СООБЩЕНИЙ - ТОЛЬКО ASCII (латиница). Терминалы под Windows
  *          по умолчанию в CP1251/CP866, а исходники в UTF-8: кириллица
  *          приходит на терминал кракозябрами ("РѕР±СЂР°Р·Р° РЅРµС‚"). Либо
  *          переводите терминал в UTF-8, либо держите строки латиницей -
  *          второе надёжнее.
  *
  *          КОНТЕКСТ: только поток или инициализация до планировщика. Из ISR
  *          вызов игнорируется (растёт vlog_dbg_isr_skipped) - блокирующая
  *          передача в прерывании недопустима.
  ******************************************************************************
  */
#ifndef VECTOR_LOG_H
#define VECTOR_LOG_H

#include <stdint.h>
#include "vector_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Уровни (чем больше число, тем важнее сообщение) */
#define VLOG_DEBUG   1u   /* мелочёвка: каждый мелкий обмен, каждый байт моста */
#define VLOG_INFO    2u   /* обычные события: чтение/запись flash, старт звука  */
#define VLOG_WARN    3u   /* странности: таймаут, откат с DMA на опрос, ORE     */
#define VLOG_ERROR   4u   /* ошибки: flash не ответила, DMA не запустилась      */

/* Маски модулей: можно оставить только нужный */
#define VLOG_M_SYS     0x00000001u  /* старт, инициализация                    */
#define VLOG_M_FLASH   0x00000002u  /* spiflash/extstore: чтение/запись/стирание */
#define VLOG_M_AUDIO   0x00000004u  /* плеер: старт/стоп/ошибки звука          */
#define VLOG_M_BRIDGE  0x00000008u  /* мост UART4 <-> USART2                   */
#define VLOG_M_ALL     0xFFFFFFFFu

#if VECTOR_LOG_ENABLE

/** Инициализация: мьютекс + стартовая строка. Звать из tx_application_define()
    (можно и раньше - до планировщика лог работает без мьютекса). */
void vlog_init(void);

/** Печать одной строки. fmt: %s %d %u %x %X %c %%. Контекст: поток/init. */
void vlog(uint32_t mask, uint8_t level, const char *fmt, ...);

/** Захватить/отпустить шину вывода, если ваш UART совпадает с UART лога
    (аргумент - Instance: vlog_bus_lock(UART4)). Иначе пустая операция.
    Контекст: только поток. */
void vlog_bus_lock(void *uart_instance);
void vlog_bus_unlock(void *uart_instance);

/** Runtime-фильтры. Уровень: VLOG_DEBUG..VLOG_ERROR (0 = молчать совсем). */
void     vlog_set_level(uint8_t level);
uint8_t  vlog_get_level(void);
void     vlog_set_mask(uint32_t mask);
uint32_t vlog_get_mask(void);

/** Сколько строк напечатано / сколько потеряно (переполнение буфера строки) /
    сколько вызовов отброшено из ISR. */
extern volatile uint32_t vlog_dbg_lines;
extern volatile uint32_t vlog_dbg_isr_skipped;

/* Удобные макросы: LOG_I(VLOG_M_FLASH, "read %u bytes", n) */
#define LOG_D(mask, fmt, ...)   vlog((mask), VLOG_DEBUG, (fmt), ##__VA_ARGS__)
#define LOG_I(mask, fmt, ...)   vlog((mask), VLOG_INFO,  (fmt), ##__VA_ARGS__)
#define LOG_W(mask, fmt, ...)   vlog((mask), VLOG_WARN,  (fmt), ##__VA_ARGS__)
#define LOG_E(mask, fmt, ...)   vlog((mask), VLOG_ERROR, (fmt), ##__VA_ARGS__)

#else  /* VECTOR_LOG_ENABLE == 0: кода и строк в прошивке не остаётся */

#define vlog_init()                     ((void)0)
#define vlog_bus_lock(u)                ((void)(u))
#define vlog_bus_unlock(u)              ((void)(u))
#define vlog_set_level(l)               ((void)(l))
#define vlog_get_level()                ((uint8_t)0)
#define vlog_set_mask(m)                ((void)(m))
#define vlog_get_mask()                 ((uint32_t)0)
#define LOG_D(mask, fmt, ...)           ((void)0)
#define LOG_I(mask, fmt, ...)           ((void)0)
#define LOG_W(mask, fmt, ...)           ((void)0)
#define LOG_E(mask, fmt, ...)           ((void)0)

#endif /* VECTOR_LOG_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_LOG_H */
