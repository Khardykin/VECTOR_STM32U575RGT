# Система: карта кода, контексты, время, CubeMX

Короткий ответ на вопрос «где что делается» и «что настраивается в CubeMX».
Про звук (управление, образ, сборка, диагностика) — `AUDIO.md`, про запись
всего программатором — `FLASHING.md`, история правок — `archive/`.

---

## 1. Два слоя кода

| Слой | Файлы | Кто правит |
|---|---|---|
| **CubeMX-генерация** | `Core/Src/{main,gpio,spi,sai,gpdma,usart,i2c,tim,stm32u5xx_it}.c`, `AZURE_RTOS/App/app_azure_rtos.c` | куб; руками — **только** внутри `/* USER CODE BEGIN … */`, иначе потеряется при перегенерации |
| **VectorLib (наш код)** | `Core/VectorLib/**` | мы, свободно |

Правило проекта: **всю конфигурацию периферии делает CubeMX** (пины,
тактирование, DMA, NVIC, режимы UART/SPI/SAI, таймеры). VectorLib не
конфигурирует периферию — только пользуется ей.

`Core/VectorLib`:

```
Inc/vector_config.h        ВСЕ переключатели проекта (тест/рабочее, DMA, подтяжка кнопок)
Audio/Inc, Audio/Src
  spiflash.[ch]            драйвер MX25R6435F: команды, чтение (опрос/DMA), запись, стирание
  extstore.[ch]            мьютекс шины + конфиг (1 страница) + кольцевой журнал + ретраи чтения
  audio_player.[ch]        ПЛЕЕР: поток, очередь команд, повтор звука, громкость, стриминг в SAI-DMA
  audio_beep.[ch]          аварийный писк (const PCM 44.1 кГц во внутренней flash)
Src/Vector_main.c          ПОТОКИ ПРИБОРА: receiver_task (приём/парсинг UART), measure_task,
                           Vector_Run_Measure/Pre_Init, датчики I2C1 (sensors_init/read), SNS_CFG
Src/Gps.c                  GPS/GNSS: NMEA (GLL/GGA/RMC), автоопределение чипа, координаты для LoRa
Src/lis3dh.c               акселерометр LIS3DH: 6D-ориентация, кэш ускорений, ACCEL_INT
Src/bme280_com.c           BME280: T/H/P (обёртка над Src/BME280/bme280.c от Bosch)
Src/MAX17048.c             топливный счётчик MAX17048 (I2C): SOC, VCELL, ALRT
Src/BME280/, Src/Lis3dh_reg/  вендорские драйверы (тела закрыты CONFIG_BME / CONFIG_LIS3DH)
Src/TFT/LCD_platform.[ch]  ДРАЙВЕР ПАНЕЛИ: SPI2 + GPDMA1 Ch8 (HAL, v17), LCD_writeBulk/flush
Src/TFT/TFT.[ch]           инициализация ST7789P3 172x320, окно, поворот, TFT_FlushBuffer
Src/TFT/TFT_indicator.[ch] индикация прибора из Avis - ВЫКЛЮЧЕНА (CONFIG_TYPE_LCD == 2), ждёт переноса
Common/Src/vector_macros.c Delay/DelayInt/GetTick/Search_text + Uart_SetBaudrate
Inc/vector_macros.h        макросы и хелперы проекта: TIME_DEL_1, TIME_OUT_LORA,
                           VECTOR_DEVICE_IS_ON (статусы - в shared_macros.h)
Inc/shared_macros.h        агрегатор макросов для перенесённого кода (main.h + статусы + время)
Common/Src/vector_log.c    консольный лог: ITM/SWO и/или UART (выключается макросом)
Inc/vector_tick.h          время ПОТОКОВ (сон, таймауты модулей) - тик ThreadX
Src/Vector_main.c          ТАКТЫ ПРИБОРА: timer/countdown_time/countdown_time_rtc,
                           Timer_Tick_1ms() (TIM3) и Timer_Tick_1s() (RTC wakeup),
                           а также Vector_Run_Board_Init() (всё, что делается в
                           main() до RTOS: SRAM4, заморозка TIM6, усилитель,
                           проба flash, selftest) - бывшие vector_board.[ch]
Inc/vector_tasks.h         API потоков: vector_tasks_init, Vector_Run_*, структура tasks_status
Inc/Gps.h                  API GPS-модуля (включён: CONFIG_GPS 1, USART1)
Test/  audio_demo.[ch]     ТЕСТ: кнопки PB1/PB2/PB3 как пульт плеера
```

`Tools/ExtLoader_MX25R64/` — отдельный мини-проект: external loader для
STM32CubeProgrammer (запись `sounds.bin` во внешнюю flash через ST-LINK,
см. `FLASHING.md`). В прошивку MCU он не линкуется.

Удалено и больше не существует: `vector_sys.[ch]` (приборы времени),
`audio_factory.[ch]` + `audio_factory_image.c` (образ в прошивке и заводская
запись при старте). Внешнюю flash прошивка **не программирует никогда**.

---

## 2. Контексты: инициализация / поток / прерывание

Это главное, что нужно держать в голове. Одно и то же имя функции может означать
разные правила (что можно вызывать, можно ли блокироваться).

### 2.1 До планировщика (стек MSP, RTOS ещё не запущен)

```
main()
 ├─ MX_GPIO_Init()          пины + NVIC EXTI1..3
 ├─ MX_GPDMA1_Init()        такт GPDMA1 + NVIC каналов 9/10/11
 ├─ MX_SAI1_Init()          SAI1_Block_A: I2S, 44.1 кГц, моно, 16 бит, master TX
 ├─ MX_SPI1_Init()          SPI1 master 20 МГц + привязка GPDMA ch9/ch10 (USER CODE: 8 бит, /8)
 ├─ MX_ICACHE_Init()        ICache (DCache на U5 нет — когерентность DMA не нужна)
 ├─ Vector_Run_Board_Init():
 │    ├─ DBG_TIM6_STOP      (VECTOR_DBG_FREEZE_TICK) тайм-база HAL стоит под halt'ом отладчика
 │    ├─ SD_MODE = 1, 5 мс  включить оконечный усилитель (страховка до настройки в кубе)
 │    ├─ sf_probe()         JEDEC ID + статус внешней flash -> sf_jedec / sf_probe_rc
 │    └─ audio_selftest() x VECTOR_AUDIO_SELFTEST (по умолчанию 0)
 └─ MX_ThreadX_Init() -> tx_kernel_enter() -> tx_application_define()
      ├─ vlog_init()        консольный лог
      ├─ ext_init()         мьютекс шины, sf_dma_init(), сканирование журнала
      ├─ audio_init()       семафор ap_wake, очередь ap_queue, поток "Audio Player",
      │                     load_image()  <-- чтение таблицы образа из внешней flash
      ├─ vector_tasks_init() потоки "Receiver Task" (приём/парсинг UART) и
      │                     "Measure Task" (периодика прибора, Vector_main.c)
      └─ tx_thread_create(lvgl_thread)     заглушка под будущую графику
```

Здесь **нельзя** блокироваться на RTOS-объектах и нельзя спать: `sf_read()` сам
определяет, что планировщик не запущен (`tx_thread_identify() == NULL`), и идёт
опросом, а не DMA. Времени до планировщика тоже нет (тик RTOS ещё не идёт) —
ожидания ограничены числом проходов цикла (`wait_busy()`, guard в selftest).

### 2.2 Потоки

| Поток | Приоритет | Стек | Что делает |
|---|---|---|---|
| `Audio Player` (`ap_thread_entry`) | 10 | 4096 | **единственный владелец** SAI/DMA и `ap_buf[]`: ждёт `ap_wake` (heartbeat или будильник паузы повтора), разбирает команды, дозагружает половины стрим-буфера, запускает DMA, повторяет последний звук по кругу |
| `Receiver Task` (`receiver_task_function`) | 12 | 2048 | **единственный потребитель** колец приёма `InputBuffer[TYPE_USART/TYPE_LORA/TYPE_BLE/TYPE_LTE/TYPE_SENSOR]` (GPS — исключение: байты из ISR идут прямо в `Gps_Data_Verification()`, а `Gps_Receive()` разбирает внутренний буфер `Gps.c`): `command_message()`, `Lora_Receive()`, `Ble_Receive()`, `Lte_Receive()`, `Gps_Receive()`, `Uart_Channel_Receive()`, затем `Vector_Options_System()`; пауза `VECTOR_TASKS_RECEIVER_DELAY_MS` |
| `Measure Task` (`measure_task_function`) | 13 | 4096 | `Vector_Run_Pre_Init()` один раз (`sensors_init()`, биты включения модулей), затем по флагу `timer.flag_1s` (такт 1 с от TIM3): `Vector_Run_Measure()` → `sensors_read()` (BME280/LIS3DH/MAX17048), поворот экрана по 6D-ориентации, `Ble_Run/Lora_Run/Lte_Run/Gps_Run`; и `Vector_RunFlashMemory()` |
| `LVGL Task` | 15 | 4096 | заглушка: спит по 1 с |

Мёртвый поток `Audio Task` и семафор `audio_done_sem` удалены: звуком владеет
только `Audio Player`. Потоки прибора создаёт `vector_tasks_init()` (файл
`Core/VectorLib/Src/Vector_main.c`, выключатель `VECTOR_TASKS_ENABLE`) — структура
повторяет `Avis_main.c` с другого прибора: приём и парсинг в одном потоке,
периодика и обмен модулей в другом. Заглушки модулей объявлены `weak`,
поэтому ваша реализация в файле модуля заменит их без правки `Vector_main.c`.

### 2.3 Прерывания

| IRQ | Что в нём | Можно ли звать ThreadX |
|---|---|---|
| `EXTI1/2/3` | `audio_demo_key_handler` → `audio_play_now/stop/set_volume` → очередь + `tx_semaphore_put(ap_wake)` | да |
| `GPDMA1_Channel11` | DMA звука SAI1_A → `HAL_SAI_TxHalfCplt/TxCpltCallback` → счётчик выданных сэмплов, флаг «половина освободилась», `tx_semaphore_put(ap_wake)` | да |
| `GPDMA1_Channel10` + `SPI1` | DMA приёма SPI1 → ЕOT → `HAL_SPI_RxCpltCallback` → `tx_semaphore_put(sf_dma_sem)` | да |
| `UART4` / `USART2` / `USART3` | 1 байт → кольцо (`add_to_buffer`) или эхо в TDR | да |
| `USART1` (GPS, PB6/PB7) | 1 байт → `Gps_Data_Verification()` (кадр NMEA и таймаут ведёт `Gps.c`) | да, но из ISR только накопление байта |
| `EXTI11` (`ACCEL_INT`, PC11) | `lis3dh_irq_handler()` — только флаг; фронт переключается `LL_EXTI_*Trig_0_31` | да |
| `SPI2` + `GPDMA1_Channel8` | выдача кадра на экран → `HAL_SPI_TxCpltCallback` → `LCD_transferCpltCallback()` → `lv_display_flush_ready(disp)` | да |
| `TIM3_UP` (1 кГц) | `Timer_Tick_1ms()` (`Vector_main.c`): `countdown_time[]`, флаги `timer.flag_1ms/10ms/100ms/1s`, счётчики кнопок, таймауты кадров COM/GPS/LoRa | только счёт и флаги |
| `RTC WakeUp` (1 Гц) | `Timer_Tick_1s()` (`Vector_main.c`): `countdown_time_rtc[]`, мото-часы `working_hours` (в секундах), `flag_start_work`, `timer_end_data_exchange` | только счёт и флаги |
| `TIM6_UP` | только `HAL_IncTick()` — внутренняя тайм-база HAL (тик ThreadX — SysTick от порта, 100 Гц) | — |

> Порт ThreadX для Cortex-M33 маскирует критические секции через **PRIMASK**
> (`TX_PORT_USE_BASEPRI` не определён), поэтому вызовы `tx_*` разрешены из ISR с
> любым приоритетом, включая кубовский 0. Но приоритет 0 у EXTI означает, что
> кнопка может отложить DMA-звук — при желании поднимите EXTI до 5..8 в кубе.

---

## 3. Три цепочки, которые надо знать наизусть

**Старт**
```
ext_init -> audio_init -> load_image -> ap_img_ok=1 (или 0 -> только аварийный писк)
                                      -> поток: вечный цикл команд
```

**Кнопка → звук**
```
EXTI -> audio_demo_key_handler -> audio_play_now(idx)
     -> tx_queue_send(ap_queue, CMD_PLAY_NOW) + tx_semaphore_put(ap_wake)
     -> ap_thread: CMD_PLAY_NOW -> stop_now -> queue_clear -> start_now(idx)
          idx -> ap_tab[idx] (таблица образа в RAM, загружена в load_image)
               -> адрес во внешней flash = AUDIO_IMG_BASE_ADDR + offset
        -> stream_start_src: ext_read кусками в 2 половины + громкость
        -> HAL_SAI_Transmit_DMA (circular) -> звук
```

**Половина отыграла → дозагрузка**
```
GPDMA1_Channel11 -> HAL_SAI_TxHalfCplt/TxCpltCallback -> played += CHUNK,
     req0/req1 = 1, put(ap_wake)
     -> ap_thread: stream_service -> stream_fill(половина)
          ext_read(смещение += CHUNK; ретраи VECTOR_SF_READ_RETRY) -> тишина в хвосте
     -> played >= total -> звук закончен:Abort, ap_playing_idx = -1
          приоритет "что дальше": 1) ap_pending_idx (одноразовый play)
                                  2) ap_loop_idx (пауза ap_loop_pause, затем повтор)
                                  3) тишина
```

Повтор снимается `audio_stop()` (CMD_STOP гасит и `ap_loop_idx`) или
`audio_set_loop(0)`. Включается `audio_set_loop(1)` и взводится для последнего
успешно запущенного звука (`ap_last_idx` в `start_now()`). Подробно — `AUDIO.md`.

---

## 4. Ресурсы и кто за ними следит

| Ресурс | Владелец | Защита |
|---|---|---|
| SPI1 + внешняя flash | `extstore` | `ext_mtx` (мьютекс на всю операцию, включая стирание) |
| SAI1_A + GPDMA ch11 + `ap_buf[]` (2 × 4096 сэмплов = 16 КБ) | поток `Audio Player` | один владелец: ISR ставит только флаги «половина освободилась» и счётчик, дозагружает всегда поток |
| USART1/UART4 (лог) | `vector_log.c` (`VECTOR_LOG_ENABLE`) | только инициализация/поток, из ISR вызов отбрасывается; UART4 занят только логом |
| Кольца приёма `InputBuffer[TYPE_*]` | поток `Receiver Task` | SPSC: производитель — ISR UART (`add_to_buffer`), потребитель — **только** `receiver_task_function()` (напрямую или через `Lora_Receive()`) |
| Состояние плеера `audio_status` | `audio_player.c` | volatile-структура целиком; пишут поток и ISR SAI-DMA, порядок записи `sound_index`→`output` фиксирован |
| I2C1 (PB8/PB9): BME280 0x76, LIS3DH 0x19, MAX17048 0x36 | поток `Measure Task` (`Vector_main.c`) | один владелец: чтение только из `sensors_read()`, блокирующий обмен через макросы `I2C_Master_Transmit/Receive` (main.h) |
| `LCD_platform.c` | ошибки SPI2/DMA экрана, принудительное освобождение порта, предупреждения о конфигурации куба |
| Экран как ресурс LVGL | `LVGL Task` (порт дисплея) и `Measure Task` (поворот) | поворот безопасен благодаря флагу порта; приоритеты 15 и 13 |

Внешняя flash поделена без пересечений (`sfmap.h`): `SOUNDS 0..4 МБ` (пишет
программатор), `CONFIG 0x400000 (4 КБ)`, `LOG 0x401000..8 МБ` (пишет прошивка).

---

## 5. Что смотреть в отладчике (Expressions)

| Переменная | О чём говорит |
|---|---|
| `sf_jedec[3]`, `sf_probe_rc` | жива ли внешняя flash (`C2 28 17`, `rc == 0`) |
| `audio_status.boot_stage` | этап плеера: `AUDIO_BOOT_INIT` / `_IMAGE_OK` / `_IMAGE_BAD` / `_RUNNING` |
| `demo_status.cnt_edges` / `cnt_pressed` | доходят ли кнопки: 0 / 0 — EXTI молчит, >0 / 0 — не та полярность или подтяжка |
| `audio_status.output` | что звучит: `AUDIO_OUT_SILENT` / `_SOUND` / `_ALARM_BEEP` |
| `audio_status.cnt_commands` / `last_command` | доходят ли команды до плеера (`AUDIO_CMD_PLAY`, `_STOP`, `_BEEP`, `_LOOP`, `_PLAY_NOW`) |
| `audio_status.cnt_started` / `cnt_played` | стартовала ли DMA / доиграла ли |
| `audio_status.cnt_underruns` | заикания стрима (поток не успел дозагрузить половину за 93 мс) |
| `audio_status.cnt_watchdog` | звук добит watchdog'ом: колбэк завершения DMA не пришёл |
| `audio_status.cnt_wakes` / `cnt_wake_timeouts` | поток жив: `cnt_wake_timeouts` растёт = heartbeat, событий просто нет |
| `audio_status.cnt_loops` | сколько повторов цикла сыграно |
| `audio_status.last_error` / `sai_error_code` | код `audio_err_t` и сырой `SAI.ErrorCode` (больше не смешаны в одном числе) |
| `audio_status.loop_enabled` / `loop_index` / `loop_next_at_ms` | повтор: включён ли, какой звук, когда следующий |
| `tasks_status` | потоки прибора: `receiver_running` / `measure_running`, `cnt_receiver_passes`, `cnt_measure_runs`, по модулям `mod[0..5]` (com/lora/ble/lte/gps/sensor) — `cnt_receive_calls`, `cnt_run_calls`, `cnt_rx_bytes`, `cnt_rx_frames`, `cnt_tx_frames`, `cnt_errors`, `last_*_ms` |
| `sensors_status` | диагностика шины I2C1: `cnt_init_ok`, `cnt_bme_reads`, `cnt_accel_reads`, `cnt_battery_reads`, `cnt_rotation_events`, `cnt_read_ms`, `last_read_ms`. Данные — в `Sns_Cfg_struct.Config_common`, живость чипов — маска `Config_common.Sensors_ok` (`SENSORS_OK_BME/_LIS3DH/_MAX17048`) |
| `lcd_status` | экран: `busy`, `dma_active`, `cnt_cmd/data8/data16/bulk`, `cnt_flush_ready` (0 при растущем `cnt_bulk` = нет прерывания DMA), `cnt_dma_fail`, `cnt_wait_timeout`, `last_hal_error` |
| `sf_dbg_dma_chunks` / `_fallback` / `_tmo` | работает ли SPI-DMA и сколько раз откатились на опрос |
| `vlog_dbg_lines` | сколько строк ушло в лог |

---

## 6. Консольный лог (`vector_log.c`)

Куда выводится — два **независимых** переключателя в `vector_config.h`, можно
оба сразу:

| Куда | Макрос | Где видно |
|---|---|---|
| ITM/SWO | `VECTOR_LOG_ITM 1` | **консоль внутри CubeIDE** (вкладка SWV). Нужен подключённый ST-LINK и **свободный PB3**: PB3 = JTDO/TRACESWO, а сейчас там BUTTON3, поэтому SWO-вывода физически нет, пока кнопка на PB3 |
| UART | `VECTOR_LOG_UART 4` | ваш терминал на UART4 (PC10). `0` = не печатать, `1/2/3` = USART1/2/3 |

Три выключателя:

| Выключатель | Где | Что делает |
|---|---|---|
| `VECTOR_LOG_ENABLE 0` | `vector_config.h`, компиляция | все `LOG_*()` → `((void)0)`, `vector_log.c` пустой |
| `VECTOR_LOG_ITM` / `VECTOR_LOG_UART` | `vector_config.h`, компиляция | выбор канала вывода |
| `vlog_set_level()` / `vlog_set_mask()` | рантайм | порог важности и набор модулей без пересборки |

Уровни: `VLOG_DEBUG`(1) → `VLOG_INFO`(2, по умолчанию) → `VLOG_WARN`(3) →
`VLOG_ERROR`(4). Маски модулей: `VLOG_M_SYS` (S), `VLOG_M_FLASH` (F),
`VLOG_M_AUDIO` (A), `VLOG_M_TASKS` (T — потоки прибора). Модуль BRIDGE удалён: мост не
логирует ничего.

Метка времени `[сек.мс]` — **тик ThreadX** (разрешение 10 мс). До планировщика
тик не идёт, поэтому стартовые строки печатаются с `[0.000]` — это нормально.

Пример вывода старта (прошивка прошита программатором, образ записан):

```
[0.000] 2/S: probe rc=0 jedec=c2 28 17
[0.000] 2/S: --- vector log on (115200 8N1 UART4) ---
[0.000] 2/S: level=2 mask=ffffffff
[0.010] 2/A: audio init
[0.060] 2/A: image ok=1 sounds=5
[0.065] 2/T: tasks init: receiver(prio 12 stack 2048) measure(prio 13 stack 4096)
[0.070] 2/T: receiver task: com=1 lora=0 ble=0 lte=0 gps=0 (delay 10 ms)
[12.340] 2/A: play #2 'd_myvoice' stream 358306 samples @44100 Hz (chunk 4096)
```

Что где печатается:

| Модуль | События |
|---|---|
| `Vector_main.c` (`Vector_Run_Board_Init`) | результат `sf_probe()` (JEDEC ID) — ещё до RTOS |
| `spiflash.c` | чтение ≥ 1 КБ (адрес, объём, миллисекунды) — только при `VECTOR_SPI_LOG_READS 1`, иначе уровень DEBUG; каждая запись и стирание сектора, откат с DMA на опрос |
| `extstore.c` | ретраи чтения (`ext_read: recovered after N retry`), конфиг/журнал (DEBUG) |
| `audio_player.c` | старт, состояние образа, команды, запуск и ошибки звука, «звук доигран», watchdog |
| `Vector_main.c` | старт потоков прибора (`tasks init`, `receiver task`, `measure task`, `pre init`) |
| `Vector_main.c` (датчики) | инициализация I2C1-модулей (кто ответил), смена ориентации/поворота экрана |
| `LCD_platform.c` | ошибки SPI2/DMA экрана, принудительное освобождение порта, предупреждения о конфигурации куба |

Правила модуля (важно при доработке):

* **текст сообщений — только ASCII (латиница).** Исходники в UTF-8, а
  терминалы под Windows по умолчанию в CP1251/CP866: кириллица приходит
  кракозябрами. Держите строки латиницей;
* из **ISR лог не печатается** — вызов отбрасывается и плюсит
  `vlog_dbg_isr_skipped`. События из ISR показываются из потока: например,
  «звук доигран» поток печатает, заметив изменение `audio_status.cnt_played`;
* строка собирается в один статический буфер 160 Б под мьютексом, поэтому
  два потока не перемешают вывод;
* формат только `%s %d %u %x %X %c %%`, числа 32-битные: своя печать, чтобы не
  тянуть в прошивку `vsnprintf`.

---

## 7. Время: три источника — и кто за что отвечает

| Источник | Частота | Кто настроил | Для чего |
|---|---|---|---|
| **SysTick** (приоритет 4) | **100 Гц = 10 мс** | `Core/Src/tx_initialize_low_level.S` (порт ThreadX, `SYSTEM_CLOCK = 160000000`) | **всё время приложения**: тик ядра ThreadX, `VTICK_*`, метки лога |
| **TIM6** (`TIM6_IRQn`, приоритет 10) | 1 кГц | куб (`NVIC.TimeBase = TIM6_IRQn`, `TICK_INT_PRIORITY`), `stm32u5xx_hal_timebase_tim.c` | **только внутренняя тайм-база HAL**: `HAL_GetTick()` для таймаутов внутри драйверов HAL (`HAL_SPI_Transmit(..., 250)` и т.п.) |
| **TIM3** (`TIM3_IRQn`, приоритет 2) | **1 кГц = 1 мс** | куб (`tim.c`) | **таймеры прибора**: `countdown_time[]` (мс), флаги `timer.flag_1ms/10ms/100ms/1s`, счётчики кнопок, таймауты кадров COM/GPS/LoRa. Обслуживает `Timer_Tick_1ms()` в `Vector_main.c` |
| **RTC WakeUp** (`RTC_IRQn`, приоритет 0) | **1 Гц = 1 с** | куб (`.ioc`: `RTC → Wakeup Interrupt` + NVIC `RTC global interrupt`); сгенерированный `rtc.c` | **секундные таймеры прибора**: `countdown_time_rtc[]`, мото-часы `working_hours` (единица = 1 с), `timer.flag_start_work`. Обслуживает `Timer_Tick_1s()` в `Vector_main.c` |

### 7.1 Таймеры прибора (как в Avis)

```c
/* секундный таймер: запуск, опрос, сброс - макросы из shared_macros.h */
START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, time_data_set);  /* запустить    */
if (TEST_TIMER_RTC(TIMER_RTC_LORA_DATA_SET)) {            /* время вышло? */
  RESET_TIMER_RTC(TIMER_RTC_LORA_DATA_SET);               /* снять флаг   */
}
if (!TEST_TIMER_RUN_RTC(TIMER_RTC_X)) { ... }             /* ещё идёт?    */
START_TIMER(TIMER_SHUTDOWN, TIME_DEL_10);                 /* то же в мс   */

/* такты: поток читает флаг и сам его сбрасывает */
if (timer.flag_1s) { timer.flag_1s = 0; Vector_Run_Measure(); }
```

ID таймеров — `enum` в `shared_types.h` (`TIMER_*` для мс, `TIMER_RTC_*` для
секунд), длительности — `TIME_RTC_*` там же. Экземпляры `timer`,
`countdown_time`, `countdown_time_rtc` объявлены `volatile` (пишет
прерывание, читают потоки) и живут в `Vector_main.c`.

Что где использовать: **сон потока и таймауты модулей** — `VTICK_*` (тик
ThreadX); **логика прибора** — `timer.flag_*` и `START_TIMER*`; **то, что
должно идти и во сне** (период передачи LoRa/BLE, мото-часы) — `*_RTC`.

> ⚠ `START_TIMER*` — это запись счётчика и `CLRBIT` в общем слове `TimerIsEnd`,
> а прерывание в это же время декрементирует счётчик и ставит биты. Окно
> гонки мало (один проход ISR), но оно есть. Если когда-нибудь таймер «потеряет»
> запуск, оберните макро-операцию в `NVIC_DisableIRQ(TIM3_IRQn)` /
> `NVIC_EnableIRQ(TIM3_IRQn)` (для секундных — `RTC_IRQn`). На других приборах
> схема работает без этого.

Приложение `HAL_GetTick()`/`HAL_Delay()` **не использует вообще** — ни в
VectorLib, ни в логе. Раньше здесь жил модуль `vector_sys` с приборами,
которые сравнивали TIM6, SysTick и RTC; диагностика показала, что тайм-база
HAL под отладчиком теряет прерывания (TIM6 считает, а IRQ с приоритетом 15 не
обслуживается), поэтому всё приложение переведено на тик ThreadX, а приборы
удалены за ненадобностью.

```c
uint32_t t0 = VTICK_MS();            /* текущее время, мс (тик RTOS, 10 мс)   */
if (VTICK_ELAPSED_MS(t0) > 250u) ... /* переполнение учтено                   */
VTICK_SLEEP_MS(100);                 /* сон потока, CPU свободен              */
ULONG t = VTICK_MS2TICKS(500);       /* мс -> тики для tx_semaphore_get       */
if (VTICK_IN_THREAD()) { ... }       /* можно ли спать/брать мьютекс          */
```

Разрешение 10 мс хватает для всего приложения: антидребезг 50 мс, пауза цикла
500 мс, таймауты SPI 250–500 мс, watchdog стрима. **Звук от тика не зависит
вообще**: темп выдачи сэмплов держат SAI+DMA аппаратно, поток только
дозагружает буфер с запасом 93 мс.

**До планировщика тика нет** (`tx_time_get()` всегда 0, `tx_thread_sleep()`
нельзя), поэтому код в `main()`/`tx_application_define()` время не
использует — ожидания ограничены числом проходов:

| Место | Как ограничено |
|---|---|
| `wait_busy()` в `spiflash.c` | 20000 опросов RDSR (~1 с); в потоке дополнительно спит по 2 мс и ограничен 1000 мс по тику RTOS |
| `audio_selftest()` | 40 000 000 проходов ожидания флага DMA (~1 с) |
| задержка на пробуждение усилителя | цикл ~5 мс в `Vector_Run_Board_Init()` (исчезнет, когда PC9 = High сделает куб — раздел 9) |

### Отладчик и время

Остановка ядра (Live Watch / Expressions / SFR) замораживает SysTick — тик
ThreadX отстаёт от настенных часов, метки лога «сжимаются». Это не баг
прошивки: ядро действительно не работало. Два следствия, которые закрыты:

* `VECTOR_DBG_FREEZE_TICK 1` (по умолчанию): `DBGMCU->APB1FZR1 |=
  DBG_TIM6_STOP` в `Vector_Run_Board_Init()` — тайм-база HAL тоже стоит под
  halt'ом, внутренние таймауты HAL не «сгорают» за одну остановку;
* `VECTOR_SF_READ_RETRY 3`: одиночный SPI-обмен, попавший на halt,
  возвращается по таймауту HAL — чтения повторяются, звук не рвётся
  (в логе видно `ext_read: recovered after N retry`).

Если тайминги важны — прошивайте через CubeProgrammer и смотрите лог в
терминале без подключённого отладчика (`FLASHING.md`, раздел 2).

Очереди ThreadX (`tx_queue_send/receive`) тик не подменяют: блокирующий
`tx_queue_receive(..., 100)` ждёт 100 тиков ThreadX = 1 с (SysTick), а
`TX_NO_WAIT` не блокируется вовсе.

---

## 8. Документы

| Файл | О чём |
|---|---|
| `README.md` | индекс, быстрый старт, все переключатели одним списком |
| `PLAN.md` | описание программы и план: модули, транспорт BLE↔LoRa, этапы работ, принятые решения, известные дефекты |
| `FLASHING.md` | **запись программатором**: прошивка MCU, external loader для внешней flash, запись sounds.bin, CubeMX-настройки |
| `AUDIO.md` | как запустить/остановить звук, 44.1 кГц, качество, набор звуков, сборка образа, диагностика |
| `SYSTEM.md` | этот: слои кода, контексты, цепочки, IRQ, время, лог, что настраивать в CubeMX |
| `archive/FIX_REPORT.md` | история: разбор «зависания» в `tx_semaphore_get`, SPI-DMA |
| `archive/AUDIO_MAP.md` | исторический справочник по фазам разработки |

---

## 9. Что настроить в CubeMX (чек-лист)

Правило проекта: конфигурацию периферии делает куб; после перегенерации наши
вызовы остаются в `USER CODE`-секциях.

### 9.0. ОБЯЗАТЕЛЬНО: SAI1 → Audio Frequency = 44.1 kHz **и PLL3 под неё**

Multimedia → SAI1 → SAI_A_Master → **Audio Frequency = 44.1 KHz**. Остальное не
менять: Master, I2S standard, 16 bit, 2 слота, Mono, MCK off.

**Этого недостаточно.** Частота кадров `FS = SAI_CK / (256 × MCKDIV)`, а `MCKDIV`
— целое (его HAL считает сам), поэтому ядро SAI1 обязано быть кратно
`256 × 44100 = 11.2896 МГц`. Куб на «Audio Frequency = 44.1 kHz» сам предлагает
PLL3P = 44.1 МГц — число красивое, но это 1000 × 44100, а не 256 × целое: HAL
берёт `MCKDIV = 4` и реальная частота получается **43 066 Гц (−2.34 %)** — звук
ниже на 41 цент и длиннее на 2.4 %. Куб показывает это сам: `SAI1.RealAudioFreq`
и `SAI1.ErrorAudioFreq` (в `.ioc` так и лежало `43.066 KHz` / `-2.34 %`).

Поэтому в Clock Configuration → PLL3 ставим руками (источник — HSE 16 МГц):

| Параметр | Значение |
|---|---|
| `PLL3M` | 1 (PFD3 = 16 МГц, VCO input range не трогаем) |
| `PLL3N` | **12** |
| `PLL3 FRACN` | **5741** |
| `PLL3P` | **9** |
| VCO3 | 203.212891 МГц (допустимо 96…344 МГц) |
| `SAI1Freq` (= PLL3P) | **22.579210 МГц** = 512 × 44100 |
| SAI1: RealAudioFreq / ErrorAudioFreq | 44.1 KHz / **0.0 %** |

SAI1 → Clock Mux = **PLL3P**. После Generate Code в `sai.c` (`HAL_SAI_MspInit`)
должны стоять `PLL3N = 12`, `PLL3FRACN = 5741.0`, `PLL3P = 9` — проверьте, что
регенерация их не сбила. Почему ровно 44 100.000 Гц из 16 МГц не выходит в
принципе и какие есть запасные делители — AUDIO.md, раздел 2,
«Тактирование SAI1: как получить ровно 44100 Гц».

Проверка на железе: в логе старта строка
`sai ck=22579210 Hz mckdiv=2 fs=44100019 mHz (need 44100000 mHz)`
(печатает `audio_init()`), либо осциллографом: PA9 (FS) = 44 100.0 Гц,
PA8 (SCK) = 1 411 200 Гц.

### 9.1. ОБЯЗАТЕЛЬНО для стриминга: SAI1_A DMA → Mode = Circular

Multimedia → SAI1 → SAI_A_Master → (вкладка DMA Settings) → GPDMA1 Channel11 →
**Mode = Circular** (уже стоит в `.ioc`: `GPDMA1.CIRCULARMODE_GPDMACH11=ENABLE`).
На U5 circular для GPDMA — связный список; только в режиме
`DMA_LINKEDLIST_CIRCULAR` HAL не гасит SAI по завершении блока, поэтому стыки
кусков без щелей. Без Circular плеер откатится на одну транзакцию (предел
1.49 с при 44.1 кГц) и напечатает подсказку в лог.

### 9.2. PC9 (SD_MODE) = High при старте

System Core → GPIO → PC9 → GPIO_Output → **GPIO output level = High**,
Maximum output speed = Low. Усилитель сидит в shutdown, пока SD_MODE низкий:
без этого звука не будет вообще. До настройки в кубе код поднимает пин сам
(`Vector_Run_Board_Init()`, шаг можно будет удалить).

### 9.3. SPI1: 8 бит, 20 МГц, быстрые пины (`spi.c`, `USER CODE SPI1_Init 2`)

Сейчас куб генерирует SPI1 с 4-битными фреймами и делителем /2 (80 МГц) —
внешняя flash так не разговаривает, поэтому блок переинициализирует SPI1
руками. Чтобы удалить блок, в кубе (Connectivity → SPI1 → Full-Duplex Master):

| Параметр | Значение |
|---|---|
| Frame Format | Motorola |
| Data Size | **8 Bits** |
| First Bit | MSB First |
| CPOL / CPHA | Low / 1 Edge |
| NSS Signal | Software Management |
| Baud Rate Prescaler | **8** → 160 МГц / 8 = **20 МГц** |
| Master SS/Inter-Data Idleness | 00 cycle |

Пины PA5/PA6/PA7: Alternate Function Push Pull, No pull-up/down,
**Maximum output speed = High** (сейчас Low — на 20 МГц фронты пологие).
CS (PA4): GPIO_Output, **GPIO output level = High**, метка `CS_FLASH`.

### 9.4. Приоритет тайм-базы TIM6 (сейчас 10)

TIM6 = 10 (`TICK_INT_PRIORITY` в `stm32u5xx_hal_conf.h` и NVIC в `.ioc`).
Приложение HAL-тик не использует, но внутренние таймауты HAL-драйверов
(SPI-обмены) считаются по нему: пока TIM6 ниже периферийных IRQ (0–2),
таймауты под нагрузкой растягиваются — не смертельно. Остановки ядра закрыты
`VECTOR_DBG_FREEZE_TICK`. Если нужна точность — снизить до 5 (для ThreadX
безопасно: порт маскирует критические секции через PRIMASK).

### 9.x. Экран, GPS, акселерометр (v17)

* **SPI2**: Simplex Bidirectional Master, 8 бит, MSB, PB13 SCK / PB15 MOSI;
  **Baud Rate Prescaler — поставить /4 или /8** (сейчас /2 = 80 МГц, что выше
  допустимого для ST7789P3);
* **GPDMA1 Channel8** = SPI2_TX, Memory→Periph, BYTE/BYTE, Normal mode;
  NVIC: `GPDMA1_Channel8_IRQn` и `SPI2_IRQn` включены;
* **USART1** (PB6/PB7) = GPS: `USART1_IRQn` включён, в `USART1_IRQHandler`
  байт уходит в `Gps_Data_Verification()`;
* **PC11 = ACCEL_INT**: `GPIO_MODE_IT_RISING_FALLING` + `GPIO_PULLDOWN`,
  `EXTI11_IRQn` (в `main.h` есть `ACCEL_INT_EXINT_LINE LL_EXTI_LINE_11`);
* **TIM3**: 1 кГц (Prescaler 159, Period 1000), `TIM3_IRQn` — такт таймеров
  прибора `Timer_Tick_1ms()` (включая таймауты кадров COM/GPS/LoRa);
* пины экрана: PB14 `LCD_CS`, PB10 `LCD_DC`, PB12 `LCD_RST`, PC6 `LCD_LED`
  (GPIO Output; ШИМ подсветки не настроен).

### 9.5. UART4: однопроводный полудуплекс → обычный асинхронный

Сейчас `PC10.Mode = Half_duplex(single_wire_mode)`: приёмник слышит собственную
передачу (эхо), линия открытый сток. В кубе: Connectivity → UART4 → Mode:
**Asynchronous**, пины PC10 = TX, **PA1 = RX** (PC11 брать нельзя — он занят
`ACCEL_INT`/EXTI11; PA0/PA1 свободны); для обоих AF Push Pull, speed
Low, No pull (или Pull-up на длинной линии). Код моста подстроится сам
(`ub_transmit()` проверяет бит `HDSEL` в рантайме).

### 9.6. PB3: BUTTON3 или SWO-консоль — что-то одно

PB3 = JTDO/TRACESWO. Хотите лог в консоли CubeIDE без проводов — уберите
BUTTON3 с PB3, оставьте `SYS_JTDO-SWV` и включите SWV в Run Configurations
(Core Clock = 160 MHz). Хотите кнопку — используйте `VECTOR_LOG_UART 4`.

### 9.7. EXTI8 (CHARGE_STATE, PC8) настроен, но прерывание не включено

В NVIC → `EXTI line[8] interrupt` → Enabled, приоритет 5–8; заодно решите
подтяжку (`NOPULL` на неподключённом входе даёт ложные срабатывания).

### 9.8. RTC WakeUp: счётчик в секундах

`RTC_WAKEUPCLOCK_CK_SPRE_17BITS`: CK_SPRE = 1 Гц, период = (WUT + 1) секунд.
Нужен мелкий шаг — `RTC_WAKEUPCLOCK_RTCCLK_DIV16` (≈ 2 кГц).

**Настроен в кубе** (автор): `RTC → Wakeup Interrupt` + `NVIC → RTC global
interrupt` (приоритет 0) — `HAL_RTCEx_SetWakeUpTimer_IT` и NVIC генерирует
куб, в `USER CODE` у `rtc.c` ничего не нужно. Период не менять: 1 с — такт
`Timer_Tick_1s()`, секундных таймеров прибора и мото-часов `working_hours`.

### Что должно остаться в `USER CODE` (это не конфигурация периферии)

`main.c`, `USER CODE BEGIN 2`:

```c
  Vector_Run_Board_Init();   /* freeze TIM6 под отладчиком, SD_MODE, sf_probe, selftest */
```

`main.c`, `USER CODE BEGIN Callback 1` — ветка TIM3 с вызовом
`Timer_Tick_1ms()` (TIM6 обслуживает только `HAL_IncTick()` выше, в
сгенерированной части).

`stm32u5xx_it.c` — колбэк `HAL_RTCEx_WakeUpTimerEventCallback` →
`Timer_Tick_1s()` и приём байт в `USART*_IRQHandler` (кольца/
`Gps_Data_Verification()`). `rtc.c` — пусто (всё настроено в кубе).

`AZURE_RTOS/App/app_azure_rtos.c`, `tx_application_define`:

```c
  vlog_init();  ext_init();  audio_init();  vector_tasks_init();
  /* + создание потока LVGL-заглушки */
```

Всё остальное (пины, тактирование, DMA, NVIC, режимы UART/SPI/SAI) — из куба.

---

## Проверено и трогать не нужно

* GPDMA1: ch9 = SPI1_TX, ch10 = SPI1_RX, ch11 = SAI1_A (circular), все три
  IRQ включены;
* SPI1_IRQn включён — он **обязателен** для DMA-чтения: HAL вызывает
  `HAL_SPI_RxCpltCallback` из прерывания EOT самого SPI, а не из DMA;
* тайм-база HAL = TIM6, `NVIC.TimeBase = TIM6_IRQn`;
* тик ThreadX = SysTick 100 Гц, константы в `tx_initialize_low_level.S`
  (`SYSTEM_CLOCK = 160000000`) — **если поменяете частоту ядра, правьте эту
  константу**, иначе `tx_thread_sleep()` поедет. Файл генерируется кубом:
  после перегенерации проверьте, что `SYSTEM_CLOCK` совпадает с SYSCLK;
* SAI1: делитель MCLK (`MCKDIV`) `HAL_SAI_Init` считает сам из
  `Init.AudioFrequency` и фактической частоты ядра SAI1 — руками в `sai.c` его
  не править. Но само ядро обязано быть кратно 11.2896 МГц (= 256 × 44100),
  иначе `MCKDIV` округлится и частота уедет на проценты (раздел 9.0 и AUDIO.md,
  «Тактирование SAI1»). Контроль — строка `sai ck=... mckdiv=... fs=... mHz`
  в логе старта; при расхождении больше 0.1 % там же печатается `SAI fs off`.
