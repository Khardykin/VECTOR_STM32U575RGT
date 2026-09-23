/**
  ******************************************************************************
  * @file    spiflash.c
  * @brief   Драйвер внешней SPI NOR flash MX25K6435F (SPI1, CS = PA4)
  *
  *          Всё блокирующее и простое: на этапе отладки это важнее скорости.
  *          Чтение 2 КБ при 20 МГц занимает ~0.8 мс, что с запасом хватает
  *          для подкачки аудио в кольцевой буфер (нужно 16 КБ/с при 8 кГц).
  ******************************************************************************
  */
#include "spiflash.h"
#include "spi.h"
#include "main.h"

#define SF_TIMEOUT_MS   250u
#define SF_ERASE_TMO_MS 500u
#define SF_PROG_TMO_MS  100u

volatile uint8_t  sf_jedec[3] = { 0, 0, 0 };
volatile uint8_t  sf_rdsr     = 0xFF;
volatile int32_t  sf_probe_rc = -1;

/* ------------------------------------------------------------------ CS --- */
static void cs_low(void)  { HAL_GPIO_WritePin(CS_FLASH_GPIO_Port, CS_FLASH_Pin, GPIO_PIN_RESET); }
static void cs_high(void) { HAL_GPIO_WritePin(CS_FLASH_GPIO_Port, CS_FLASH_Pin, GPIO_PIN_SET); }

/* Одиночная команда без адреса (WREN, RDID-команда и т.п.) */
static HAL_StatusTypeDef cmd_only(uint8_t cmd)
{
  HAL_StatusTypeDef st;
  cs_low();
  st = HAL_SPI_Transmit(&hspi1, &cmd, 1, SF_TIMEOUT_MS);
  cs_high();
  return st;
}

/* Команда + 3-байтный адрес. ВНИМАНИЕ: CS остаётся НИЗКИМ,
   вызывающий продолжает обмен и сам вызывает cs_high(). */
static HAL_StatusTypeDef cmd_addr(uint8_t cmd, uint32_t addr)
{
  uint8_t b[4];
  b[0] = cmd;
  b[1] = (uint8_t)((addr >> 16) & 0xFFu);
  b[2] = (uint8_t)((addr >> 8)  & 0xFFu);
  b[3] = (uint8_t)(addr & 0xFFu);
  cs_low();
  return HAL_SPI_Transmit(&hspi1, b, 4, SF_TIMEOUT_MS);
}

/* Ожидание сброса бита WIP статус-регистра */
static HAL_StatusTypeDef wait_busy(uint32_t timeout_ms)
{
  uint32_t t0 = HAL_GetTick();
  for (;;)
  {
    uint8_t cmd = SF_CMD_RDSR;
    uint8_t sr  = 0xFF;
    cs_low();
    (void)HAL_SPI_Transmit(&hspi1, &cmd, 1, SF_TIMEOUT_MS);
    (void)HAL_SPI_Receive(&hspi1, &sr, 1, SF_TIMEOUT_MS);
    cs_high();
    if ((sr & SF_SR_WIP) == 0u)
    {
      return HAL_OK;
    }
    if ((HAL_GetTick() - t0) > timeout_ms)
    {
      return HAL_TIMEOUT;
    }
  }
}

/* ---------------------------------------------------------------- probe --- */
HAL_StatusTypeDef sf_probe(void)
{
  uint8_t cmd = SF_CMD_RDID;
  uint8_t id[3] = { 0, 0, 0 };
  HAL_StatusTypeDef st;

  cs_high();

  cs_low();
  st = HAL_SPI_Transmit(&hspi1, &cmd, 1, SF_TIMEOUT_MS);
  if (st == HAL_OK)
  {
    st = HAL_SPI_Receive(&hspi1, id, 3, SF_TIMEOUT_MS);
  }
  cs_high();
  sf_jedec[0] = id[0];
  sf_jedec[1] = id[1];
  sf_jedec[2] = id[2];

  cmd = SF_CMD_RDSR;
  cs_low();
  if (st == HAL_OK)
  {
    st = HAL_SPI_Transmit(&hspi1, &cmd, 1, SF_TIMEOUT_MS);
  }
  if (st == HAL_OK)
  {
    uint8_t sr = 0xFF;
    st = HAL_SPI_Receive(&hspi1, &sr, 1, SF_TIMEOUT_MS);
    sf_rdsr = sr;
  }
  cs_high();

  sf_probe_rc = (int32_t)st;
  return st;
}

/* ---------------------------------------------------------------- read ---- */
HAL_StatusTypeDef sf_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
  HAL_StatusTypeDef st;

  if ((addr + len) > SF_TOTAL_SIZE)
  {
    return HAL_ERROR;
  }

  st = cmd_addr(SF_CMD_READ, addr);
  while ((st == HAL_OK) && (len > 0u))
  {
    uint32_t chunk = (len > 0x8000u) ? 0x8000u : len;   /* предел uint16_t Size */
    st = HAL_SPI_Receive(&hspi1, dst, (uint16_t)chunk, SF_TIMEOUT_MS);
    dst += chunk;
    addr += chunk;
    len -= chunk;
  }
  cs_high();
  return st;
}

/* --------------------------------------------------------------- erase ---- */
HAL_StatusTypeDef sf_sector_erase(uint32_t addr)
{
  HAL_StatusTypeDef st;

  if (((addr % SF_SECTOR_SIZE) != 0u) || (addr >= SF_TOTAL_SIZE))
  {
    return HAL_ERROR;
  }
  st = cmd_only(SF_CMD_WREN);
  if (st != HAL_OK)
  {
    return st;
  }
  st = cmd_addr(SF_CMD_SE, addr);
  cs_high();
  if (st != HAL_OK)
  {
    return st;
  }
  return wait_busy(SF_ERASE_TMO_MS);
}

/* ------------------------------------------------------------- program ---- */
HAL_StatusTypeDef sf_program(uint32_t addr, const uint8_t *src, uint32_t len)
{
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t done = 0;

  if ((addr + len) > SF_TOTAL_SIZE)
  {
    return HAL_ERROR;
  }

  while (done < len)
  {
    uint32_t in_page = addr % SF_PAGE_SIZE;
    uint32_t chunk   = SF_PAGE_SIZE - in_page;
    uint8_t *p       = (uint8_t *)(uintptr_t)(src + done);

    if (chunk > (len - done))
    {
      chunk = len - done;
    }

    st = cmd_only(SF_CMD_WREN);
    if (st != HAL_OK)
    {
      break;
    }
    st = cmd_addr(SF_CMD_PP, addr);
    if (st == HAL_OK)
    {
      st = HAL_SPI_Transmit(&hspi1, p, (uint16_t)chunk, SF_TIMEOUT_MS);
    }
    cs_high();
    if (st != HAL_OK)
    {
      break;
    }
    st = wait_busy(SF_PROG_TMO_MS);
    if (st != HAL_OK)
    {
      break;
    }

    addr += chunk;
    done += chunk;
  }
  return st;
}

/* -------------------------------------------------------------- verify ---- */
HAL_StatusTypeDef sf_verify(uint32_t addr, const uint8_t *src, uint32_t len)
{
  static uint8_t buf[SF_PAGE_SIZE];
  uint32_t done = 0;

  while (done < len)
  {
    uint32_t chunk = (len - done > sizeof buf) ? sizeof buf : (len - done);
    uint32_t i;
    if (sf_read(addr + done, buf, chunk) != HAL_OK)
    {
      return HAL_ERROR;
    }
    for (i = 0; i < chunk; i++)
    {
      if (buf[i] != src[done + i])
      {
        return HAL_ERROR;
      }
    }
    done += chunk;
  }
  return HAL_OK;
}
