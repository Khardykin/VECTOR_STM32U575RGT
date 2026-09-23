/**
  ******************************************************************************
  * @file    spiflash.h
  * @brief   Драйвер внешней SPI NOR flash MX25K6435F (DD2, "EEPROM" на схеме)
  *
  *          Шина: SPI1, PA4 = CS (GPIO), PA5 = SCK, PA6 = MISO, PA7 = MOSI.
  *          LoRa-модуль этой шиной не управляет (его NSS не разведён),
  *          конфликтов нет.
  *
  *          Объём: 64 Мбит = 8 МБ. Сектор 4 КБ, страница записи 256 Б.
  *
  *          РЕЖИМЫ ОБМЕНА (переключатель VECTOR_SPI_DMA в vector_config.h):
  *            команды/адрес/статус/короткие чтения - блокирующий опрос
  *              (HAL_SPI_Transmit/Receive): там 4-256 байт, DMA дороже setup;
  *            большие чтения (>= VECTOR_SPI_DMA_MIN_LEN, т.е. подкачка звука)
  *              - HAL_SPI_Receive_DMA + сон на семафоре: поток не жжёт CPU
  *              десятки миллисекунд, а другие потоки (LVGL/мост) работают.
  *          Любой сбой DMA -> автоматический повтор того же чтения опросом,
  *          поэтому корректность данных не зависит от исправности DMA.
  *
  *          ИЗ КОНТЕКСТА ISR НЕ ВЫЗЫВАТЬ: функции ждут завершения обмена.
  ******************************************************************************
  */
#ifndef SPIFLASH_H
#define SPIFLASH_H

#include <stdint.h>
#include "stm32u5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- геометрия чипа ------------------------------------------------------- */
#define SF_PAGE_SIZE        256u          /* страница программирования        */
#define SF_SECTOR_SIZE      4096u         /* сектор стирания                  */
#define SF_BLOCK32_SIZE     32768u
#define SF_BLOCK64_SIZE     65536u
#define SF_TOTAL_SIZE       (8u * 1024u * 1024u)   /* 8 МБ                   */

/* --- команды MX25K6435F --------------------------------------------------- */
#define SF_CMD_RDID         0x9Fu
#define SF_CMD_RDSR         0x05u
#define SF_CMD_WREN         0x06u
#define SF_CMD_WRDI         0x04u
#define SF_CMD_READ         0x03u
#define SF_CMD_PP           0x02u         /* page program                     */
#define SF_CMD_SE           0x20u         /* sector erase  4 КБ               */
#define SF_CMD_BE32         0x52u
#define SF_CMD_BE64         0xD8u
#define SF_CMD_CE           0x60u         /* chip erase                       */

/* Биты статус-регистра */
#define SF_SR_WIP           0x01u         /* write in progress                */
#define SF_SR_WEL           0x02u         /* write enable latch               */

/* --- результаты последней пробы (смотреть в отладчике) -------------------- */
extern volatile uint8_t  sf_jedec[3];    /* [0]=0xC2 Macronix, [2]=0x17 (64M) */
extern volatile uint8_t  sf_rdsr;
extern volatile int32_t  sf_probe_rc;    /* HAL_OK если чип отвечает          */

/* --- отладочные счётчики -------------------------------------------------- */
extern volatile uint32_t sf_dbg_dma_chunks;    /* кусков принято по DMA       */
extern volatile uint32_t sf_dbg_dma_tmo;       /* таймаутов ожидания DMA      */
extern volatile uint32_t sf_dbg_dma_err;       /* HAL_SPI_ErrorCallback       */
extern volatile uint32_t sf_dbg_dma_fallback;  /* чтений перечитано опросом   */
extern volatile uint32_t sf_dbg_poll_bytes;    /* байт принято опросом        */

/* --- API ------------------------------------------------------------------ */
/** Поднять CS, прочитать JEDEC ID и статус. Вызывать после MX_SPI1_Init().
    Работает и до старта RTOS (чистый опрос). */
HAL_StatusTypeDef sf_probe(void);

/** Создать семафор DMA-приёма. Вызывать из tx_application_define() (например
    из ext_init()) - до потоков. До этого вызова sf_read() всегда опросный. */
void sf_dma_init(void);

/** Произвольное чтение. Блокирующее для вызывающего ПОТОКА: короткие блоки -
    опрос (~0.8 мс на 2 КБ при 20 МГц), большие - DMA (поток спит, CPU свободен). */
HAL_StatusTypeDef sf_read(uint32_t addr, uint8_t *dst, uint32_t len);

/** Стирание сектора 4 КБ (addr должен быть кратен 4096). ~40-100 мс. */
HAL_StatusTypeDef sf_sector_erase(uint32_t addr);

/** Запись с автоматическим переходом через границы страниц 256 Б.
    ВАЖНО: область должна быть предварительно стёрта. */
HAL_StatusTypeDef sf_program(uint32_t addr, const uint8_t *src, uint32_t len);

/** Сравнить содержимое с образцом (верификация после записи). */
HAL_StatusTypeDef sf_verify(uint32_t addr, const uint8_t *src, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* SPIFLASH_H */
