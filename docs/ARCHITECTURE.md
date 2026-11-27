# Архитектура: блоки, потоки, периферия

Картинка: **[architecture.svg](architecture.svg)** (открывать в браузере/IDE; в
git лежит рядом). Этот файл — табличная версия той же схемы. Сверяйте значения
переключателей по [`vector_config.h`](../Core/VectorLib/Inc/vector_config.h) и
[`config_device.h`](../Core/VectorLib/Inc/config_device.h), состояние работ и
дефекты — по `PLAN.md` §5/§7.

---

## 0. Текущая конфигурация сборки

| Переключатель | Значение | Что это значит |
|---|---|---|
| `CONFIG_GPS` | **1** | GPS/GNSS на USART1 (PB6/PB7), байты из ISR → `Gps_Data_Verification()`, разбор — `Gps_Receive()` в Receiver Task |
| `CONFIG_BME` / `CONFIG_MAX17048` | **1** | BME280 (0x76) и MAX17048 (0x36) на I2C1 |
| `CONFIG_LIS3DH` | **0** | LIS3DH (0x19) выключен: EXTI11, 6D-ориентация и детектор движения в сборку не входят |
| `CONFIG_UART` | **1** | COM/Modbus RTU на **UART5** (`USART_COM`), кольцо `TYPE_USART` |
| `CONFIG_BLE` | **0** | BLE Task не создаётся; код [`Ble_vector.c`](../Core/VectorLib/Src/Ble_vector.c) есть, но реализует прежнюю редакцию протокола |
| `CONFIG_LORA` | **0** | LoRa Task не создаётся; `USART_RF` = UART4 |
| `CONFIG_G4` | **0** | LTE Task не создаётся |
| `CONFIG_DISPLAY` / `CONFIG_TYPE_LCD_TFT` | **1 / 1** | Меню и индикация включены. При `1` компилируются И драйвер панели (`TFT.c`, `LCD_platform.c`), И слой индикации Avis (`TFT_indicator.c` — guard `== 1`) |
| `VECTOR_LCD_USE_LVGL` | **1** | Порт LVGL живёт в `TFT_indicator.c` (`ui_lv_init()`), `lv_timer_handler()` — в LVGL Task |
| `VECTOR_LOG_ENABLE` | **0** | Лог выключен на этапе компиляции: `LOG_*` = `((void)0)` |
| `VECTOR_LOG_UART` | **3** | При включённом логе порт — **USART3** (он же порт BLE), ITM — вкл. |
| `VECTOR_AUDIO_DEMO_KEYS` | **0** | Демо-кнопки плеера выключены, кнопки отданы меню |
| `VECTOR_AUDIO_STREAM` / `_CHUNK` / `_SMALLBUF` | **1 / 4096 / 1** | Стриминг: `ap_buf` = 2 × 4096 сэмплов = 16 КБ, запас дозагрузки 93 мс |
| `VECTOR_SF_READ_RETRY` | **3** | Повтор сбойного чтения внешней flash |
| `VECTOR_EXT_LOCK_READ_MS` / `_WRITE_MS` | **250 / 0** | Чтения ждут шину до 250 мс, записи — try-lock (`EXT_BUSY`) |

---

## 1. Потоки: кто чем управляет

Приоритеты ThreadX: меньше число = выше приоритет. Стеки — в байтах.
Все потоки создаются в `vector_tasks_init()` из `tx_application_define()`;
потоки модулей — только при своих `CONFIG_*`.

| Поток | Прио/стек | Ритм | Управляет | Читает | Пишет |
|---|---|---|---|---|---|
| **Audio Player** (`ap_thread_entry`) | 10 / 4096 | событие `ap_wake` (половина DMA, команда, heartbeat 1000 мс) | SAI1_A + GPDMA1 Ch11 + `ap_buf[2×4096]`, громкость, повтор (пауза 500 мс = «окно тишины») | внешняя flash SOUNDS через `ext_read` (DMA-куски) | `audio_status` |
| **BLE Task** (`ble_task_function`) — только `CONFIG_BLE` | 11 / 2048 | 5 мс | `Ble_Run()`: INIT/SET/SLEEP-таймеры, `ADVMSDDATAGET`, мосты `Ble_UpdateInputData()` / `Ble_Control_States_Update()` | кольцо `TYPE_BLE` читает `Ble_TransmitReceive`; USART3 | `Connected_device`, `Ble_telemetry`, `ble_status`, `ST_COMMON_BIT_ERR_BLE` |
| **Receiver Task** (`receiver_task_function`) | 12 / 2048 | 10 мс | `command_message()` (UART5/Modbus RTU), `Lora_Receive()` (`CONFIG_LORA`), `Ble_Receive()` (пустая заглушка), `Lte_Receive()` (`CONFIG_G4`), `Gps_Receive()` (`CONFIG_GPS`) | кольца `InputBuffer[TYPE_*]` (SPSC); GPS — байты из ISR напрямую в `Gps.c` | `tasks_status.mod[]`, флаги/ответы COM |
| **LoRa Task** (`lora_task_function`) — только `CONFIG_LORA` | 12 / 2048 | 10 мс | `Lora_Run()`: `Lora_UpdateInputData()`, ритм `TIMER_RTC_LORA_DATA_*`, сборка/передача | — (приём — Receiver Task) | `StateErr` (под `Sys_Cfg_Lock`), `tasks_status.mod[TASK_MOD_LORA]` |
| **LTE Task** (`lte_task_function`) — только `CONFIG_G4` | 12 / 2048 | 50 мс | `Lte_Run()` (weak-заглушка, AT-машина не написана) | — (приём — Receiver Task) | `tasks_status.mod[TASK_MOD_LTE]` |
| **Measure Task** (`measure_task_function`) | 13 / 4096 | 50 мс; `Vector_Run_Measure()` — по `timer.flag_1s` | `Vector_Run_Pre_Init()` один раз; `sensors_read()` (I2C1) + `Gps_Run()`; `Vector_RunFlashMemory()` (журнал v2 + конфиг); `Vector_Options_System()`; каждые 100 мс `lis3dh_motion_poll()` (при `CONFIG_LIS3DH`) + `Alert_Tick()` | `timer.flag_1s`, флаги `cmd_state`, I2C1 | `sensors_status`, `Sns_Cfg_struct`, внешняя flash (LOG/CONFIG/LOG_PTR), `tasks_status` |
| **LVGL Task** (`lvgl_task_function`) | 15 / 4096 | 10 мс; `timer.flag_100ms` | по `flag_100ms` — `Vector_Run_Indication()` (в ON — `Menu_Run()`, в OFF — `Button_Run_Start()`); каждый цикл — `Alert_IndicationTick()`; при `TFT_Initialized == 1` — `lv_timer_handler()` | `timer.flag_100ms`, `button1/2/3`, `sys_state.power` | виджеты LVGL/SquareLine, `menu_status`, фазы свет/звук/вибро |
| ThreadX idle | — | — | WFI (опция `TX_ENABLE_WFI` в `tx_user.h` **не задана**) | — | — |

Особенности:

* Receiver/Measure/LVGL Task стартуют всегда; BLE/LoRa/LTE — по `CONFIG_*`.
* Receiver Task и BLE Task ждут снятия `DEVICE_TURNED_OFF_PRE_INIT`: до конца
  `Vector_Run_Pre_Init()` рабочие циклы не начинаются.
* `Vector_Options_System()` (сервис: soft reset, батарея, сверка RTC, конец
  обмена) живёт в **Measure Task**, а не в Receiver.
* `Alert_Tick()` (реестр статусов) — Measure Task 100 мс; `Alert_IndicationTick()`
  (фазы свет/звук/вибро) — LVGL Task каждые ~10 мс.

---

## 2. Прерывания → потоки

| IRQ | Что делает | Кто продолжает |
|---|---|---|
| TIM3 1 кГц | `Timer_Tick_1ms()`: `countdown_time[]`, флаги `flag_1ms/10ms/100ms/1s`, счётчики `button1/2/3` и `button_sos`, таймауты кадров COM/GPS/LoRa | Measure (1 с), LVGL Task (100 мс), декодер меню |
| RTC wakeup 1 Гц | `Timer_Tick_1s()`: `working_hours += 1`, `flag_start_work`, `flag_start_work_bat_sys`, `countdown_time_rtc[]` | Measure/Receiver (флаги), сверка RTC в `Vector_Options_System()` |
| EXTI1 / EXTI2 / EXTI3 | `BUTTON_3` (PB3) / `BUTTON_1` (PB1) / `BUTTON_2` (PB2): фронт ставит `button_flag`, обнуляет `button_count`; `button_sos` — зеркало Button1 со своим счётчиком | `Menu_Run()` (LVGL Task), `Button_Run_Backlight()` |
| EXTI8 (PC8 CHARGE_STATE) | `HAL_GPIO_EXTI_IRQHandler(CHARGE_STATE_Pin)`; логики батареи нет (IRQ в NVIC не включён, `Battery_System` — weak) | — |
| EXTI11 (PC11 ACCEL_INT) | `lis3dh_irq_handler()` — только флаг; **сейчас выключено** (`CONFIG_LIS3DH = 0`) | Measure: `sensors_read()` → `TFT_Rotation()` |
| GPDMA1 Ch11 + SAI1 | половина буфера сыграна → `Audio_Out_OnHalfBlock/OnBlockDone` → флаги + `tx_semaphore_put(ap_wake)` | Audio Player: `stream_fill()` (дедлайн 93 мс) |
| GPDMA1 Ch10 + SPI1 | конец DMA-чтения flash → `tx_semaphore_put(sf_dma_sem)` | тот, кто читал (Audio/Measure, под `ext_mtx`) |
| GPDMA1 Ch8 + SPI2 | кадр LCD выдан → `LCD_transferCpltCallback()` → `lv_display_flush_ready()` | LVGL Task |
| USART1 (GPS) | байт → `Gps_Data_Verification()` | Receiver Task: `Gps_Receive()` |
| USART2 | байт → `add_to_buffer(TYPE_LORA)` при `CONFIG_LORA` (порт помечен как LTE — несоответствие, PLAN п.24) | Receiver Task |
| USART3 | приём **не сохраняется** (`add_to_buffer(TYPE_BLE)` закомментирован); при `CONFIG_BLE` включать | BLE Task (кольцо `TYPE_BLE`) |
| UART4 | байт → `add_to_buffer(TYPE_LORA)` при `CONFIG_LORA` (`USART_RF`) | Receiver Task: `Lora_Receive()` |
| UART5 | байт → `add_to_buffer(TYPE_USART)` + `ST_COMMON_BIT_DATA_EXCHANGE` + таймер окончания обмена | Receiver Task: `command_message()` |
| TIM6 1 кГц | только `HAL_IncTick()` — внутренняя тайм-база HAL | драйверы HAL |

> Порт ThreadX для Cortex-M33 маскирует критические секции через **PRIMASK**,
> поэтому `tx_*` из ISR допустимы при любом приоритете. Но приоритет 0 у EXTI
> означает, что кнопка может отложить DMA-звук.

---

## 3. Периферия и память

| Ресурс | Назначение | Владелец/защита |
|---|---|---|
| SAI1_A + GPDMA1 Ch11 (circular) | звук 44.1 кГц / 16 бит / моно → MAX98357A (SD_MODE PC9) | Audio Player (единственный) |
| SPI1 20 МГц + GPDMA Ch9 (TX) / Ch10 (RX) | внешняя flash MX25R6435F 8 МБ | `extstore`, `ext_mtx` (TX_INHERIT, bounded read / try-lock write, `EXT_BUSY`); стирание — только в «окно тишины» (`audio_is_busy()==0`) |
| SPI2 + GPDMA Ch8 | ST7789P3 172×320 | `LCD_platform`; ⚠ в кубе prescaler /2 = 80 МГц > spec, поставить /4../8 |
| I2C1 | BME280 0x76, LIS3DH 0x19 (`CONFIG_LIS3DH=0`), MAX17048 0x36 | Measure Task (один владелец) |
| USART1 | GPS (работает) | Receiver Task / `Gps.c` |
| USART2 | LTE (`CONFIG_G4=0`); ISR при `CONFIG_LORA` кладёт байты в `TYPE_LORA` | Receiver Task |
| USART3 | BLE (`CONFIG_BLE=0`); при включённом логе — `VECTOR_LOG_UART=3` | BLE Task (кольцо `TYPE_BLE`) |
| UART4 | LoRa (`USART_RF`, `CONFIG_LORA=0`) | LoRa Task / Receiver Task |
| UART5 | COM/Modbus RTU (`USART_COM`) + кольцо `TYPE_USART` | Receiver Task |
| TIM3 / TIM6 / TIM8_CH1 / RTC+BKP0 | такты прибора / тайм-база HAL / ШИМ подсветки 25 кГц / календарь и BKP0 | `main_vector.c`; DBP открыт в `Vector_Run_Board_Init()` |
| SRAM4 16 КБ | `buffer_transmit[]`, такт включает `Vector_Run_Board_Init()` | `.ld` секция `.sram4`; для сна — SRAM4PD |
| `ap_buf` | 2 × 4096 сэмплов = 16 КБ (стриминг) | Audio Player |

### Карта внешней flash (`sfmap.h`)

| Область | Адрес | Размер | Назначение |
|---|---|---|---|
| SOUNDS | 0x000000 | 4 МБ | `sounds.bin` (пишет только программатор) |
| CONFIG | 0x400000 | 4 КБ | рабочий конфиг `SNS_CFG` (`read_param`/`save_param`) |
| CONFIG_DEFAULT | 0x401000 | 4 КБ | заводской конфиг (сброс к заводским) |
| LOG | 0x402000 | 2 МБ | кольцевой журнал v2 |
| LOG_PTR | 0x602000 | 4 КБ | страница-кольцо указателей архива (16 Б запись) |
| SF_APP_STAGE | 0x703000 | 20 КБ | образ бутлоадера для `0x0F` (`CV_TGT_FW`) |
| резерв | 0x705000.. | ~1 МБ | — |

Журнал v2 (подробно — `LOG_FORMAT.md`):

* слот **40 Б** = заголовок 12 Б (`working_hours` на +4, `CurrentAddrFile` на +8)
  + payload 28 Б; все записи одной длины;
* в секторе 4 КБ — **102 слота** (4080 Б, хвост 16 Б не используется);
* кольцо — **`LOG_RING_SLOTS = 32767`** записей; физическая позиция
  `phys = ptr % 32767`, адрес слота:
  `base + (phys / 102) * 4096 + (phys % 102) * 40`;
* доступно без затирания — `LOG_USABLE_SLOTS = 32767 - 102 = 32665` записей
  (один сектор запаса);
* переполнение: на границе сектора стирание сектора + `BegginAddrFile += 102`;
* RAM-очередь **16 × 40 Б = 640 Б** со снимком на момент события:
  `log_post(type)` / `log_archive_tick()` ставят, `log_flush()` переносит;
* `program` (1–6 мс) разрешён под звуком, `erase` ждёт «окно тишины»
  (`EXT_BUSY` → запись остаётся в очереди и повторяется через 50 мс);
* `LOG_PTR` сохраняется **один раз на пачку** записей;
* чтение: `log_read(ptr)`, поиск по времени — `log_find_by_time(t_from,t_to)`;
* миграции со старого формата (слот 64 Б) нет: при первом старте кольцо
  переинициализируется.

Внутренняя flash: код ≤ **1016K** (`.ld`, после регенерации CubeMX проверять),
последняя страница `0x080FE000` — калибровка T / бренд / LoRa AppKey.

---

## 4. Состояние блоков

| Блок | Состояние в коде |
|---|---|
| **Звук** | Работает: стриминг через circular DMA, громкость, повтор, watchdog, блокировка звука (`audio_sound_block_*`) |
| **Журнал v2** | Работает: RAM-очередь, `log_flush`, `LOG_PTR`, чтение и поиск по времени |
| **COM/Modbus RTU** | Работает: `Command_vector.c` — slave-адрес = серийник % 100, регистры 0x03/0x04/0x06/0x10, журнал 0x0C/0x14, запись 0x0E/0x0F; потребитель — Receiver Task |
| **Меню и индикация** | Основа и отрисовка готовы: `Menu_vector.c` — экраны `screen_t`, жесты B1>B2>B3, авто-возврат, `Menu_Draw()` **не weak** (рисует через `LCD_*` из `TFT_indicator.c`); состав экранов/пунктов уточняет автор |
| **LVGL/UI** | `ui_lv_init()` (lv_init, дисплей 172×320, flush → `TFT_FlushBuffer`, `lv_tick_set_cb(VTICK_MS)`, `ui_init()`, `lv_i18n`) вызывается в конце `Vector_Run_Pre_Init()` до потоков; `lv_timer_handler()` — в LVGL Task |
| **BLE** | Код `Ble_vector.c/h` есть (порт `Ble_mk01.c`, опрос `ADVMSDDATAGET`, `Connected_device`/`Ble_telemetry`), но по **прежней** редакции протокола; действующая спека — `Mirax-Ble-protocol-v1.md`; `CONFIG_BLE=0` |
| **LoRa** | Драйвер и протокол v4 есть, `CONFIG_LORA=0`; `USART_RF` = UART4 |
| **LTE** | Только каркас потока и weak-заглушки, `CONFIG_G4=0` |
| **Батарея** | `Battery_System()` — weak-заглушка; EXTI8 (PC8) без логики; пороги и статусы заряда считает `Alert_vector.c` по MAX17048 |
| **Сон** | Не реализован (`CONFIG_SLEEP=1`, но Stop2/Standby не написаны; PLAN §5 — вероятно, не будет) |
| **Каналы измерения** | Типы `SNS_Dev_Type/SNS_DevChan_Type` и экземпляр `Connected_device` есть, логика не наполнена |

---

## 5. Сквозные цепочки

```
Старт: main → Vector_Run_Board_Init (DBP/BKP0, SD_MODE, sf_probe, selftest)
       → tx_application_define: vlog_init → ext_init → audio_init (load_image)
       → vector_tasks_init (Receiver/Measure/LVGL + модульные потоки)
       → планировщик

Время: RTC wakeup 1 Гц → working_hours += 1, флаги; 1/с Measure
       (Vector_Options_System) сверяет RTC_ReadTime() и RTC_WriteTime()
       Старт Pre_Init: read_param(0x400000) → Init_Build_Type()
       → log_ptr_load(0x602000) → working_hours = RTC_ReadTime()
       → START_TIMER_RTC(TIMER_RTC_LOG) → Menu_Init/Alert_Init/ui_lv_init
       → sys_state.power = DEVICE_TURNED_OFF (включение — Button3 LONG)

Журнал: log_post(TYPE_*) / log_archive_tick (SNAPSHOT+READINGS+PPE)
       → RAM-очередь 16×40 → Measure: log_flush → слот 40 Б
       → LOG_PTR (16 Б, один раз на пачку)
       → переполнение: erase сектора + BegginAddrFile += 102
       → чтение: log_read(ptr) / log_find_by_time(t1,t2)

Индикация: LVGL Task: flag_100ms → Vector_Run_Indication
       → ON: Menu_Run (жесты B1>B2>B3 → навигация → Menu_Draw)
       → OFF: Button_Run_Start (Button3 LONG: LCD_Init + AlertSeq_PowerOn)
       → Alert_IndicationTick каждые 10 мс (свет/звук/вибро)

BLE (CONFIG_BLE): BLE Task 5 мс → Ble_Run → INIT/SET/SLEEP
       → Ble_MsdDataGet (ADVMSDDATAGET) → Connected_device/Ble_telemetry

Звук: EXTI/команда → audio_play[_now] → ap_queue + ap_wake
       → Audio Player: ext_read 8 КБ (ретраи 3) → громкость
       → SAI-DMA circular (2×4096 сэмплов, дедлайн 93 мс)
```

---

## 6. COM-протокол: Modbus RTU (UART5)

Слейв-адрес: `((SerialHi << 16) | SerialLo) % 100`, `0 → 100`. Принимаются
адрес устройства, broadcast `0x00` и `0xFF`; на broadcast контроллер
**отвечает**. Кадр без совпадения адреса игнорируется.

| Код | Назначение | Запрос | Ответ |
|---|---|---|---|
| `0x03` / `0x04` | чтение holding / input | 8 Б | `[slave, op, byte_count, data…, CRC]` |
| `0x06` | запись одного регистра | 8 Б | эхо 6 Б |
| `0x10` | запись нескольких регистров | `7 + 2·cnt + 2` | `[slave, op, addr, cnt, CRC]` |
| `0x0C` | чтение журнала | 8 Б: `slave, 0x0C, ptr u16 BE, cnt u16 BE, CRC` | `[slave, 0x0C, cnt, cnt × 40 Б, CRC]` |
| `0x14` | поиск журнала по времени | 12 Б: `slave, 0x14, t_from u32 BE, t_to u32 BE, CRC` | `[slave, 0x14, ptr_from u32 LE, ptr_to u32 LE, CRC]` |
| `0x0E` | запись во внутреннюю flash (образ бутлоадера) | `slave, 0x0E, addr u32 BE, size u8, data, CRC` | эхо 6 Б (успех) либо Modbus exception |
| `0x0F` | запись во внешнюю flash (SOUNDS / SF_APP_STAGE) | `slave, 0x0F, offset u32 BE, size u8, data, CRC` | эхо 6 Б (успех) либо Modbus exception |

Особенности текущей реализации:

* **0x0C**: `ptr` — монотонный номер записи; физический адрес считает
  `log_slot_addr_v2()`; `cnt` ограничен размером ответа (payload ≤ 3000 Б),
  ответ = `3 + cnt*40` без CRC.
* **0x0E**: адрес **абсолютный** `0x0800xxxx`, `size` кратен 16 (quad-word U5),
  требуется `flash_key != 0` и `boot_flash_allowed()`; запись **синхронная** в
  Receiver Task, занятость — `CV_ST_BUSY` (регистр 0x004E).
* **0x0F**: адрес — **смещение** внутри области (`CV_TGT_SOUND` → SOUNDS,
  `CV_TGT_FW` → SF_APP_STAGE), после записи выполняется проверка чтением
  (`CV_ST_VERIFY` отличается от `CV_ST_IO`); `EXT_BUSY` → `CMD_ERR_BUSY`.
* Блокировку звука мастер ставит сам регистром `0x0058` (автоматической нет).
* Неизвестный код функции **игнорируется** — ответа нет (мастер получает
  таймаут).
* Указатель журнала — монотонный `uint32_t`; кольцо `LOG_RING_SLOTS = 32767`,
  физический индекс = `ptr % 32767`.
