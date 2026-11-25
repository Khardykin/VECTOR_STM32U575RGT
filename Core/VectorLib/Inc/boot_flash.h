#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H
/**
  ******************************************************************************
  * @file    boot_flash.h
  * @brief   Запись/стирание области бутлоадера во ВНУТРЕННЕЙ flash STM32U5.
  *
  *          STM32U5 программирует только quad-word (16 байт), адрес и длина
  *          обязаны быть кратны 16. Стирание - страницами по 8 КБ.
  *
  *          ЗАЩИТА: пока приложение слинковано с 0x08000000 (бутлоадера нет),
  *          любая операция возвращает отказ. Разрешается только когда
  *          CONFIG_BOOTLOADER_PRESENT = 1 И SCB->VTOR выше области бутлоадера.
  ******************************************************************************
  */
#include <stdint.h>

#ifndef CONFIG_BOOTLOADER_PRESENT
#define CONFIG_BOOTLOADER_PRESENT 0   /* 1 - когда приложение перенесено на 0x08005000 */
#endif

#define BOOT_FLASH_PAGE_SIZE   0x2000u        /* 8 КБ - страница STM32U575 */

typedef struct __attribute__((packed))
{
  uint32_t magic;      /* BOOT_IMG_MAGIC */
  uint32_t size;       /* размер тела, кратен 16 */
  uint32_t crc32;      /* CRC32 тела (без заголовка) */
  uint32_t version;
} boot_img_hdr_t;

#define BOOT_ST_IDLE       0u
#define BOOT_ST_BUSY       1u
#define BOOT_ST_OK         2u
#define BOOT_ST_PROTECTED  3u
#define BOOT_ST_ERASE_ERR  4u
#define BOOT_ST_WRITE_ERR  5u
#define BOOT_ST_VERIFY_ERR 6u
#define BOOT_ST_IMG_ERR    7u

extern volatile uint8_t boot_flash_status;

int      boot_flash_allowed(void);
int      boot_flash_erase(void);
int      boot_flash_write(uint32_t addr, const uint8_t *src, uint32_t len);
uint32_t boot_crc32(const uint8_t *p, uint32_t len);

#endif /* BOOT_FLASH_H */
