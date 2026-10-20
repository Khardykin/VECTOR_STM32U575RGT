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
| Внешняя flash | `Src/Audio/Src/spiflash.c`, `extstore.c` | работает: DMA/опрос, ретраи, области SOUNDS/CONFIG/CONFIG_DEFAULT/LOG (`sfmap.h`) |
| Лог | `Src/Common/Src/vector_log.c` | работает: ITM/SWO и/или UART4, уровни и маски, выключатели |
| Кольцевые буферы UART | `Src/buffer.c`, `Inc/buffer.h` | каркас: приём по байту из ISR, передача polling/DMA |
| Потоки прибора | `Src/Vector_main.c`, `Inc/vector_tasks.h` | каркас готов (GPS ранее ) по образцу `Avis_main.c`: поток `Receiver Task` (приём и парсинг по каждому UART) и поток `Measure Task` (периодика 1 с → `Vector_Run_Measure()` → `Ble_Run/Lora_Run/Lte_Run/Gps_Run`). Заглушки модулей — слабые (`__attribute__((weak))`): ваша реализация в файле модуля заменит их сама |
| GPS/GNSS | `Src/Gps.c`, `Inc/Gps.h` | **приложен из другого прибора, включён** (`CONFIG_GPS 1` — решение автора): USART1 (PB6/PB7), NMEA (GGA/GLL/RMC), автоопределение чипа (Allystar/LOCOSYS), координаты для LoRa-трека, команды LOCOSYS. Проверка на живом модуле — раздел 5, этап 2b |
| Экран TFT | `Src/TFT/LCD_platform.[ch]`, `TFT.[ch]`, `TFT_indicator.[ch]` | `LCD_platform` переведён с AT32 на HAL (SPI2 + GPDMA1 Ch8, имена функций сохранены), ST7789P3 172×320, `lcd_status`; `TFT_indicator` (2783 строки индикации Avis) пока выключен — нужны `SNS_CFG_Type`/`CALIB_CFG`/`COUNT_CHAN` |
| Датчики I2C1 | `Src/lis3dh.c` + `Src/Lis3dh_reg/`, `Src/bme280_com.c` + `Src/BME280/`, `Src/MAX17048.c`, чтение — в `Src/Vector_main.c` | обмен через макросы `I2C_Master_Transmit/I2C_OK` (main.h), HAL внутри библиотек нет; `sensors_init()/sensors_read()` в Measure Task; в `sensors_status` — оси `Accel_x/y/z`, копия `Orientation/Screen_rotation`, статусы падения (free-fall, генератор INT2 LIS3DH) и счётчики диагностики; поворот экрана по `ACCEL_INT` |
| Графика LVGL | `Drivers/lvgl` (9.2) + UI SquareLine Studio, `Core/ui/lv_i18n`, `Core/translations/*.yml` | LVGL и UI в git **не лежат**; описание и чек-лист — `docs/LVGL.md`; поток `LVGL Task` пока заглушка |
| Макросы и хелперы | `Inc/shared_macros.h`, `Inc/vector_macros.h` + `Src/Common/Src/vector_macros.c`, `Core/Inc/main.h` (USER CODE EM) | HAL спрятан в макросы `main.h` (`I2C_*`, `SPI_*`, `GPIO_*`, `Uart_*`, `usart_init`), общие функции перенесённого кода (`Delay/DelayInt/GetTick/Search_text`, `TIME_DEL_1/TIME_OUT_LORA`) — в одном `vector_macros.[ch]`; биты статусов — в вашем `shared_macros.h` |
| Кнопки | `stm32u5xx_it.c` (EXTI), `Src/Test/Src/audio_demo.c` | два слоя: продуктовые флаги `button1/2/3/button_sos` и тестовое демо плеера |
| LoRa S7678S | `Src/Lora_S7678S.c` (2094 строки) | реализация автора: AT-обмен, классы A/C, регионы, GPS-трек, `SNS_CFG` |
| Конфигурация прибора | `Inc/config_device.h`, `Inc/shared_types.h` | флаги сборки `CONFIG_*`, структура `SNS_CFG`, биты статусов `ST_COMMON` |
| Конфиг/журнал (сохранение) | `Src/Config_save_read.c`, `Inc/Config_save_read.h` | переработан под этот проект — `SNS_CFG` и журнал во внешней flash (`cfg_save/cfg_load`, `log_append` extstore), заводские данные (калибровка/бренд/AppKey) во внутренней странице 0x080FE000; загрузка при старте в `Vector_Run_Pre_Init()`, запись — только из `Vector_RunFlashMemory()`. области с явным `addr` — рабочий конфиг 0x400000 (`SaveConfig`), заводской 0x401000 (`SaveConfigDefault` пишет текущий, `ReadConfigDefault` — сброс к заводским в рабочий `Sns_Cfg_struct`), журнал с 0x402000. журнал ПЕРЕНЕСЁН из extstore сюда — кольцо слотов 64 Б (0x7FFF), `log_read(ptr)` + `log_find_by_time()`; `EVENT_LOG.Reserve[19]` добивает слот до 64 Б |
| Общие включения | `Inc/Vector_main.h` | агрегатор заголовков (см. раздел 6, решение про инклуды) |

UART-роли (как задумано):

| Роль | Периферия | Куда |
|---|---|---|
| `USART_COM` / `USART_DEBUG` | UART4 (**только PC10**, однопроводный полудуплекс; PC11 занят `ACCEL_INT`) | терминал + лог (`VECTOR_LOG_UART 4`), кольцо `TYPE_USART` |
| `USART_BLE` (не определён) | USART3 (PC4/PC5, метки `USART3_*_BLE`) | BLE-модуль (`BLE_RESET` = PC12) |
| `USART_LORA` | **UART4 (`huart4`)** | так задано в `Lora_S7678S.h:21` — КОНФЛИКТ: UART4 занят терминалом и логом (`USART_COM`/`USART_DEBUG`, `VECTOR_LOG_UART 4`) и работает в полудуплексе. До включения `CONFIG_LORA` порт надо переназначить (раздел 7, п.27) |
| (LTE, макроса нет) | USART2 (PA2/PA3, метки `USART2_*_LTE`) | сотовый модем: пины `LTE_EN`/`LTE_RESET`/`LTE_STATUS`/`LTE_LED` (PC0..PC3), кольцо `TYPE_LTE` |
| `USART_GPS` | **USART1 (PB6/PB7)** | GPS/GNSS (`Gps.h`). `USART1_IRQn` включён, байт из ISR уходит в `Gps_Data_Verification()`, таймаут кадра — от TIM3 1 кГц |
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

* **`SNS_CFG`** (`shared_types.h`, структура — автор разделил блоки):
  `Config_common` (State, StateErr, LED-периоды, ПОРОГИ батареи
  `battery_charge_volt_max/min/lim_1/lim_2`, серийный номер, версия железа,
  дата производства, GPS, `PeriodTimeLora`, `Lora_Config_Flags`,
  `Lora_freq_rx2`, `Lora_dr_rx2`, `current_build_type`, резерв);
  `Config_archive` (`SNS_Archive_Type`: указатели кольца журнала
  `CurrentAddrFile`/`BegginAddrFile`, `ArchiveRecording`, `Archiveinterval`);
  `Working_data` (`SNS_Working_Type`: часы `working_hours` — ВРЕМЯ в секундах,
  и `working_hours_offset` — СМЕЩЕНИЕ, offset НЕ часы);
  `application_language`, `CRC_CONFIG`. ИЗМЕРЕННЫЕ ДАННЫЕ (T/H/P, батарея,
  ориентация/поворот, `Sensors_ok`, калибровочное смещение) ранее ПЕРЕЕХАЛИ
  из `SNS_CFG` в `sensors_status` — во flash остаётся только то, что
  действительно конфигурация. В  автор убрал `Bump_interval/
  DataLastBumpTest/TimeLimit/TimeLimitRST/Type_lcd` и добавил типы каналов
  `SNS_Dev_Type/SNS_DevChan_Type` (+ экземпляр `Connected_device`).
  **Хранение (области )**:
  рабочий конфиг — страница внешней flash 0x400000 `SF_CONFIG_BASE`,
  заводской конфиг — страница 0x401000 `SF_CONFIG_DEFAULT` (`cfg_save/
  cfg_load` extstore с явным `addr`, CRC32 extstore + CRC16 `CRC_CONFIG`;
  записи добавляются в страницу без стирания, свежая = максимальный seq);
  журнал событий — КОЛЬЦЕВАЯ область LOG 0x402000..8 МБ (схема автора):
  слоты фиксированной длины 64 байта = `sizeof(EVENT_LOG)` (per-record
  заголовок `LOG_REC_HDR` упразднён — длина одинаковая), кольцо 0x8000
  (32768) слотов = 2 МБ, позиция в памяти = указатель & 0x7FFF; указатели
  `CurrentAddrFile`/`BegginAddrFile` монотонно растут и хранятся в
  `Config_archive` (в flash попадают вместе с конфигом); переполнение —
  стирание сектора и сдвиг начала на число записей в секторе (64 — чётное,
  сектор делится нацело); API `Config_save_read.c`: `save_event()`,
  `log_read(ptr)`, `log_find_by_time(t_from,t_to)` (бинпоиск по
  `Event_log.working_hours`);
  заводские данные (калибровка температуры, бренд `current_build_type`,
  LoRa AppKey) — последняя страница ВНУТРЕННЕЙ flash 0x080FE000 (код
  ограничен 1016K в `.ld`). Оси акселерометра и статусы падения — runtime,
  только в `sensors_status`, во flash не сохраняются (в журнал попадают
  снимком внутри `EVENT_LOG`).
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
COM (UART4)  --байты--> ISR --> InputBuffer[TYPE_USART]   ---\
LoRa (UART4!) --байты--> ISR --> InputBuffer[TYPE_LORA]     ---+--> Receiver Task
BLE (USART3) --байты--> ISR --> InputBuffer[TYPE_BLE]      ---+   (Vector_main.c)
LTE (USART2) --байты--> ISR --> InputBuffer[TYPE_LTE]      ---/
GPS (USART1) --байты--> ISR --> Gps_Data_Verification(): свой буфер в Gps.c
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

**Этап 1. Транспорт BLE** — мы ХОЗЯИН своего модуля MK01, как Avis
* [x] определить `USART_BLE` в `main.h` (`huart3`, PC4/PC5 — PLAN п.13 закрыт)
* [x] приём USART3 → `InputBuffer[TYPE_BLE]` — работает в `stm32u5xx_it.c` под `CONFIG_BLE`
* [x] ~~детект конца кадра: терминатор `0x0D` + таймаут тишины~~ — НЕ НУЖНО,
      обмен синхронный (Ble_TransmitReceive ждёт ответ ≤35 мс + 5 мс, как в Avis)
* [x] **порт Ble_mk01.c (мастерская сторона) — `Src/Ble_vector.c` + `Inc/Ble_vector.h`
      (замечание автора п.2: «полностью копируешь весь функционал, кроме
      ADVMSDDATASET, которую заменяем обычным запросом ADVMSDDATAGET, и на
      которую нам отвечают структурой. И так периодически опрашиваем»)**:
      `Ble_Init(serial)` = AT→AT→`BLE_InitName` (ADVBLENAMESET: метка + %04d%04d)→
      BLEADDR?→период 300→мощность 8→старт; `Ble_TransmitReceive` 4 попытки,
      OK/ERROR, 3 серии ошибок → `Ble_flag_err`; `Ble_MsdDataGet()` — опрос
      принятых данных (ответ hex → `str_ble_data_set_t` → `Ble_ApplyConnectedDevice`
      → `Connected_device`); `Ble_MsdDataSleep/AdvStart/AdvPeriodms/AdvPower/
      SystemOff/DeInit/SystemDFU` — один в один Avis; `Ble_Run()` — скелет
      Avis (INIT 2 с / SET 60 с / SLEEP 30 с таймеры, `ST_COMMON_BIT_ERR_BLE`)
* [x] кольцо `TYPE_BLE` читает ТОЛЬКО `Ble_TransmitReceive` (публичный
      `Ble_Receive()` — пустой: в Avis Receiver Task BLE не обслуживает)
* [x] счётчики: `ble_status` (tx/ok/error/noanswer/unknown/datasets/init/err)
      + `tasks_status.mod[TASK_MOD_BLE]`; хост-тест 9 сценариев пройден
* [x] **спецификация BLE-протокола переписана по новым вводным автора
      (`docs/Mirax-Ble-protocol-v1.md` — единственная редакция; версии не
      плодим, пока не доделан конечный результат)**: фильтр эфира по MAC-таблице слотов (вместо UUID; в payload
      нет ни MAC, ни Header-байта — тип устройства из таблицы MCU, контроль по
      длине); маяки вещают **Eddystone-UID+TLM** (совместимость со сторонними;
      поля BE), а **модуль сам парсит Eddystone и упаковывает в наш формат** —
      нормализованная запись {ShortID u16, Vbatt u16 мВ} 4 Б (0xFFFF = часть
      не принята; MCU мержит части и конвертирует Vbatt→0/1/2 по порогам
      конфига; ShortID = BID[4..5] у наших / из таблицы у сторонних); AVIS —
      `active_sensors` + `sensors[8]` с `SensorIndex` (6 Б/канал, макс. 54;
      `dev_id` = НОМЕР МОДЕЛИ, не серийник); AT-набор (§4) = НОРМАТИВНАЯ спека
      прошивки MK01 («как сделаем — так и будет»): ОДНА команда таблицы
      `MACSET=<N>,<mac>,<тип>,...` — **тип привязан к индексу слота** (нужен
      модулю для нормализации Eddystone; пустой слот = `000000000000,0`,
      нумерация не ломается; 26 записей = 403 символа — влезает в одну строку),
      кэш слота самоочищается при чтении (`MSDCLR`/`MACADD`/`MACDEL` отменены),
      **bulk `MSDGETALL`** (по умолчанию каждые 5 с; ответ комплекта ≈612
      hex-символов — буфер приёма 640) И по слоту `MSDGET=<i>` (разная
      периодичность по устройствам), мост конфигуратора **Modbus RTU**:
      URC `+MBUS:<len>` (модуль→MCU, очередь ≥8) / `AT+MBUSRSP=<len>` + prompt
      `>` (MCU→модуль→notify); live-поток отклонён; таблица MCU
      `MAC[6]+DevType+Param+ShortID` = 10 Б × 32 в CONFIG (заполняет
      конфигуратор, MCU шлёт `MACSET` при каждом включении); скан непрерывный
      (без сна между опросами); **правило LoRa: данные не обновились / не
      готовы / устройство не отвечает → блока-секции НЕТ в пакете** (TTL 5 мин)
* [ ] код `Ble_vector.c/h` по новой спеке: таблица слотов в CONFIG (10 Б × 32),
      `MACSET=<N>,<mac>,<тип>,...` при инициализации, парсинг
      `MSDGETALL`/`MSDGET`, кэш per-slot с `last_seen` (сейчас метки общие на
      группу), обработка нормализованной записи маяка {ShortID, Vbatt} (мерж
      частей по 0xFFFF, Vbatt→Battery 0/1/2 по порогам конфига), приём URC
      `+MBUS:` (оживление `Ble_Receive()`/Receiver Task) + Modbus-слейв +
      `AT+MBUSRSP`, буфер приёма 640; пакеты (Header/BEACONS/PPE/SUBDEV/
      AGG) — удалить
* прошивка модуля MK01-сканера и прошивки устройств комплекта (маяки, метки
      СИЗ, браслеты) — **вне рамок проекта трекера** (решение автора);
      требования к ним — протокол §3 (эфир) и §4 (AT-набор модуля, включая
      нормализацию Eddystone → {ShortID, Vbatt})
* [ ] `CONFIG_BLE 1` — после проверки на живом модуле
* [ ] метка имени `BLE_Label[0]` = "VEC" — предварительная (автор);
      дискретность/коэффициент концентрации отправителя (в `ConcentrationVal`
      пока raw u16)

**Этап 1b. Журнал событий — новый формат (`docs/LOG_FORMAT.md`)** — спецификация согласована, КОД НЕ ПРАВЛЕН
* [x] спецификация формата журнала: слот **40 байт** = заголовок 12 Б
      (Type_log/Fmt_ver/Payload_len/Flags + `working_hours`@+4 — ключ поиска +
      `CurrentAddrFile`@+8 — абсолютный номер в каждой записи: восстановление
      нумерации сканом кольца после любых переполнений/потери указателей) +
      payload 28 Б по типу; геометрия: 102 записи/сектор (хвост 16 Б впустую),
      кольцо LOG 2 МБ = **52224 записи**; миграции нет — кольцо
      переинициализируется
* [x] договорённости по типам/периодике: все записи ОДНОЙ длины (поиск
      арифметикой); `Firmware_version` — только в записях питания/прошивки;
      периодический набор SNAPSHOT (координата + 8 концентраций + биты порогов
      + заряд + поза MOTION_BITS) + READINGS (State/StateErr, BME280 T/H/P,
      заряд) + PPE (маска+заряды+типы+серийники — самодостаточная) — по тику
      **10–15 мин** (`Archiveinterval` = (мин<<8)|сек как в Avis, период ОДИН
      на весь набор, дефолт 10 мин) → 4–6 месяцев истории; маяки в журнале
      ВЫКЛЮЧЕНЫ (тип зарезервирован; на сервер уходят LoRa-uplink'ами);
      события МГНОВЕННО: GAS (сработка/сброс порога — полный срез 8 каналов с
      `chan_state[8]`, антидребезг 10 с), PPE (изменение маски), TURN_ON/OFF/
      FW_UPDATE; полей `altitude`/`activity` нет (активность = бит
      MOTION_BIT_INACTIVE), `aqi` — задел под будущую микросхему-датчик
* [x] договорённости по записи: **RAM-очередь 16 × 40 Б со снимком НА МОМЕНТ
      СОБЫТИЯ** — лечит два дефекта текущей схемы (время берётся в момент
      записи, а не события; однослотовые `SaveEvent`/`TypeLogEvent` теряют
      событие при всплеске); program (1–6 мс) разрешён ПОД ЗВУКОМ, erase ждёт
      «окно тишины» (пауза повтора 500 мс есть у каждого цикла сирены);
      LOG_PTR — один раз на пачку записей
* [ ] код `Config_save_read.c/h` + `Vector_main.c`: `EVENT_LOG` 40 Б +
      структуры payload'ов, адресация 102/сектор (`addr = base + (phys/102)×4096
      + (phys%102)×40`), очередь + писатель, событийные триггеры (пороги AVIS
      из BLE-опроса, маска СИЗ, питание), снятие сплошного гейта
      `audio_is_busy()` для program-операций в `Vector_RunFlashMemory()`,
      единицы `Archiveinterval` — минуты в старшем байте
* [ ] опционально (понадобится erase под звуком): `VECTOR_AUDIO_STREAM_CHUNK
      8192` (окно дозагрузки 186 мс ≥ худшего erase 100 мс) или erase-suspend
      MX25R6435F (команды 0xB0/0x30 — сверить с даташитом)

**Этап 2. Транспорт LoRa**
* [x] удалён дубль typedef `ble_beacon_t` (`Lora_S7678S.h` ↔ `shared_types.h`):
      единственное определение — `shared_types.h` (межмодульный тип),
      `Lora_S7678S.h` включает его — иначе redefinition при `CONFIG_LORA=1`
      (правило слоёв типов — строка «Общие типы vs структуры модулей» в §6)
* [ ] `CONFIG_LORA 1`, определить `USART_RF` в `main.h`
* [ ] `FIRMWARE_VERSION` — дефолт для всех `DEVICE_NUMBER` (иначе LoRa не соберётся)
* [ ] приём → `InputBuffer[TYPE_LORA]`, AT-машина состояний S7678S — `Lora_Receive()` вызывается из `Receiver Task` под `CONFIG_LORA`; порт LoRa сейчас = UART4 (конфликт с логом, п.27), что мешает включить конфиг — п.25
* [ ] `SNS_CFG` в CONFIG-странице внешней flash: загрузка при старте,
      сохранение по изменению, контроль `CRC_CONFIG`
* [ ] передача: очередь пакетов + `transmit_buffer(..., TYPE_RF)` — вызывается из `Lora_Run()` (поток `Measure Task`), счётчики в `tasks_status.mod[TASK_MOD_LORA]`

**Этап 2b. GPS/GNSS — модуль подключён к проекту, собирается при `CONFIG_GPS 1`
* [x] `CONFIG_GPS 1`, типы/константы модуля в `Gps.h`, `USART_GPS (huart1)`, `TIME_DEL_1` — собирается (проверено gcc -fsyntax-only)
* [x] UART под GNSS = USART1 (PB6/PB7): `USART1_IRQn` включён, `USART1_IRQHandler` → `Gps_Data_Verification()`
* [x] `Uart_Gps_Set_Baudrate()`: AT32 `usart_init()` → `HAL_UART_Init()` + возврат прерываний RXNE/ERR
* [x] общие хелперы `Delay()` / `DelayInt()` / `GetTick()` / `Search_text()` — `vector_macros.c` (время только тик RTOS, до планировщика busy-wait); они же нужны LoRa-драйверу
* [x] `Uart_Gps_Receive_Timer_Inc()` вызывается из `HAL_TIM_PeriodElapsedCallback(TIM3)` (1 кГц) в `main.c`
* [x] `Gps_Run()` в `Vector_main.c` (weak): `Gps_Init()` → `Gps_Init_Nav_Sys()` по секундному таймеру прибора (`TIMER_RTC_GPS_DATA_INIT`), `Gps_DeInit()` при снятом статусе, ошибка приёма → `ST_COMMON_BIT_ERR_GPS`
* [ ] подтвердить по схеме полярность и длительность сброса `GNSS_RST` (PB5), режим `GNSS_MODE` (PB4) — пока пины не дёргаем
* [ ] проверить приём NMEA на живом модуле: `tasks_status.mod[TASK_MOD_GPS]`, `Latitude/Longitude`, `Gps_flag_err`

**Этап 2c. Экран и графика (LVGL 9.2 + SquareLine Studio)**
* [x] `LCD_platform.c/h` переведён с AT32 на HAL (SPI2 + GPDMA1 Ch8), имена функций сохранены —
* [x] `TFT.h`: ST7789P3 172×320 + `TFT_COL_OFFSET` (RAM контроллера 240×320) —
* [x] include paths: `Drivers/lvgl`, `Drivers/ui_Vector`, `Core/ui/lv_i18n`, `Src/TFT`, `Src/BME280`, `Src/Lis3dh_reg` —
* [x] UI-экспорт SquareLine Studio 1.6.0 (LVGL 9.2.2) В GIT — `Drivers/ui_Vector/` (ui.[ch], ui_events/ui_helpers/темы, screens, fonts, images, components), `.gitignore`: `/Drivers/*` + `!/Drivers/ui_Vector/`; include path `../Drivers/ui_Vector` в `.cproject` (4 блока) —
* [ ] `lv_conf.h` (LV_COLOR_DEPTH 16, память, тик) — в git его нет
* [ ] порт дисплея: `lv_display_create(172,320)` + `lv_display_set_flush_cb` → `TFT_FlushBuffer`, `lv_tick_set_cb(VTICK_MS)`
* [ ] тело потока `LVGL Task` (`ui_init()`, `lv_i18n_init()`, `lv_timer_handler()`) и `LVGL_STACK_SIZE` 8–16 КБ
* [ ] в кубе SPI2 prescaler /4 или /8 (сейчас /2 = 80 МГц — выше spec ST7789P3)
* [ ] перенос `TFT_indicator.c` (нужны `SNS_CFG_Type`, `CALIB_CFG`, `COUNT_CHAN`)

**Этап 3. Мост и статусы**
* [ ] таблица «статус → индекс звука» и «статус → LED»
* [x] BLE-данные → LoRa v4 пакет: `Ble_telemetry`/`Connected_device` →
      контейнерные блоки (PPE/BLEBeacons) и секция AVIS (`SUB_TYPE_GAS`);
      downlink `Message` → `Lora_Message` (New/`Lora_MessageShown()`) для дисплея;
      проверено хост-тестом `test_lora_v4` (побайтовая сверка с v4)
* [ ] LoRa → BLE-ответ (не требуется протоколом v1: downlink адресует трекер;
      порт/DR/класс A/C — в `Lora_Init`, зашиты v3-логикой)
* [ ] учёт `ST_COMMON_BIT_BLOCK_SOUND`, `BLOCK_SOUND_LIMIT_CALIB`,
      `DATA_EXCHANGE`, `TURN_ON_BLE/LORA/GPS/GSM`, `TURN_OFF`
* [ ] приоритеты: аварийный статус важнее информационного (очередь звуков)

**Этап 4. Кнопки (продуктовая логика)**
* [x] инкремент `button_count` — в `Timer_Tick_1ms()` (TIM3 1 кГц = мс), EXTI ставит
      `button_flag`/обнуляет счёт; отпускание гасит флаг (`stm32u5xx_it.c`)
* [x] короткое/длинное/двойное/удержание + антидребезг в таймере, не в ISR —
      `Src/Menu_vector.c`: декодер жестов по образцу `Button_1/2/3_Menu`
      из Avis_Menu_2 (`GEST_SHORT/LONG/DOUBLE/SHORT_LONG/FAKE`, LONG срабатывает
      до отпускания), пороги — `VECTOR_MENU_PRESS_SHORT_MS`/`_LONG_MS`/`_DOUBLE_MS`
* [x] основа меню: машина экранов `screen_t` (Start/HeadMain/Measurements/
      Menu/TestSound — по имеющимся экспортам SquareLine), `Menu_GoTo`,
      авто-возврат на главный по `TIMER_RTC_MAIN_RESET`, `dirty_data` → `SaveConfig=1`;
      отрисовка — weak `Menu_Draw()` (подставит LVGL-порт).  по замечаниям
      автора: хук `Menu_EnterScreen()` УДАЛЁН (п.1 — как `LCD_Fill_Template`
      в Avis, экран перерисовывается целиком каждый цикл, `lv_screen_load`
      порт сделает внутри `Menu_Draw()` при смене screen); приоритет жестов
      ПОСТОЯННЫЙ B1>B2>B3 (п.3); `Menu_Run()` переехал из Measure Task в поток
      LVGL Task по `timer.flag_100ms` через `Vector_Run_Indication()` — аналог
      `indication_task_function` из Avis_main.c (п.5). Состав экранов/пунктов/
      статусов ещё не определён (автор) — расширяется в `Menu_vector.h` +
      `menu_navigate()`
* [ ] `button_sos` — зафиксировать, что это та же BUTTON1
* [ ] `VECTOR_AUDIO_DEMO_KEYS 0` — демо-кнопки отдаются рабочему коду (сейчас демо
      плеера и `Menu_vector` читают одни и те же `button1/2/3`: при включённом демо
      нажатие листает звуки И ходит по меню)

**Этап 5. Сон и питание — вероятно, НЕ будет (решение автора: прибор постоянно
запитан, глубокий сон не планируем)**
* [ ] `CONFIG_SLEEP`: Stop2 (RAM сохраняется) или Standby (меньше ток, RAM
      теряется) при «прибор выключен» — справочно, если когда-нибудь
      понадобится; источник пробуждения — RTC Wakeup 1 с (в кубе уже включён:
      Wakeup Interrupt + RTC global interrupt); для Standby — WKUP-пин кнопки;
      перед сном: усилитель `SD_MODE` low, flash в deep power-down (B9h —
      команды в драйвере пока нет), дождаться TC у UART, `HAL_SuspendTick()`;
      после: `SystemClock_Config()`, реинит периферии, компенсация
      `tx_time_set()`
* `TX_ENABLE_WFI` в `Core/Inc/tx_user.h` **НЕ задан** — idle-поток ThreadX
      сейчас без WFI. Пояснение: WFI в idle БЕЗОПАСЕН и при проигрывании звука
      (idle выполняет WFI только когда готовых потоков нет; SAI+GPDMA
      продолжают работать аппаратно, прерывание половины DMA будит ядро за
      микросекунды — запас до дедлайна дозагрузки 93 мс огромен; это НЕ
      Stop2 — такты/RAM/периферия живы). Опциональная микро-экономия, не
      обязательна; глубокий сон при звучащем звуке, разумеется, недопустим

**Этап 6. Журнал и сервис**
* [x] кольцевой журнал в области LOG (0x402000..): `save_event()` (слот
      64 Б, кольцо 0x7FFF, указатели в `Config_archive`); события «включение»
      и архив по `Archiveinterval`
* [x] конфигурация `SNS_CFG` во внешней flash: `read_param/save_param` →
      `cfg_load/cfg_save`, загрузка при старте, заводской фолбэк
* [ ] чтение журнала/конфига — **только COM-порт** (решение автора: через
      BLE или LoRa по команде — НЕ будет): `log_read(ptr)` и
      `log_find_by_time(t_from,t_to)` готовы, `compare_param` — каркас;
      протокол команд — вместе с COM-протоколом
* [ ] событие `TYPE_TURN_OFF` при выключении (нужен обработчик выключения)
* [ ] версионирование прошивки и (опционально) OTA

---

## 6. Решения и договорённости (чтобы не повторять)

| Тема | Решение |
|---|---|
| Общие типы vs структуры модулей | **Правило слоёв (решение автора)**: `shared_types.h` — ЕДИНСТВЕННОЕ место **межмодульных** типов (данные, читаемые через границы модулей): `ble_beacon_t` (= проводная запись LoRa v4 блока `BLEBeacons`, байт-в-байт), `ble_ppe_item_t`, `BLE_TELEMETRY`, `SNS_CFG`, `SNS_Dev_Type` — вне `CONFIG_*`. Заголовки модулей (`Lora_S7678S.h`, `Ble_vector.h`) — только **wire-структуры своего протокола** (блоки LoRa-кадров; кадры BLE `msd_slot_rec_t`/`ble_beacon_norm_t`/`ble_mac_entry_t`/payload'ы реклам) — другие модули их НЕ инклюдят. Направления включений: модуль → `shared_types.h`; `shared_types.h` → никто; модуль → модуль — запрещено. Wire-структура и структура кэша совпадают байт-в-байт → одно определение (в `shared_types.h`). **Исправлено**: дубль typedef `ble_beacon_t` (`Lora_S7678S.h` ↔ `shared_types.h`) удалён — ошибка redefinition возникла бы при первом `CONFIG_LORA=1` (`Vector_main.h` включает оба заголовка); `Lora_S7678S.h` теперь включает `shared_types.h`. Осталось: локальная копия `channel_cfg_t` в `TFT_indicator.c` (файл выведен из сборки) — при переносе индикации убрать в пользу общего типа. Побочно: комментарии кода вычищены от маркеров ранних правок v11–v29 (25 файлов), шапка `Ble_vector.h` прямо говорит, что код реализует ПРЕЖНЮЮ редакцию BLE-протокола (действующая спека — `docs/Mirax-Ble-protocol-v1.md`) |
| Протоколы и журнал (ДОКУМЕНТЫ согласованы, код не правили) | **BLE** (`docs/Mirax-Ble-protocol-v1.md`; файл прежней редакции удалён, v2 переименован в v1 — «не плодим протоколы, пока не доделан конечный результат»): MAC-фильтр по слотам вместо UUID; в payload нет ни MAC, ни Header (тип — из таблицы MCU, контроль по длине); заводские идентификаторы 1 байт; маяки вещают **Eddystone-UID+TLM** (решение автора: совместимость со сторонними маяками), **модуль сам парсит Eddystone и упаковывает в наш формат** — {ShortID u16, Vbatt u16 мВ} 4 Б (0xFFFF = часть не принята, MCU мержит; Vbatt→0/1/2 по порогам конфига MCU; ShortID = BID[4..5] у наших / назначается конфигуратором у сторонних; поля Eddystone BE; собственный 3-Б эфирный формат маяка отменён); AVIS — `active_sensors`+`sensors[8]` с `SensorIndex` (6 Б/канал, макс. 54; `dev_id` = номер модели, НЕ серийник); AT-набор §4 = спека прошивки MK01 («как сделаем — так и будет», внешней информации не будет): ОДНА `MACSET=<N>,<mac>,<тип>,...` — **тип привязан к индексу слота** (модулю нужен для нормализации Eddystone; пустой слот = `000000000000,0`), кэш слота самоочищается при чтении (live-поток отклонён: ~80 кадров/с не нужны при опросе), **bulk `MSDGETALL` каждые 5 с** (≈612 hex-символов, буфер приёма 640) + `MSDGET=<i>` для разной периодичности по устройствам, мост конфигуратора **Modbus RTU** — URC `+MBUS:<len>` / `AT+MBUSRSP=<len>`+`>` (свои кадры 0xA5/CRC16 отменены — у Modbus свои рамки; карта регистров вне рамок BLE-протокола); таблица MCU `MAC+DevType+Param+ShortID` = 10 Б × 32 в CONFIG; скан непрерывный; **правило LoRa: не обновились / не готовы / не отвечает → блока-секции НЕТ в пакете** (TTL 5 мин, `last_seen` per-slot). **Журнал** (новый `docs/LOG_FORMAT.md`): слот **40 Б** (заголовок 12 Б: `working_hours`@+4 + `CurrentAddrFile`@+8 — номер в каждой записи, восстановление после любых переполнений; payload 28 Б по типу), все записи одной длины; 102 записи/сектор, кольцо 2 МБ = 52224; периодика **10–15 мин** (`Archiveinterval` = (мин<<8)|сек, ОДИН период на набор SNAPSHOT+READINGS+PPE) → 4–6 месяцев; события мгновенно (GAS-порог с антидребезгом 10 с, маска СИЗ, питание/прошивка); маяки в журнале выключены; `altitude`/`activity` убраны (активность = MOTION_BIT_INACTIVE), `aqi` — задел под будущую микросхему; `Firmware_version` — только в записях питания; **RAM-очередь 16×40 Б со снимком на момент события** (лечит время-в-момент-записи и потерю всплесков однослотовым `SaveEvent`); миграции нет. **Звук/flash**: вторая микросхема НЕ нужна (шина занята звуком ~4 %: 8 КБ/93 мс ≈ 3–4 мс; program 1–6 мс идёт под звуком, erase ждёт окно тишины ≤500 мс) — при ревизии платы возможен футпринт второй flash на ТОЙ ЖЕ SPI1 (отдельный CS, не отдельный SPI); если понадобится erase под звуком — `VECTOR_AUDIO_STREAM_CHUNK 8192` (окно 186 мс) или erase-suspend 0xB0/0x30 |
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
| Перенос с AT32 | **HAL внутри библиотек не используем**: весь HAL спрятан в макросы `main.h` (USER CODE EM) — `I2C_Master_Transmit/Receive/Mem_*`, `I2C_OK`, `i2c_status_type`, `I2C_IsBusy/I2C_ReConfig`, `SPI_Transmit[_DMA]/SPI_Abort/SPI_OK`, `GPIO_WritePin/PIN_SET/PIN_RESET`, `usart_init → Uart_SetBaudrate`. Поэтому `lis3dh.c`, `bme280_com.c`, `MAX17048.c`, `Gps.c` остались вашими (отличаются только строкой include); HAL виден лишь в `vector_macros.c`, в колбэках (`HAL_SPI_TxCpltCallback` в `LCD_platform.c`) и в сгенерированном кубом коде. `EXINT->polcfg1/2` → `LL_EXTI_*Trig_0_31` |
| Экран | ST7789P3 **172×320** (`CONFIG_MODEL_LCD 0`), SPI2 8 бит + GPDMA1 Ch8 (Normal), кадр уходит `LCD_writeBulk()` → колбэк DMA → `lv_display_flush_ready`. Байты RGB565 меняются местами (`VECTOR_LCD_SWAP_RGB565`). `HAL_SPI_ErrorCallback` не занимаем (он уже у `spiflash.c`) — залипание порта лечим таймаутом ожидания, всё видно в `lcd_status` |
| Датчики | I2C1: BME280 (0x76), LIS3DH (0x19), MAX17048 (0x36). Владелец шины — `Measure Task` (`Vector_main.c`), чтение раз в секунду (`timer.flag_1s`). Значения → только `Sns_Cfg_struct.Config_common` (в `sensors_status` — счётчики диагностики). Поворот экрана: `ACCEL_INT` (PC11) → только флаг → `lis3dh_update_all()` в потоке → `TFT_Rotation()` |
| Данные прибора | Экземпляры `Sns_Cfg_struct`, `Cfg_structdef_read`, `tempsensor_calib`, `device_turn` определены в `Vector_main.c` (как в Avis_main.c). Калибровка температуры выключена (`VECTOR_BME_CALIBRATION 0`): `flash_write_calibration_safe()` в проекте нет |
| i18n | Переводы в `Core/translations/*.yml` (ru-RU, en-GB), генерация `Core/update_lang.bat` → `Core/ui/lv_i18n/*`; в UI текст через `_("ключ")`. Описание — `docs/LVGL.md` |
| Таймеры прибора | Схема автора из Avis оставлена — она рабочая и одинаковая на всех приборах, переводить на `tx_timer` смысла нет: TIM3 1 кГц → `Timer_Tick_1ms()` (декремент `countdown_time[]`, флаги `timer.flag_1ms/10ms/100ms/1s`, счётчики кнопок, таймауты кадров UART), RTC wakeup 1 с → `Timer_Tick_1s()` (декремент `countdown_time_rtc[]`, `working_hours`). Всё обслуживание переехало из `main.c`/`stm32u5xx_it.c` в `Vector_main.c` — в CubeMX-файлах осталось по одной строке вызова. Экземпляры `volatile` (пишет ISR, читают потоки). Макросы `START/TEST/RESET/END_TIMER[_RTC]` прежние, из `shared_macros.h` |
| Борд-инит | `vector_board.[ch]` удалён: тело (`SRAM4`, заморозка `TIM6` под отладчиком, `SD_MODE`, `sf_probe`, `audio_selftest`) переехало в `Vector_Run_Board_Init()` в `Vector_main.c` — вне `#if VECTOR_TASKS_ENABLE`, чтобы работало всегда |
| Данные датчиков | Конфигурационные измерения (T/H/P, батарея, `Orientation`, `Screen_rotation`, маска `Sensors_ok`) — в `Sns_Cfg_struct.Config_common`. Runtime-данные (оси `Accel_x/y/z`, `fall_detected`/`cnt_fall_events`, копия ориентации/поворота) и счётчики диагностики шины I2C1 — в `sensors_status` (решение автора, оси во flash не сохраняются) |
| Хранение конфига/журнала | `SNS_CFG` и журнал — ВО ВНЕШНЕЙ flash через `extstore` (`cfg_save/cfg_load` — два слота с CRC, `log_append` — кольцо LOG); заводские данные (калибровка T, бренд, AppKey) — во ВНУТРЕННЕЙ flash, страница 0x080FE000 (`.ld` ограничен 1016K). Адресация Avis (`reg_mem`, AT45/W25) не переносилась. Часы (уточнение автора): `working_hours` — САМО ВРЕМЯ в секундах (`volatile`, +1 каждый такт `Timer_Tick_1s()` от RTC 1 Гц), `working_hours_offset` — СМЕЩЕНИЕ (не часы); в журнал `save_event()` пишет сумму offset + working_hours. (С  области и адреса — в строке «Области конфига » ниже) |
| Шина flash и звук | «Окно тишины» вместо `TX_WAIT_FOREVER` (решение автора): `Vector_RunFlashMemory()` заходит во внешнюю flash только когда звук молчит (`audio_is_busy()==0` — доигран или пауза повтора 500 мс), иначе флаги живут и повторяются каждый проход (50 мс); звук всегда доигрывает до конца, форс-стирания сквозь звук нет. В `extstore`: чтения ждут шину ограниченно (`VECTOR_EXT_LOCK_READ_MS`=250 мс), записи — try-lock (`VECTOR_EXT_LOCK_WRITE_MS`=0) с кодом `EXT_BUSY (-2)`; стирания под звук не стартуют и изнутри `extstore` (защита гонки старта с кнопки), счётчики `ext_dbg_busy`/`ext_dbg_erase_defer`; `ext_mtx` с `TX_INHERIT` (Audio 10 не ждёт, пока Receiver 12 вытесняет Measure 13). Записи последовательные (опрос, без DMA). `save_param/save_event` → `int`, флаги гасятся только по успеху, жёсткая ошибка → `cnt_flash_errors`; `compare_param` пишет журнал отложенно через `SaveEvent` (поток приёма flash не трогает) |
| Области конфига | Решение автора: у `cfg_save/cfg_load/save_param/read_param` явный адрес области `uint32_t addr` (НЕ `uint16_t` — 0x400000 в 16 бит не помещается); ДВЕ config-страницы (`sfmap.h`): `SF_CONFIG_BASE 0x400000` — рабочий конфиг (загрузка при старте, флаг `SaveConfig`), `SF_CONFIG_DEFAULT 0x401000` — заводской (флаг `SaveConfigDefault` пишет туда ТЕКУЩИЙ конфиг, `ReadConfigDefault` — СБРОС К ЗАВОДСКИМ: читает область через буфер `Cfg_structdef_read` и при валидном CRC копирует в рабочий `Sns_Cfg_struct`, пустая/битая область → рабочий конфиг не тронут; закрепление сброса во flash — отдельным `SaveConfig`); журнал сдвинут на `0x402000`. «Дефолтный» флаг — это ОБЛАСТЬ ПАМЯТИ заводских настроек, а не запись дефолтов при первом пуске (та остаётся в рабочей области). Флаг `ext_ready` удалён (решение автора): порядок `ext_init()` → `audio_init()` → потоки фиксирован, звук и обращения к конфигу возникают только по событиям; до `ext_init()` `tx_mutex_get` на нулевом мьютексе вернёт ошибку штатно. Журнал единый: единичные события (`SaveEvent`) + периодический архив (`TIMER_RTC_LOG`, `ArchiveRecording==1`) — оба через `save_event()`; из `SNS_CFG` убраны `TimeLimit/TimeLimitRST/Bump_interval/DataLastBumpTest/Type_lcd`, добавлены `SNS_Dev_Type/SNS_DevChan_Type` |
| Журнал-кольцо и данные | Схема автора: журнал — слоты ФИКСИРОВАННОЙ длины 64 Б (`sizeof(EVENT_LOG)`, `LOG_REC_HDR` не нужен), кольцо на 0x7FFF (маска указателя), указатели `CurrentAddrFile`/`BegginAddrFile` — в `SNS_CFG.Config_archive` (растут монотонно, в памяти — по маске); переполнение: стирание сектора + сдвиг начала на ЧИСЛО ЗАПИСЕЙ В СЕКТОРЕ (64 — чётное/кратное, сектор делится нацело); чтение по номеру `log_read(ptr)` и поиск по времени `log_find_by_time(t_from,t_to)` → номера указателей (время = `Event_log.working_hours`, бинпоиск). Разделение данных: измеренное — в `sensors_status`, сохраняемое — в `SNS_CFG` (`Config_archive`/`Working_data`). Часы: `working_hours` при включении берётся ИЗ РЕЗЕРВНОГО РЕГИСТРА RTC BKP0 ПОСЛЕ чтения конфига; `Timer_Tick_1s()` зеркалит счётчик в BKP0 каждую секунду (DBP открыт в `Vector_Run_Board_Init()`); offset — смещение, не часы |
| BLE-периферия и основа меню — **ОТМЕНЕНО, см. след. строку** | Роли поменялись относительно `Ble_mk01` (docs/): там MCU был МАСТЕРОМ своего модуля и пушил данные (`AT+ADVMSDDATASET=`), у нас мастер — Avis, а VECTOR отвечает. `Ble_vector.c/h` (`CONFIG_BLE`): неблокирующая строковая машина по кольцу `TYPE_BLE` (терминатор `0x0D`, страховка — таймаут тишины `VECTOR_BLE_FRAME_TMO_MS` в `Ble_Run()`), таблица команд `ble_cmds[]` (дополняется одной строкой): `ADVMSDDATAGET` → `str_ble_vector_data_t` hex-строкой + `OK`, `ADVMSDDATASET=<hex>` → `str_ble_data_set_t` (формат Ble_mk01 байт-в-байт) → `Ble_ApplyConnectedDevice()` → `Connected_device` (`SNS_Dev_Type`: dev_id/версии/заряд + `Sensor[]`: `num_formula_gas`, `ConcentrationVal` = raw u16 LE, `StateErr`, `Setting`; общий `StateErr` = OR по каналам), `MODINFO?`, прочее → `ERROR`. Ответы: `[данные + 0x0D] [«OK»|«ERROR» + 0x0D]` — формат предварительный, протокол будет дополняться (автор). `Menu_vector.c/h` (`CONFIG_DISPLAY`): основа слоя меню — `screen_t` по экранам SquareLine, жесты кнопок 1/2/3 как в `Button_1/2/3_Menu` (Avis_Menu_2), роли B1=вход/назад, B2=вниз, B3=вверх, авто-возврат `TIMER_RTC_MAIN_RESET`, `dirty_data`→`SaveConfig`, weak-хуки `Menu_Draw()`/`Menu_EnterScreen()` под LVGL-порт. Вызовы: `Menu_Init()` в `Pre_Init`, `Menu_Run()` каждый проход `measure_task` (50 мс — LONG 1.5 с и окно DOUBLE 400 мс требуют частого опроса). Проверено хост-тестами: BLE 10/10 (пинг, DATAGET-содержимое, DATASET→Connected_device, битый hex, переполнение строки, таймаут кадра, выключенный модуль), меню — жесты/навигация/авто-возврат/dirty→SaveConfig |
| BLE и меню — исправления по замечаниям автора | **(1)** `Menu_EnterScreen` удалён: как `LCD_Fill_Template`/`DisplayIndication` в Avis, экран перерисовывается ЦЕЛИКОМ каждый цикл — слабое `Menu_Draw()`, `lv_screen_load` при смене screen сделает порт внутри него. **(2)** BLE — роли НЕ менялись: полный порт `Ble_mk01.c` (мастерская сторона своего модуля: AT/ADVBLENAMESET/BLEADDR?/ADVPERIODMS/ADVPOWER/ADVSTART/SLEEP/SYSTEMOFF/DFU/MODINFO?, 4 попытки, OK/ERROR, 3 серии → flag_err), вместо `ADVMSDDATASET` — периодический запрос `ADVMSDDATAGET` (`Ble_Run`, SET-таймер 60 с), ответ hex → `str_ble_data_set_t` (64 байта с паддингом внутреннего struct — ОДИН В ОДИН сборка Avis) → `Connected_device`; кольцо TYPE_BLE читает только `Ble_TransmitReceive`, публичный `Ble_Receive()` пустой. **(3)** Приоритет жестов постоянный: B1 > B2 > B3 (LONG > SHORT внутри кнопки), единая else-if цепочка на экран. **(4)** Указатели архива `CurrentAddrFile/BegginAddrFile` сохраняются после КАЖДОЙ записи журнала ОТДЕЛЬНОЙ страницей-кольцом `SF_LOG_PTR` 0x602000 (запись 16 Б {seq,cur,beg,crc16,pad}, 256 шт на стирание, свежая = max seq; аналог `SaveCurrentAddrFile/ReadCurrentAddrFile` Avis) — конфиг из-за двух слов не перезаписывается; стирание только в «окне тишины» (EXT_BUSY → указатели ждут в RAM). **(5)** Меню/индикация переехали из Measure Task в поток LVGL Task (`app_azure_rtos.c`) по образцу `indication_task_function` Avis: `timer.flag_100ms` → `Vector_Run_Indication()` (switch device_turn → `Menu_Run()`), затем `lv_timer_handler()` при `TFT_Initialized`, цикл 10 мс. Побочно: `Init_Build_Type()`/`current_build_type` (бренд из последней страницы внутренней flash, линкер уже резервирует 1016K) — закрыта висячая ссылка `TFT_indicator.c`; сброс к заводским теперь как в Avis закрепляется `SaveConfig=1` + `ST_COMMON_BIT_DATA_EXCHANGE`. Хост-тесты: menu (жесты/приоритет/indication), ble (9 сценариев: init/DATAGET/sleep/ошибки/таймауты/DeInit), logptr (кольцо указателей: append/wrap/busy/CRC) — все прошли |
| LoRa v4 + BLE-протокол (прежняя редакция) + календарный RTC | **(1) LoRa**: апгрейд v3→v4 по `docs/Mirax-Lora-protocol-v4.md` БЕЗ переименования функций (дополнение к рабочему коду): `TYPE_DYNAMIC` = `ContainerMask`(13 бит) + контейнерные блоки (Battery/GPS/GPSTrack/TimeRef/**ControlLora16**/**Environment**/**Motion**/**PPE**/**BLEBeacons**/**контейнерный EventLog**/GPSHistory/ConfigAck/**MsgAck**) + `SubCount` и секции под-устройств (`SubID+SubMask`); газовые данные (Sensors/канальный EventLog/заряд) переехали в секцию AVIS (`SUB_TYPE_GAS`, SubID 1) — источник `Connected_device`/`Ble_telemetry` (по BLE), НЕ каналы `SNS_CFG` (у трекера газовых сенсоров нет); концентрации — pass-through (отправитель уже масштабировал). `ControlLora` 8→16 бит (+`EVACUATION` бит 8; защита SOS\|CURRENT_C маска `0x00A0`; ACK-маска `0x0137`). `TYPE_STATIC` v4: +`radio_cfg_t` (ground truth), +`PPEEnabledMask`/`ppe_descr_t`, +`sub_descr_t` с `CfgLen/Cfg` (самоописываемо). Downlink: блок Control 4 Б (Mask/Value u16), +блок `Message` (CP1251, ≤237 симв.) → `Lora_Message` (идемпотентность по MsgSeq, квитирование MsgAck битом 12). ACK/TURN_OFF: статус u16. Контейнерный EventLog — типизированные 10-байтовые записи (падение/поза/SOS/СИЗ; коды EventType предварительные), якорь TimeRef/GPS общий для всех журналов; исправлен LE-порядок маркера «нет фикса» (было 0x0080, стало 0x8000). Буферы UART: receive 256→512, transmit 400→560 (hex-downlink с Message). Дополнения, чтобы `CONFIG_LORA=1` вообще собирался: макрос `LORA_UART_Init`, `TIME_RTC_LORA_DATA_SLEEP_AFTER_DL`, weak `Get_Conversion_factor_rf` (1.0), `deveui_com`, убрана мина Avis `#define TYPE_LORA (TYPE_RF)` из `buffer.h` (TYPE_RF в enum нет — макрос затенял TYPE_LORA). **(2) BLE-протокол v1** (новый `docs/Mirax-Ble-protocol-v1.md`, стиль LoRa-дока): Header-байт (тип<<4\|версия); legacy-пакет Avis (0x02, 6+5N) принимается КАК ЕСТЬ — `str_ble_data_set_t` приведена к РЕАЛЬНОМУ формату `Ble_mk01.h` (версия с u16-полями была неверной, 64 Б вместо 41); новые пакеты: BEACONS 0x11 (Count + 4Б-записи = `ble_beacon_t` LoRa v4), PPE 0x21 (Count + 6Б: тип/серийник/заряд/RSSI/надето), SUBDEV 0x31 (с серийником — задел новой прошивки AVIS), AGG 0xF1 (BlockMask + блоки — «всё одним пакетом»); лимит пакета 256 Б (автор), UART-буфер 520 (hex); диспетчер `Ble_ParsePacket` → `Connected_device` + `Ble_telemetry` (`shared_types.h`, вне CONFIG_* — LoRa читает даже при CONFIG_BLE=0; свежесть TTL 5 мин). **(3) Часы**: подход автора (коммит «6») — календарный HAL RTC ↔ Unix-секунды (`mktime`/`gmtime`, AN4759) вместо BKP0; исправления: база года ±100 (HAL 0..99 от 2000, `tm_year` от 1900 — иначе 1926 вместо 2026), порядок Time→Date и на чтение, и на запись (RM0456: теневые регистры), контроль ошибок HAL (возврат 0/-1), совместимость `ErrorStatus/SUCCESS` в `main.h`; `HAL_PWR_EnableBkUpAccess` на U5 корректен (правка автора подтверждена по заголовкам ST — функция в PWR-модуле, не Ex). TZ newlib = UTC (setenv не вызывать); `time_t` 32 бит — 2038 (как и у AT32). **(4) Единицы BME280**: `BME280_FLOAT_ENABLE` НЕ определён (`.cproject`) → int32-драйвер: `Temperature` = °C×100, `Pressure` = гПа×10, `Humidity` = %×1024 (комментарии врали) — журнал исправлен (было повторное ×100 → переполнение int16), LoRa Environment конвертирует по факту; индикации (`TFT_indicator.c`) понадобятся /100 и /1024 — TODO автора. Хост-тесты: `test_lora_v4` (7 сценариев, побайтово по v4), `test_ble` (13 сценариев протокола v1), menu/logptr — регрессия |
| Часы в RTC BKP0 и сервис (позже заменены календарным RTC — см. строку «LoRa v4 + BLE-протокол + календарный RTC») | Автор заменил зеркало BKP0 из ISR на Avis-функции `RTC_ReadTime()/RTC_WriteTime()` (реализованы в `vector_macros.c` поверх HAL_RTCEx_BKGet/SetRegister(BKP0) — на U5 нет 32-битного счётчика RTC как на AT32): `Timer_Tick_1s()` только `+1`, сверку и запись раз в секунду делает `Vector_Options_System()` (Receiver Task, `flag_start_work_bat_sys`), чтение при старте — `Pre_Init` после конфига; DBP открыт в `Board_Init`. Там же: `SoftResetProgram` (+ `TIMER_RTC_SOFT_RESET_PROGRAM`) → `save_event_type_2(TYPE_FW_UPDATE)` (пока = `save_event` + bounded-ретрай; второй формат журнала — после переноса каналов) → стоп TIM3 → `NVIC_SystemReset()`; `Avis_Battery_System()` — weak-заглушка (перенести из Avis). Блок-схема проекта — `docs/ARCHITECTURE.md` + `architecture.svg` |
| Часы и сброс к заводским | Уточнение автора: `working_hours_offset` — СМЕЩЕНИЕ, а не часы; ВРЕМЯ — `Config_common.working_hours` (`volatile`, +1 каждую секунду из `Timer_Tick_1s()`, RTC wakeup 1 Гц; сохраняется/грузится вместе с `SNS_CFG`). Сброс к заводским (`ReadConfigDefault`) ЧАСЫ СОХРАНЯЕТ: `working_hours` переносится при копировании структуры, остальное (включая offset) заменяется заводским; потеря ≤1 с из-за гонки с RTC-ISR допустима. Журнал: `Event_log.working_hours` = offset + working_hours |
| Подсветка экрана | ШИМ: TIM8_CH1 (PC6, AF3), куб — PSC 159 / ARR 39 → 25 кГц. `backlight_set(%)` масштабирует скважность от фактического ARR (`__HAL_TIM_GET_AUTORELOAD`), `VECTOR_LCD_PWM_PERIOD` больше не используется |
| Заголовки | Один `vector_macros.h` на все макросы/хелперы проекта вместо `vector_status.h` + `vector_compat.h`; API и состояние датчиков (`sensors_status`) живут в `Vector_main.h`, реализация — в `Vector_main.c` (как `Ble_Run`/`Lora_Run` в `Avis_main.c`). Биты статусов — в `shared_macros.h` автора. Новых `vector_*.h` без нужды не заводим |
| Порядок работ (решение автора) | Сначала **база**: модули прикладываем к проекту (`Gps.c`, потоки, кольца `TYPE_*`, флаги `CONFIG_* = 0`), периферию не трогаем. UART/куб/NVIC (`USART1_IRQn`, `USART_GPS`, `USART_BLE`/`USART_RF` в `main.h`) и значения `CONFIG_*` **пока не настраиваем и не включаем** — вернёмся к этому отдельным этапом |

---

## 7. Известные дефекты и риски (найдены при разборе, ждут решения)

**buffer.c / buffer.h**
1. `T_Buffer.begin/end` не `volatile` — потребитель в потоке может не увидеть
   байты, записанные из ISR. — **исправлено**: метки `volatile`, правило
   владения (SPSC) задокументировано в `buffer.h`.
2. Гонка в `receive_buffer()`: при `begin == end` потребитель обнуляет **оба**
   индекса, в том числе `end`, который принадлежит производителю → потеря
   байта. Обнуление не нужно: индексы заворачиваются сами. Правило SPSC —
   потребитель трогает только `begin`. — **исправлено**: обнуление `end`
   убрано, `end` читается одним снимком; заодно исправлена парная ошибка в
   `add_to_buffer()` (при завороте кольца `end` оставался 0 вместо 1 →
   потребитель перечитывал чужой старый байт как новый).
3. Два одинаковых `if` (`begin < end` и `begin > end`) — достаточно `!=`.
   — **исправлено**: одна ветка `begin != end`, метки заворачиваются
   по BUFFER_LENGTH.
4. `add_to_buffer()` делает `__disable_irq()/__enable_irq()` безусловно: вызов
   из потока внутри критической секции ThreadX сорвёт маску. Безопасно —
   сохранять/восстанавливать PRIMASK. — **исправлено**: после перехода
   на честное SPSC-кольцо маскирование не нужно вовсе (писатель трогает только
   `end`, читатель только `begin`, 16-битные доступы атомарны).
5. Переполнение кольца молчаливое (`res = 1`) — нет счётчика потерь.
   — **не исправляем (решение автора)**: переполнение кольца не должно
   допускаться размерами и ритмом чтения; счётчик потерь
   (`tasks_status.mod[].cnt_errors`) не вводится и не используется.
6. `transmit_buffer()`: нет проверки `Size <= 300` и
   `type_transmit < TYPE_USART_COUNT` перед `memcpy` в `buffer_transmit` →
   выход за границы массива. — **исправлено**: проверки NULL/Size/типа
   в начале, пакет длиннее буфера отбрасывается целиком (не режется).
7. `__attribute__((section(".sram4")))`: в `STM32U575RGTX_FLASH.ld` область
   SRAM4 объявлена, но выходной секции `.sram4` нет → секция-сирота
   (ld предупреждает и размещает её на своё усмотрение, не в SRAM4).
   — **исправлено**: секция `.sram4 (NOLOAD) >SRAM4` добавлена в оба
   линкер-скрипта; ВАЖНО — такт SRAM4 по умолчанию выключен, его включает
   `Vector_Run_Board_Init()` (`__HAL_RCC_SRAM4_CLK_ENABLE()`), иначе обращение
   к SRAM4 = bus fault. Для сна (этап 5) не забыть биты SRAM4PD/SRAM4PDS.
8. `GetTick()` в DMA-ветке нигде не определён (сейчас ветка не компилируется,
   `DMA_USART 0`); по правилам проекта время — `VTICK_MS()`.
9. `memcpy` без `#include <string.h>`. — **исправлено** в `buffer.c`
   (заодно `<string.h>` добавлен в `spiflash.c` для `memcmp`).
10. `BUFFER_LENGTH 255` → полезная вместимость 254 байта (один байт теряется
    на различение «пусто/полно») — стоит отметить в комментарии.
    — **отмечено** в `buffer.h`.

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
21. **[закрыт: куб]** RTC Wakeup: куб запускал
    `HAL_RTCEx_SetWakeUpTimer()` БЕЗ прерывания, `RTC_IRQn` не был разрешён →
    секундные таймеры прибора не шли.  временно чинил это USER CODE-блоком
    в `rtc.c`; автор включил то же в кубе (`RTC → Wakeup Interrupt` +
    `NVIC → RTC global interrupt`, `.ioc` и код перегенерированы: куб сам зовёт
    `HAL_RTCEx_SetWakeUpTimer_IT` и настраивает NVIC в `HAL_RTC_MspInit`,
    приоритет 0). В  страховочный блок удалён: он дублировал куб, а его
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
    внешней flash). План тестов — в сообщении к .
24. Маппинг «модуль → UART» (фактический, +): GPS = USART1 (PB6/PB7,
    `USART1_IRQn` включён); BLE = USART3 (PC4/PC5) → `InputBuffer[TYPE_BLE]`
    под `CONFIG_BLE`; LTE = USART2 (PA2/PA3, метки `_LTE`, пины
    `LTE_EN/RESET/STATUS/LED`), но `USART2_IRQHandler` кладёт байты в
    `InputBuffer[TYPE_LORA]` — надо `TYPE_LTE`; LoRa = **huart4**
    (`Lora_S7678S.h:21`) — конфликт с COM/логом (п.27). `USART_BLE` /
    `USART_RF` в `main.h` не определены (п.13) — `transmit_buffer()` без них
    не передаст ничего.
25. `CONFIG_LORA 1` / `CONFIG_BLE 1`: прежние блокеры сняты (–) —
    `shared_types.h` включён в `Vector_main.h`, экземпляры `Sns_Cfg_struct` и
    `device_turn` определены в `Vector_main.c`, `COUNT_CHAN` = 10
    (`shared_types.h`, решение автора), макросы статусов/таймеров есть в
    `shared_macros.h`, `FIRMWARE_VERSION` для `Dev1` задан. Осталось до
    включения:
    * определить `USART_BLE` / `USART_RF` в `main.h` (п.13);
    * решить порт LoRa: `USART_LORA = huart4` конфликтует с COM/логом (п.27);
    * перепроверить сборку с `CONFIG_LORA 1` / `CONFIG_BLE 1`
      (gcc -fsyntax-only), затем на живом железе.
26. **[закрыт: ]** `CONFIG_GPS 1` собирается: типы и константы — в
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
29. `TFT_indicator.c` (2783 строки индикации Avis) выключен (`CONFIG_TYPE_LCD_TFT == 2`, макрос объединён автором: 0 = выкл, 1 = драйвер панели, 2 = слой Avis) и без переноса не соберётся:
    нужны `SNS_CFG_Type`, `CALIB_CFG`, `COUNT_CHAN`, значения `DEVICE_NUMBER` (Device2/Device3_Pro) и функции Avis
    (`Avis_Run_Temp`, `DisplayIndication`, `Button_Run`). Из `TFT.h` он больше не включается — драйвер панели от слоя
    индикации не зависит.
30. **[частично закрыт]** Графика не воспроизводится из git: UI SquareLine Studio теперь В РЕПОЗИТОРИИ —
    `Drivers/ui_Vector/` (`.gitignore`: `/Drivers/*` + `!/Drivers/ui_Vector/` — рабочий вариант исключения),
    include path `../Drivers/ui_Vector` добавлен в `.cproject` (без него сборка падала:
    `TFT.h:25: fatal error: ui.h: No such file or directory`). Осталось: `Drivers/lvgl` (9.2, пак CubeMX) и
    `lv_conf.h` в git не лежат — зафиксировать версии в общем хранилище/репозитории.
31. `LVGL Task`: стек 4096 байт для LVGL 9 мал (нужно 8–16 КБ), тело потока — заглушка (спит 1 с), порт дисплея и
    `lv_tick` не настроены. Шаблон — `docs/LVGL.md` раздел 4.
32. `flash_write_calibration_safe()` и `Avis_Run_Temp()` в проекте не определены: калибровка температуры
    выключена (`VECTOR_BME_CALIBRATION 0`), `Vector_Search_Temp_Start()` — weak-заглушка (переименована с
    `Avis_Search_Temp_Start`). `TIMER_RTC_*` и `COUNT_CHAN` **[исправлено в ]**: таймеры прибора перенесены
    (см. п.33), `COUNT_CHAN` = 10 (решение автора).
33. **Ошибки в пуше с таймерами (исправлены ранее )**

 | Что | Почему ломалось | Как исправлено |
 |---|---|---|
 | `Countdown_Timer_Chan()` в `main.c` | обращалась к `COUNT_CHAN`, `COUNT_TIMERS_CHAN`, `countdown_time_chan[]`, `END_TIMER_CH` — их автор удалил из `shared_types.h` вместе с каналами | функция удалена: каналов измерения на этом приборе нет |
 | `button.button_flag` | экземпляр называется `button1` (`stm32u5xx_it.c`) | `button1` в `Timer_Tick_1ms()` |
 | `Uart_Command_Receive_Timer_Inc()` | реализации нет (модуль `command_message` ещё не перенесён) | `__attribute__((weak))` заглушка в `Vector_main.c` |
 | `timer`, `countdown_time`, `countdown_time_rtc` без `volatile` | пишет прерывание, читают потоки: на `-O2` флаги кэшируются в регистрах и такты теряются | `volatile` в объявлениях и определениях |
 | `Timer_variables` объявлен в `stm32u5xx_it.h` | тип жил в заголовке прерываний, экземпляры и обслуживание — в двух разных файлах | тип и `extern` — в `shared_types.h`, экземпляры и `Timer_Tick_*` — в `Vector_main.c` |
 | `TIMER_RTC_GPS_DATA_INIT`, `TIMER_RTC_LORA_DATA_SET_ALARM`, `TIME_RTC_*` | используются в `Gps_Run`/`Lora_Run`, но нигде не определены | добавлены в `shared_types.h` (длительности стартовые — подобрать под модули) |
 | `Lora_Run()` вставлен вариантом `Lora_g_*` | драйвер `Lora_g` (`CONFIG_LORA_G`) в этом проекте отсутствует | переписан под имеющийся `Lora_S7678S.c` (`Lora_Init/Lora_DataSet/Lora_DeInit/Lora_IsTxBusy/get_state_*_lora`) по образцу `Lora_Run()` из `Avis_main.c`; джиттер периода через `rand()` |
 | `COUNT_CHAN` не определён | `Lora_S7678S.h`: `#define COUNT_CHAN_LORA (COUNT_CHAN)` → с `CONFIG_LORA 1` не собиралось | `#define COUNT_CHAN (10)` в `shared_types.h` (решение автора) |
 | `VECTOR_GPS_NAV_PERIOD_MS` | автор удалил макрос, а `Gps_Run()` на нём строился | `Gps_Run()` переведён на секундные таймеры прибора |
 | `VECTOR_TASKS_MEASURE_PERIOD_MS` | период задавал поток, хотя такт 1 с уже даёт TIM3 | макрос удалён, `Measure Task` работает по `timer.flag_1s` |
 | `working_hours += 1` в колбэке RTC | в Avis единица поля — 937.5 мкс, такт здесь — 1 с | **решение автора **: на этом приборе часы в секундах, `+= 1` оставлен; при сохранении/загрузке конфига единицы с Avis не смешивать (`working_hours_offset` — смещение, не часы) |
34. **[закрыт]** Пуш с `Config_save_read.c` не собирался: файл перенесён
    из GA/Avis почти как есть — `#include "Ga_main.h"` (нет в проекте),
    `GetPage/GetBank`, `reg_mem`, `Save_param_memory/save_log_memory/
    ultra_deep_power_down_memory`, `EVENT_LOG/EVENT_LOG_TYPE_2`, `SIZE_LOG`,
    `ST_COMMON_BIT_ERR_AT45`, `BAT_*_DEF`, `TIME_RTC_LED_STATE_*`, поля
    `SNS_CFG` (`Archiveinterval`, `Bump_interval`, `DataLastBumpTest`,
    `CurrentAddrFile`, `Type_lcd`, `TimeLimit*`) — ничего этого не было.
    модуль переписан под extstore (конфиг/журнал — внешняя flash),
    заводская страница — внутренняя flash (окно 64 Б, read-modify-write,
    U5: страница 8 КБ, quadword 16 Б), недостающие поля/макросы добавлены в
    `shared_types.h`, загрузка конфига включена в `Vector_Run_Pre_Init()`,
    запись — только из `Vector_RunFlashMemory()`.
35. **[закрыт]** ШИМ подсветки: TIM8 настроен автором в кубе (PC6 =
    TIM8_CH1 AF3, PSC 159 / ARR 39 → 25 кГц), но `backlight_set()` считал
    скважность от `VECTOR_LCD_PWM_PERIOD = 249` → при percent ≥ 16 CCR
    превышал ARR и подсветка всегда горела на 100%; `htim8` не был объявлен
    (нет `tim.h` в `LCD_platform.c`). duty от фактического ARR,
    `#include "tim.h"`, убран `(void)duty` и устаревшие TODO-комментарии.
36. **[закрыт]** `MIRAX_BACS_BUILD` = `current_build_type` — переменная
    рантайма, но `TFT_indicator.c` сравнивал её в `#if` (препроцессор
    превращал неопределённый идентификатор в 0 → ветка MIRAX всегда).
    сравнение в рантайме, предупреждение в `config_device.h`.
37. `[.ld]` `FLASH LENGTH = 1016K` защищает заводскую страницу
    0x080FE000 от кода/констант. **CubeMX регенерирует `.ld` и вернёт 1024K**
    — после каждой перегенерации проверять LENGTH (либо освободить страницу,
    сдвинув адреса заводских данных, что хуже).
38. Сохранение/чтение конфига проверено только компилятором: на железе
    проверить — первый пуск (чистая внешняя flash → `DefaultConfig` + запись +
    лог "config: load/CRC fail"), повторный пуск ("config: loaded",
    `working_hours` продолжается с сохранённого значения), `SaveConfig=1` из отладчика → "config:
    saved", журнал: `SaveEvent`, `ext_dbg_log_sector/_offset`, `CurrentAddrFile`.
    `TYPE_TURN_OFF` не пишется — нет обработчика выключения (этап 6).
39. **[закрыт]** Два TODO о блокировке на шине внешней flash: в
    `Vector_RunFlashMemory()` («будет зависание, если все флаги сработают…
    поток зависнет») и в `extstore.c` над `lock()` («бесконечный
    TX_WAIT_FOREVER делать не хорошо»). Причина: журнал/конфиг и звук читают
    и пишут одну SPI1-шину, мьютекс брался навечно, а стирание сектора
    (40–100 мс) могло попасть на playing звук и сорвать дедлайн дозагрузки
    (93 мс). схема «окно тишины» (решение автора, см. раздел 6):
    `Vector_RunFlashMemory()` не заходит во flash, пока `audio_is_busy()`,
    флаги/архивный таймер не гасятся и повторяются на следующем проходе
    (50 мс); `ext_mtx` берётся с ограниченным ожиданием (чтения —
    `VECTOR_EXT_LOCK_READ_MS` 250 мс, записи — try-lock, занято →
    `EXT_BUSY (-2)`); стирания не стартуют под звучащий звук (проверка в
    `extstore`, счётчик `ext_dbg_erase_defer`), звук всегда доигрывает до
    конца, форс-стирания нет; `ext_mtx` создан с `TX_INHERIT`; сон ретрая
    `ext_read` вынесен из-под мьютекса; `save_param/save_event` возвращают
    `int`, флаги гасятся только по успеху (жёсткая ошибка →
    `tasks_status.cnt_flash_errors`, без ретраев 20 раз/с); `compare_param()`
    больше не пишет журнал из потока приёма — взводит `SaveEvent`/
    `TypeLogEvent`. Проверено компилятором; на железе проверить п.40.
40. «Окно тишины»  проверено только компилятором: на живом железе
    проверить — звук по кругу (`VECTOR_AUDIO_BOOT_PLAY 1`) + `SaveEvent=1` из
    отладчика: запись проходит в паузу повтора, `cnt_flash_defers` растёт
    во время звука и останавливается в паузу, `audio_status.cnt_underruns`
    НЕ растёт на ротации сектора журнала (`ext_dbg_erase_defer` > 0);
    `SaveConfig=1` → "config: saved"; при занятой шине флаг остаётся
    взведённым и запись всё равно проходит позже.
41. **[закрыт]** Коммиты автора `d3c4b40`/`5f76902` (адрес области у
    `cfg_save/cfg_load`, `SF_CONFIG_DEFAULT`, чистка `SNS_CFG`) не
    собирались: `addr` не добрался до `cfg_scan()` и до вызовов
    `cfg_write_record()`; в `extstore.h` адрес объявлен `uint16_t` (0x400000
    не помещается — молчаливое обрезание до 0 = область SOUNDS), в
    `Config_save_read.h` — расхождение с определением (`uint32_t`);
    `save_param/read_param` не передавали `addr` в `cfg_save/cfg_load`;
    `Vector_main.c` использовал `SF_CONFIG_BASE/DEFAULT` без `#include
    "sfmap.h"`; `read_param(..., 0)` в `Pre_Init`/`compare_param` читал
    область SOUNDS; `DefaultConfig()` обращался к удалённым полям
    `TimeLimit/TimeLimitRST/Type_lcd`. `ReadConfigDefault` — семантика автора
    ПОДТВЕРЖДЕНА (сброс к заводским: чтение `SF_CONFIG_DEFAULT` в рабочий
    `Sns_Cfg_struct`), добавлены только лог результата и комментарий.
    всё перечисленное исправлено, `ext_ready`
    удалён (решение автора, п.3), assert-цепочка `sfmap.h` дополнена
    (CONFIG→CONFIG_DEFAULT→LOG), карта flash в AUDIO/FLASHING/SYSTEM/PLAN
    обновлена. Проверено `gcc -fsyntax-only`; на железе дополнительно к п.40:
    `SaveConfigDefault=1` → "config: saved to factory area" и запись в
    0x401000, `ReadConfigDefault=1` → рабочий `Sns_Cfg_struct` заменяется заводским из
    0x401000 (лог "config: factory defaults restored"), пустая/битая область →
    "factory area empty/invalid - kept current", рабочий конфиг не тронут;
    для закрепления сброса во flash — `SaveConfig=1` следом.
42. `docs/README.md` ссылается на `archive/FIX_REPORT.md` и
    `archive/AUDIO_MAP.md`, но папки `docs/archive/` в git нет (ни в одном
    коммите). Либо добавить файлы в репозиторий, либо убрать строки из
    таблицы документов (в README помечено, что файлов нет).
43. **[закрыт]** `working_hours_offset` в комментариях и docs назывался
    «наработком» и трактовался как накопленные часы («save_param складывает в
    него сессию» — PLAN §6 ), хотя по автору это СМЕЩЕНИЕ, а время живёт в
    `Config_common.working_hours`; сброс к заводским (`ReadConfigDefault`)
    затирал часы значением из заводской области. часы сохраняются при
    сбросе (переносятся через копию), терминология исправлена в
    `shared_types.h`, `Config_save_read.h`, `Vector_main.c`, SYSTEM.md,
    PLAN.md. На железе: набрать `working_hours` (или выставить отладчиком),
    `ReadConfigDefault=1` → лог "factory defaults restored (wh=N s kept)",
    N не сбрасывается; `SaveConfigDefault=1` записывает в 0x401000 ТЕКУЩИЙ
    конфиг вместе с часами — решить, нужно ли туда писать часы обнулёнными
    (сейчас пишутся как есть).
44. **[закрыт]** Ошибки линковки после первого выхода сборки на этап link
    (до этого сборка падала раньше — на `ui.h`):
    (а) `undefined reference to tasks_status` — определение экземпляра
    `volatile tasks_status_t tasks_status = {...}` было случайно удалено из
    `Vector_main.c` в коммите 6b3df8b (08.10); все потоки и `Vector_Run*`
    ссылаются на extern из `vector_tasks.h` — восстановлено ранее ;
    (б) `undefined reference to LCD_writeCommand8Bit/LCD_writeData8Bit` —
    `LCD_platform.c` ранее НЕ КОМПИЛИРОВАЛСЯ: guard `#if (CONFIG_TYPE_LCD_TFT)`
    не видел макрос (файл включает только `LCD_platform.h` → `main.h`/
    `vector_config.h`, а `CONFIG_TYPE_LCD_TFT` живёт в `config_device.h`;
    неопределённый макрос в `#if` = 0, тело молча выпадало). добавлен
    `#include "config_device.h"`. Попутно в первом же реальном проходе
    компилятора найден и исправлен `LCD_SPI_TIMEOUT_MS` → макрос нигде не
    определён, правильное имя `VECTOR_LCD_SPI_TIMEOUT_MS` (vector_config.h,
    4 использования). ВНИМАНИЕ: `LCD_platform.c` компилируется впервые —
    если вылезут новые ошибки (пины куба, HAL), прислать лог.
    (в) `task_module_name()` не знал про добавленный автором `TASK_MOD_SENSOR`
    (ветка возвращала "?") — добавлен case "sensor" .
45. ** (пункты 1-3 автора: кольцо журнала, данные/часы, чтение по времени)**
    реализовано: журнал — слоты 64 Б (`sizeof(EVENT_LOG)`, assert в
    `Config_save_read.c`), кольцо 0x8000 по маске 0x7FFF, указатели в
    `Config_archive.CurrentAddrFile/BegginAddrFile`; переполнение — стирание
    сектора + сдвиг начала на 64 записи; `log_read(ptr)` и
    `log_find_by_time(t_from,t_to,&ptr_from,&ptr_to)` (бинпоиск по
    `Event_log.working_hours`); измеренные данные перенесены автором из
    `SNS_CFG` в `sensors_status` — все обращения по коду перенаправлены
    (Vector_main/bme280_com/Lora_S7678S/TFT_indicator); часы при включении —
    из RTC BKP0 после чтения конфига, `Timer_Tick_1s()` зеркалит счётчик в
    BKP0 (DBP открыт в `Board_Init`); попутно: `Battery_Init()` вызывается из
    `Pre_Init` (guard исправлен CONFIG_BME→CONFIG_MAX17048), дубль полей
    `Orientation/Screen_rotation` в `sensors_status_t` убран, экземпляр
    `Connected_device` определён, `TYPE_LOG_PARAM_UPDATE_0` в `compare_param`
    заменён на `TYPE_LOG_PARAM_NOP` (enum-значения удалены автором).
    Сброс к заводским (ReadConfigDefault) сохраняет не только часы, но и
    указатели кольца CurrentAddrFile/BegginAddrFile: журнал — история, а
    не настройка; заводские указатели отмотали бы кольцо (вплоть до
    Beggin > Current и поломки инварианта стираний).
    Допущения/ограничения (подтвердить автору): «регистр RTC» = резервный
    регистр BKP0 (не календарь); время монотонно только при живой VBAT (после
    полной потери питания BKP0=0 — часы с нуля, порядок времени в старом
    журнале может нарушиться); журнал старого формата (сектора 'LOGS' старых ревизий)
    несовместим — кольцо стартует по указателям конфига, мусор старого
    формата стирается проверкой «слот пуст» на границе сектора.
    На железе: `ArchiveRecording=1` → записи `log: rec N`; `log_read` из
    отладчика по номеру; `log_find_by_time` надиапазоне; переполнение кольца →
    `BegginAddrFile += 64`; часы переживают перезагрузку (VBAT); `SaveEvent`
    по-прежнему пишется только в окно тишины.
46. **[закрыт]** Пуш `95581e4` не собирался: `Config_common.working_hours`
    (×3 — поле в `Working_data`), `RTC_ReadTime/RTC_WriteTime`,
    `save_event_type_2`, `Avis_Battery_System`, `SoftResetProgram` — не
    определены нигде. функции часов реализованы в `vector_macros.c`
    (BKP0, см. §6 «Часы в RTC BKP0 и сервис»), `save_event_type_2` — обёртка
    над `save_event` с bounded-ретраем (зовётся перед NVIC_SystemReset из
    потока приёма — осознанное исключение из правила «flash пишет только
    Measure Task»), `Avis_Battery_System` — weak-заглушка, `SoftResetProgram`
    — экземпляр в `Vector_main.c` (взводить будет COM/BLE-модуль ВМЕСТЕ с
    `START_TIMER_RTC(TIMER_RTC_SOFT_RESET_PROGRAM, N)`). На железе: выставить
    `SoftResetProgram=1` + таймер → перезагрузка с записью TYPE_FW_UPDATE;
    часы переживают NVIC_Reset и питание при живой VBAT (BKP0).
47. **ОТМЕНЕНО** (ответная сторона BLE не понадобилась: мы, как и Avis,
    хозяин своего модуля — см. п. 48 и строку «BLE и меню» в §6). Из открытого
    остались: `CONFIG_BLE` = 0 до проверки на живом модуле; `ConcentrationVal`
    пишется raw (дискретность отправителя неизвестна); состав экранов/пунктов
    меню не определён; `VECTOR_AUDIO_DEMO_KEYS 1` конфликтует с меню — для
    продукта поставить 0.
48. **Порт Ble_mk01 + страница указателей + поток индикации, открытые
    вопросы**: правки по пяти замечаниям автора (строка «BLE и меню —
    исправления по замечаниям автора» в §6).
    Открытые вопросы автору:
    (а) wire-формат ответа `ADVMSDDATAGET` — сейчас принято как у `BLEADDR?`:
    2 байта эха + hex-строка структуры (64 байта = 128 hex) + «OK»; если модуль
    отвечает иначе — поправить `Ble_ReadAnswer()` (одно место);
    (б) метка имени модуля `BLE_Label[0]` = "VEC" — предварительная (в Avis:
    "MIRAX"/"22.");
    (в) полярность `BLE_RESET` (PC12) в `Ble_SystemOff`/`Ble_DeInit` — проверить
    на железе (скопирована схема Avis: SET_OFF → 50 мс → SET_ON);
    (г) указатели архива теперь живут в `SF_LOG_PTR` (0x602000) — в конфиге
    `Config_archive.CurrentAddrFile/BegginAddrFile` остаются рабочей копией;
    при чтении конфига на старте страница указателей приоритетнее
    (`log_ptr_load` после `read_param`, как `ReadCurrentAddrFile` в Avis);
    (д) `Init_Build_Type()` читает полуслово бренда из `FLASH_ADDRESS_BUILD_TYPE`
    0x080FE000 (последняя страница, линкер LENGTH=1016K) — запись бренда при
    производстве пока не реализована (пусто = MIRAX);
    (е) стек потока LVGL Task 4096 — при подключении LVGL поднять до 8–16 КБ.
    На железе: `ble_status`/`menu_status`/`log_ptr_dbg_saves/defers` в
    Expressions; эхо-кейс AT → OK; DATAGET-опрос раз в `TIME_RTC_BLE_DATA_SET`
    (60 с); жесты → `cnt_gestures[0..2]`, приоритет B1 в SCREEN_MENU.
49. **LoRa v4 + календарный RTC, открытые вопросы**:
    (а) **Avis-Lora-protocol-v3.md** — автор предлагал прислать: сверить карту
    дискретности Setting-байта (биты 2..3; предварительно 0→1, 1→10, 2→100,
    3→1000 — нужна только для мёртвой зоны O₂ 20.85..20.95) и код кислорода
    `LORA_GAS_ID_O2 = 68` (взят из примера v4 п.8.2);
    (б) `TYPE_STATIC.DeviceID` — сейчас `DEVICE_NUMBER` (как в v3-коде автора);
    по v4 это «заводской идентификатор трекера» — кандидат `SerialLo`, решение
    за автором;
    (в) заделы без источника: `AirQualityIndex` (шлём 0), `ActivityLevel`/
    `MOTION_BIT_IMPACT`/`MOTION_BIT_INACTIVE` (шлём 0), `Radio.TxPower`
    (нет AT-геттера, шлём 0), `SUB_TYPE_PULSE` (пульсометр не подключается);
    (г) провод `ADVMSDDATAGET` — прежнее допущение (2 байта эха + hex + OK);
    скан/агрегация маяков и СИЗ — на стороне прошивки MK01 (протокол v1
    рассчитан на это; если модуль отдаёт по одному устройству — MCU копит
    списки сам, структура пакетов не меняется); UUID инфраструктуры — значение
    назначается при монтаже;
    (д) потребители v4-новинок НЕ подключены: показ `Lora_Message` на экране
    (New/`Lora_MessageShown()`, CP1251→шрифт), звуковая/экранная реакция на
    `EVACUATION` (бит 8 ControlLora16) — за индикацией/меню;
    (е) `TFT_indicator.c` — единицы BME280: показ температуры/влажности нужно
    делить на 100/1024 (int32-драйвер; см. строку в §6);
    (ж) `CONFIG_LORA` по-прежнему 0: порт UART4 конфликтует с логом (п.25/27),
    `USART_RF` не определён — включение после решения по порту;
    (з) часы: 32-битный `time_t` newlib — переполнение в 2038; `mktime` трактует
    struct tm как локальное время — TZ в проекте НЕ задавать (newlib default =
    UTC, симметрично `gmtime`);
    (и) хост-проверки `test_lora_v4` (динамический/статический кадры
    побайтово, Message/MsgAck, Control16/EVACUATION/защита SOS, контейнерный
    EventLog+ECHO, RTC round-trip, ACK/TURN_OFF), `test_ble` (legacy Avis 6+5N,
    BEACONS/PPE/SUBDEV/AGG, чужие версии, ошибки/таймауты) — ВСЕ ЗЕЛЁНЫЕ;
    синтаксис всех затронутых файлов проверен в тени CONFIG_BLE=1+CONFIG_LORA=1
    и в дефолтной сборке.
50. **Спецификации согласованы — КОД НЕ ПРАВЛЕН** (всё зафиксировано в
    `docs/Mirax-Ble-protocol-v1.md` и `docs/LOG_FORMAT.md`, сводка — строка
    «Протоколы и журнал» в §6, задачи — Этап 1/1b в §5):
    (а) `Ble_vector.c/h` реализует ПРЕЖНЮЮ редакцию протокола (DATAGET с
    Header-байтом, BEACONS/PPE/SUBDEV/AGG) — подлежит переписыванию под новую
    спеку (MAC-слоты с типом, `MACSET`, `MSDGETALL`/`MSDGET`, нормализованная
    запись маяка {ShortID,Vbatt} — Eddystone парсит модуль, `+MBUS:`/
    `MBUSRSP`, буфер приёма 640); прошивки MK01-сканера не существует — спека
    в §4 протокола;
    (б) журнал: текущий `EVENT_LOG` 64 Б берёт время/снимок В МОМЕНТ ЗАПИСИ,
    отложенное событие однослотовое (`SaveEvent`/`TypeLogEvent` — потеря при
    всплеске) — заменяется по `docs/LOG_FORMAT.md` (40 Б, очередь со снимком
    на момент события, 102 записи/сектор, типы SNAPSHOT/READINGS/GAS/PPE/POWER);
    (в) `Vector_RunFlashMemory()` откладывает под звуком ВСЕ записи (сплошной
    гейт `audio_is_busy()`) — program следует разрешить под звуком (1–6 мс
    поглощаются 93-мс окном дозагрузки плеера), erase оставить в «окне тишины»;
    (г) исторические строки §6, пп. 47–49 и комментарии кода (`Ble_vector.h`,
    `shared_types.h`, `Config_save_read.h`) намеренно НЕ правились — они
    описывают текущий код; вычистим при переписывании кода.
51. **Дубли типов между модулями**: `ble_beacon_t` был определён дважды
    (`Lora_S7678S.h` и `shared_types.h`) — **исправлено** (единственное
    определение в `shared_types.h`, `Lora_S7678S.h` его включает; правило
    слоёв — строка «Общие типы vs структуры модулей» в §6).
    `channel_cfg_t` — дубль в `TFT_indicator.c` (локальная копия Avis); файл
    выведен из сборки (`CONFIG_TYPE_LCD_TFT`), при переносе индикации копию
    убрать в пользу общего определения.

52. **[ВРЕМЕННО] Тест «звук + журнал» (snd_log_test)**: по просьбе автора
    добавлен временный тест совместной работы звука и записи журнала
    (`VECTOR_SND_LOG_TEST=1` в `vector_config.h`, код
    `Core/VectorLib/Src/Test/*/snd_log_test.*`, хуки помечены
    «[ВРЕМЕННЫЙ ТЕСТ]» в `Vector_main.h`/`Vector_main.c`): громкость 0%,
    `SND_ZVONOK_1` зациклен с паузой 1 с, каждую секунду взводится
    `TYPE_LOG_PARAM_NOP -> SaveEvent` (запись штатным
    `Vector_RunFlashMemory` в окно тишины), раз в 5 с отчёт в терминал
    (VLOG_M_TASKS): req/lost/written/defers/errs, `ext_dbg_*`,
    контрольное `log_read` последней записи; watchdog перезапускает звук,
    если цикл встал (демо-кнопка PB2). Критерий успеха: `written ≈ req −
    lost`, `errs=0`, `last rc=0`, `erase_def` растёт при стираниях во время
    звука. **ПОСЛЕ ПРОВЕРКИ УДАЛИТЬ**: макроблок в `vector_config.h`,
    include в `Vector_main.h`, вызов в `Vector_main.c`, оба файла теста
    (и этот пункт).
