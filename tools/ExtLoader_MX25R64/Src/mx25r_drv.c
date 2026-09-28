/**
  ******************************************************************************
  * @file    mx25r_drv.c
  * @brief   Драйвер MX25R6435F для external loader'а: чистый опрос HAL_SPI,
  *          CS - программный (PA4, метка CS_FLASH из CubeMX).
  *
  *          КОНТЕКСТ: функции external loader'а, которые вызывает
  *          STM32CubeProgrammer через SWD. Прерывания в этот момент включены
  *          (Loader_Src.c снимает PRIMASK), поэтому HAL-таймауты, которые
  *          считает SysTick, работают штатно.
  *
  *          Всё, что связано с геометрией чипа:
  *            страница записи      256 Б   (PP 0x02)
  *            сектор стирания      4096 Б  (SE 0x20)
  *            объём                8 МБ    (3-байтная адресация)
  *            WIP                  бит 0 регистра статуса (RDSR 0x05)
  ******************************************************************************
  */
#include "mx25r_drv.h"
#include "main.h"          /* CS_FLASH_Pin / CS_FLASH_GPIO_Port (метка CubeMX)
                              и stm32u5xx_hal.h */

/* hspi1 объявляем extern, а не включаем spi.h: проект лоадера можно
   сгенерировать как с раздельными .c/.h на периферию, так и одним main.c -
   символ hspi1 существует в обеих раскладках.                              */
extern SPI_HandleTypeDef hspi1;

/* ---------------------------------------------------------------- команды - */
#define CMD_RDID   0x9Fu
#define CMD_RDSR   0x05u
#define CMD_WREN   0x06u
#define CMD_READ   0x03u
#define CMD_PP     0x02u
#define CMD_SE     0x20u
#define CMD_CE     0x60u

#define MX_PAGE        256u
#define MX_SECTOR      4096u
#define MX_CHIP_SIZE   0x00800000u

#define SPI_TMO_MS     1000u     /* таймаут одного SPI-обмена               */
#define WIP_TMO_PP_MS  500u      /* запас на Page Program (тип. ~0.2 мс)    */
#define WIP_TMO_SE_MS  3000u     /* запас на Sector Erase (тип. ~16 мс)     */
#define WIP_TMO_CE_MS  120000u   /* запас на Chip Erase  (тип. ~8 с)        */

/* JEDEC ID MX25R6435F: производитель C2 (Macronix), тип 28, объём 17 (64 Мбит) */
#define JEDEC0   0xC2u
#define JEDEC1   0x28u
#define JEDEC2   0x17u

/* -------------------------------------------------------------------- CS -- */
static void cs_low(void)  { HAL_GPIO_WritePin(CS_FLASH_GPIO_Port, CS_FLASH_Pin, GPIO_PIN_RESET); }
static void cs_high(void) { HAL_GPIO_WritePin(CS_FLASH_GPIO_Port, CS_FLASH_Pin, GPIO_PIN_SET); }

/* Команда (+ 3 байта адреса), CS остаётся НИЗКИМ - как в firmware-драйвере. */
static HAL_StatusTypeDef cmd_addr(uint8_t cmd, uint32_t addr)
{
  HAL_StatusTypeDef st;
  uint8_t a[3];

  a[0] = (uint8_t)(addr >> 16);
  a[1] = (uint8_t)(addr >> 8);
  a[2] = (uint8_t)addr;

  st = HAL_SPI_Transmit(&hspi1, &cmd, 1, SPI_TMO_MS);
  if (st == HAL_OK)
  {
    st = HAL_SPI_Transmit(&hspi1, a, 3, SPI_TMO_MS);
  }
  return st;
}

static HAL_StatusTypeDef cmd_only(uint8_t cmd)
{
  return HAL_SPI_Transmit(&hspi1, &cmd, 1, SPI_TMO_MS);
}

/* Один байт регистра статуса. Возврат: HAL_OK/HAL_*, *sr - прочитанное. */
static HAL_StatusTypeDef read_sr(uint8_t *sr)
{
  HAL_StatusTypeDef st;
  uint8_t cmd = CMD_RDSR;

  cs_low();
  st = HAL_SPI_Transmit(&hspi1, &cmd, 1, SPI_TMO_MS);
  if (st == HAL_OK)
  {
    st = HAL_SPI_Receive(&hspi1, sr, 1, SPI_TMO_MS);
  }
  cs_high();
  return st;
}

/* Ждать сброса WIP (бит 0) не дольше tmo_ms. Опрос раз в 1 мс (HAL_Delay -
   SysTick работает: PRIMASK снят вызывающей функцией лоадера). */
static int wait_wip(uint32_t tmo_ms)
{
  uint32_t t;

  for (t = 0; t < tmo_ms; t++)
  {
    uint8_t sr = 0xFFu;
    if (read_sr(&sr) != HAL_OK)
    {
      return -1;
    }
    if ((sr & 0x01u) == 0u)
    {
      return 0;             /* чип готов */
    }
    HAL_Delay(1);
  }
  return -1;                /* таймаут */
}

static int wren(void)
{
  cs_low();
  {
    HAL_StatusTypeDef st = cmd_only(CMD_WREN);
    cs_high();
    return (st == HAL_OK) ? 0 : -1;
  }
}

/* =============================================================== публичное */
int mx25r_probe(uint8_t id[3])
{
  uint8_t cmd = CMD_RDID;

  id[0] = id[1] = id[2] = 0;
  cs_high();

  cs_low();
  {
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi1, &cmd, 1, SPI_TMO_MS);
    if (st == HAL_OK)
    {
      st = HAL_SPI_Receive(&hspi1, id, 3, SPI_TMO_MS);
    }
    cs_high();
    if (st != HAL_OK)
    {
      return -1;
    }
  }
  return ((id[0] == JEDEC0) && (id[1] == JEDEC1) && (id[2] == JEDEC2)) ? 0 : -1;
}

int mx25r_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
  /* кусками по 32 КБ: HAL принимает любой Size, но умеренные транзакции
     устойчивее к помехам и проще для отладки */
  while (len > 0u)
  {
    uint32_t chunk = (len > 0x8000u) ? 0x8000u : len;
    HAL_StatusTypeDef st;

    if ((addr + chunk) > MX_CHIP_SIZE)
    {
      return -1;
    }

    cs_low();
    st = cmd_addr(CMD_READ, addr);
    if (st == HAL_OK)
    {
      st = HAL_SPI_Receive(&hspi1, dst, (uint16_t)chunk, SPI_TMO_MS);
    }
    cs_high();

    if (st != HAL_OK)
    {
      return -1;
    }
    addr += chunk;
    dst  += chunk;
    len  -= chunk;
  }
  return 0;
}

int mx25r_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
  while (len > 0u)
  {
    uint32_t room  = MX_PAGE - (addr % MX_PAGE);   /* до границы страницы   */
    uint32_t chunk = (len < room) ? len : room;
    HAL_StatusTypeDef st;

    if ((addr + chunk) > MX_CHIP_SIZE)
    {
      return -1;
    }
    if (wren() != 0)
    {
      return -1;
    }

    cs_low();
    st = cmd_addr(CMD_PP, addr);
    if (st == HAL_OK)
    {
      st = HAL_SPI_Transmit(&hspi1, (uint8_t *)src, (uint16_t)chunk, SPI_TMO_MS);
    }
    cs_high();

    if ((st != HAL_OK) || (wait_wip(WIP_TMO_PP_MS) != 0))
    {
      return -1;
    }
    addr += chunk;
    src  += chunk;
    len  -= chunk;
  }
  return 0;
}

int mx25r_erase_sector(uint32_t addr)
{
  HAL_StatusTypeDef st;

  addr &= ~(MX_SECTOR - 1u);
  if (addr >= MX_CHIP_SIZE)
  {
    return -1;
  }
  if (wren() != 0)
  {
    return -1;
  }

  cs_low();
  st = cmd_addr(CMD_SE, addr);
  cs_high();

  if ((st != HAL_OK) || (wait_wip(WIP_TMO_SE_MS) != 0))
  {
    return -1;
  }
  return 0;
}

int mx25r_erase_chip(void)
{
  HAL_StatusTypeDef st;

  if (wren() != 0)
  {
    return -1;
  }
  cs_low();
  st = cmd_only(CMD_CE);
  cs_high();

  if ((st != HAL_OK) || (wait_wip(WIP_TMO_CE_MS) != 0))
  {
    return -1;
  }
  return 0;
}
