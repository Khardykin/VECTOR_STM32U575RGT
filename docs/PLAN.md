# PLAN: описание программы, состав модулей, план работ

Единая точка, куда записаны назначение проекта, договорённости и план — чтобы
не повторять их в разговорах и не искать по переписке. Файл живой: решение
принято → строка в раздел 6, задача сделана → отметка в разделе 5.

---

## 1. Что делает устройство

Портативный прибор на **STM32U575RGT** (Cortex-M33, 160 МГц, ThreadX/Azure RTOS):

* **звуковая сигнализация** — PCM 44.1 кГц/16 бит/моно во внешней SPI flash
  MX25R6435F (8 МБ), воспроизведение через SAI1 (I2S) → усилитель MAX98357A;
* **мост BLE ↔ LoRa**: то, что пришло по BLE, уходит по протоколу LoRa
  (модуль S7678S), и наоборот;
* **статусы**: в BLE-сообщениях приходят статусы, на которые прибор
  *сигнализирует звуком/индикацией* и которые *так же передаются по LoRa*;
* **GPS/GNSS** (USART1): NMEA, автоопределение чипа (Allystar/LOCOSYS),
  координаты для LoRa-трека — включён (`CONFIG_GPS 1`);
* **датчики** (I2C1): BME280 — T/H/P, LIS3DH — 6D-ориентация и поворот экрана,
  MAX17048 — батарея;
* **экран** TFT ST7789P3 172×320 (SPI2), графика — LVGL 9.2 + SquareLine
  (подключается, `docs/LVGL.md`); **LTE-модем** (USART2, пины `LTE_*`) —
  каркас потока готов, `CONFIG_G4` выключен;
* **кнопки** PB1/PB2/PB3, RTC (LSE), режим пониженного потребления
  (сон + пробуждение по RTC), кольцевой журнал и страница конфигурации во
  внешней flash.

Состав модулей и кто за что отвечает — `SYSTEM.md` раздел 1.

---

## 2. Транспорт и модули (как есть сейчас)

| Модуль | Файлы | Состояние |
|---|---|---|
| Звук: плеер, образ, писк | `Src/Audio/*` | работает: стриминг через circular DMA, громкость, повтор, watchdog |
| Внешняя flash | `Src/Audio/Src/spiflash.c`, `extstore.c` | работает: DMA/опрос, ретраи, области SOUNDS/CONFIG/LOG (`sfmap.h`) |
| Лог | `Src/Common/Src/vector_log.c` | работает: ITM/SWO и/или UART4, уровни и маски, выключатели |
| Кольцевые буферы UART | `Src/buffer.c`, `Inc/buffer.h` | каркас: приём по байту из ISR, передача polling/DMA |
| Потоки прибора | `Src/Vector_main.c`, `Inc/vector_tasks.h` | каркас готов (v15, GPS в v16) по образцу `Avis_main.c`: поток `Receiver Task` (приём и парсинг по каждому UART) и поток `Measure Task` (периодика 1 с → `Vector_Run_Measure()` → `Ble_Run/Lora_Run/Lte_Run/Gps_Run`). Заглушки модулей — слабые (`__attribute__((weak))`): ваша реализация в файле модуля заменит их сама |
| GPS/GNSS | `Src/Gps.c`, `Inc/Gps.h` | **приложен из другого прибора (v16), включён** (`CONFIG_GPS 1` — решение автора): USART1 (PB6/PB7), NMEA (GGA/GLL/RMC), автоопределение чипа (Allystar/LOCOSYS), координаты для LoRa-трека, команды LOCOSYS. Проверка на живом модуле — раздел 5, этап 2b |
| Экран TFT | `Src/TFT/LCD_platform.[ch]`, `TFT.[ch]`, `TFT_indicator.[ch]` | **v17**: `LCD_platform` переведён с AT32 на HAL (SPI2 + GPDMA1 Ch8, имена функций сохранены), ST7789P3 172×320, `lcd_status`; `TFT_indicator` (2783 строки индикации Avis) пока выключен — нужны `SNS_CFG_Type`/`CALIB_CFG`/`COUNT_CHAN` |
| Датчики I2C1 | `Src/lis3dh.c` + `Src/Lis3dh_reg/`, `Src/bme280_com.c` + `Src/BME280/`, `Src/MAX17048.c`, чтение — в `Src/Vector_main.c` | **v17/v18**: обмен через макросы `I2C_Master_Transmit/I2C_OK` (main.h), HAL внутри библиотек нет; `sensors_init()/sensors_read()` в Measure Task, состояние в `sensors_status`, поворот экрана по `ACCEL_INT` |
| Графика LVGL | `Drivers/lvgl` (9.2) + UI SquareLine Studio, `Core/ui/lv_i18n`, `Core/translations/*.yml` | LVGL и UI в git **не лежат**; описание и чек-лист — `docs/LVGL.md`; поток `LVGL Task` пока заглушка |
| Макросы и хелперы | `Inc/shared_macros.h`, `Inc/vector_macros.h` + `Src/Common/Src/vector_macros.c`, `Core/Inc/main.h` (USER CODE EM) | **v18**: HAL спрятан в макросы `main.h` (`I2C_*`, `SPI_*`, `GPIO_*`, `Uart_*`, `usart_init`), общие функции перенесённого кода (`Delay/DelayInt/GetTick/Search_text`, `TIME_DEL_1/TIME_OUT_LORA`) — в одном `vector_macros.[ch]`; биты статусов — в вашем `shared_macros.h` |
| Кнопки | `stm32u5xx_it.c` (EXTI), `Src/Test/Src/audio_demo.c` | два слоя: продуктовые флаги `button1/2/3/button_sos` и тестовое демо плеера |
| LoRa S7678S | `Src/Lora_S7678S.c` (2094 строки) | реализация автора: AT-обмен, классы A/C, регионы, GPS-трек, `SNS_CFG` |
| Конфигурация прибора | `Inc/config_device.h`, `Inc/shared_types.h` | флаги сборки `CONFIG_*`, структура `SNS_CFG`, биты статусов `ST_COMMON` |
| Общие включения | `Inc/Vector_main.h` | агрегатор заголовков (см. раздел 6, решение про инклуды) |

UART-роли (как задумано):

| Роль | Периферия | Куда |
|---|---|---|
| `USART_COM` / `USART_DEBUG` | UART4 (**только PC10**, однопроводный полудуплекс; PC11 занят `ACCEL_INT`) | терминал + лог (`VECTOR_LOG_UART 4`), кольцо `TYPE_USART` |
| `USART_BLE` (не определён) | USART3 (PC4/PC5, метки `USART3_*_BLE`) | BLE-модуль (`BLE_RESET` = PC12) |
| `USART_LORA` | **UART4 (`huart4`)** | так задано в `Lora_S7678S.h:21` — КОНФЛИКТ: UART4 занят терминалом и логом (`USART_COM`/`USART_DEBUG`, `VECTOR_LOG_UART 4`) и работает в полудуплексе. До включения `CONFIG_LORA` порт надо переназначить (раздел 7, п.27) |
| (LTE, макроса нет) | USART2 (PA2/PA3, метки `USART2_*_LTE`) | сотовый модем: пины `LTE_EN`/`LTE_RESET`/`LTE_STATUS`/`LTE_LED` (PC0..PC3), кольцо `TYPE_LTE` |
| `USART_GPS` | **USART1 (PB6/PB7)** | GPS/GNSS (`Gps.h`). **v17**: `USART1_IRQn` включён, байт из ISR уходит в `Gps_Data_Verification()`, таймаут кадра — от TIM3 1 кГц |
| (сенсоры) | не назначен | кольцо `TYPE_SENSOR`; свободных портов кроме USART1 пока нет |

Макросы `USART_BLE` и `USART_RF` в `main.h` пока **не определены** — они
понадобятся сразу, как только включатся `CONFIG_BLE` / `CONFIG_LORA`
(`transmit_buffer()` в `buffer.c` ссылается именно на них).

Маппинг «линк → UART» в потоке RF жёстко не зашит: линк определяется типом кольца
(`TYPE_LORA` / `TYPE_BLE` / `TYPE_LTE` в `buffer.h`), а порт передачи выбирает
`transmit_buffer()` по тому же типу. Поэтому вопрос «какой USART какому линку»
можно решить позже, не переписывая поток: в `.ioc` USART2 (PA2/PA3) подписан
`_LTE` и к нему относятся пины `LTE_EN/RESET/STATUS/LED`, тогда как
`USART2_IRQHandler` кладёт байты в `InputBuffer[TYPE_LORA]`; USART1 (PB6/PB7)
занят GPS (`USART1_IRQn` включён, байты из ISR идут прямо в
`Gps_Data_Verification()`, минуя кольца `InputBuffer`).

---

## 3. Данные и состояния

* **`SNS_CFG`** (`shared_types.h`): `Config_common` (State, StateErr, звук/LED,
  батарея, мото-часы, температура/влажность/давление, серийный номер, версия
  железа, дата производства, GPS, `PeriodTimeLora`, `Lora_Config_Flags`,
  `Lora_freq_rx2`, `Lora_dr_rx2`, резерв), `application_language`,
  `CRC_CONFIG`. Хранить — в странице CONFIG внешней flash (0x400000, 4 КБ),
  область уже размечена в `sfmap.h`.
* **Статусы `ST_COMMON`** (битовое поле): блокировка звука
  (`BLOCK_SOUND`, `BLOCK_SOUND_LIMIT_CALIB`), блокировка выключения,
  блокировка калибровки, режим обмена данными, зарядка, BUMP TEST (статус и
  старт), единица измерения, включение BLE/GPS/GSM/LoRa, run-LED, тест звука,
  `TURN_OFF = 31`.
* **Звук статусов**: плеер играет **по индексу** в образе
  (`audio_play(idx)` / `audio_play_now(idx)`), слой «состояний» удалён.
  Соответствие «статус → индекс звука» надо задать таблицей в рабочем коде
  (например, `sound_by_status[]`), а не в плеере. Индексы звуков — в
  `audio_ids.h` (генерируется `pack_sounds.py` вместе с образом).

---

## 4. Сквозной сценарий (целевая логика)

```
COM  (UART4)  --байты--> ISR --> InputBuffer[TYPE_USART]   ---\
LoRa (UART4!) --байты--> ISR --> InputBuffer[TYPE_LORA]     ---+--> Receiver Task
BLE  (USART3) --байты--> ISR --> InputBuffer[TYPE_BLE]      ---+   (Vector_main.c)
LTE  (USART2) --байты--> ISR --> InputBuffer[TYPE_LTE]      ---/
GPS  (USART1) --байты--> ISR --> Gps_Data_Verification(): свой буфер в Gps.c
               (мимо колец; разбор - Gps_Receive() в Receiver Task, CONFIG_GPS 1)

(!) LoRa пока назначен на huart4 (Lora_S7678S.h:21) - конфликт с COM и логом;
до включения CONFIG_LORA порт надо переназначить (раздел 7, п.27).

Receiver Task: command_message() / Lora_Receive() / Ble_Receive() /
               Lte_Receive() / Gps_Receive() / Uart_Channel_Receive() +
               Vector_Options_System()
               кадр --> (а) статусы --> звук/индикация: audio_play_now(idx)
                      (б) данные   --> другой модуль: transmit_buffer(..., TYPE_*)

Measure Task (1 с): Vector_Run_Measure() --> измерения + Ble_Run(); Lora_Run();
                    Lte_Run(); Gps_Run();
                    Vector_RunFlashMemory() --> журнал/конфиг во внешней flash
```

Правила, которые стоит зафиксировать сразу:

1. Из ISR — **только** положить байт в кольцо и (при необходимости)
   `tx_semaphore_put`; парсинг и AT-машина — в потоках.
2. Один владелец на UART: у каждого порта ровно один поток-читатель. Сейчас это
   `Receiver Task` — единственный потребитель колец `InputBuffer[TYPE_USART/
   TYPE_LORA/TYPE_BLE/TYPE_LTE/TYPE_SENSOR]` (правило SPSC из `buffer.h`);
   существующий `Lora_Receive()` читает то же кольцо, поэтому вызывать его надо
   из `receiver_task_function()`, а не из другого потока или таймера.
3. Лог и рабочий порт не смешивать: сейчас `USART_DEBUG == USART_COM == huart4`,
   а прямой write в `TDR` в обработчиках идёт мимо блокировки лога
   (`vlog_bus_lock`) — байты эха вклиниваются в строки лога.
4. Звук — только через API плеера (`audio_play_now` для немедленного,
   `audio_play` для очереди, `audio_set_loop(1)` для сигнализации «по кругу»);
   уважать `ST_COMMON_BIT_BLOCK_SOUND`.

---

## 5. План работ

Отметки: `[ ]` не сделано, `[x]` сделано.

**Этап 0. Фундамент (готово)**
* [x] Такты: HSE 8 МГц, SYSCLK 160 МГц, SAI_CK = 11.2896 МГц → FS = 44100.000 Гц
* [x] RTC от LSE 32768 Гц (предделители 127/255 = ровно 1 Гц)
* [x] Образ звуков `tools/sounds.bin` + `pack_sounds.py` + external loader + `flash_sounds.bat`
* [x] Плеер: стриминг, громкость, повтор, watchdog, лог частоты SAI
* [x] Убраны: мост UART4↔USART2, слой «состояний» плеера

**Этап 1. Транспорт BLE**
* [ ] `CONFIG_BLE 1`, определить `USART_BLE` в `main.h`
* [ ] приём USART3 → `InputBuffer[TYPE_BLE]` (сейчас вызов вырезан флагом)
* [ ] детект конца кадра: IDLE-линия (`UART_IT_IDLE` + `__HAL_UART_CLEAR_IDLEFLAG`)
      или таймаут межбайтового интервала
* [ ] поток-парсер BLE: кольцо → кадр → обработчик — каркас готов (v15): `Ble_Receive()` в `Vector_main.c` (слабая заглушка, кольцо уже вычитывается под `CONFIG_BLE`)
* [ ] счётчик потерь при переполнении кольца (сейчас байты теряются молча) — ставить в ISR по возврату `add_to_buffer()`; место под счётчик: `tasks_status.mod[].cnt_errors`

**Этап 2. Транспорт LoRa**
* [ ] `CONFIG_LORA 1`, определить `USART_RF` в `main.h`
* [ ] `FIRMWARE_VERSION` — дефолт для всех `DEVICE_NUMBER` (иначе LoRa не соберётся)
* [ ] приём → `InputBuffer[TYPE_LORA]`, AT-машина состояний S7678S — `Lora_Receive()` вызывается из `Receiver Task` под `CONFIG_LORA`; порт LoRa сейчас = UART4 (конфликт с логом, п.27), что мешает включить конфиг — п.25
* [ ] `SNS_CFG` в CONFIG-странице внешней flash: загрузка при старте,
      сохранение по изменению, контроль `CRC_CONFIG`
* [ ] передача: очередь пакетов + `transmit_buffer(..., TYPE_RF)` — вызывается из `Lora_Run()` (поток `Measure Task`), счётчики в `tasks_status.mod[TASK_MOD_LORA]`

**Этап 2b. GPS/GNSS — модуль подключён к проекту (v17), собирается при `CONFIG_GPS 1`
* [x] `CONFIG_GPS 1`, типы/константы модуля в `Gps.h`, `USART_GPS (huart1)`, `TIME_DEL_1` — собирается (проверено gcc -fsyntax-only)
* [x] UART под GNSS = USART1 (PB6/PB7): `USART1_IRQn` включён, `USART1_IRQHandler` → `Gps_Data_Verification()`
* [x] `Uart_Gps_Set_Baudrate()`: AT32 `usart_init()` → `HAL_UART_Init()` + возврат прерываний RXNE/ERR
* [x] общие хелперы `Delay()` / `DelayInt()` / `GetTick()` / `Search_text()` — `vector_macros.c` (время только тик RTOS, до планировщика busy-wait); они же нужны LoRa-драйверу
* [x] `Uart_Gps_Receive_Timer_Inc()` вызывается из `HAL_TIM_PeriodElapsedCallback(TIM3)` (1 кГц) в `main.c`
* [x] `Gps_Run()` в `Vector_main.c` (weak): `Gps_Init()` → `Gps_Init_Nav_Sys()` по секундному таймеру прибора (`TIMER_RTC_GPS_DATA_INIT`), `Gps_DeInit()` при снятом статусе, ошибка приёма → `ST_COMMON_BIT_ERR_GPS`
* [ ] подтвердить по схеме полярность и длительность сброса `GNSS_RST` (PB5), режим `GNSS_MODE` (PB4) — пока пины не дёргаем
* [ ] проверить приём NMEA на живом модуле: `tasks_status.mod[TASK_MOD_GPS]`, `Latitude/Longitude`, `Gps_flag_err`

**Этап 2c. Экран и графика (LVGL 9.2 + SquareLine Studio)**
* [x] `LCD_platform.c/h` переведён с AT32 на HAL (SPI2 + GPDMA1 Ch8), имена функций сохранены — v17
* [x] `TFT.h`: ST7789P3 172×320 + `TFT_COL_OFFSET` (RAM контроллера 240×320) — v17
* [x] include paths: `Drivers/lvgl`, `Drivers/ui`, `Core/ui/lv_i18n`, `Src/TFT`, `Src/BME280`, `Src/Lis3dh_reg` — v17
* [ ] `lv_conf.h` (LV_COLOR_DEPTH 16, память, тик) — в git его нет
* [ ] порт дисплея: `lv_display_create(172,320)` + `lv_display_set_flush_cb` → `TFT_FlushBuffer`, `lv_tick_set_cb(VTICK_MS)`
* [ ] тело потока `LVGL Task` (`ui_init()`, `lv_i18n_init()`, `lv_timer_handler()`) и `LVGL_STACK_SIZE` 8–16 КБ
* [ ] в кубе SPI2 prescaler /4 или /8 (сейчас /2 = 80 МГц — выше spec ST7789P3)
* [ ] перенос `TFT_indicator.c` (нужны `SNS_CFG_Type`, `CALIB_CFG`, `COUNT_CHAN`)

**Этап 3. Мост и статусы**
* [ ] таблица «статус → индекс звука» и «статус → LED»
* [ ] BLE-кадр → LoRa-пакет (порт, DR, класс A/C), LoRa → BLE-ответ
* [ ] учёт `ST_COMMON_BIT_BLOCK_SOUND`, `BLOCK_SOUND_LIMIT_CALIB`,
      `DATA_EXCHANGE`, `TURN_ON_BLE/LORA/GPS/GSM`, `TURN_OFF`
* [ ] приоритеты: аварийный статус важнее информационного (очередь звуков)

**Этап 4. Кнопки (продуктовая логика)**
* [ ] инкремент `button_count` (сейчас только обнуление) — удобно в TIM3 1 кГц
* [ ] короткое/длинное/удержание, антидребезг в таймере, а не в ISR
* [ ] `button_sos` — зафиксировать, что это та же BUTTON1
* [ ] `VECTOR_AUDIO_DEMO_KEYS 0` — демо-кнопки отдаются рабочему коду

**Этап 5. Сон и питание**
* [ ] `CONFIG_SLEEP`: Stop2 (RAM сохраняется) или Standby (меньше ток, RAM
      теряется) при «прибор выключен»
* [x] источник пробуждения: RTC Wakeup 1 с (`HAL_RTCEx_SetWakeUpTimer_IT` +
      `RTC_IRQn` в NVIC + `RTC_IRQHandler` + колбэк → `Timer_Tick_1s()`) — v19,
      в кубе включено автором (`.ioc`: Wakeup Interrupt + RTC global interrupt)
* [ ] для Standby — WKUP-пин кнопки; если период wakeup в снах менять (≠ 1 с),
      пересчитать секундные таймеры и мото-часы (сейчас такт ровно 1 с)
* [ ] перед сном: усилитель `SD_MODE` low, flash в deep power-down (B9h —
      команды в драйвере пока нет), дождаться TC у UART, `HAL_SuspendTick()`,
      остановить SysTick; после: `SystemClock_Config()`, реинит периферии,
      компенсация `tx_time_set()`
* [ ] опция «быстрая экономия без сна»: `#define TX_ENABLE_WFI` в
      `Core/Inc/tx_user.h` — idle-поток ThreadX начнёт выполнять WFI вместо
      вращения на 160 МГц

**Этап 6. Журнал и сервис**
* [ ] кольцевой журнал в области LOG (0x401000..), `extstore` уже умеет
* [ ] чтение журнала/конфига через BLE или LoRa по команде
* [ ] версионирование прошивки и (опционально) OTA

---

## 6. Решения и договорённости (чтобы не повторять)

| Тема | Решение |
|---|---|
| Кварц | HSE = **8 МГц**; `HSE_VALUE` и `RCC.HSE_VALUE` обязаны совпадать с реальным, иначе все UART/таймеры/тик идут в N раз медленнее |
| Такты | PLL1 M=1 N=20 R=1 → VCO1 160 МГц → SYSCLK 160 МГц; APB 1/2/3 = /1 |
| Звук | PLL3 M=2 N=56 FRACN=3670 P=9 → VCO3 203.2 МГц → SAI_CK 11.2896 МГц, MCKDIV=1, **FS = 44100.000 Гц**; образ — только 44.1 кГц/16 бит/моно |
| Формат образа | `sounds.bin` (не `.img`), magic `SNDI`, собирает `tools/pack_sounds.py` |
| Плеер | слоя «состояний» нет: `audio_play(idx)`, `audio_play_now(idx)`, `audio_stop()`, `audio_set_loop(on)` (повтор последнего звука, по умолчанию выключен), `audio_set_volume(%)` |
| Лог | `LOG_D/I/W/E` — одна `vlog()`, отличаются уровнем; фильтры `vlog_level` (дефолт 2 = INFO) и `vlog_mask`; порт один (ITM и/или `VECTOR_LOG_UART`); из ISR вызовы отбрасываются |
| Шумные строки | `VECTOR_SPI_LOG_READS 0` — чтения flash в DEBUG; `VECTOR_AUDIO_SELFTEST N` — N писков до RTOS для проверки тракта |
| Время | только тик ThreadX (`vector_tick.h`, 10 мс); HAL-тик (TIM6) — лишь внутренние таймауты HAL |
| Скрипты Windows | `*.bat` хранятся побайтово (`-text` в `.gitattributes`), переводы строк CRLF |
| Инклуды | каждый `.c` включает то, что использует сам; `Vector_main.h` остаётся тонким агрегатором конфигурации и модулей приложения, без CubeMX-заголовков периферии |
| Патчи | правки передаются как `git apply --binary <file>.diff` (не `git am`: он срезает CRLF у `.bat`); коммиты делает автор проекта |
| Состояние плеера | ОДНА volatile-структура `audio_status` (вместо 14 переменных `audio_dbg_*`): состояние — enum'ы `audio_output_t` (`AUDIO_OUT_SILENT/_SOUND/_ALARM_BEEP`), `audio_boot_t`, `audio_cmd_t`, `audio_err_t`; счётчики — `cnt_*`. Дубли состояния убраны: `ap_playing_idx`, `ap_loop_*`, `ap_img_ok` удалены. То же для кнопок — `demo_status` |
| Потоки прибора | Два потока по образцу `Avis_main.c`: `Receiver Task` (приоритет 12, стек 2 КБ) — приём и парсинг по каждому UART (`command_message`, `Lora_Receive`, `Ble_Receive`, `Lte_Receive`, `Gps_Receive`, `Uart_Channel_Receive`); `Measure Task` (13 / 4 КБ) — `Vector_Run_Pre_Init()` один раз и `Vector_Run_Measure()` + `Vector_RunFlashMemory()` по флагу `timer.flag_1s` (такт 1 с от TIM3) (внутри `Ble_Run/Lora_Run/Lte_Run/Gps_Run`). Модули закрыты `CONFIG_*` из `config_device.h`, заглушки — `__attribute__((weak))`. Выключатель всего: `VECTOR_TASKS_ENABLE` |
| Перенос с AT32 | **HAL внутри библиотек не используем** (v18): весь HAL спрятан в макросы `main.h` (USER CODE EM) — `I2C_Master_Transmit/Receive/Mem_*`, `I2C_OK`, `i2c_status_type`, `I2C_IsBusy/I2C_ReConfig`, `SPI_Transmit[_DMA]/SPI_Abort/SPI_OK`, `GPIO_WritePin/PIN_SET/PIN_RESET`, `usart_init → Uart_SetBaudrate`. Поэтому `lis3dh.c`, `bme280_com.c`, `MAX17048.c`, `Gps.c` остались вашими (отличаются только строкой include); HAL виден лишь в `vector_macros.c`, в колбэках (`HAL_SPI_TxCpltCallback` в `LCD_platform.c`) и в сгенерированном кубом коде. `EXINT->polcfg1/2` → `LL_EXTI_*Trig_0_31` |
| Экран | ST7789P3 **172×320** (`CONFIG_MODEL_LCD 0`), SPI2 8 бит + GPDMA1 Ch8 (Normal), кадр уходит `LCD_writeBulk()` → колбэк DMA → `lv_display_flush_ready`. Байты RGB565 меняются местами (`VECTOR_LCD_SWAP_RGB565`). `HAL_SPI_ErrorCallback` не занимаем (он уже у `spiflash.c`) — залипание порта лечим таймаутом ожидания, всё видно в `lcd_status` |
| Датчики | I2C1: BME280 (0x76), LIS3DH (0x19), MAX17048 (0x36). Владелец шины — `Measure Task` (`Vector_main.c`), чтение раз в секунду (`timer.flag_1s`). Значения → только `Sns_Cfg_struct.Config_common` (в `sensors_status` — счётчики диагностики). Поворот экрана: `ACCEL_INT` (PC11) → только флаг → `lis3dh_update_all()` в потоке → `TFT_Rotation()` |
| Данные прибора | Экземпляры `Sns_Cfg_struct`, `Cfg_structdef_read`, `tempsensor_calib`, `device_turn` определены в `Vector_main.c` (как в Avis_main.c). Калибровка температуры выключена (`VECTOR_BME_CALIBRATION 0`): `flash_write_calibration_safe()` в проекте нет |
| i18n | Переводы в `Core/translations/*.yml` (ru-RU, en-GB), генерация `Core/update_lang.bat` → `Core/ui/lv_i18n/*`; в UI текст через `_("ключ")`. Описание — `docs/LVGL.md` |
| Таймеры прибора | Схема автора из Avis оставлена — она рабочая и одинаковая на всех приборах, переводить на `tx_timer` смысла нет: TIM3 1 кГц → `Timer_Tick_1ms()` (декремент `countdown_time[]`, флаги `timer.flag_1ms/10ms/100ms/1s`, счётчики кнопок, таймауты кадров UART), RTC wakeup 1 с → `Timer_Tick_1s()` (декремент `countdown_time_rtc[]`, `working_hours`). Всё обслуживание переехало из `main.c`/`stm32u5xx_it.c` в `Vector_main.c` — в CubeMX-файлах осталось по одной строке вызова. Экземпляры `volatile` (пишет ISR, читают потоки). Макросы `START/TEST/RESET/END_TIMER[_RTC]` прежние, из `shared_macros.h` |
| Борд-инит | `vector_board.[ch]` удалён: тело (`SRAM4`, заморозка `TIM6` под отладчиком, `SD_MODE`, `sf_probe`, `audio_selftest`) переехало в `Vector_Run_Board_Init()` в `Vector_main.c` — вне `#if VECTOR_TASKS_ENABLE`, чтобы работало всегда |
| Данные датчиков | Одно место: `Sns_Cfg_struct.Config_common` (T/H/P, батарея, `Accel_x/y/z`, `Orientation`, `Screen_rotation`, маска `Sensors_ok`). Дубли из `sensors_status` убраны — там остались только счётчики диагностики шины I2C1. `sizeof(SNS_CFG)` вырос: учесть при разметке CONFIG-страницы и CRC |
| Заголовки | Один `vector_macros.h` на все макросы/хелперы проекта вместо `vector_status.h` + `vector_compat.h`; API и состояние датчиков (`sensors_status`) живут в `Vector_main.h`, реализация — в `Vector_main.c` (как `Ble_Run`/`Lora_Run` в `Avis_main.c`). Биты статусов — в `shared_macros.h` автора. Новых `vector_*.h` без нужды не заводим |
| Порядок работ (решение автора, v16) | Сначала **база**: модули прикладываем к проекту (`Gps.c`, потоки, кольца `TYPE_*`, флаги `CONFIG_* = 0`), периферию не трогаем. UART/куб/NVIC (`USART1_IRQn`, `USART_GPS`, `USART_BLE`/`USART_RF` в `main.h`) и значения `CONFIG_*` **пока не настраиваем и не включаем** — вернёмся к этому отдельным этапом |

---

## 7. Известные дефекты и риски (найдены при разборе, ждут решения)

**buffer.c / buffer.h**
1. `T_Buffer.begin/end` не `volatile` — потребитель в потоке может не увидеть
   байты, записанные из ISR. — **исправлено (v11)**: метки `volatile`, правило
   владения (SPSC) задокументировано в `buffer.h`.
2. Гонка в `receive_buffer()`: при `begin == end` потребитель обнуляет **оба**
   индекса, в том числе `end`, который принадлежит производителю → потеря
   байта. Обнуление не нужно: индексы заворачиваются сами. Правило SPSC —
   потребитель трогает только `begin`. — **исправлено (v11)**: обнуление `end`
   убрано, `end` читается одним снимком; заодно исправлена парная ошибка в
   `add_to_buffer()` (при завороте кольца `end` оставался 0 вместо 1 →
   потребитель перечитывал чужой старый байт как новый).
3. Два одинаковых `if` (`begin < end` и `begin > end`) — достаточно `!=`.
   — **исправлено (v11)**: одна ветка `begin != end`, метки заворачиваются
   по BUFFER_LENGTH.
4. `add_to_buffer()` делает `__disable_irq()/__enable_irq()` безусловно: вызов
   из потока внутри критической секции ThreadX сорвёт маску. Безопасно —
   сохранять/восстанавливать PRIMASK. — **исправлено (v11)**: после перехода
   на честное SPSC-кольцо маскирование не нужно вовсе (писатель трогает только
   `end`, читатель только `begin`, 16-битные доступы атомарны).
5. Переполнение кольца молчаливое (`res = 1`) — нет счётчика потерь.
6. `transmit_buffer()`: нет проверки `Size <= 300` и
   `type_transmit < TYPE_USART_COUNT` перед `memcpy` в `buffer_transmit` →
   выход за границы массива. — **исправлено (v11)**: проверки NULL/Size/типа
   в начале, пакет длиннее буфера отбрасывается целиком (не режется).
7. `__attribute__((section(".sram4")))`: в `STM32U575RGTX_FLASH.ld` область
   SRAM4 объявлена, но выходной секции `.sram4` нет → секция-сирота
   (ld предупреждает и размещает её на своё усмотрение, не в SRAM4).
   — **исправлено (v11)**: секция `.sram4 (NOLOAD) >SRAM4` добавлена в оба
   линкер-скрипта; ВАЖНО — такт SRAM4 по умолчанию выключен, его включает
   `Vector_Run_Board_Init()` (`__HAL_RCC_SRAM4_CLK_ENABLE()`), иначе обращение
   к SRAM4 = bus fault. Для сна (этап 5) не забыть биты SRAM4PD/SRAM4PDS.
8. `GetTick()` в DMA-ветке нигде не определён (сейчас ветка не компилируется,
   `DMA_USART 0`); по правилам проекта время — `VTICK_MS()`.
9. `memcpy` без `#include <string.h>`. — **исправлено (v11)** в `buffer.c`
   (заодно `<string.h>` добавлен в `spiflash.c` для `memcmp`).
10. `BUFFER_LENGTH 255` → полезная вместимость 254 байта (один байт теряется
    на различение «пусто/полно») — стоит отметить в комментарии.
    — **отмечено (v11)** в `buffer.h`.

**UART**
11. `USART_DEBUG == USART_COM == huart4`: прямой write в `TDR` в обработчиках
    идёт в порт лога мимо `vlog_bus_lock()` → эхо вклинивается в строки лога,
    а при неполном TXE байт теряется; в `UART4_IRQHandler` это ещё и эхо
    принятого в тот же порт.
12. `LTE_DEBUG` используется в `stm32u5xx_it.c`, но не определён → `#if` молча
    равен 0.
13. `USART_BLE` / `USART_RF` не определены в `main.h` → при `CONFIG_BLE` /
    `CONFIG_LORA = 1` сборка упадёт.
14. `CONFIG_BLE = 0` и `CONFIG_LORA = 0`: вызовы `add_to_buffer()` для
    TYPE_BLE/TYPE_LORA вырезаны, принятые байты USART2/USART3 никуда не
    складываются.

**Кнопки / EXTI**
15. `audio_demo_key_handler()` вызывается в конце
    `HAL_GPIO_EXTI_Falling_Callback` безусловно: продуктовые флаги
    `button1/2/3/button_sos` и тестовое демо плеера срабатывают на одно и то же
    нажатие. Для продуктового режима — `VECTOR_AUDIO_DEMO_KEYS 0`.
16. `button_count` только обнуляется, инкремента нет → длинное нажатие не
    работает; антидребезга у продуктовых флагов нет (у демо — свой, 200 мс).
17. Макросы `SET_ON/SET_OFF/SET_TGL` в `main.h` содержат `;` внутри → в
    `if (...) SET_TGL(X); else ...` сломаются. Обернуть в `do { } while (0)`.

**config_device.h**
18. Открыт `extern "C" {`, закрывающей `}` нет — для C не проявляется, для C++
    ошибка.
19. `FIRMWARE_VERSION` определён только при `DEVICE_NUMBER == Dev1`, а
    используется в `Lora_S7678S.c` → смена номера устройства сломает сборку.
20. `MIRAX_BACS_BUILD`, `DEVICE_NUMBER_COM`, `DEVICE_NUMBER_MODIF`,
    `TEST_PROG` пока нигде не используются; пустые ветки `CONFIG_HW` без
    комментариев.

**Прочее**
21. **[закрыт: v19 + куб, v20]** RTC Wakeup: куб запускал
    `HAL_RTCEx_SetWakeUpTimer()` БЕЗ прерывания, `RTC_IRQn` не был разрешён →
    секундные таймеры прибора не шли. v19 временно чинил это USER CODE-блоком
    в `rtc.c`; автор включил то же в кубе (`RTC → Wakeup Interrupt` +
    `NVIC → RTC global interrupt`, `.ioc` и код перегенерированы: куб сам зовёт
    `HAL_RTCEx_SetWakeUpTimer_IT` и настраивает NVIC в `HAL_RTC_MspInit`,
    приоритет 0). В v20 страховочный блок удалён: он дублировал куб, а его
    3-аргументный вызов не собирался бы с новой HAL (у
    `HAL_RTCEx_SetWakeUpTimer_IT` появился 4-й аргумент AutoReload).
22. `tools/Документ Microsoft Word.odt` — в репозитории, но не используется
    (тестовые образы `build/*.bin` из репозитория уже убраны).
23. «Хрип» при воспроизведении (слышен даже на чистом `tone_1k.wav` при 50%
    громкости → аналоговое клиппирование исключено). Данные в образе
    проверены (SHA256 совпадает), масштабирование громкости `(*g)>>15`
    корректно, SAI-тактирование точное (44100.000 Гц). Подозреваемые:
    (а) битовые ошибки чтения SPI на 20 МГц — для их поимки добавлен
    `sf_selftest()` (двойное чтение, `VECTOR_SPI_SELFTEST`, счётчик
    `sf_dbg_selftest_bad`); (б) остановки ядра отладчиком (Live Watch) —
    SAI-DMA во время halt продолжает играть, дозагрузки нет → underrun
    (счётчик `audio_status.cnt_underruns`); (в) аналог: питание MAX98357A/GAIN/
    динамик (проверяется писком `VECTOR_AUDIO_SELFTEST 1` — он идёт мимо
    внешней flash). План тестов — в сообщении к v11.
24. Маппинг «модуль → UART» (фактический, v17+): GPS = USART1 (PB6/PB7,
    `USART1_IRQn` включён); BLE = USART3 (PC4/PC5) → `InputBuffer[TYPE_BLE]`
    под `CONFIG_BLE`; LTE = USART2 (PA2/PA3, метки `_LTE`, пины
    `LTE_EN/RESET/STATUS/LED`), но `USART2_IRQHandler` кладёт байты в
    `InputBuffer[TYPE_LORA]` — надо `TYPE_LTE`; LoRa = **huart4**
    (`Lora_S7678S.h:21`) — конфликт с COM/логом (п.27). `USART_BLE` /
    `USART_RF` в `main.h` не определены (п.13) — `transmit_buffer()` без них
    не передаст ничего.
25. `CONFIG_LORA 1` / `CONFIG_BLE 1`: прежние блокеры сняты (v18–v20) —
    `shared_types.h` включён в `Vector_main.h`, экземпляры `Sns_Cfg_struct` и
    `device_turn` определены в `Vector_main.c`, `COUNT_CHAN` = 10
    (`shared_types.h`, решение автора), макросы статусов/таймеров есть в
    `shared_macros.h`, `FIRMWARE_VERSION` для `Dev1` задан. Осталось до
    включения:
    * определить `USART_BLE` / `USART_RF` в `main.h` (п.13);
    * решить порт LoRa: `USART_LORA = huart4` конфликтует с COM/логом (п.27);
    * перепроверить сборку с `CONFIG_LORA 1` / `CONFIG_BLE 1`
      (gcc -fsyntax-only), затем на живом железе.
26. **[закрыт: v16/v17]** `CONFIG_GPS 1` собирается: типы и константы — в
    `Gps.h`, `USART_GPS` = `huart1`, хелперы `Delay()/DelayInt()/GetTick()/
    Search_text()` — в `vector_macros.c`, AT32-`usart_init()` заменён на
    `Uart_SetBaudrate()` (HAL). `CONFIG_GPS 1` включён автором. Осталось
    проверить на живом модуле (этап 2b): приём NMEA, полярность
    `GNSS_RST`/`GNSS_MODE`.
27. `USART_LORA` назначен на **UART4** (`Lora_S7678S.h:21`), а UART4 — терминал и лог (`USART_COM`/`USART_DEBUG`,
    `VECTOR_LOG_UART 4`), к тому же полудуплекс (`HAL_HalfDuplex_Init`). При `CONFIG_LORA 1` передача LoRa пойдёт в порт
    лога. До включения LoRa: перенести `USART_LORA` на свободный порт (USART1 занят GPS) или убрать лог с UART4.
28. **SPI2 = 80 МГц** (Baud Rate Prescaler 2 при PCLK1 160 МГц) — выше допустимого для ST7789P3 (~62.5 МГц на запись).
    Поставить в кубе /4 или /8, иначе возможны битые пиксели и зависания `LCD_writeBulk`.
29. `TFT_indicator.c` (2783 строки индикации Avis) выключен (`CONFIG_TYPE_LCD == 2`) и без переноса не соберётся:
    нужны `SNS_CFG_Type`, `CALIB_CFG`, `COUNT_CHAN`, значения `DEVICE_NUMBER` (Device2/Device3_Pro) и функции Avis
    (`Avis_Run_Temp`, `DisplayIndication`, `Button_Run`). Из `TFT.h` он больше не включается — драйвер панели от слоя
    индикации не зависит.
30. Графика не воспроизводится из git: `Drivers/lvgl` (9.2), UI SquareLine Studio и `lv_conf.h` в репозитории не лежат
    (`/Drivers/` в `.gitignore`). Строка `!/Debug/ui_Vector/` после `/Debug/` не работает (git не заходит в исключённый
    каталог): нужно `/Debug/*` + `!/Debug/ui_Vector/`, либо держать UI в `Drivers/` и зафиксировать версии в README.
31. `LVGL Task`: стек 4096 байт для LVGL 9 мал (нужно 8–16 КБ), тело потока — заглушка (спит 1 с), порт дисплея и
    `lv_tick` не настроены. Шаблон — `docs/LVGL.md` раздел 4.
32. `flash_write_calibration_safe()` и `Avis_Run_Temp()` в проекте не определены: калибровка температуры
    выключена (`VECTOR_BME_CALIBRATION 0`), `Vector_Search_Temp_Start()` — weak-заглушка (переименована с
    `Avis_Search_Temp_Start`). `TIMER_RTC_*` и `COUNT_CHAN` **[исправлено в v19/v20]**: таймеры прибора перенесены
    (см. п.33), `COUNT_CHAN` = 10 (решение автора).
33. **Ошибки в пуше с таймерами (исправлены в v19)**

    | Что | Почему ломалось | Как исправлено |
    |---|---|---|
    | `Countdown_Timer_Chan()` в `main.c` | обращалась к `COUNT_CHAN`, `COUNT_TIMERS_CHAN`, `countdown_time_chan[]`, `END_TIMER_CH` — их автор удалил из `shared_types.h` вместе с каналами | функция удалена: каналов измерения на этом приборе нет |
    | `button.button_flag` | экземпляр называется `button1` (`stm32u5xx_it.c`) | `button1` в `Timer_Tick_1ms()` |
    | `Uart_Command_Receive_Timer_Inc()` | реализации нет (модуль `command_message` ещё не перенесён) | `__attribute__((weak))` заглушка в `Vector_main.c` |
    | `timer`, `countdown_time`, `countdown_time_rtc` без `volatile` | пишет прерывание, читают потоки: на `-O2` флаги кэшируются в регистрах и такты теряются | `volatile` в объявлениях и определениях |
    | `Timer_variables` объявлен в `stm32u5xx_it.h` | тип жил в заголовке прерываний, экземпляры и обслуживание — в двух разных файлах | тип и `extern` — в `shared_types.h`, экземпляры и `Timer_Tick_*` — в `Vector_main.c` |
    | `TIMER_RTC_GPS_DATA_INIT`, `TIMER_RTC_LORA_DATA_SET_ALARM`, `TIME_RTC_*` | используются в `Gps_Run`/`Lora_Run`, но нигде не определены | добавлены в `shared_types.h` (длительности стартовые — подобрать под модули) |
    | `Lora_Run()` вставлен вариантом `Lora_g_*` | драйвер `Lora_g` (`CONFIG_LORA_G`) в этом проекте отсутствует | переписан под имеющийся `Lora_S7678S.c` (`Lora_Init/Lora_DataSet/Lora_DeInit/Lora_IsTxBusy/get_state_*_lora`) по образцу `Lora_Run()` из `Avis_main.c`; джиттер периода через `rand()` |
    | `COUNT_CHAN` не определён | `Lora_S7678S.h`: `#define COUNT_CHAN_LORA (COUNT_CHAN)` → с `CONFIG_LORA 1` не собиралось | `#define COUNT_CHAN (10)` в `shared_types.h` (решение автора, v20) |
    | `VECTOR_GPS_NAV_PERIOD_MS` | автор удалил макрос, а `Gps_Run()` на нём строился | `Gps_Run()` переведён на секундные таймеры прибора |
    | `VECTOR_TASKS_MEASURE_PERIOD_MS` | период задавал поток, хотя такт 1 с уже даёт TIM3 | макрос удалён, `Measure Task` работает по `timer.flag_1s` |
    | `working_hours += 1` в колбэке RTC | в Avis единица поля — 937.5 мкс, такт здесь — 1 с | **решение автора (v20)**: на этом приборе мото-часы в секундах, `+= 1` оставлен; при сохранении/загрузке конфига (`working_hours_offset`) единицы с Avis не смешивать |
