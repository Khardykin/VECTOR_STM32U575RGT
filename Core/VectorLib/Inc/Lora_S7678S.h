#ifndef _LORA_S7678S_H_
#define _LORA_S7678S_H_

//===========================================================================================================================
// ОГЛАВЛЕНИЕ ФАЙЛА:
// 1. Аппаратная конфигурация (UART, количество каналов)
// 2. Базовые константы протокола (Версии, Типы сообщений)
// 3. Статусы и состояния (Enums, классы, запросы)
// 4. Макросы конфигурации (Побитовое хранение флагов LORA_CFG)
// 5. Маски полей (Field Masks для Uplink и Downlink)
// 6. Структуры данных (Payloads: Static, Dynamic, ACK, Downlink)
// 7. Команды и строковые константы для парсинга UART
// 8. Глобальные переменные и буферы
// 9. Public API (Прототипы функций, сгруппированные по назначению)
//===========================================================================================================================

//===========================================================================================================================
// 1. АППАРАТНАЯ КОНФИГУРАЦИЯ
//===========================================================================================================================
#if (DEVICE_NUMBER == Dev1)
	#define USART_LORA		(USART1)
#endif

#define COUNT_CHAN_LORA	(COUNT_CHAN)

#define LORA_REGION_EU868    0
#define LORA_REGION_RU864    1
#define LORA_MAX_CHANNELS    10

#define FLASH_ADDRESS_APPKEY    (0x0803A000)
//===========================================================================================================================
// ТАЙМЕР ЗАНЯТОСТИ МОДЕМА (защита от "MAC TX running")
// Тик Uart_Lora_Receive_Timer_Inc = 1 мс
//===========================================================================================================================
#define TX_BUSY_TIMEOUT_TX_MS      10000   // для ЛЮБОЙ отправки в эфир
#define TX_BUSY_RX_WINDOW_MS        1500   //  защитное окно после ЛЮБОГО ответа модема (Ok, accepted, err)
//===========================================================================================================================
// 2. БАЗОВЫЕ КОНСТАНТЫ ПРОТОКОЛА
//===========================================================================================================================
#define DATA_STRUCT_LORA_VER 	(3)

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

enum STATUS_LORA
{
    ST_LORA_BIT_GROUP = 0,
    ST_LORA_BIT_BACK_GROUP,
    ST_LORA_BIT_SOS_GROUP,
    ST_LORA_BIT_ACTIV_MOD,
    ST_LORA_BIT_ZONE,
    ST_LORA_BIT_SOS,
    ST_LORA_BIT_ECHO,
    ST_LORA_BIT_CURRENT_C = 7
};
#define ST_LORA_BIT_DATA_TYPE ST_LORA_BIT_CURRENT_C

enum STATUS_LORA_2
{
    ST_LORA2_REQ_STATIC   = 0,
    ST_LORA2_SET_CLASS_C  = 3,
    // Биты 1-2, 4-7 зарезервированы
};

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
// --- UPLINK (Прибор -> Сервер) ---
#define FIELD_MASK_BATTERY      (1 << 0)
#define FIELD_MASK_GPS          (1 << 1)
#define FIELD_MASK_SENSORS      (1 << 2)
#define FIELD_MASK_DEV_STATUS   (1 << 3)
#define FIELD_MASK_GPS_TRACK    (1 << 4) // Штатный трек (цепочка дельт int8)
#define FIELD_MASK_TIME_REF     (1 << 5) // Опора абсолютного времени (Unix)
#define FIELD_MASK_EVENT_LOG    (1 << 6) // Журнал событий за время потери связи
#define FIELD_MASK_GPS_HISTORY  (1 << 7) // Прореженная история GPS (int16 + Scale)
#define FIELD_MASK_CONFIG_ACK   (1 << 8) // Квитирование применённой конфигурации

// --- DOWNLINK (Сервер -> Прибор) ---
#define FIELD_MASK_DL_CONTROL       (1 << 0)  // Блок ControlLora (Mask + Value)
#define FIELD_MASK_DL_MODE          (1 << 1)  // Блок ControlLora2 (Mask + Value)
#define FIELD_MASK_DL_PERIOD_NORM   (1 << 2)  // Штатный период
#define FIELD_MASK_DL_PERIOD_ACT    (1 << 3)  // Аварийный период
#define FIELD_MASK_DL_GPS_FIX       (1 << 4)  // Интервал фиксации GPS
#define FIELD_MASK_DL_CONFIG_SEQ    (1 << 5)  // Версия конфигурации
#define FIELD_MASK_DL_TIME          (1 << 6)  // Unix-время для RTC

//===========================================================================================================================
// 6. СТРУКТУРЫ ДАННЫХ (PAYLOADS)
//===========================================================================================================================
typedef struct
{
    uint16_t num_formula_gas;
    uint8_t  Setting;
} __attribute__ ((__packed__)) sensor_data_lora_1_t;

// TYPE_MSG_STATIC
typedef struct
{
    uint8_t  data_struct_ver;
    uint16_t dev_id;
    uint8_t  dev_hw_ver;
    uint8_t  dev_fw_ver;
    uint8_t  sensors_enabled;
    sensor_data_lora_1_t sensors[COUNT_CHAN_LORA];
} __attribute__ ((__packed__)) str_lora_data_set_1_t;

// TYPE_MSG_DYNAMIC (Основной пакет)
typedef struct
{
    uint8_t  Header;
    uint16_t FieldMask;
    uint8_t  Payload[256 - 3];
} __attribute__ ((__packed__)) str_lora_data_set_t;

// TYPE_MSG_TURN_OFF
typedef struct
{
    uint8_t  data_struct_ver;
    uint16_t dev_id;
    uint8_t  dev_hw_ver;
    uint8_t  dev_fw_ver;
    uint8_t  ControlLora;
} __attribute__ ((__packed__)) str_lora_data_set_2_t;

// TYPE_MSG_ACK
typedef struct
{
    uint8_t  data_struct_ver;
    uint8_t  ControlLora;
} __attribute__ ((__packed__)) str_lora_data_set_ack_t;

// DOWNLINK (Сервер -> Прибор)
typedef struct
{
    uint8_t  Header;              // 1 байт: 0x03 (TYPE_CONFIG + Version 3)
    uint16_t FieldMask;           // 2 байта: маска присутствующих блоков (little-endian)
    uint8_t  ControlLoraMask;     // 1 байт: какие биты ControlLora изменить
    uint8_t  ControlLoraValue;    // 1 байт: новые значения битов ControlLora
    uint8_t  ControlLora2Mask;    // 1 байт: какие биты ControlLora2 изменить
    uint8_t  ControlLora2Value;   // 1 байт: новые значения битов ControlLora2
    uint8_t  NewPeriodNorm;       // 1 байт: штатный период (если бит 2 установлен)
    uint8_t  NewPeriodAct;        // 1 байт: аварийный период (если бит 3 установлен)
    uint8_t  GpsFixInterval;      // 1 байт: интервал фиксации GPS (если бит 4 установлен)
    uint8_t  ConfigSeq;
    uint32_t UnixTime;
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
#define RES_TX_OK  			("tx_ok")
#define RES_RX 				("mac rx")
#define RES_ERR  			("err")
#define RES_INV_DATA_LEN	("invalid_data_length")
#define RES_ACCEPTED		("accepted")

//===========================================================================================================================
// 8. ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ И БУФЕРЫ
//===========================================================================================================================
typedef struct
{
    uint8_t receive[256];
    uint8_t transmit[400];
    uint8_t count_byter_r;
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
extern uint8_t  Lora_GetControlLora(void);
extern void     Lora_SetControlLoraBit(uint8_t bit_num, uint8_t state);

// 9.8. Опциональные модули (GPS, Debug)
#if CONFIG_GPS
extern void Lora_UpdateGPSTrack(int32_t cur_lat, int32_t cur_lon);
#endif

#if LORA_DEBUG
extern void Test_Lora_Parsing(void);
#endif

#if LORA_DEBUG
extern void Lora_DebugPrintStatus(void);
#endif

//===========================================================================================================================
#endif /* _LORA_S7678S_H_ */
