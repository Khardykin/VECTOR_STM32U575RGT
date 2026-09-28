# External loader MX25R6435F для STM32CubeProgrammer

Запись образа звуков `tools/sounds.bin` во внешнюю SPI flash платы VECTOR
(STM32U575RGT + MX25R6435F на SPI1) напрямую из ST-LINK/CubeProgrammer.

Это НЕ проект CubeIDE, а **файлы для вставки** в пустой CubeMX-проект того же
MCU (STM32U575RGTx). Полная пошаговая инструкция (создание проекта, настройки
CubeMX, линкер, post-build, установка `.stldr`, запись образа, диагностика):

**→ [docs/FLASHING.md](../../docs/FLASHING.md), раздел 0–1.**

| Файл | Назначение |
|---|---|
| `Src/Loader_Src.c` | API лоадера: `Init/Read/Write/SectorErase/MassErase` |
| `Src/Dev_Inf.c`, `Inc/Dev_Inf.h` | паспорт памяти `StorageInfo` (SPI_FLASH, 8 МБ, сектора 4 КБ, страница 256 Б, база 0x00000000) |
| `Src/mx25r_drv.c`, `Inc/mx25r_drv.h` | драйвер MX25R6435F: опросный SPI без DMA/прерываний (команды 03/02/20/06/05/9F/60, WIP-поллинг) |
| `linker.ld` | линкер «исполнения из RAM»: `ENTRY(Init)`, код по 0x20000004+, сегмент `.Dev_info` для `StorageInfo` |

Пины (обязаны совпадать с основной прошивкой): **SCK=PA5, MISO=PA6,
MOSI=PA7, CS=PA4 (метка CubeMX `CS_FLASH`, уровень по умолчанию High)**.

Основано на шаблоне ST [stm32-memory-loaders](https://github.com/STMicroelectronics/stm32-memory-loaders)
(ветка contrib, `Loader_Files/other devices`) и статье ST «How to add your SPI
flash into the STM32CubeProgrammer's external loader» (части 1–2).
