#include "boot_flash.h"
#include <string.h>
#include "main_vector.h"      /* FLASH_ADDRESS_*_BOOTLOADER */
#include "sfmap.h"            /* SF_APP_STAGE_BASE */
#include "spiflash.h"         /* sf_read */
#include "stm32u5xx_hal.h"

volatile uint8_t boot_flash_status = BOOT_ST_IDLE;

int boot_flash_allowed(void)
{
#if (CONFIG_BOOTLOADER_PRESENT == 0)
  return 0;
#else
  if (SCB->VTOR < (uint32_t)FLASH_ADDRESS_STOP_BOOTLOADER)
  {
    return 0;
  }
  return 1;
#endif
}

uint32_t boot_crc32(const uint8_t *p, uint32_t len)
{
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t i;
  uint8_t  b;

  for (i = 0u; i < len; i++)
  {
    crc ^= (uint32_t)p[i];
    for (b = 0u; b < 8u; b++)
    {
      crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
  }
  return ~crc;
}

int boot_flash_erase(void)
{
  FLASH_EraseInitTypeDef e;
  uint32_t page_err = 0u;
  HAL_StatusTypeDef st;
  uint32_t first;
  uint32_t nb;

  if (boot_flash_allowed() == 0)
  {
    boot_flash_status = BOOT_ST_PROTECTED;
    return -1;
  }

  first = ((uint32_t)FLASH_ADDRESS_START_BOOTLOADER - 0x08000000u) / BOOT_FLASH_PAGE_SIZE;
  nb    = (((uint32_t)FLASH_ADDRESS_STOP_BOOTLOADER -
            (uint32_t)FLASH_ADDRESS_START_BOOTLOADER) + BOOT_FLASH_PAGE_SIZE - 1u) /
          BOOT_FLASH_PAGE_SIZE;

  boot_flash_status = BOOT_ST_BUSY;
  e.TypeErase = FLASH_TYPEERASE_PAGES;
  e.Banks     = FLASH_BANK_1;
  e.Page      = first;
  e.NbPages   = nb;

  (void)HAL_FLASH_Unlock();
  st = HAL_FLASHEx_Erase(&e, &page_err);
  HAL_FLASH_Lock();

  if (st != HAL_OK)
  {
    boot_flash_status = BOOT_ST_ERASE_ERR;
    return -2;
  }
  boot_flash_status = BOOT_ST_OK;
  return 0;
}

int boot_flash_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
  uint32_t off;
  uint32_t tmp[4];
  HAL_StatusTypeDef st;

  if (boot_flash_allowed() == 0)
  {
    boot_flash_status = BOOT_ST_PROTECTED;
    return -1;
  }
  if ((len == 0u) || ((len & 0x0Fu) != 0u) || ((addr & 0x0Fu) != 0u))
  {
    boot_flash_status = BOOT_ST_WRITE_ERR;
    return -3;
  }
  if ((addr < (uint32_t)FLASH_ADDRESS_START_BOOTLOADER) ||
      (((uint64_t)addr + len) > (uint64_t)FLASH_ADDRESS_STOP_BOOTLOADER))
  {
    boot_flash_status = BOOT_ST_WRITE_ERR;
    return -3;
  }

  boot_flash_status = BOOT_ST_BUSY;
  (void)HAL_FLASH_Unlock();
  for (off = 0u; off < len; off += 16u)
  {
    memcpy(tmp, src + off, 16u);
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, addr + off, (uint32_t)tmp);
    if (st != HAL_OK)
    {
      HAL_FLASH_Lock();
      boot_flash_status = BOOT_ST_WRITE_ERR;
      return -4;
    }
  }
  HAL_FLASH_Lock();

  if (memcmp((const void *)addr, src, len) != 0)
  {
    boot_flash_status = BOOT_ST_VERIFY_ERR;
    return -2;
  }
  boot_flash_status = BOOT_ST_OK;
  return 0;
}
