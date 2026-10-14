# Архитектура: блоки, потоки, периферия (v26)

Картинка: **[architecture.svg](architecture.svg)** (открывать в браузере/IDE; в
git лежит рядом). Этот файл — табличная версия той же схемы + список того, что
добавляем. Актуальность проверяйте по PLAN.md §5 (план работ) и §7 (дефекты).

---

## 1. Потоки: кто чем управляет

| Поток | Прио/стек | Ритм | Управляет | Читает | Пишет |
|---|---|---|---|---|---|
| **Audio Player** (`ap_thread_entry`) | 10 / 4 КБ | событие (`ap_wake`: DMA-половина, команда, heartbeat 1 с) | SAI1_A + GPDMA Ch11 + `ap_buf[2×4096]`, громкость, повтор (пауза 500 мс = «окно тишины») | внешняя flash SOUNDS через `ext_read` (DMA-куски) | `audio_status`; кольца не трогает |
| **Receiver Task** | 12 / 2 КБ | 10 мс | парсинг всех UART: `command_message`*, `Lora/Ble/Lte/Gps_Receive`, `Uart_Channel_Receive`*; сервис `Vector_Options_System` (SoftReset→NVIC_Reset, 1/с `Avis_Battery_System`* + сверка часов `RTC_WriteTime`) | кольца `InputBuffer[TYPE_*]` (SPSC, единственный потребитель; GPS — исключение, байты идут в `Gps.c` из ISR) | флаги (`SaveConfig/SaveEvent/SoftResetProgram`…), `tasks_status.mod[]` |
| **Measure Task** | 13 / 4 КБ | 1 с (`timer.flag_1s` от TIM3) + 50 мс проходы | `sensors_read` (I2C1: BME280/LIS3DH/MAX17048), поворот экрана, `Ble/Lora/Lte/Gps_Run`, **вся запись во внешнюю flash** (`Vector_RunFlashMemory`: журнал-кольцо + конфиг, только в окно тишины) | `timer.flag_1s`, флаги Save*, I2C1 | `sensors_status`, `Sns_Cfg_struct`, внешняя flash (CONFIG/CONFIG_DEFAULT/LOG), `tasks_status` |
| **LVGL Task** | 15 / 4 КБ | 1 с | **заглушка**; целевое: `lv_timer_handler` + дисплей-порт (SPI2 через `LCD_platform`) | `sensors_status`, `Sns_Cfg_struct` | виджеты UI (SquareLine `Drivers/ui_Vector`) |
| ThreadX idle | — | — | WFI (опция `TX_ENABLE_WFI` в `tx_user.h`) | — | — |

\* weak-заглушки / не наполнено — см. раздел 4.

## 2. Прерывания → потоки

| IRQ | Что делает | Кто продолжает |
|---|---|---|
| TIM3 1 кГц | `Timer_Tick_1ms()`: `countdown_time[]`, флаги `timer.flag_*`, **счёт кнопок** `button_count` | Measure (такт 1 с), слой меню (жесты — будет) |
| RTC wakeup 1 Гц | `Timer_Tick_1s()`: `countdown_time_rtc[]`, `working_hours += 1` | Measure/Receiver (флаги `flag_start_work*`) |
| EXTI1/2/3 (PB1..PB3) | демо-плеер (`audio_demo`) + `button1/2/3`, `button_sos` | Audio (очередь), меню (будет) |
| EXTI11 (PC11 ACCEL_INT) | `lis3dh_irq_handler()` — только флаг | Measure: `lis3dh_update_all()` → `TFT_Rotation` |
| GPDMA Ch11 + SAI1 | половина буфера сыграна → `req0/1` + `put(ap_wake)` | Audio: `stream_fill` (дедлайн 93 мс) |
| GPDMA Ch10 + SPI1 | конец DMA-чтения flash → `put(sf_dma_sem)` | тот, кто читал (Audio/Measure — под `ext_mtx`) |
| GPDMA Ch8 + SPI2 | кадр LCD выдан → `lv_display_flush_ready` | LVGL |
| UART RX (USART1/2/3, UART4) | байт → `add_to_buffer` (кольцо) / GPS → `Gps_Data_Verification` | Receiver Task |
| EXTI8 (PC8 CHARGE_STATE) | **настроен, IRQ в кубе НЕ включён** (SYSTEM.md п.9.7) | `Avis_Battery_System`* |

## 3. Периферия и память

| Ресурс | Назначение | Владелец/защита |
|---|---|---|
| SAI1_A + GPDMA Ch11 | звук 44.1 кГц 16 бит моно → MAX98357A (SD_MODE PC9) | Audio Player (единственный) |
| SPI1 20 МГц + GPDMA Ch10 | внешняя flash MX25R6435F 8 МБ | `extstore`, `ext_mtx` (TX_INHERIT, bounded/try-lock, EXT_BUSY) |
| SPI2 + GPDMA Ch8 | ST7789P3 172×320 | `LCD_platform`; ⚠ в кубе prescaler /2 = 80 МГц > spec, поставить /4../8 |
| I2C1 | BME280 0x76, LIS3DH 0x19, MAX17048 0x36 | Measure Task (один владелец) |
| USART1 / USART2 / USART3 / UART4 | GPS (работает) / LTE* / BLE* / COM+лог | Receiver Task (кольца); ⚠ LoRa хочет UART4 — конфликт (PLAN п.27) |
| TIM3 / TIM6 / TIM8_CH1 / RTC+BKP0 | такты прибора / тайм-база HAL / ШИМ подсветки 25 кГц / часы (BKP0, v26) | `Vector_main.c`; DBP открыт в `Board_Init` |
| Внешняя flash | SOUNDS 0..4 МБ (программатор) · CONFIG 0x400000 · CONFIG_DEFAULT 0x401000 · LOG 0x402000.. кольцо: слоты 64 Б × 32768 (маска 0x7FFF), указатели `Config_archive.CurrentAddrFile/BegginAddrFile`, ёмкость 32704, стирание сектора сдвигает начало на 64 | `Config_save_read.c` (логика) + `extstore` (шина); запись только Measure Task |
| Внутренняя flash | код ≤1016K (.ld!) · страница 0x080FE000: калибровка T/бренд/AppKey | `Internal_Flash_*`; .ld слетает при регенерации куба (п.37) |
| RAM | SRAM1/2 (потоки, буферы), SRAM4 16 КБ (`buffer_transmit[]`, такт включает Board_Init) | .ld `.sram4`; для сна не забыть SRAM4PD |

## 4. Блоки, которых нет (что добавляем)

| # | Блок | Куда встаёт | Образец |
|---|---|---|---|
| 1 | **BLE-транспорт** — КАРКАС ГОТОВ (v26): `Ble_vector.c/h` — ОТВЕТНАЯ сторона для Avis-мастера: строковая машина AT (терминатор 0x0D, неблокирующая), `ADVMSDDATAGET` → наши данные hex+OK, `ADVMSDDATASET=<hex>` → `Connected_device`, `MODINFO?`, неизвестное → ERROR; таймаут кадра в `Ble_Run()`; `USART_BLE=huart3` определён; хост-тест 10/10. Осталось: `CONFIG_BLE=1` после проверки на живом модуле, дополнения протокола (автор), формат ответа DATAGET — предварительный | `Ble_vector.c` (замещает weak `Ble_Receive/Ble_Run` без правки Vector_main.c) | `docs/Ble_mk01.c.md` (мастерская сторона) |
| 2 | **LVGL-порт**: тело LVGL Task (lv_init, lv_display_create 172×320, flush_cb→TFT_FlushBuffer, lv_tick_set_cb(VTICK_MS), ui_init, lv_i18n_init, lv_timer_handler) | `AZURE_RTOS/App/app_azure_rtos.c` USER CODE или свой `lv_port.c`; стек 8–16 КБ | LVGL.md §4; lv_conf.h уже в git |
| 3 | **Слой меню/индикации** — ОСНОВА ГОТОВА (v26): `Menu_vector.c/h` — `screen_t` по имеющимся экранам SquareLine (Start/HeadMain/Measurements/Menu/TestSound), декодер жестов кнопок 1/2/3 (SHORT/LONG/DOUBLE/SHORT_LONG/FAKE — логика Avis_Menu_2, `button_count` от TIM3), роли: B1=вход/назад, B2=вниз, B3=вверх; авто-возврат `TIMER_RTC_MAIN_RESET` (30 с), `dirty_data`→`SaveConfig=1`; хост-тест жестов/навигации пройден. Осталось: состав экранов/пунктов/статусов (автор), weak-хуки `Menu_Draw()`/`Menu_EnterScreen()` заменит LVGL-порт (строка 2) | вызовы уже подключены: `Menu_Init()` в Pre_Init, `Menu_Run()` каждый проход Measure Task | `docs/Avis_Menu_2.c.md`, `Avis_Menu.h.md` |
| 4 | **COM-протокол** `command_message`: кадры терминала, команды (SaveConfig, SoftResetProgram + START_TIMER_RTC(TIMER_RTC_SOFT_RESET_PROGRAM), FlashErase, чтение журнала/конфига — `log_read`/`log_find_by_time` готовы) | Receiver Task; кольцо TYPE_USART | Avis (не в docs — нужен исходник) |
| 5 | **Avis_Battery_System**: зарядка (CHARGE_STATE PC8 — включить EXTI8 IRQ в кубе), пороги BAT_LIM_1/2, биты BAT_CHARGE/CRITICAL(_LOW), звук/LED | weak-заглушка есть (Vector_main.c) — заменить реализацией | Avis (нужен исходник) |
| 6 | **LoRa**: перенести порт с UART4 (конфликт с логом), `USART_RF`/`FIRMWARE_VERSION` для всех DEVICE_NUMBER, `Lora_Init/DataSet` по SNS_CFG | `Lora_S7678S.c` (уже в проекте, CONFIG_LORA=0) | PLAN п.25/27 |
| 7 | **LTE-модем**: AT-машина (инициализация, регистрация, отправка), PIH/LTE_EN/RST/LED | `Lte_Receive`/`Lte_Run` (заглушки) | PLAN этап 2 |
| 8 | **Кнопки продуктовые**: короткое/длинное/удержание/SOS, антидребезг в TIM3, `VECTOR_AUDIO_DEMO_KEYS 0` | `Timer_Tick_1ms` + слой меню | PLAN этап 4 |
| 9 | **Сон**: Stop2/Standby, SRAM4PD, flash deep-power-down (B9h — команды в драйвере нет), компенсация `tx_time_set` | Board/Options | PLAN этап 5 |
| 10 | **Каналы измерения**: `SNS_Dev_Type/SNS_DevChan_Type` (резерв v23), экземпляр `Connected_device` определён, логика не наполнена | Measure Task | Avis (газоанализатор) |

## 5. Сквозные цепочки (как в SYSTEM.md §3, дополнено v26)

```
Старт: main → Board_Init(SRAM4, DBP/BKP0, SD_MODE, sf_probe) → tx_application_define
       (vlog → ext_init[мьютекс] → audio_init[load_image] → vector_tasks_init) → планировщик
Время: RTC wakeup 1 Гц → working_hours+1; 1/с Receiver сверяет RTC_ReadTime!=wh → RTC_WriteTime(BKP0)
       Старт: Pre_Init: cfg_load(0x400000) → working_hours = RTC_ReadTime()  [BKP0 главнее flash]
Журнал: событие (SaveEvent/TypeLogEvent) ИЛИ архив (TIMER_RTC_LOG, ArchiveRecording=1)
       → Measure: окно тишины → save_event → слот 64 Б = (CurrentAddrFile & 0x7FFF)*64 + 0x402000
       → переполнение: erase сектора + BegginAddrFile += 64; чтение: log_read(ptr)/log_find_by_time(t1,t2)
Звук:  EXTI/команда → ap_queue → start_now → stream_fill: ext_read 8 КБ (дедлайн 93 мс) → SAI-DMA circular
```
