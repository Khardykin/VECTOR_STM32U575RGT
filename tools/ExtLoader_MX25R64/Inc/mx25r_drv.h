/**
  ******************************************************************************
  * @file    mx25r_drv.h
  * @brief   Минимальный драйвер MX25R6435F (SPI1, опрос, без DMA/прерываний)
  *          для external loader'а STM32CubeProgrammer.
  *
  *          Командный набор и порядок обменов повторяют проверенный на этой
  *          плате firmware-драйвер (Core/VectorLib/Audio/Src/spiflash.c):
  *          CS (PA4) низкий на всю команду, READ = 0x03 + 3 байта адреса.
  ******************************************************************************
  */
#ifndef MX25R_DRV_H
#define MX25R_DRV_H

#include <stdint.h>

/* Возврат всех функций: 0 = ok, -1 = ошибка. */

/* Прочитать JEDEC ID (0x9F). Ожидание: C2 28 17 (Macronix MX25R6435F). */
int mx25r_probe(uint8_t id[3]);

/* Произвольное чтение (0x03). */
int mx25r_read(uint32_t addr, uint8_t *dst, uint32_t len);

/* Запись с автоматической разбивкой по страницам 256 Б (WREN 0x06 + PP 0x02 + WIP).
   Область должна быть предварительно стёрта. */
int mx25r_write(uint32_t addr, const uint8_t *src, uint32_t len);

/* Стирание одного сектора 4 КБ (SE 0x20 + WIP). addr выравнивается вниз. */
int mx25r_erase_sector(uint32_t addr);

/* Стирание всего чипа (CE 0x60 + WIP). Долго: единицы-десятки секунд. */
int mx25r_erase_chip(void);

#endif /* MX25R_DRV_H */
