/**
  ******************************************************************************
  * @file    Lora_S7678S.h
  * @brief   LoRa-модуль S7678S: протокол и API драйвера (файл автора)
  *
  *          ПРОТОКОЛ v4 (MIRAX, docs/Mirax-Lora-protocol-v4.md; v28 - апгрейд
  *          с v3 без переименования функций/структур): трекер агрегирует
  *          телеметрию комплекта - контейнерные блоки (батарея/GPS/среда/
  *          движение/СИЗ/BLE-маяки/журнал/история) + секции под-устройств
  *          (SubID+SubMask; AVIS = SUB_TYPE_GAS, данные из Connected_device /
  *          Ble_telemetry по BLE). ControlLora расширен до 16 бит (+бит 8
  *          EVACUATION), downlink +блок Message (текст CP1251 на дисплей) и
  *          uplink +MsgAck, TYPE_STATIC v4 (радиопараметры + СИЗ + под-
  *          устройства), EventLog разделён: контейнерный (типизированный,
  *          10-байтовые записи) и газовый (v3-формат, в секции AVIS).
  *
  *          Оглавление файла - ниже (разделы 1-9): аппаратная конфигурация
  *          (USART_LORA, COUNT_CHAN_LORA от COUNT_CHAN), константы протокола,
  *          статусы/состояния, флаги LORA_CFG, маски контейнера/под-устройств/
  *          downlink, структуры payload (Static/Dynamic/ACK/Downlink/Message),
  *          AT-строки.
  *          Реализация - Lora_S7678S.c (компилируется под CONFIG_LORA; сейчас
  *          CONFIG_LORA = 0: порт UART4 конфликтует с логом и не определён
  *          USART_RF - PLAN.md п.25/27, этап 2 плана).
  *          Контекст: Lora_Receive() - Receiver Task, Lora_Init()/Lora_Run() -
  *          Measure Task (weak-заглушки в Vector_main.c замещаются драйвером).
  ******************************************************************************
  */
#ifndef _LORA_S7678S_H_
#define _LORA_S7678S_H_

//===========================================================================================================================
// ОГЛАВЛЕНИЕ ФАЙЛА:
// 1. Аппаратная конфигурация (UART, количество каналов)
// 2. Базовые константы протокола (Версии, Типы сообщений)
// 3. Статусы и состояния (Enums, классы, запросы)
// 4. Макросы конфигурации (Побитовое хранение флагов LORA_CFG)
// 5. Маски полей (ContainerMask/SubMask v4 для Uplink и FieldMask для Downlink)
// 6. Структуры данных (Payloads: Static, Dynamic, ACK, Downlink, Message)
// 7. Команды и строковые константы для парсинга UART
// 8. Глобальные переменные и буферы
// 9. Public API (Прототипы функций, сгруппированные по назначению)
//===========================================================================================================================
#if CONFIG_LORA
//===========================================================================================================================
// 1. АППАРАТНАЯ КОНФИГУРАЦИЯ
//===========================================================================================================================
#if (DEVICE_NUMBER == Dev1)
	#define USART_LORA		(huart4)
#endif

#define COUNT_CHAN_LORA	(COUNT_CHAN)

#define LORA_REGION_EU868    0
#define LORA_REGION_RU864    1
#define LORA_MAX_CHANNELS    10

/* v28: инициализация UART модуля (S7678S: 115200 8N1). Раньше символ
   LORA_UART_Init() приходил из Avis-заголовков и в Vector не был определён -
   теперь макрос поверх usart_init/Uart_SetBaudrate (main.h, vector_macros.c). */
#define LORA_UART_BAUDRATE	(115200u)
#define LORA_UART_Init()	usart_init(USART_LORA, LORA_UART_BAUDRATE, USART_DATA_8BITS, USART_STOP_1_BIT)
//===========================================================================================================================
// ТАЙМЕР ЗАНЯТОСТИ МОДЕМА (защита от "MAC TX running")
// Тик Uart_Lora_Receive_Timer_Inc = 1 мс
//===========================================================================================================================
#define TX_BUSY_TIMEOUT_TX_MS      10000   // для ЛЮБОЙ отправки в эфир
#define TX_BUSY_RX_WINDOW_MS        1500   //  защитное окно после ЛЮБОГО ответа модема (Ok, accepted, err)
//===========================================================================================================================
// 2. БАЗОВЫЕ КОНСТАНТЫ ПРОТОКОЛА
//===========================================================================================================================
#define DATA_STRUCT_LORA_VER 	(4)	// v28: протокол MIRAX v4 (docs/Mirax-Lora-protocol-v4.md)

typedef enum
{
    TYPE_MSG_DYNAMIC 	= (0x00),
    TYPE_MSG_ACK		= (0x40),
    TYPE_MSG_STATIC 	= (0x80),
    TYPE_MSG_TURN_OFF 	= (0xC0)
} TYPE_MSG_LORA;

// 1 = Принудительно делать JOIN при каждом старте (даже если сессия сохранена)
// 0 = Полагаться на сохраненную сессию и не делать JOIN явно
#define LORA_FORCE_JOIN_ON_STARTUP    1

/* v4 п.5: локальные индексы под-устройств комплекта (SubID в TYPE_STATIC и в
   секциях TYPE_DYNAMIC). Газоанализатор AVIS - один (решение автора).       */
#define LORA_SUBID_GAS_AVIS		(1)		// SUB_TYPE_GAS: газоанализатор AVIS (по BLE)
#define LORA_SUBID_PULSE		(2)		// SUB_TYPE_PULSE: пульсометр (задел, ещё не подключается)

/* v4 п.8.2 (пример): GasID кислорода в каталоге формул - для мёртвой зоны
   20.85..20.95. СВЕРИТЬ с каталогом/Avis v3 (коды num_formula_gas).         */
#define LORA_GAS_ID_O2			(68u)
//===========================================================================================================================
// 3. СТАТУСЫ И СОСТОЯНИЯ
//===========================================================================================================================
#define LORA_STATUS_WAITING  0
#define LORA_STATUS_SUCCESS  1
#define LORA_STATUS_ERROR    2
#define LORA_STATUS_UNKNOWN  3

#define LORA_REQ_CLASS_NONE  0
#define LORA_REQ_CLASS_A     1
#define LORA_REQ_CLASS_C     2

/* v4 п.4.2: ControlLora16 - биты 0..7 как в v3, бит 8 EVACUATION («Всем на
   выход», RW от сервера, подтверждается эхом в uplink). SOS (бит 5) и
   CURRENT_C (бит 7) защищены от перезаписи сервером (маска 0x00A0).        */
enum STATUS_LORA
{
    ST_LORA_BIT_GROUP = 0,
    ST_LORA_BIT_BACK_GROUP,
    ST_LORA_BIT_SOS_GROUP,
    ST_LORA_BIT_ACTIV_MOD,
    ST_LORA_BIT_ZONE,
    ST_LORA_BIT_SOS,
    ST_LORA_BIT_ECHO,
    ST_LORA_BIT_CURRENT_C = 7,
    ST_LORA_BIT_EVACUATION = 8	// v28/v4: команда эвакуации от сервера
};
#define ST_LORA_BIT_DATA_TYPE ST_LORA_BIT_CURRENT_C

/* Биты ControlLora16, которые сервер НЕ может перезаписать (SOS | CURRENT_C) */
#define ST_LORA16_PROTECTED_MASK	(0x00A0u)
/* Биты ControlLora16, чьё изменение требует квитирования ACK-пакетом
   (v3 маска 0x37 + v4 бит EVACUATION).                                     */
#define ST_LORA16_ACK_MASK			(0x0137u)

enum STATUS_LORA_2
{
    ST_LORA2_REQ_STATIC   = 0,
    ST_LORA2_SET_CLASS_C  = 3,
    // Биты 1-2, 4-7 зарезервированы
};

/* v4 п.4.5: биты StatusBytes блока Motion */
enum MOTION_BITS
{
    MOTION_POSTURE_MASK = 0x03,  // биты 0-1: поза (0 неизвестно, 1 вертикальное, 2 горизонтальное)
    MOTION_BIT_FALL     = 2,     // зафиксировано падение
    MOTION_BIT_IMPACT   = 3,     // зафиксирован удар (факт события)
    MOTION_BIT_INACTIVE = 4,     // прибор неподвижен (отсутствие активности)
    // биты 5-7 - зарезервировано
};
#define MOTION_POSTURE_UNKNOWN		(0u)
#define MOTION_POSTURE_VERTICAL		(1u)
#define MOTION_POSTURE_HORIZONTAL	(2u)

/* v4 п.4.6: состояние заряда СИЗ (байт Charge[k]; те же коды у Battery маяка) */
enum PPE_CHARGE_STATE
{
    PPE_CHARGE_LOW     = 0,  // разряжен
    PPE_CHARGE_WARNING = 1,  // предупреждение
    PPE_CHARGE_OK      = 2,  // в норме
    // 3..255 - зарезервировано
};

/* v4 п.4.8: коды EventType контейнерного журнала (таблица ПРЕДВАРИТЕЛЬНАЯ -
   не зафиксирована, сверять при согласовании перечня офлайн-событий).       */
enum CONTAINER_EVENT_TYPE
{
    CONT_EV_AIR_QUALITY   = 0x01,  // качество воздуха (порог), Value = индекс
    CONT_EV_HUMIDITY      = 0x02,  // влажность (порог), Value = %
    CONT_EV_TEMPERATURE   = 0x03,  // температура (порог), Value = °C×10
    CONT_EV_PULSE         = 0x04,  // пульс (порог), Value = уд/мин
    CONT_EV_ALTITUDE      = 0x05,  // высота (порог), Value = м
    CONT_EV_INACTIVE      = 0x10,  // отсутствие активности, Value = 0x8000
    CONT_EV_POSTURE_HOR   = 0x11,  // горизонтальное положение, Value = 0x8000
    CONT_EV_POSTURE_VER   = 0x12,  // вертикальное положение, Value = 0x8000
    CONT_EV_FALL          = 0x13,  // зафиксировано падение, Value = 0x8000
    CONT_EV_IMPACT        = 0x14,  // зафиксирован удар, Value = 0x8000
    CONT_EV_SOS           = 0x15,  // сигнал SOS, Value = 0x8000
    CONT_EV_PPE_OFF       = 0x20,  // СИЗ не надеты, Value = маска снятых
    CONT_EV_PPE_OK        = 0x21,  // СИЗ в норме, Value = 0x8000
    CONT_EV_PPE_CHARGE    = 0x22,  // заряд СИЗ (порог), Value = индекс единицы
};
#define CONT_EV_VALUE_NA	((int16_t)0x8000)	// "нет значения/нет фикса"

/* v4 п.4.8: Severity контейнерного события */
#define CONT_SEV_INFO		(0u)
#define CONT_SEV_WARNING	(1u)
#define CONT_SEV_CRITICAL	(2u)

/* v4 п.5: типы под-устройств */
enum SUB_TYPE
{
    SUB_TYPE_GAS   = 0,   // газоанализатор AVIS
    SUB_TYPE_PULSE = 1,   // пульсометр (браслет)
    // 2..255 - зарезервировано (новые носимые устройства)
};

/* v4 п.6/7: радиопараметры и прочее для TYPE_STATIC */
#define LORA_RX1_DELAY_SEC		(3u)	// фактический Rx1Delay, с (Lora_Init: CMD_SET_RXDELAY)
#define LORA_RX1_DR_OFFSET		(0u)	// EU868/RU864 по умолчанию (геттера в AT нет)
#define LORA_TX_POWER_REPORT	(0u)	// TxPower для статики: 0 = по умолчанию модулем (TODO: геттер)
#define LORA_MAX_DUTY_CYCLE		(0u)	// v4 п.7.1: комплекс работает с выключенным duty cycle
//===========================================================================================================================
// 4. МАКРОСЫ КОНФИГУРАЦИИ (Побитовое хранение в 2 байтах)
//===========================================================================================================================
#define LORA_CFG_BIT_SAVED          (0)
#define LORA_CFG_BIT_CLASS_C        (1)
#define LORA_CFG_BIT_REGION_RU      (2)
#define LORA_CFG_BIT_RESERVED_0     (3)
#define LORA_CFG_BIT_RESERVED_1     (4)

#define LORA_CFG_DR_SHIFT           (5)
#define LORA_CFG_DR_MASK            (0x07)

#define LORA_CFG_GPSFIX_SHIFT       (8)
#define LORA_CFG_GPSFIX_MASK        (0xFF)

#define LORA_CFG_GET_DR(flags)          (((flags) >> LORA_CFG_DR_SHIFT) & LORA_CFG_DR_MASK)
#define LORA_CFG_SET_DR(flags, dr)      do { (flags) &= ~(LORA_CFG_DR_MASK << LORA_CFG_DR_SHIFT); (flags) |= (((dr) & LORA_CFG_DR_MASK) << LORA_CFG_DR_SHIFT); } while(0)
#define LORA_CFG_GET_GPSFIX(flags)      (((flags) >> LORA_CFG_GPSFIX_SHIFT) & LORA_CFG_GPSFIX_MASK)
#define LORA_CFG_SET_GPSFIX(flags, val) do { (flags) &= ~(LORA_CFG_GPSFIX_MASK << LORA_CFG_GPSFIX_SHIFT); (flags) |= (((val) & LORA_CFG_GPSFIX_MASK) << LORA_CFG_GPSFIX_SHIFT); } while(0)

//===========================================================================================================================
// 5. МАСКИ ПОЛЕЙ (FIELD MASKS)
//===========================================================================================================================
// --- UPLINK v4 (Прибор -> Сервер): ContainerMask, биты по п.3.1 протокола ---
// (v3-маски FIELD_MASK_* упразднены - в v4 биты переразделены;downlink-имя
//  FieldMask сохранено, раскладка битов downlink в v4 НЕ изменилась)
#define CONTAINER_MASK_BATTERY      (1u << 0)   // заряд трекера, 1 Б
#define CONTAINER_MASK_GPS          (1u << 1)   // полная координата, 8 Б
#define CONTAINER_MASK_GPS_TRACK    (1u << 2)   // текущий трек (дельты int8), 1+2N
#define CONTAINER_MASK_TIME_REF     (1u << 3)   // опора абсолютного времени, 4 Б
#define CONTAINER_MASK_DEV_STATUS   (1u << 4)   // ControlLora16, 2 Б (в v3 был бит 3, 1 Б)
#define CONTAINER_MASK_ENVIRONMENT  (1u << 5)   // среда: AQI/T/H/P/Alt, 9 Б
#define CONTAINER_MASK_MOTION       (1u << 6)   // положение/падение/активность, 2 Б
#define CONTAINER_MASK_PPE          (1u << 7)   // СИЗ: PresenceMask + заряды, 1+K
#define CONTAINER_MASK_BLE_BEACONS  (1u << 8)   // BLE-маяки: Count + 4K
#define CONTAINER_MASK_EVENT_LOG    (1u << 9)   // контейнерный журнал, 1+10N
#define CONTAINER_MASK_GPS_HISTORY  (1u << 10)  // история GPS, 6+2N
#define CONTAINER_MASK_CONFIG_ACK   (1u << 11)  // AppliedRev, 1 Б (в v3 бит 8)
#define CONTAINER_MASK_MSG_ACK      (1u << 12)  // MsgSeq доставленного сообщения, 1 Б

// --- UPLINK v4: SubMask секции под-устройства (п.5) ---
// Газоанализатор AVIS (SUB_TYPE_GAS):
#define SUB_MASK_GAS_BATTERY        (1u << 0)   // uint8 заряд, %
#define SUB_MASK_GAS_SENSORS        (1u << 1)   // Count + sensor_data_t[] (v3 п.4.3)
#define SUB_MASK_GAS_EVENT_LOG      (1u << 2)   // Count + event_record_t[] (v3 п.4.8, канальный)
// Пульсометр (SUB_TYPE_PULSE, задел):
#define SUB_MASK_PULSE_BATTERY      (1u << 0)
#define SUB_MASK_PULSE_HEARTRATE    (1u << 1)
#define SUB_MASK_PULSE_SPO2         (1u << 2)

// --- DOWNLINK (Сервер -> Прибор), v4 п.7: биты 0-6 как в v3, +бит 7 Message ---
#define FIELD_MASK_DL_CONTROL       (1 << 0)  // Блок Control (Mask u16 + Value u16, v4: 4 Б)
#define FIELD_MASK_DL_MODE          (1 << 1)  // Блок ControlLora2 (Mask + Value)
#define FIELD_MASK_DL_PERIOD_NORM   (1 << 2)  // Штатный период
#define FIELD_MASK_DL_PERIOD_ACT    (1 << 3)  // Аварийный период
#define FIELD_MASK_DL_GPS_FIX       (1 << 4)  // Интервал фиксации GPS
#define FIELD_MASK_DL_CONFIG_SEQ    (1 << 5)  // Версия конфигурации
#define FIELD_MASK_DL_TIME          (1 << 6)  // Unix-время для RTC
#define FIELD_MASK_DL_MESSAGE       (1 << 7)  // v28/v4 п.7.1: текст на дисплей (CP1251)

//===========================================================================================================================
// 6. СТРУКТУРЫ ДАННЫХ (PAYLOADS)
//===========================================================================================================================
/* Конфигурация газового канала (v3 п.4.11 = v4 п.6 channel_cfg_t: GasID u16 +
   Setting u8, 3 Б) - в TYPE_STATIC v4 идёт в Cfg[] дескриптора SUB_TYPE_GAS. */
typedef struct
{
    uint16_t num_formula_gas;
    uint8_t  Setting;
} __attribute__ ((__packed__)) sensor_data_lora_1_t;

/* v4 п.6: фактические радиопараметры трекера (ground truth, 9 Б) */
typedef struct
{
    uint8_t  Rx1DrOffset;
    uint8_t  Rx2DataRate;
    uint32_t Rx2FrequencyHz;
    uint8_t  Rx1DelaySec;
    uint8_t  TxPower;
    uint8_t  MaxDutyCycle;
} __attribute__ ((__packed__)) radio_cfg_t;

/* v4 п.6: описание единицы СИЗ (в порядке битов PPEEnabledMask, 3 Б) */
typedef struct
{
    uint8_t  Type;             // тип СИЗ (приложение протокола: 0 каска, 1 привязь...)
    uint16_t Serial;           // заводской идентификатор единицы
} __attribute__ ((__packed__)) ppe_descr_t;

/* TYPE_MSG_STATIC v4 (п.6): фиксированная часть + хвост переменной длины
   (PPEEnabledMask описания СИЗ + SubCount + дескрипторы под-устройств с
   CfgLen/Cfg[]). Хвост собирается побайтно в Tail[] (Lora_CompleteDataSet_1). */
#define LORA_STATIC_TAIL_MAX	(64u)	// 8×ppe_descr_t + SubCount + 2×sub_descr(с Cfg) с запасом
typedef struct
{
    uint8_t  data_struct_ver;	// Header: TYPE_MSG_STATIC | DATA_STRUCT_LORA_VER
    uint16_t dev_id;			// заводской идентификатор трекера (TODO: DEVICE_NUMBER vs SerialLo - см. PLAN)
    uint8_t  dev_hw_ver;
    uint8_t  dev_fw_ver;
    radio_cfg_t Radio;			// фактические радиопараметры (v4 п.6)
    uint8_t  PPEEnabledMask;	// какие из 8 позиций СИЗ заведены в комплекте
    uint8_t  Tail[LORA_STATIC_TAIL_MAX];	// ppe_descr_t[] + SubCount + sub_descr_t[]
} __attribute__ ((__packed__)) str_lora_data_set_1_t;

// TYPE_MSG_DYNAMIC v4 (основной пакет: контейнер + секции под-устройств)
typedef struct
{
    uint8_t  Header;			// TYPE_MSG_DYNAMIC | DATA_STRUCT_LORA_VER
    uint16_t ContainerMask;		// v4 п.3.1: состав контейнерной части
    uint8_t  Payload[256 - 3];
} __attribute__ ((__packed__)) str_lora_data_set_t;

// TYPE_MSG_TURN_OFF (v4 п.9: статус расширен до uint16)
typedef struct
{
    uint8_t  data_struct_ver;
    uint16_t dev_id;
    uint8_t  dev_hw_ver;
    uint8_t  dev_fw_ver;
    uint16_t ControlLora;		// v28/v4: ControlLora16
} __attribute__ ((__packed__)) str_lora_data_set_2_t;

// TYPE_MSG_ACK (v4 п.9: статус расширен до uint16)
typedef struct
{
    uint8_t  data_struct_ver;
    uint16_t ControlLora;		// v28/v4: ControlLora16
} __attribute__ ((__packed__)) str_lora_data_set_ack_t;

/* v4 п.4.4: блок Environment (9 Б). Единицы: Temperature °C×10, Pressure
   гПа×10 (= sensors_status.Pressure напрямую), Altitude - барометрическая
   высота, м (считается из давления; самостоятельная величина со своими
   порогами). AirQualityIndex - задел (датчика AQI пока нет, шлём 0).        */
typedef struct
{
    uint16_t AirQualityIndex;
    int16_t  Temperature;
    uint8_t  Humidity;
    uint16_t Pressure;
    int16_t  Altitude;
} __attribute__ ((__packed__)) environment_block_t;

/* v4 п.4.5: блок Motion (2 Б) */
typedef struct
{
    uint8_t StatusBits;			// enum MOTION_BITS
    uint8_t ActivityLevel;		// 0..255 (задел: источник активности не определён, шлём 0)
} __attribute__ ((__packed__)) motion_block_t;

/* v4 п.4.8: запись контейнерного журнала (10 Б, на проводе) */
typedef struct
{
    uint16_t Dt;				// секунд ВПЕРЁД от TimeRef
    int16_t  Dx;				// смещение широты от опоры, м (0x8000 - нет фикса)
    int16_t  Dy;				// смещение долготы от опоры, м (0x8000 - нет фикса)
    uint8_t  EventType;			// enum CONTAINER_EVENT_TYPE
    uint8_t  Severity;			// 0 инфо / 1 предупреждение / 2 критическое
    int16_t  Value;				// значение величины (0x8000 - неприменимо)
} __attribute__ ((__packed__)) container_event_t;

/* v4 п.7.1: текстовое сообщение сервера на дисплей (CP1251). Максимум
   текста = лимит payload DR - 5 (DR0: 46, DR3: 110, DR4-5: 237 символов).  */
#define LORA_MSG_TEXT_MAX	(237u)
typedef struct
{
    uint8_t  MsgSeq;			// идентификатор сообщения (квитируется в MsgAck)
    uint8_t  Length;			// длина текста, байт (= символов CP1251)
    uint8_t  New;				// 1 = принято НОВОЕ сообщение (не показанное); снимает потребитель (меню/индикация) через Lora_MessageShown()
    uint8_t  Text[LORA_MSG_TEXT_MAX];
} lora_msg_t;
extern lora_msg_t Lora_Message;	// последнее принятое сообщение (экземпляр - Lora_S7678S.c)

// DOWNLINK (Сервер -> Прибор), v4 п.7
typedef struct
{
    uint8_t  Header;              // 1 байт: 0x04 (TYPE_CONFIG + Version 4)
    uint16_t FieldMask;           // 2 байта: маска присутствующих блоков (little-endian)
    uint16_t ControlLoraMask;     // v28/v4: 2 байта - какие биты ControlLora16 изменить
    uint16_t ControlLoraValue;    // v28/v4: 2 байта - новые значения битов ControlLora16
    uint8_t  ControlLora2Mask;    // 1 байт: какие биты ControlLora2 изменить
    uint8_t  ControlLora2Value;   // 1 байт: новые значения битов ControlLora2
    uint8_t  NewPeriodNorm;       // 1 байт: штатный период (если бит 2 установлен)
    uint8_t  NewPeriodAct;        // 1 байт: аварийный период (если бит 3 установлен)
    uint8_t  GpsFixInterval;      // 1 байт: интервал фиксации GPS (если бит 4 установлен)
    uint8_t  ConfigSeq;
    uint32_t UnixTime;
    uint8_t  MsgSeq;              // v28/v4 п.7.1: идентификатор сообщения
    uint8_t  MsgLen;              // v28/v4 п.7.1: длина текста
    uint8_t  MsgText[LORA_MSG_TEXT_MAX];	// текст CP1251
} __attribute__ ((__packed__)) str_lora_data_get_t;

//===========================================================================================================================
// 7. КОМАНДЫ И СТРОКОВЫЕ КОНСТАНТЫ ДЛЯ ПАРСИНГА UART
//===========================================================================================================================
typedef enum
{
    CMD_TX_UCNF = 0,
    CMD_TX_CNF,
    CMD_SET_CH_FR,
    CMD_SET_CH_ST_ON,
    CMD_SET_CH_ST_OFF,
    CMD_SET_CH_JOIN_ON,
    CMD_SET_CH_JOIN_OFF,
    CMD_SET_DEVEUI,
    CMD_SET_APPEUI,
    CMD_SET_APPKEY,
    CMD_JOIN_OTAA,
    CMD_MAC_SAVE,
    CMD_SIP_SLEEP,
    CMD_SET_ADR,
    CMD_SET_DR,
    CMD_GET_DR,
    CMD_SET_TX_MODE,
    CMD_GET_TX_MODE,
    CMD_SET_UPCNT,
    CMD_GET_UPCNT,
    CMD_SET_TXRETRY,
    CMD_SET_RXDELAY,
    CMD_LBT_ON,
    CMD_SET_CR,
    CMD_SET_CLASS,
    CMD_SET_RX2,
    CMD_SET_DEVADDR,
    CMD_SET_NWKSKEY,
    CMD_SET_APPSKEY,
    CMD_SET_RT_STORE,
} COMMAND_LORA;

#define UART_ON				(" uart_on")
#define SIZE_UART_ON        (sizeof(UART_ON) - 1)

#define RES_OK 				("Ok")
#define RES_MAC_RUN 		("MAC TX running")
#define RES_NOT_JOINED  	("not_joined")
#define RES_NO_FREE_CH  	("no_free_ch")
#define RES_TX_OK 			("tx_ok")
#define RES_RX 				("mac rx")
#define RES_ERR  			("err")
#define RES_INV_DATA_LEN	("invalid_data_length")
#define RES_ACCEPTED		("accepted")

//===========================================================================================================================
// 8. ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ И БУФЕРЫ
//===========================================================================================================================
/* v28: receive увеличен 256->512 (downlink v4 с Message до 242 байт приходит
   hex-строкой: 484 символа + префикс "mac rx NN "); transmit 400->560
   (uplink до 253 байт: AT-команда + hex 506 символов).                     */
typedef struct
{
    uint8_t receive[512];
    uint8_t transmit[560];
    uint16_t count_byter_r;   // v28: uint16 - receive увеличен до 512 (hex downlink v4)
    uint8_t TimeRX;
    uint8_t TimeFlagRX;
} Message_buffer_uart_lora;

extern Message_buffer_uart_lora buffer_uart_lora;
extern uint8_t deveui[8];
extern uint8_t appkey[16];

//===========================================================================================================================
// 9. PUBLIC API (ПРОТОТИПЫ ФУНКЦИЙ)
//===========================================================================================================================

// 9.1. Системные сообщения
extern void     Lora_ack_system_set(void);
extern void     Lora_turnoff_system_set(void);
extern void     Lora_system_msg_clr(void);
extern uint8_t  Lora_system_msg_read(void);

// 9.2. Флаги инициализации и старта
extern uint8_t  get_state_init_flag_lora(void);
extern uint8_t  get_state_start_flag_lora(void);

// 9.3. Флаги приема из UART и таймингов
extern void     Lora_flag_receive_set(void);
extern void     Lora_flag_receive_clr(void);
extern uint8_t  Lora_flag_receive_read(void);
extern uint8_t  get_flag_receive_res_tx_lora(void);

// 9.4. Флаги повтора и ошибок
extern uint8_t  get_state_repeat_flag_lora(void);
extern void     set_state_repeat_flag_lora(void);
extern void     clr_state_repeat_flag_lora(void);
extern uint8_t  get_state_err_lora(void);
extern void     clr_state_err_lora(void);

// 9.5. Управление флагами тревоги и каналов
extern uint8_t  get_state_alarm_flag_lora(void);
extern void     set_state_alarm_flag_lora(void);
extern void     clr_state_alarm_flag_lora(void);
extern uint8_t  test_state_alarm_flag_lora(SNS_CFG *Cfg_struct);

// 9.6. Основные системные функции модуля LoRa
extern uint8_t  Lora_Init(SNS_CFG *Cfg_struct);
extern void     Lora_DeInit(uint8_t init);
extern void     Lora_DataSet(SNS_CFG *Cfg_struct);
extern void     Lora_SystemSleep(uint32_t time_s);
extern void     Lora_SystemWakeUp(void);
extern void     Lora_Receive(void);
extern void     Uart_Lora_Receive_Timer_Inc(void);
extern void     Lora_Cfg_ApplyDefaults(SNS_CFG *Cfg_struct);

// 9.7. API для работы с состоянием LoRa из ДРУГИХ файлов (Инкапсуляция)
extern uint8_t 	Lora_IsTxBusy(void);
extern uint16_t Lora_GetControlLora(void);	// v28/v4: ControlLora16 (биты enum STATUS_LORA)
extern void     Lora_SetControlLoraBit(uint8_t bit_num, uint8_t state);

// 9.8. Сообщение сервера на дисплей (v28/v4 п.7.1): потребитель (меню/индикация)
// при Lora_Message.New показывает Text[Length] (CP1251) и зовёт Lora_MessageShown().
// Квитирование MsgAck уходит в uplink автоматически.
extern void     Lora_MessageShown(void);

// 9.9. Опциональные модули (GPS, Debug)
#if CONFIG_GPS
extern void Lora_UpdateGPSTrack(int32_t cur_lat, int32_t cur_lon);
#endif

#if LORA_DEBUG
extern void Test_Lora_Parsing(void);
#endif

#if LORA_DEBUG
extern void Lora_DebugPrintStatus(void);
#endif
#endif
//===========================================================================================================================
#endif /* _LORA_S7678S_H_ */
