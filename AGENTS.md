# AGENTS.md

## Язык общения
- ВСЕГДА отвечай, объясняй свои действия, рассуждения и планы строго на русском языке.
- Не используй английский язык в ответах пользователю (кроме названий функций, переменных, файлов и команд терминала).


Прошивка для STM32U575RGT (Cortex-M33, ThreadX/Azure RTOS, STM32CubeIDE).
Документация проекта — на русском; точка входа — `docs/README.md`.

## Источники истины
- `docs/SYSTEM.md` — карта кода, контексты выполнения, источники времени, чек-лист CubeMX.
- `docs/ARCHITECTURE.md` — потоки, прерывания, периферия, карта внешней flash.
- `docs/PLAN.md` §5/§7 — состояние работ и известные дефекты; смотреть до того, как
  «чинить» задокументированное поведение.
- Документация местами отстаёт от кода (например, сейчас `VECTOR_LOG_UART=3`,
  `VECTOR_AUDIO_DEMO_KEYS=0`). Сверяйте значения по `vector_config.h` /
  `config_device.h`, а не по тексту документации.

## Сборка, IntelliSense, тесты, прошивка
- Сборка — конфигурация `Debug` в STM32CubeIDE. `Debug/` генерируется и лежит
  в `.gitignore`; `arm-none-eabi-gcc` в PATH не установлен.
- IntelliSense настроен в `.vscode/c_cpp_properties.json`: компилятор
  `D:/ST/STM32CubeIDE_2.1.1/.../gnu-tools-for-stm32.14.3.rel1.../tools/bin/arm-none-eabi-gcc.exe`,
  `STM32U575xx`, `USE_HAL_DRIVER`, все include-пути проекта.
- HAL/CMSIS/ThreadX (`Drivers/STM32U5xx_HAL_Driver`, `Drivers/CMSIS`,
  `Middlewares/`) и `Drivers/lvgl` не хранятся в git — ставятся из пака CubeMX.
  Не вендорить и не коммитить их. `Drivers/lv_conf.h` в git есть.
- Единственный подключённый хост-тест: `make -C tools/test_logq run` (нужны
  gcc+make). Он собирает настоящий `Core/VectorLib/Src/log_queue.c`, а не
  копию-мок.
- Образ звуков: `python tools/pack_sounds.py <упорядоченный список wav>`. Порядок
  файлов задаёт индексы `SND_*` и перезаписывает
  `Core/VectorLib/Src/Audio/Inc/audio_ids.h` — после упаковки образа пересоберите
  прошивку. `python tools/pack_sounds.py tools/sounds.bin --check` сверяет образ
  с манифестом.
- Прошивка: `docs/FLASHING.md`. MCU — через ST-LINK/CubeProgrammer; `sounds.bin` —
  во внешнюю MX25R6435F через `tools/ExtLoader_MX25R64` по адресу 0x0. Прошивка
  никогда не пишет область SOUNDS.
- Отчёт по памяти: `python tools/mem_report.py Debug/VECTOR_STM32U575RGT.map`.

## Не читать без необходимости
- `Drivers/**`, `Middlewares/**`, `Debug/**`, `Drivers/ui_Vector/fonts/**` —
  сторонний или сгенерированный код (HAL/CMSIS/ThreadX/LVGL, шрифты SquareLine
  Studio). Не открывать их для обзора, поиска примеров или «как принято»: это
  тысячи строк и лишний контекст. При необходимости — точечно один конкретный
  файл (например, `Drivers/lv_conf.h`), без рекурсивного чтения.
- Корневые `*.diff` / `*.patch` — история правок автора, они в `.gitignore`;
  не читать и не трогать.

## Не править / не включать бездумно
- CubeMX генерирует `Core/Src/*.c`, `Core/Inc/main.h` и `AZURE_RTOS/App/*`.
  Правьте только внутри `/* USER CODE BEGIN ... */`; остальное затирается при
  регенерации. Код приложения — в `Core/VectorLib/{Inc,Src}`.
- Периферия, DMA, NVIC и тактирование меняются только в
  `VECTOR_STM32U575RGT.ioc` через CubeMX, а не в сгенерированном коде.
- После регенерации проверьте `STM32U575RGTX_FLASH.ld`: `FLASH LENGTH` должен
  остаться `1016K` (CubeMX возвращает 1024K и открывает заводскую страницу
  по 0x080FE000).
- Переключатели фич — только в `vector_config.h` (`VECTOR_*`) и `config_device.h`
  (`CONFIG_*`). Не размазывайте новые `#define` по модулям.
- Текущая сборка: включены GPS, TFT/LVGL, датчики I2C; `CONFIG_BLE=0`,
  `CONFIG_LORA=0`, `CONFIG_G4=0`. Для включения BLE/LoRa всё ещё нужны
  `USART_BLE`/`USART_RF` в `main.h`, порт LoRa без конфликта с логом/COM
  на UART4 и `FIRMWARE_VERSION` для всех номеров устройств (PLAN §7).
- Точки входа модулей (`Ble_Run`, `Lora_Run`, `Lte_Run`, ...) — weak-заглушки
  в `main_vector.c`. Заменяйте их в `.c` самого модуля; заглушки не правьте.
- `CONFIG_TYPE_LCD_TFT` противоречив: в `config_device.h` написано, что
  1 = драйвер панели, 2 = индикация Avis, но `TFT_indicator.c` компилируется
  при `== 1`. Сейчас собираются оба модуля экрана. Перед изменением
  конфигурации экрана проверьте условия `#if`.
- `Drivers/ui_Vector/**` — экспорт SquareLine Studio 1.6.x, `Core/ui/lv_i18n/**` —
  генерируемый. Правьте `Core/translations/*.yml`, затем запускайте
  `Core/update_lang.bat` (нужен CLI `lv_i18n`).

## ThreadX: контексты и время
- Контексты важны: до планировщика (`main`, `tx_application_define` — без
  блокировок RTOS, мьютексов и сна), потоки, ISR. В ISR — только неблокирующие
  `tx_queue_send(TX_NO_WAIT)`, `tx_semaphore_put`, флаги и запись в кольцо.
  Вызовы лога из ISR отбрасываются.
- Время приложения — тик ThreadX 10 мс (`vector_tick.h`: `VTICK_MS`,
  `VTICK_SLEEP_MS`, `VTICK_MS2TICKS`). Никогда не используйте `HAL_GetTick()` /
  `HAL_Delay()` в VectorLib: тик HAL (TIM6) — только для драйверов HAL.
- Таймеры прибора — `START_TIMER*` / `TEST_TIMER*` / `timer.flag_*`
  (TIM3 1 кГц, RTC wakeup 1 Гц). До планировщика тика нет — ожидания только
  числом проходов цикла.
- `Sys_Cfg_Lock()` — мьютекс только для потоков, для RMW `Sns_Cfg_struct` /
  `StateErr`; `TX_DISABLE` — для коротких (мкс) снимков и обновлений очереди
  в любом контексте. Порядок замков: cfg -> ext. Нельзя держать `TX_DISABLE`
  во время операций с flash.
- Динамического выделения нет: ThreadX byte pool не создаётся, malloc
  не используется. Память — статическая.

## SPSC-кольца и владение ресурсами
- `InputBuffer[TYPE_*]` — строгий SPSC (`buffer.h`): производитель (ISR UART)
  пишет только `end`, потребитель — только `begin`. У каждого кольца ровно один
  читающий поток; второго потребителя не добавлять. `TYPE_BLE` читает BLE Task,
  остальные — Receiver Task. GPS идёт мимо колец, в `Gps.c`.
- `Audio Player` — единственный владелец SAI1/DMA и буфера `ap_buf[]`.
  Из ISR только флаги/счётчики и `tx_semaphore_put(ap_wake)`; дозагрузка —
  всегда из потока плеера.
- `Receiver Task` — единственный потребитель колец и парсер UART/LoRa/LTE.
  `Measure Task` владеет I2C1 (BME280/LIS3DH/MAX17048) и является штатным
  писателем внешней flash.
- Внешняя flash — только через `extstore` (`ext_read/ext_write/ext_erase_sector`).
  Запись и стирание — только из `Measure Task` и только в «окно тишины»,
  то есть при `audio_is_busy()==0`. Пока звук играет, флаги/очередь ждут
  следующего прохода; `EXT_BUSY` означает «повторить позже», а не ошибку.
- Журнал: `log_post(type)` делает снимок события в RAM-очередь 16×40 Б
  (только из потока), `log_flush()` переносит её во flash из `Measure Task`.
  Не писать записи журнала во flash напрямую. Исключение —
  `save_event_type_2()` из Receiver Task перед сбросом.

## Лог и стиль кода
- Строки лога — только ASCII (терминалы Windows в CP1251). Комментарии кода
  и `docs/` — на русском; держите этот стиль.
- Комментарий каждой функции описывает назначение, контекст (init/поток/ISR)
  и возвращаемое значение.
- Лог — через `LOG_D/I/W/E` (`vector_log.h`), уровни и маски задаются
  в `vector_config.h` и рантаймом `vlog_set_level()` / `vlog_set_mask()`.

## Git и файлы
- `.bat` хранятся побайтово в CRLF (`-text` в `.gitattributes`). Патчи применять
  через `git apply --binary`, никогда `git am` (он срезает CRLF у `.bat`).
- Коммиты делает автор проекта; не коммитьте без просьбы.
- CI, линтера и форматтера нет: не выдумывайте команды lint/format.