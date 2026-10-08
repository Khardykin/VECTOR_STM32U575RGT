/**
  ******************************************************************************
  * @file    vector_compat.c
  * @brief   Delay/DelayInt/GetTick/Search_text для перенесённого кода
  *          (см. vector_compat.h - почему это нужно и как себя вести)
  ******************************************************************************
  */
#include "vector_compat.h"
#include "tx_api.h"
#include <string.h>

/* Busy-wait в условных итерациях (volatile, чтобы компилятор не свернул цикл).
   КОНТЕКСТ: любой, включая инициализацию до планировщика.                    */
static void compat_busy(uint32_t units)
{
  volatile uint32_t d = units;
  while (d != 0u)
  {
    d--;
  }
}

/* Пауза в мс: в потоке - сон (CPU свободен, другие потоки работают), до
   планировщика - цикл ядра (тика RTOS ещё нет, спать нельзя).                */
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

/* Пауза в мс циклом ядра - всегда, независимо от контекста. */
void DelayInt(uint32_t ms)
{
  compat_busy(ms * COMPAT_BUSY_UNITS_PER_MS);
}

/* Время приложения - только тик RTOS (vector_tick.h). */
uint32_t GetTick(void)
{
  return VTICK_MS();
}

/* Поиск подстроки. Возвращаем смещение ЗА найденным текстом: перенесённый код
   (parsing_gga/gll/rmc в Gps.c) складывает эти смещения и сразу читает поле
   через atof(). Не найдено -> 0 (останемся в начале буфера, atof вернёт 0.0 -
   так же вёл себя оригинал).
   ВАЖНО: buffer обязан быть NUL-terminated - в Gps.c накопитель memset'ится
   после каждого кадра, поэтому завершающий '\0' там всегда есть.             */
uint16_t Search_text(uint8_t *data1, uint8_t *data2)
{
	uint8_t * index;
	uint16_t pos = 0;
	index = strstr(data1, data2) + 1;
	pos = (uint16_t)(index - data1);
	return pos;
}
