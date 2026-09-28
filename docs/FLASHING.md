# Прошивка программатором (ST-LINK + STM32CubeProgrammer)

С этого патча устройство прошито **только программатором**:

* прошивка MCU — как обычно, по SWD;
* образ звуков `tools/sounds.bin` — во внешнюю SPI flash **MX25R6435F** тем же
  ST-LINK через **external loader** (проект лежит в `Tools/ExtLoader_MX25R64/`).

Прошивка звуков во внутреннюю flash MCU и автоматическая factory-запись при
старте **удалены** (экономия ~350 КБ внутренней flash и 131 КБ RAM; внешний
flash на старте больше не стирается и не пишется вообще).

---

## 0. Один раз: собрать external loader

External loader — это маленький исполняемый модуль (`.stldr`), который
CubeProgrammer загружает в RAM MCU и через него стирает/пишет/читает внешнюю
SPI flash. Собирается один раз в STM32CubeIDE (5–10 минут).

### 0.1. Создать проект лоадера

1. `File → New → STM32 Project`, выбрать **STM32U575RGTx**,
   имя проекта — например `ExtLoader_MX25R64`, Targeted Language = C.
2. В появившейся вкладке **CubeMX** (`.ioc` проекта лоадера) настроить:

   | Раздел | Что поставить | Зачем |
   |---|---|---|
   | System Core → SYS | Debug = **Serial Wire**; Timebase Source = SysTick | SWD остаётся живым |
   | Connectivity → **SPI1** | Mode = **Full-Duplex Master**; пины займутся сами: **PA5=SCK, PA6=MISO, PA7=MOSI** (как в основной прошивке) | шина внешней flash |
   | SPI1 → Parameter Settings | Frame = 8 Bits, MSB first, **CPOL = Low, CPHA = 1 Edge** (SPI mode 0), Baud Rate Prescaler = **8**, NSS = **Software (Disable)** | режим чипа MX25R |
   | Connectivity → **RCC** | SYSCLK Mux = **HSI16** (PLL/HSE не нужны) | детерминированные 16 МГц; SPI1 = 2 Мбит/с |
   | **PA4** | Signal = **GPIO_Output**, User Label = **CS_FLASH**, GPIO output level = **High** | программный CS внешней flash (метка обязательна: код лоадера использует `CS_FLASH_Pin/CS_FLASH_GPIO_Port`) |
   | NVIC | ничего не включать (SysTick по умолчанию) | лоадер работает опросом |

   `Project Manager → Code Generator`: можно поставить галку
   «Generate peripheral initialization as a pair of ‘.c/.h’ files per
   peripheral» — код лоадера от этого не зависит (объявляет `hspi1` и
   `MX_*_Init()` через `extern`).

3. Сгенерировать код (сохранить `.ioc`).

### 0.2. Вставить файлы лоадера

Из этой папки (`Tools/ExtLoader_MX25R64/`) скопировать в проект:

| Файл | Куда |
|---|---|
| `Src/Loader_Src.c` | `Core/Src/` |
| `Src/Dev_Inf.c`    | `Core/Src/` |
| `Src/mx25r_drv.c`  | `Core/Src/` |
| `Inc/Dev_Inf.h`    | `Core/Inc/` |
| `Inc/mx25r_drv.h`  | `Core/Inc/` |
| `linker.ld`        | в корень проекта |

Свой сгенерированный `main.c` можно оставить как есть (он не используется:
точка входа лоадера — `Init()` из `Loader_Src.c`).

### 0.3. Настроить сборку

1. `Project → Properties → C/C++ Build → Settings → MCU GCC Linker → General`:
   * **Linker Script** = `linker.ld` из корня проекта;
   * снять галку **«Discard unused sections»** (функции API вызывает
     CubeProgrammer, линкеру они кажутся неиспользуемыми; в `linker.ld` они
     дополнительно защищены `KEEP`, но галку лучше снять).
2. `C/C++ Build → Settings → Build Steps → Post-build steps`:
   * Command: `cmd /c copy /Y "${BuildArtifactFileBaseName}.elf" "..\${BuildArtifactFileBaseName}.stldr"`
   * Description: `make stldr`

   (`.stldr` — это ELF лоадера; CubeProgrammer принимает его под этим именем.)
3. Build. В корне проекта появится `ExtLoader_MX25R64.stldr`.

### 0.4. Установить в CubeProgrammer

Скопировать `.stldr` в каталог внешних лоадеров (нужны права администратора):

```
C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\ExternalLoader\
```

Перезапустить STM32CubeProgrammer.

---

## 1. Записать образ звуков во внешнюю flash

### 1.1. Сначала собрать sounds.bin (44.1 кГц!)

Прошивка настроена на **44100 Гц** (`AUDIO_SAMPLE_RATE` в `audio_player.h`).
Образ с другой частотой плеер отвергнет (`image rate mismatch` в логе).

```bash
# wav-исходники должны быть 44.1 кГц / 16 бит / моно (см. AUDIO.md, раздел
# "Качество звука"). Порядок файлов в команде = индексы звуков в прошивке.
python tools/pack_sounds.py tools/b_click.wav tools/c_voice_gas.wav tools/d_myvoice.wav ^
       --rate 44100 --out tools/sounds.bin
```

Скрипт перегенерирует `Core/VectorLib/Audio/Inc/audio_ids.h` (enum `SND_*`) —
после этого прошивку MCU нужно **пересобрать**.

### 1.2. Запись через CubeProgrammer GUI

1. Подключить ST-LINK, подать питание на плату.
2. Вкладка **Erasing & Programming** → секция **External loaders** →
   отметить `MX25R6435F_EXT_SPI1_VECTOR`.
3. Нажать **Connect** (порт SWD, скорость по умолчанию).
   * Если «Init failed» — лоадер не увидел flash: питание, CS=PA4, SPI1-пины,
     метка `CS_FLASH` в проекте лоадера.
4. Секция **Download**:
   * File path = `tools/sounds.bin`
   * Start address = **0x00000000** (адресное пространство лоадера = смещение
     в чипе; `AUDIO_IMG_BASE_ADDR` в прошивке тоже 0)
   * галки **Verify programming** (и, по желанию, *Skip flash erase before
     programming* НЕ ставить — куб сам сотрёт затронутые сектора).
5. **Start Programming**. ~1 МБ пишется за секунды (стирание 4-КБ секторами +
   верификация чтением через лоадер).
6. Проверка: вкладка Memory → адрес `0x00000000` → Read: первые байты должны
   быть `53 4E 44 49` («SNDI» — magic образа).

Звуки во внешней flash **энергонезависимы**: записали один раз — устройство
играет их после каждой перезагрузки, ничего при старте не перетирается.

### 1.3. То же самое из командной строки

```bat
"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" ^
  -c port=SWD reset=HWrst ^
  -el "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\ExternalLoader\ExtLoader_MX25R64.stldr" ^
  -d tools\sounds.bin 0x00000000 -v
```

Полное стирание чипа (если нужно): `-e all` при подключённом external loader.

---

## 2. Записать прошивку MCU

### 2.1. Без IDE (CubeProgrammer)

1. Собрать проект в CubeIDE (или взять готовый `.elf`/`.hex`/`.bin`).
2. CubeProgrammer → **Download** → File = `VECTOR_STM32U575RGT.elf` (или .hex)
   → Start address `0x08000000` (для `.elf`/`.hex` адрес подставится сам) →
   **Verify programming** → **Start Programming** → опция *Run* или кнопка
   Reset на плате.

CLI:

```bat
STM32_Programmer_CLI.exe -c port=SWD reset=HWrst -w VECTOR_STM32U575RGT.elf -v -rst
```

### 2.2. Из CubeIDE — можно, но закрывайте сессию

Run/Debug в CubeIDE захватывает SWD и **удерживает отладочный сервер**;
терминалу он не мешает (лог идёт по UART4), но Live Watch/Expressions
периодически останавливает ядро — отсюда заикания и «залипания» в отладке.
Прошили через IDE → нажмите **Terminate** (красный квадрат) и/или отключитесь
от target, либо шейте через CubeProgrammer (п. 2.1).

---

## 3. Порядок обновления набора звуков

1. Пересобрать wav-исходники в **44.1 кГц** (см. AUDIO.md).
2. `python tools/pack_sounds.py ... --rate 44100 --out tools/sounds.bin`
   (перегенерируется `audio_ids.h`).
3. **Пересобрать прошивку** MCU (индексы/имена звуков живут в `audio_ids.h`).
4. Записать `sounds.bin` во внешнюю flash (раздел 1).
5. Записать прошивку в MCU (раздел 2) — можно в любом порядке: плеер сверяет
   частоту образа с `AUDIO_SAMPLE_RATE` и не даст играть «чужой» образ.
6. Reset → в логе: `image ok=1 sounds=5`.

---

## 4. Карта внешней flash (`Core/VectorLib/Audio/Inc/sfmap.h`)

```
0x000000 +------------------------------------------+
         | SOUNDS  4 МБ   sounds.bin целиком        |  <- шьётся лоадером с адреса 0x0
0x400000 +------------------------------------------+
         | CONFIG  4 КБ   конфигурация устройства   |  <- пишется только прошивкой
0x401000 +------------------------------------------+
         | LOG     ~3.9 МБ кольцевой журнал         |  <- пишется только прошивкой
0x800000 +------------------------------------------+
```

Лоадер видит весь чип (8 МБ), но шить нужно **только область SOUNDS**
(адрес 0x0, длина = размер `sounds.bin`). Полный chip erase (`-e all`) сотрёт
конфиг и журнал — обычно это не нужно.

## 5. Быстрая диагностика

| Симптом | Причина / лечение |
|---|---|
| CubeProgrammer не видит лоадер в списке EL | `.stldr` не скопирован в `bin\ExternalLoader`, нужен перезапуск CubeProgrammer |
| `Init failed` при подключении с лоадером | flash не ответила JEDEC `C2 28 17`: питание 3.3 В, CS=PA4 (метка `CS_FLASH`!), пины SPI1 PA5/PA6/PA7, CPOL/CPHA = mode 0 |
| Ошибки записи/верификации | понизить скорость: SPI1 Baud Rate Prescaler = 16 или 32 в проекте лоадера, пересобрать |
| Плеер: `image rate mismatch` | образ собран не под 44100 Гц — пересобрать `sounds.bin` с `--rate 44100` |
| Плеер: `image header bad` | образ не записан/бит — перечитать память в CubeProgrammer по адресу 0x0 (первые байты = `SNDI`), при необходимости перезаписать |
| `flash probe FAIL` в логе прошивки | внешняя flash не отвечает основной прошивке: те же CS/пины/питание; сравнить с Init лоадера |
