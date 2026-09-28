/**
  ******************************************************************************
  * @file    Loader_Src.c
  * @brief   Функции external loader'а, которые вызывает STM32CubeProgrammer:
  *          Init / Read / Write / SectorErase / MassErase.
  *
  *          Контракт ST (UM2237, stm32-memory-loaders):
  *            - все функции исполняются из RAM (см. linker.ld, ENTRY(Init));
  *            - возврат 1 (LOADER_OK) = успех, 0 (LOADER_FAIL) = ошибка;
  *            - Address приходит в адресном пространстве StorageInfo
  *              (DeviceStartAddress = 0x00000000), то есть совпадает со
  *              смещением внутри MX25R6435F и с адресами sfmap.h прошивки.
  *
  *          Init поднимает тактирование и SPI1 конфигурацией САМОГО ПРОЕКТА
  *          ЛОАДЕРА (отдельный CubeMX-проект, см. README.md в этой папке):
  *          основной прошивке MCU заниматься внешней flash при загрузке
  *          не нужно.
  ******************************************************************************
  */
#include "main.h"
#include "Dev_Inf.h"
#include "mx25r_drv.h"

#define LOADER_OK   0x1
#define LOADER_FAIL 0x0

/* Сгенерированы CubeMX-проектом лоадера (пары .c/.h или один main.c - не
   важно, символы одинаковые). */
extern void SystemClock_Config(void);
extern void MX_GPIO_Init(void);
extern void MX_SPI1_Init(void);

/**
  * @brief  Инициализация MCU и внешней flash. Вызывается первой.
  * @retval LOADER_OK / LOADER_FAIL (flash не ответила верным JEDEC ID)
  */
__attribute__((used)) int Init(void)
{
  uint8_t id[3];
  int rc;

  /* разрешение отладки ядра (как в шаблоне ST): DBGMCU->DHCSR, ключ 0xA05F */
  *(volatile uint32_t *)0xE000EDF0u = 0xA05F0000u;

  SystemInit();

  /* Векторная таблица загруженного в RAM кода: RAM базово 0x20000004,
     .isr_vector лежит по 0x20000200 (см. linker.ld). */
  SCB->VTOR = 0x20000000u | 0x200u;

  __set_PRIMASK(0);            /* прерывания нужны: HAL-таймауты на SysTick */

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();              /* в т.ч. CS_FLASH (PA4) = High              */
  MX_SPI1_Init();

  rc = mx25r_probe(id);        /* ждём JEDEC C2 28 17 (probe сам поднимает CS) */

  if (rc != 0)
  {
    __set_PRIMASK(1);
    return LOADER_FAIL;        /* CubeProgrammer покажет "Init failed":
                                  питание flash / CS / разводка SPI1        */
  }

  __set_PRIMASK(1);
  return LOADER_OK;
}

/**
  * @brief  Чтение внешней flash (его зовёт верификация и вкладка Memory).
  */
__attribute__((used)) int Read(uint32_t Address, uint32_t Size, uint8_t *buffer)
{
  int rc;

  __set_PRIMASK(0);
  rc = mx25r_read(Address, buffer, Size);
  __set_PRIMASK(1);
  return (rc == 0) ? LOADER_OK : LOADER_FAIL;
}

/**
  * @brief  Запись (CubeProgrammer сам режет файл на куски и заранее стирает
  *         затронутые сектора через SectorErase).
  */
__attribute__((used)) int Write(uint32_t Address, uint32_t Size, uint8_t *buffer)
{
  int rc;

  __set_PRIMASK(0);
  rc = mx25r_write(Address, buffer, Size);
  __set_PRIMASK(1);
  return (rc == 0) ? LOADER_OK : LOADER_FAIL;
}

/**
  * @brief  Стирание секторов 4 КБ, покрывающих [EraseStartAddress,
  *         EraseEndAddress). Куб трактует границу как исключающую; случай
  *         "start == end" считаем запросом на один сектор.
  */
__attribute__((used)) int SectorErase(uint32_t EraseStartAddress, uint32_t EraseEndAddress)
{
  uint32_t a;

  __set_PRIMASK(0);

  a = EraseStartAddress & ~0xFFFu;
  if (EraseEndAddress <= EraseStartAddress)
  {
    EraseEndAddress = EraseStartAddress + 1u;   /* один сектор */
  }
  for (; a < EraseEndAddress; a += 0x1000u)
  {
    if (mx25r_erase_sector(a) != 0)
    {
      __set_PRIMASK(1);
      return LOADER_FAIL;
    }
  }

  __set_PRIMASK(1);
  return LOADER_OK;
}

/**
  * @brief  Стирание всего чипа (Full chip erase в CubeProgrammer). Долго.
  */
__attribute__((used)) int MassErase(void)
{
  int rc;

  __set_PRIMASK(0);
  rc = mx25r_erase_chip();
  __set_PRIMASK(1);
  return (rc == 0) ? LOADER_OK : LOADER_FAIL;
}
