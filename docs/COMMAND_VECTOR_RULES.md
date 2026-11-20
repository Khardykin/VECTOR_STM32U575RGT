# РОЛЬ И ЗАДАЧА
Ты — Senior Embedded C разработчик (STM32 / Artery, FreeRTOS). Твоя задача — написать модули `Command_vector.h` и `Command_vector.c`, взяв за основу архитектурный стиль, карты регистров и адресацию из приложенного эталонного файла, но переведя все тяжелые операции с Flash (стирание и запись) на безопасную работу через **флаги и staging-буфер** (исполнение в `Measure Task`).

---

# 1. ОБЩИЕ ТРЕБОВАНИЯ К СТИЛЮ И КОДУ
1. **Минимум сущностей.** Никаких очередей событий (`mb_u_queue`), сложных таблиц коллбэков и десятков мелких функций на каждый регистр. Логика должна быть такой же прямой, как в эталонном файле (`sn_map`, `com_map`, `in_map` + компактный `switch` для системных адресов).
2. **Без упоминания старых проектов.** Запрещено использовать слово `Avis` в именах файлов, функций, комментариях и логах (использовать `vector` или нейтральные имена). Все логи — только на английском (ASCII).
3. **Единый формат CRC.** Все функции `CmdFuncN()` формируют тело ответа **без CRC** и возвращают его длину. Диспетчер `parsing_command()` в конце сам вызывает `CreateCRC(out, AnswerLen)` и возвращает `AnswerLen + 2`.

---

# 2. АДРЕСАЦИЯ И КАРТЫ РЕГИСТРОВ (КОМАНДЫ 0x03, 0x04, 0x10)
Сохранить мультиканальную адресацию:
- `sub_addr = Address & 0x0FFF;`
- `chan = (Address >> 12) & 0x0F;` (проверка `chan >= COUNT_CHAN && chan != (NOCHAN >> 4)`).
- В `parsing_command()` для команд `0x03`, `0x04`, `0x10` брать мьютекс `xSemaphore_measure` с таймаутом `TIME_MUTEX_DELAY_MEASURE`.

### Таблицы регистров (оставить структуру `reg_map_t` и типы `T_U16`, `T_U32`, `T_STR`, `T_FLT4`):
1. **`sn_map[]`** — поканальные настройки (`Sns_Cfg_struct.Sensor[chan]`, адреса `0x0102..0x0228`).
2. **`com_map[]`** — общие настройки (`Sns_Cfg_struct.Config_common`, адреса `0x00FE..0x0316`).
3. **`in_map[]`** — входные регистры телеметрии для команды `0x04` (адреса `0x0012..0x003A` + системные `0x0010`, `0x0011`, `0x0060..0x0062`, `0x0070..0x0073`, `0x0999`).

### Системные и управляющие регистры (доступны для группового чтения `0x03` и записи `0x10`):
Сохранить привычные системные адреса:
- `0x0001`: `cmd_state.erase_mode` (запись `1` — стирание, `2` — восстановление; исполняется в `Measure Task`).
- `0x0003`: `state_f` (чтение/запись параметров прошивки через `ReadCurrentParamFirmware` / `SaveCurrentParamFirmware`).
- `0x0004`: `size_f` (размер прошивки).
- `0x0005`: `sys_state.soft_reset` (запуск таймера перезагрузки).
- `0x0006`: `TamperReset`.
- `0x0240`: при записи `!= 0` ставит флаг `cmd_state.save_config = 1`.
- `0x0250`: при записи `!= 0` ставит флаг `cmd_state.save_config_default = 1`.
- `0x0252`: при записи `!= 0` ставит флаг `cmd_state.load_config_default = 1`.
- `0x0254`: сброс бита `ST_COMMON_BIT_DATA_EXCHANGE`.
- `0x0256`: калибровка датчика температуры (`tempsensor_calib`).
- `0x0300`, `0x0301`: калибровка нуля и чувствительности АЦП (`adc_zero_calibration`, `adc_span_calibration`).
- `0x0317..0x031E`: LoRa `appkey`.
- `0x0700`: язык интерфейса (`application_language`).
- `0x0800`: выключение прибора.

### Сплошной блок управления Flash (адреса `0x0F00..0x0F09`, читаются и пишутся пачкой через `0x03` и `0x10`):
Чтобы перед вызовом команд `0x0E` (14) и `0x0F` (15) хост мог одним запросом `0x10` подготовить параметры и прочитать статус через `0x03`, реализовать непрерывный блок:
- `0x0F00` (RW): `build_type` (текущий тип сборки, совместимость с `Save_Build_Type_Dynamic`).
- `0x0F01` (RW): `flash_target` — целевая область для операций (`0` = нет, `1` = внешняя Flash звук, `2` = внешняя Flash прошивка, `3` = внутренняя Flash бутлоадер, `4` = архив/логи).
- `0x0F02..0x0F03` (RW): `flash_addr` (32-bit, Lo/Hi) — базовый адрес / смещение области.
- `0x0F04..0x0F05` (RW): `flash_len` (32-bit, Lo/Hi) — общая длина записываемого файла (звука или прошивки).
- `0x0F06..0x0F07` (RW): `flash_key` (32-bit, Lo/Hi) — ключ разблокировки (защита от случайного стирания/записи).
- `0x0F08` (WO/CMD): `flash_cmd` — командные флаги (после записи взводит флаг в `cmd_state` и сам обнуляется):
  - `1`: запрос на стирание выбранной `flash_target` (`cmd_state.erase_req = 1`, исполняется в `Measure Task`).
  - `2`: запрос на применение новой прошивки (`cmd_state.fw_apply_req = 1`).
- `0x0F09` (RO): `flash_status` — статус последней операции (`0` = Idle/OK, `1` = Busy / идет запись или стирание, `2` = Err_Protected / неверный ключ, `3` = Err_Addr, `4` = Err_Verify, `5` = Err_IO).

---

# 3. КОМАНДЫ РАБОТЫ С ПАМЯТЬЮ И ЛОГАМИ

### 1. Команда `0x0C` (12 dec) — Чтение журнала событий
- Формат запроса: `[Slave][0x0C][Addr16 BE][Count16 BE][CRC]`.
- Оставить проверку кольцевого буфера (`BegginAddrFile`, `CurrentAddrFile`, `MASK_COUNT_LOG`), ограничение `Count * SIZE_LOG <= 3000`, захват `xSemaphore_flash`, выход/вход в `ultra_deep_power_down_memory()` и чтение через `read_event()`.

### 2. Команда `0x14` (20 dec) — Поиск записей журнала по времени
- Формат запроса: `[Slave][0x14][TimeStart32 BE][TimeStop32 BE][CRC]`.
- Формат ответа: `[Slave][0x14][PtrFrom32 LE][PtrTo32 LE][CRC]`.
- Оставить логику перебора через `read_event_for_search()` под мьютексом `xSemaphore_flash`.

### 3. Команда `0x0E` (14 dec) — Запись во внутреннюю Flash (бутлоадер)
- Формат запроса: `[Slave][0x0E][Addr32 BE][Size8][Data...][CRC]`.
- Формат ответа: `[Slave][0x0E][Addr32 BE][CRC]` (первые 6 байт запроса).
- **Безопасная реализация (через флаги):**
  - Запрещено вызывать `flash_unlock()` / `flash_word_program()` прямо внутри `CmdFunc14`!
  - Проверить:
    1. Разрешение обновления бутлоадера (`CONFIG_BOOTLOADER_PRESENT != 0` и `SCB->VTOR >= FLASH_ADDRESS_STOP_BOOTLOADER`), иначе ошибка `0x03` и `flash_status = 2`.
    2. Валидность `flash_key`, заранее записанного в `0x0F06..0x0F07`.
    3. Границы `Address >= FLASH_ADDRESS_START_BOOTLOADER` и `(Address + size) <= FLASH_ADDRESS_STOP_BOOTLOADER`, кратность 4/16 байтам, `size <= 256`.
    4. Если предыдущий пакет еще пишется (`cmd_state.int_write_pending != 0`), вернуть ошибку занятости (Modbus Exception `0x06` Device Busy).
  - Скопировать `Data` в `cmd_state.write_buf`, сохранить `write_addr = Address`, `write_len = size`, выставить `cmd_state.int_write_pending = 1` и `flash_status = 1` (Busy), вернуть `6`.
  - Реальную запись во внутреннюю Flash выполняет `Measure Task`.

### 4. Команда `0x0F` (15 dec) — Запись во внешнюю Flash (звук и прошивка)
- Формат запроса: `[Slave][0x0F][Addr32 BE][Size8][Data...][CRC]`.
- Формат ответа: `[Slave][0x0F][Addr32 BE][CRC]` (первые 6 байт запроса).
- **Безопасная реализация (через флаги):**
  - Запрещено вызывать `SaveFirmwareMemory()` прямо внутри `CmdFunc15`!
  - Проверить:
    1. Валидность `flash_key` и выбранный `flash_target` (`1` = звук, `2` = прошивка).
    2. Выход за пределы отведенного раздела внешней Flash (проверка по `flash_len` / `reg_mem->Size_fw`), `size <= 256`.
    3. Если предыдущий пакет еще не записан в SPI Flash (`cmd_state.ext_write_pending != 0`), вернуть Modbus Exception `0x06` (Device Busy).
  - Скопировать `Data` в `cmd_state.write_buf`, сохранить `write_addr = Address`, `write_len = size`, выставить `cmd_state.ext_write_pending = 1` и `flash_status = 1` (Busy), вернуть `6`.
  - Реальную запись в микросхему памяти (с последующей верификацией `ReadFirmwareMemory`) выполняет `Measure Task` в «окно тишины» (когда не воспроизводится звук).

---

# 4. СТРУКТУРА СОСТОЯНИЯ (`cmd_state`)
Объявить в `Command_vector.h` и определить в `Command_vector.c` единую структуру флагов и буферов:

```c
typedef struct
{
    /* Регистры управления 0x0F00..0x0F09 */
    uint16_t build_type;
    uint16_t flash_target;      /* 1: ext_sound, 2: ext_fw, 3: int_boot, 4: logs */
    uint32_t flash_addr;
    uint32_t flash_len;
    uint32_t flash_key;
    uint16_t flash_status;      /* 0: OK, 1: Busy, 2: Protected, 3: ErrAddr, 4: ErrVerify, 5: ErrIO */

    /* Флаги запросов для Measure Task */
    volatile uint8_t erase_req;
    volatile uint16_t erase_mode;
    volatile uint8_t save_config;
    volatile uint8_t save_config_default;
    volatile uint8_t load_config_default;
    volatile uint8_t fw_apply_req;

    /* Staging-буфер для потоковой записи (команды 0x0E и 0x0F) */
    volatile uint8_t int_write_pending;
    volatile uint8_t ext_write_pending;
    uint32_t write_addr;
    uint16_t write_len;
    uint8_t  write_buf[256];
} cmd_state_t;

extern cmd_state_t cmd_state;
```

---

# 5. ЧТО НУЖНО ВЫДАТЬ В ОТВЕТЕ
1. Полный код `Command_vector.h`.
2. Полный код `Command_vector.c` (с поддержкой UART и NFC приемников, проверкой CRC и всеми функциями `CmdFunc3`, `CmdFunc4`, `CmdFunc16`, `CmdFunc12`, `CmdFunc20`, `CmdFunc14`, `CmdFunc15`).
3. Короткий, готовый к вставке фрагмент кода для **`Measure Task`**, показывающий, как в «окне тишины» обрабатываются флаги `cmd_state.erase_req`, `cmd_state.ext_write_pending` и `cmd_state.int_write_pending` с обновлением `cmd_state.flash_status`.
