/**
  ******************************************************************************
  * @file    Dev_Inf.c
  * @brief   Паспорт внешней flash для STM32CubeProgrammer
  *
  *          MX25R6435F (Macronix, 64 Мбит = 8 МБ, JEDEC ID C2 28 17) на SPI1
  *          платы VECTOR (STM32U575RGT): SCK=PA5, MISO=PA6, MOSI=PA7, CS=PA4.
  *
  *          Адресация: память НЕ memory-mapped (обычный SPI), поэтому базовый
  *          адрес условный - 0x00000000; CubeProgrammer передаёт его в
  *          Read/Write/SectorErase как есть, и он совпадает со смещением в
  *          чипе и с AUDIO_IMG_BASE_ADDR прошивки (sfmap.h).
  *
  *          Сектора 4 КБ (2048 шт.), страница записи 256 Б, стёртое состояние
  *          0xFF. Образ sounds.img (~1 МБ при 44.1 кГц) кладётся с адреса 0.
  ******************************************************************************
  */
#include "Dev_Inf.h"

#if defined (__ICCARM__)
__root struct StorageInfo const StorageInfo = {
#else
struct StorageInfo __attribute__((section(".Dev_info"), used)) const StorageInfo = {
#endif
    "MX25R6435F_EXT_SPI1_VECTOR",  /* Device Name + version  */
    SPI_FLASH,                     /* Device Type            */
    0x00000000,                    /* Device Start Address   */
    0x00800000,                    /* Device Size: 8 MB      */
    256,                           /* Programming Page Size  */
    0xFF,                          /* Erased Memory Content  */

    /* Specify Size and Address of Sectors */
    {
        { 2048, 0x1000 },          /* 2048 sectors of 4 KB   */
        { 0x00000000, 0x00000000 }
    }
};
