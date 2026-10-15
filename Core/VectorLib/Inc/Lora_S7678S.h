#ifndef _LORA_S7678S_H_
#define _LORA_S7678S_H_

//===========================================================================================================================
// 1. АППАРАТНАЯ КОНФИГУРАЦИЯ
//===========================================================================================================================
#if(DEVICE_NUMBER == Dev1)
    #define USART_LORA		(huart4)
#endif

#define COUNT_CHAN_LORA	(COUNT_CHAN)

#define LORA_REGION_EU868    0
#define LORA_REGION_RU864    1
#define LORA_MAX_CHANNELS    10

#define FLASH_ADDRESS_APPKEY    (0x080FF000)

#define TX_BUSY_TIMEOUT_TX_MS      10000
#define TX_BUSY_RX_WINDOW_MS        1500

//===========================================================================================================================
// 2. БАЗОВЫЕ КОНСТАНТЫ ПРОТОКОЛА
//===========================================================================================================================
#define DATA_STRUCT_LORA_VER 	(4)

typedef enum
{
    TYPE_MSG_DYNAMIC 	= (0x00),
    TYPE_MSG_ACK		= (0x40),
    TYPE_MSG_STATIC 	= (0x80),
    TYPE_MSG_TURN_OFF 	= (0xC0)
} TYPE_MSG_LORA;

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
    ST_LORA_BIT_GROUP       = 0,
    ST_LORA_BIT_BACK_GROUP  = 1,
    ST_LORA_BIT_SOS_GROUP   = 2,
    ST_LORA_BIT_ACTIV_MOD   = 3,
    ST_LORA_BIT_ZONE        = 4,
    ST_LORA_BIT_SOS         = 5,
    ST_LORA_BIT_ECHO        = 6,
    ST_LORA_BIT_CURRENT_C   = 7,
    ST_LORA_BIT_EVACUATION  = 8
};

#define ST_LORA_BIT_DATA_TYPE ST_LORA_BIT_CURRENT_C

enum STATUS_LORA_2
{
    ST_LORA2_REQ_STATIC   = 0,
    ST_LORA2_SET_CLASS_C  = 3,
};

//===========================================================================================================================
// 4. МАКРОСЫ КОНФИГУРАЦИИ
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
// 5. МАСКИ ПОЛЕЙ (MIRAX v4)
//===========================================================================================================================

// --- UPLINK CONTAINER MASK ---
#define CONTAINER_MASK_BATTERY       (1u << 0)
#define CONTAINER_MASK_GPS           (1u << 1)
#define CONTAINER_MASK_GPS_TRACK     (1u << 2)
#define CONTAINER_MASK_TIME_REF      (1u << 3)
#define CONTAINER_MASK_DEV_STATUS    (1u << 4)
#define CONTAINER_MASK_ENVIRONMENT   (1u << 5)
#define CONTAINER_MASK_MOTION        (1u << 6)
#define CONTAINER_MASK_PPE           (1u << 7)
#define CONTAINER_MASK_BLE_BEACONS   (1u << 8)
#define CONTAINER_MASK_EVENT_LOG     (1u << 9)
#define CONTAINER_MASK_GPS_HISTORY   (1u << 10)
#define CONTAINER_MASK_CONFIG_ACK    (1u << 11)
#define CONTAINER_MASK_MSG_ACK       (1u << 12)

// --- SUBDEVICE TYPES ---
#define SUB_TYPE_GAS                 (0u)
#define SUB_TYPE_PULSE               (1u)

// --- SUBDEVICE IDS ---
#define SUB_ID_PULSE_DEFAULT         (1u)
#define SUB_ID_GAS_DEFAULT           (2u)

// --- SUBDEVICE MASK: SUB_TYPE_GAS ---
#define SUB_MASK_GAS_BATTERY         (1u << 0)
#define SUB_MASK_GAS_SENSORS         (1u << 1)
#define SUB_MASK_GAS_EVENT_LOG       (1u << 2)

// --- SUBDEVICE MASK: SUB_TYPE_PULSE ---
#define SUB_MASK_PULSE_BATTERY       (1u << 0)
#define SUB_MASK_PULSE_HEART_RATE    (1u << 3)   // bit 3
#define SUB_MASK_PULSE_SPO2          (1u << 4)   // bit 4

// --- DOWNLINK FIELD MASK ---
#define FIELD_MASK_DL_CONTROL        (1u << 0)
#define FIELD_MASK_DL_MODE           (1u << 1)
#define FIELD_MASK_DL_PERIOD_NORM    (1u << 2)
#define FIELD_MASK_DL_PERIOD_ACT     (1u << 3)
#define FIELD_MASK_DL_GPS_FIX        (1u << 4)
#define FIELD_MASK_DL_CONFIG_SEQ     (1u << 5)
#define FIELD_MASK_DL_TIME           (1u << 6)
#define FIELD_MASK_DL_MESSAGE        (1u << 7)

//===========================================================================================================================
// MOTION (MIRAX v4)
//===========================================================================================================================
#define MOTION_POSTURE_MASK          (0x03u)

#define MOTION_POSTURE_UNKNOWN       (0u)
#define MOTION_POSTURE_VERTICAL      (1u)
#define MOTION_POSTURE_HORIZONTAL    (2u)

#define MOTION_BIT_FALL              (1u << 2)
#define MOTION_BIT_IMPACT            (1u << 3)
#define MOTION_BIT_INACTIVE          (1u << 4)

//===========================================================================================================================
// PPE (MIRAX v4)
//===========================================================================================================================
#define PPE_TYPE_HELMET              (0u)
#define PPE_TYPE_HARNESS             (1u)
#define PPE_TYPE_MASK                (2u)
#define PPE_TYPE_GLOVES              (3u)
#define PPE_TYPE_GLASSES             (4u)
#define PPE_TYPE_BOOTS               (5u)

#define PPE_CHARGE_LOW               (0u)
#define PPE_CHARGE_WARNING           (1u)
#define PPE_CHARGE_OK                (2u)

//===========================================================================================================================
// BLE BEACONS (MIRAX v4)
//===========================================================================================================================
#define BLE_BATTERY_LOW              (0u)
#define BLE_BATTERY_WARNING          (1u)
#define BLE_BATTERY_OK               (2u)

//===========================================================================================================================
// CONTAINER EVENT LOG (MIRAX v4)
//===========================================================================================================================
#define CONTAINER_EVENT_AQI_THRESHOLD        (0x01u)
#define CONTAINER_EVENT_HUMIDITY_THRESHOLD   (0x02u)
#define CONTAINER_EVENT_TEMP_THRESHOLD       (0x03u)
#define CONTAINER_EVENT_PULSE_THRESHOLD      (0x04u)

#define CONTAINER_EVENT_INACTIVITY           (0x10u)
#define CONTAINER_EVENT_HORIZONTAL           (0x11u)
#define CONTAINER_EVENT_VERTICAL             (0x12u)
#define CONTAINER_EVENT_FALL                 (0x13u)
#define CONTAINER_EVENT_IMPACT               (0x14u)
#define CONTAINER_EVENT_SOS                  (0x15u)

#define CONTAINER_EVENT_PPE_NOT_WORN         (0x20u)
#define CONTAINER_EVENT_PPE_OK               (0x21u)
#define CONTAINER_EVENT_PPE_CHARGE_THRESHOLD (0x22u)

//===========================================================================================================================
// РАЗМЕРЫ И ОПЦИИ
//===========================================================================================================================
#define LORA_MESSAGE_MAX_LEN          (237u)
#define LORA_MAX_BLE_BEACONS          (8u)
#define LORA_CONTAINER_EVENT_MAX      (16u)
#define LORA_MAX_PPE                  (8u)

// Если батарея одна на всё устройство, можно дублировать её
// в контейнер и в газовый субдевайс.
#define LORA_INCLUDE_GAS_SUB_BATTERY  (1)

// Если пульсометра нет, оставляем 0.
// Если будет, ставим 1 и заполняем pulse_state через API.
#define LORA_INCLUDE_PULSE_SUBDEVICE  (0)

//===========================================================================================================================
// 6. СТРУКТУРЫ ДАННЫХ
//===========================================================================================================================
typedef struct
{
    uint16_t num_formula_gas;
    uint8_t  Setting;
} __attribute__((__packed__)) sensor_data_lora_1_t;

typedef sensor_data_lora_1_t channel_cfg_t;

typedef struct
{
    uint8_t  Rx1DrOffset;
    uint8_t  Rx2DataRate;
    uint32_t Rx2FrequencyHz;
    uint8_t  Rx1DelaySec;
    uint8_t  TxPower;
    uint8_t  MaxDutyCycle;
} __attribute__((__packed__)) radio_cfg_t;

// TYPE_MSG_DYNAMIC (MIRAX v4)
typedef struct
{
    uint8_t  Header;
    uint16_t ContainerMask;
    uint8_t  Payload[256 - 3];
} __attribute__((__packed__)) str_lora_data_set_t;

// TYPE_MSG_TURN_OFF (MIRAX v4)
typedef struct
{
    uint8_t  data_struct_ver;
    uint16_t dev_id;
    uint8_t  dev_hw_ver;
    uint8_t  dev_fw_ver;
    uint16_t ControlLora;
} __attribute__((__packed__)) str_lora_data_set_2_t;

// TYPE_MSG_ACK (MIRAX v4)
typedef struct
{
    uint8_t  data_struct_ver;
    uint16_t ControlLora;
} __attribute__((__packed__)) str_lora_data_set_ack_t;

// DOWNLINK (MIRAX v4)
typedef struct
{
    uint8_t  Header;
    uint16_t FieldMask;

    uint16_t ControlLoraMask;
    uint16_t ControlLoraValue;

    uint8_t  ControlLora2Mask;
    uint8_t  ControlLora2Value;

    uint8_t  NewPeriodNorm;
    uint8_t  NewPeriodAct;
    uint8_t  GpsFixInterval;

    uint8_t  ConfigSeq;
    uint32_t UnixTime;

    uint8_t  MsgPresent;
    uint8_t  MsgSeq;
    uint8_t  MsgLength;

} __attribute__((__packed__)) str_lora_data_get_t;

//===========================================================================================================================
// БЛОКИ MIRAX v4
//===========================================================================================================================
typedef struct
{
    uint8_t Charge;
} __attribute__((__packed__)) battery_block_t;

typedef struct
{
    int32_t Latitude;
    int32_t Longitude;
} __attribute__((__packed__)) gps_block_t;

typedef struct
{
    int8_t Dx;
    int8_t Dy;
} __attribute__((__packed__)) point_delta_t;

typedef struct
{
    uint8_t Count;
    point_delta_t Points[];
} __attribute__((__packed__)) gps_track_t;

typedef struct
{
    uint32_t UnixTime;
} __attribute__((__packed__)) timeref_block_t;

typedef struct
{
    int8_t Dx;
    int8_t Dy;
} __attribute__((__packed__)) history_point_t;

typedef struct
{
    uint8_t  Count;
    uint16_t Dt0;
    uint16_t Step;
    uint8_t  Scale;
    history_point_t Points[];
} __attribute__((__packed__)) gpshistory_block_t;

typedef struct
{
    uint16_t AirQualityIndex;
    int16_t  Temperature;      // °C × 10
    uint8_t  Humidity;         // %
    uint16_t Pressure;         // гПа × 10
} __attribute__((__packed__)) environment_block_t;

typedef struct
{
    uint8_t StatusBits;
    uint8_t ActivityLevel;
} __attribute__((__packed__)) motion_block_t;

typedef struct
{
    uint8_t  Type;
    uint16_t Serial;
} __attribute__((__packed__)) ppe_descr_t;

typedef struct
{
    uint8_t PresenceMask;
    uint8_t Charge[];
} __attribute__((__packed__)) ppe_block_t;

typedef struct
{
    uint16_t ShortID;
    int8_t   Rssi;
    uint8_t  Battery;
} __attribute__((__packed__)) ble_beacon_t;

typedef struct
{
    uint8_t Count;
    ble_beacon_t Beacons[];
} __attribute__((__packed__)) blebeacons_block_t;

typedef struct
{
    uint16_t Dt;
    int16_t  Dx;
    int16_t  Dy;
    uint8_t  EventType;
    int16_t  Value;
} __attribute__((__packed__)) container_event_t;

typedef struct
{
    uint8_t Count;
    container_event_t Events[];
} __attribute__((__packed__)) container_eventlog_block_t;

typedef struct
{
    uint8_t MsgSeq;
    uint8_t Length;
    uint8_t Text[];
} __attribute__((__packed__)) message_block_t;

typedef struct
{
    uint8_t SubID;
    uint8_t SubType;
    uint16_t Serial;
    uint8_t CfgLen;
    uint8_t Cfg[];
} __attribute__((__packed__)) sub_descr_t;

//===========================================================================================================================
// 7. КОМАНДЫ И СТРОКОВЫЕ КОНСТАНТЫ
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
#define RES_MAC_CMD 		("mac command (downlink)")
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
// 9. PUBLIC API
//===========================================================================================================================

// Системные сообщения
extern void     Lora_ack_system_set(void);
extern void     Lora_turnoff_system_set(void);
extern void     Lora_system_msg_clr(void);
extern uint8_t  Lora_system_msg_read(void);

// Флаги инициализации и старта
extern uint8_t  get_state_init_flag_lora(void);
extern uint8_t  get_state_start_flag_lora(void);

// Флаги приема
extern void     Lora_flag_receive_set(void);
extern void     Lora_flag_receive_clr(void);
extern uint8_t  Lora_flag_receive_read(void);
extern uint8_t  get_flag_receive_res_tx_lora(void);

// Повторы и ошибки
extern uint8_t  get_state_repeat_flag_lora(void);
extern void     set_state_repeat_flag_lora(void);
extern void     clr_state_repeat_flag_lora(void);
extern uint8_t  get_state_err_lora(void);
extern void     clr_state_err_lora(void);

// Тревога и каналы
extern uint8_t  get_state_alarm_flag_lora(void);
extern void     set_state_alarm_flag_lora(void);
extern void     clr_state_alarm_flag_lora(void);
extern uint8_t  test_state_alarm_flag_lora(SNS_CFG *Cfg_struct);

// Основные функции
extern uint8_t  Lora_Init(SNS_CFG *Cfg_struct);
extern void     Lora_DeInit(uint8_t init);
extern void     Lora_DataSet(SNS_CFG *Cfg_struct);
extern void     Lora_SystemSleep(uint32_t time_s);
extern void     Lora_SystemWakeUp(void);
extern void     Lora_Receive(void);
extern void     Uart_Lora_Receive_Timer_Inc(void);
extern void     Lora_Cfg_ApplyDefaults(SNS_CFG *Cfg_struct);

// Статус и управление
extern uint8_t  Lora_IsTxBusy(void);
extern uint16_t Lora_GetControlLora(void);
extern void     Lora_SetControlLoraBit(uint8_t bit_num, uint8_t state);

// Time valid
extern void     Lora_SetTimeValid(uint8_t valid);
extern uint8_t  Lora_IsTimeValid(void);

// Message
extern uint8_t  Lora_MessageIsNew(void);
extern void     Lora_MessageDisplayed(void);
extern uint8_t  Lora_GetMessage(uint8_t **text, uint8_t *len, uint8_t *seq);

// Motion
extern void     Lora_SetMotionState(uint8_t posture, uint8_t inactive, uint8_t activity);
extern void     Lora_SetMotionEvent(uint8_t event_bits);
extern void     Lora_ClearMotionEvent(uint8_t event_bits);

// Environment
extern void     Lora_SetEnvironment(uint16_t aqi,
                                    int16_t temperature_x10,
                                    uint8_t humidity,
                                    uint16_t pressure_hpa_x10);

extern void     Lora_ClearEnvironment(void);

// PPE
extern void     Lora_SetPPEEnabledMask(uint8_t mask);
extern void     Lora_SetPPEDescriptor(uint8_t bit_index, uint8_t type, uint16_t serial);
extern void     Lora_SetPPEState(uint8_t presence_mask, const uint8_t *charge_by_bit);
extern void     Lora_ClearPPEState(void);

// BLE beacons
extern void     Lora_ClearBleBeacons(void);
extern uint8_t  Lora_AddBleBeacon(uint16_t short_id, int8_t rssi, uint8_t battery_state);

// Container events
extern void     Lora_AddContainerEvent(uint8_t event_type, int16_t value);

// Pulse (опционально)
extern void     Lora_SetPulsePresent(uint8_t present);
extern void     Lora_SetPulseSerial(uint16_t serial);
extern void     Lora_SetPulseData(uint8_t battery_percent, uint8_t heart_rate, uint8_t spo2_percent);

#if CONFIG_GPS
extern void Lora_UpdateGPSTrack(int32_t cur_lat, int32_t cur_lon);
#endif

#if LORA_DEBUG
extern void Test_Lora_Parsing(void);
extern void Lora_DebugPrintStatus(void);
#endif

//===========================================================================================================================
#endif /* _LORA_S7678S_H_ */
