/**
  ******************************************************************************
  * @file    Lora_S7678S.c
  * @brief   Драйвер LoRa S7678S: AT-команды и payload (файл автора, CONFIG_LORA)
  *
  *          v28: ПРОТОКОЛ v4 (MIRAX, docs/Mirax-Lora-protocol-v4.md) -
  *          дополнение к рабочей v3-реализации БЕЗ переименования функций:
  *          TYPE_DYNAMIC = контейнерные блоки трекера по ContainerMask
  *          (батарея/GPS/трек/TimeRef/ControlLora16/среда/движение/СИЗ/
  *          BLE-маяки/контейнерный журнал/история GPS/ConfigAck/MsgAck) +
  *          SubCount и секции под-устройств (SubID+SubMask); газовые данные
  *          (Sensors/канальный EventLog/заряд) - в секции AVIS (SUB_TYPE_GAS,
  *          источник Connected_device/Ble_telemetry по BLE). ControlLora
  *          8->16 бит (+EVACUATION бит 8, защита SOS|CURRENT_C 0x00A0,
  *          ACK-маска 0x0137). TYPE_STATIC v4: +radio_cfg_t, +СИЗ, +дескрип-
  *          торы под-устройств (CfgLen/Cfg[]). Downlink: Control 4 Б, +блок
  *          Message (CP1251 -> Lora_Message, квитирование MsgAck). ACK/
  *          TURN_OFF: статус u16. Контейнерный EventLog - типизированные
  *          10-байтовые записи (падение/поза/SOS/СИЗ) alongside газового v3.
  *
  *          Блоки: таблица AT-команд и машина состояний парсинга ответов/URC,
  *          инициализация (Lora_Init: версия, класс, DR, частоты rx2 из
  *          SNS_CFG), передача (Lora_DataSet -> transmit_buffer TYPE_LORA,
  *          сборка uplink по COUNT_CHAN_LORA каналам), GPS-трек
  *          (Lora_UpdateGPSTrack), системные сообщения (ack/turnoff),
  *          занятость передатчика (Lora_IsTxBusy), DeInit/сон.
  *          Контекст: Lora_Receive() - Receiver Task (кольцо TYPE_LORA),
  *          Lora_Init()/Lora_Run()/Lora_DeInit() - Measure Task (вызовы и
  *          weak-заглушка Gps/Lora - Vector_main.c).
  *          Статус: CONFIG_LORA = 0 - порт не подключён (USART_LORA = UART4
  *          конфликтует с логом, USART_RF не определён - PLAN.md п.25/27).
  ******************************************************************************
  */
#include "main.h"
#include "Vector_main.h"
#include "lis3dh.h"     /* LIS3DH_ORIENT_*: поза для блока Motion (v4 п.4.5) */
#include <stdio.h>      /* sprintf/sscanf: AT-строки, парсинг hex */
#include <stdlib.h>     /* strtol */
#include <string.h>     /* memcpy/memset/strstr */
#include <math.h>       /* cosf/powf/lround: дельты метров, баровысота */

#if CONFIG_LORA

//===========================================================================================================================
typedef struct {
    const char* const cmd_str;
    const uint8_t     cmd_len;
} lora_cmd_info_t;

static const lora_cmd_info_t lora_commands[] = {
    [CMD_TX_UCNF]         = { "mac tx ucnf ",           sizeof("mac tx ucnf ") - 1 },
    [CMD_TX_CNF]          = { "mac tx cnf ",            sizeof("mac tx cnf ") - 1 },
    [CMD_SET_CH_FR]       = { "mac set_ch_freq ",       sizeof("mac set_ch_freq ") - 1 },
    [CMD_SET_CH_ST_ON]    = { "mac set_ch_status ",     sizeof("mac set_ch_status ") - 1 },
    [CMD_SET_CH_ST_OFF]   = { "mac set_ch_status ",     sizeof("mac set_ch_status ") - 1 },
    [CMD_SET_CH_JOIN_ON]  = { "mac set_join_ch ",       sizeof("mac set_join_ch ") - 1 },
    [CMD_SET_CH_JOIN_OFF] = { "mac set_join_ch ",       sizeof("mac set_join_ch ") - 1 },
    [CMD_SET_DEVEUI]      = { "mac set_deveui ",        sizeof("mac set_deveui ") - 1 },
    [CMD_SET_APPEUI]      = { "mac set_appeui ",        sizeof("mac set_appeui ") - 1 },
    [CMD_SET_APPKEY]      = { "mac set_appkey ",        sizeof("mac set_appkey ") - 1 },
    [CMD_JOIN_OTAA]       = { "mac join otaa",          sizeof("mac join otaa") - 1 },
    [CMD_MAC_SAVE]        = { "mac save",               sizeof("mac save") - 1 },
    [CMD_SIP_SLEEP]       = { "sip sleep ",             sizeof("sip sleep ") - 1 },
    [CMD_SET_ADR]         = { "mac set_adr ",           sizeof("mac set_adr ") - 1 },
    [CMD_SET_DR]          = { "mac set_dr ",            sizeof("mac set_dr ") - 1 },
    [CMD_GET_DR]          = { "mac get_dr",             sizeof("mac get_dr") - 1 },
    [CMD_SET_TX_MODE]     = { "mac set_tx_mode ",       sizeof("mac set_tx_mode ") - 1 },
    [CMD_GET_TX_MODE]     = { "mac get_tx_mode",        sizeof("mac get_tx_mode") - 1 },
    [CMD_SET_UPCNT]       = { "mac set_tx_confirm ",    sizeof("mac set_tx_confirm ") - 1 },
    [CMD_GET_UPCNT]       = { "mac get_tx_confirm",     sizeof("mac get_tx_confirm") - 1 },
    [CMD_SET_TXRETRY]     = { "mac set_txretry ",       sizeof("mac set_txretry ") - 1 },
    [CMD_SET_RXDELAY]     = { "mac set_rxdelay1 ",      sizeof("mac set_rxdelay1 ") - 1 },
    [CMD_LBT_ON]          = { "mac set_lbt on",         sizeof("mac set_lbt on") - 1 },
    [CMD_SET_CR]          = { "rf set_cr ",             sizeof("rf set_cr ") - 1 },
    [CMD_SET_CLASS]       = { "mac set_class ",         sizeof("mac set_class ") - 1 },
    [CMD_SET_RX2]         = { "mac set_rx2 ",           sizeof("mac set_rx2 ") - 1 },
    [CMD_SET_DEVADDR]     = { "mac set_devaddr ",       sizeof("mac set_devaddr ") - 1 },
    [CMD_SET_NWKSKEY]     = { "mac set_nwkskey ",       sizeof("mac set_nwkskey ") - 1 },
    [CMD_SET_APPSKEY]     = { "mac set_appskey ",       sizeof("mac set_appskey ") - 1 },
    [CMD_SET_RT_STORE]    = { "mac set_realtime_store ", sizeof("mac set_realtime_store ") - 1 }
};

#define LORA_MODE_NO_CYCLE   0
#define LORA_MODE_CYCLE      1

static const char* const lora_tx_modes[] = {
    [LORA_MODE_NO_CYCLE] = "no_cycle",
    [LORA_MODE_CYCLE]    = "cycle"
};

static const uint8_t lora_tx_modes_len[] = {
    [LORA_MODE_NO_CYCLE] = sizeof("no_cycle") - 1,
    [LORA_MODE_CYCLE]    = sizeof("cycle") - 1
};

#define LORA_FLAG_OFF        0
#define LORA_FLAG_ON         1

static const char* const lora_flags[] = {
    [LORA_FLAG_OFF] = "off",
    [LORA_FLAG_ON]  = "on"
};

static const uint8_t lora_flags_len[] = {
    [LORA_FLAG_OFF] = sizeof("off") - 1,
    [LORA_FLAG_ON]  = sizeof("on") - 1
};

#define LORA_CLASS_A   0
#define LORA_CLASS_C   1

#define DEFAULT_LORA_CLASS      (LORA_CLASS_A)
#define DEFAULT_LORA_DR         (5)
#define RX2_DEFAULT_LORA_DR     (0)
#define RX2_DEFAULT_LORA_FR     (869525000)

static const char* const lora_classes[] = { "A", "C" };
static const char* const lora_coding_rates[] = {"4/5", "4/6", "4/7", "4/8"};

//===========================================================================================================================
// ФИКСИРОВАННЫЙ ЯКОРЬ (Anchor) — момент начала серии простоя
//===========================================================================================================================
typedef struct {
    bool     is_valid;        // Якорь зафиксирован
    uint32_t unix_time;       // Время на момент пропажи связи
    int32_t  lat;             // GPS-широта на момент пропажи
    int32_t  lon;             // GPS-долгота на момент пропажи
} event_log_anchor_t;

static event_log_anchor_t event_anchor = {0};
//===========================================================================================================================
// EVENT LOG BUFFER (Журнал событий с поддержкой частичной отправки)
// Размер: EVENT_LOG_MAX_RECORDS * 29 байт ≈ 1856 байт (при 64 записях)
//===========================================================================================================================
#define EVENT_LOG_MAX_RECORDS  64  // <-- МЕНЯЙТЕ ЭТОТ МАКРОС ПРИ НЕОБХОДИМОСТИ

typedef struct {
    uint8_t  Channel;          // Номер канала
    uint8_t  StateErr;         // Новое состояние ошибки
    int16_t  Concentration;    // Концентрация (уже масштабированная в int16)
} event_channel_t;

typedef struct {
    uint32_t unix_time;        // Абсолютное время события (для пересчета Dt)
    int32_t  lat;              // Абсолютная широта события (или 0, если нет GPS)
    int32_t  lon;              // Абсолютная долгота события (или 0, если нет GPS)
    uint8_t  channel_count;    // Количество каналов в этом событии
    event_channel_t channels[COUNT_CHAN_LORA]; // Массив изменившихся каналов
} event_log_record_t;

typedef struct {
    uint8_t count;                 // Всего записей в буфере
    uint8_t send_index;            // Индекс следующей записи для отправки (для частичной передачи)
    uint8_t last_sent_count;       // Сколько записей было упаковано в ПОСЛЕДНИЙ кадр (для подтверждения по ECHO)
    event_log_record_t records[EVENT_LOG_MAX_RECORDS];
} event_log_buffer_t;

static event_log_buffer_t event_log = {0};

//===========================================================================================================================
// GPS HISTORY BUFFER (История GPS за время потери связи)
//===========================================================================================================================
#define GPS_HISTORY_MAX_POINTS  32  // 32 точки × 8 байт + 8 байт управления = 264 байта

typedef struct {
    int32_t lat;          // Абсолютная широта
    int32_t lon;          // Абсолютная долгота
} gps_history_point_t;

typedef struct {
    uint8_t count;                    // Всего точек в буфере
    uint8_t send_index;               // Индекс следующей точки для отправки
    uint8_t last_sent_count;          // Сколько точек упаковано в последний кадр
    uint32_t first_point_unix_time;   // Время самой первой (старейшей) точки в буфере
    uint32_t last_point_unix_time;    // Время самой последней точки в буфере
    gps_history_point_t points[GPS_HISTORY_MAX_POINTS];
} gps_history_buffer_t;

static gps_history_buffer_t gps_history = {0};

//===========================================================================================================================
// КОНТЕЙНЕРНЫЙ EVENT LOG v4 (п.4.8): типизированные события КОМПЛЕТА за время
// потери связи (падение/поза/SOS/СИЗ; пороги среды - задел). Записи 10 байт,
// механизм частичной отправки и квитирования ECHO - как у газового журнала.
// Газовые пороги ЗДЕСЬ НЕ хранятся - они в StateErr секции AVIS (п.5.2-5.3).
//===========================================================================================================================
#define CONTAINER_EVENT_MAX_RECORDS  32

typedef struct {
    uint32_t unix_time;        // абсолютное время события (для пересчета Dt)
    int32_t  lat;              // широта события (0 - нет GPS)
    int32_t  lon;              // долгота события (0 - нет GPS)
    uint8_t  type;             // enum CONTAINER_EVENT_TYPE
    uint8_t  severity;         // CONT_SEV_*
    int16_t  value;            // значение величины (CONT_EV_VALUE_NA - неприменимо)
} container_event_record_t;

typedef struct {
    uint8_t count;                 // всего записей в буфере
    uint8_t send_index;            // индекс следующей записи для отправки
    uint8_t last_sent_count;       // сколько упаковано в ПОСЛЕДНИЙ кадр (подтверждение по ECHO)
    container_event_record_t records[CONTAINER_EVENT_MAX_RECORDS];
} container_event_buffer_t;

static container_event_buffer_t container_event_log = {0};

//===========================================================================================================================
// СООБЩЕНИЕ СЕРВЕРА НА ДИСПЛЕЙ (v4 п.7.1): экземпляр публичного хранилища.
// Потребитель (меню/индикация) при Lora_Message.New показывает Text[Length]
// (CP1251) и вызывает Lora_MessageShown(); квитирование MsgAck уходит
// автоматически следующим uplink (бит 12 ContainerMask). Идемпотентность:
// повтор того же MsgSeq флаг New не взводит (дубль не показывается), MsgAck
// при этом уходит снова (сервер ждёт совпадения).
//===========================================================================================================================
lora_msg_t Lora_Message = {0};

static uint8_t lora_msg_last_seq = 0;   // MsgSeq последнего показанного сообщения (идемпотентность, v4 п.7.1)
static uint8_t lora_msg_seen = 0;       // 0 = сообщений ещё не было (seq 0 валиден)

void Lora_MessageShown(void)
{
    Lora_Message.New = 0;
}

//===========================================================================================================================
// СВЕЖЕСТЬ BLE-ТЕЛЕМЕТРИИ (v28): контейнерные блоки PPE/BLEBeacons и секция
// AVIS включаются в кадр, только если данные не старше BLE_TELEMETRY_TTL_MS
// (Ble_vector.c обновляет *_ms по GetTick при каждом принятом пакете).
// Беззнаковое вычитание корректно при переполнении GetTick (32 бит, ~49 сут).
//===========================================================================================================================
static uint8_t Lora_BleFresh(uint32_t timestamp_ms)
{
    return ((timestamp_ms != 0u) &&
            ((GetTick() - timestamp_ms) < BLE_TELEMETRY_TTL_MS)) ? 1u : 0u;
}

/* Барометрическая высота, м (международная барометрическая формула):
   h = 44330*(1-(P/P0)^0.1903), P0 = 1013.25 гПа. Вход - гПа×10 (=
   sensors_status.Pressure, int32-драйвер BME280: Па×100 / 1000).
   v4 п.4.4: высота - самостоятельная величина со своими порогами.          */
static int16_t Lora_CalcAltitudeM(float pressure_hpa_x10)
{
    float p_hpa = pressure_hpa_x10 / 10.0f;
    float h;

    if ((p_hpa < 100.0f) || (p_hpa > 1200.0f)) {
        return 0;    // нет данных / вне диапазона датчика
    }
    h = 44330.0f * (1.0f - powf(p_hpa / 1013.25f, 0.1903f));
    if (h > 32767.0f)  h = 32767.0f;
    if (h < -32768.0f) h = -32768.0f;
    return (int16_t)lround(h);
}

//===========================================================================================================================
//===========================================================================================================================
// ОПТИМИЗИРОВАННЫЕ СТРУКТУРЫ И ФЛАГИ
//===========================================================================================================================
typedef enum {
    LORA_FLAG_INIT              = 0,
    LORA_FLAG_SLEEP             = 1,
    LORA_FLAG_START             = 2,
    LORA_FLAG_ALARM             = 3,
    LORA_FLAG_REPEAT            = 4,
    LORA_FLAG_CONNECTION_LOST   = 5,
    LORA_FLAG_REQ_RESET_DR      = 6,
    LORA_FLAG_REQ_CHANGE_REGION = 7,
    LORA_FLAG_MAC_SAVE_PENDING  = 8,
    LORA_FLAG_RX2_SETUP_PENDING = 9,
    LORA_FLAG_RECEIVE           = 10, // Вместо lora.flag_receive
    LORA_FLAG_RECEIVE_TX_OK     = 11, // Вместо lora.flag_receive_tx_ok
    LORA_FLAG_ERR               = 12, // Вместо lora.flag_err
	LORA_FLAG_JOINING           = 13,

    // 2-битные поля для состояний (экономия памяти)
    LORA_RX_STATUS_SHIFT        = 14, // 2 бита: 0=WAITING, 1=SUCCESS, 2=ERROR, 3=UNKNOWN
} LORA_FLAG_T;

#define LORA_SET_FLAG(flag)         SETBIT(lora.flags, flag)
#define LORA_CLR_FLAG(flag)         CLRBIT(lora.flags, flag)
#define LORA_TEST_FLAG(flag)        (TESTBIT(lora.flags, flag) ? 1 : 0)

#define LORA_RX_STATUS_MASK         (0x03 << LORA_RX_STATUS_SHIFT)
#define LORA_GET_RX_STATUS()        ((lora.flags & LORA_RX_STATUS_MASK) >> LORA_RX_STATUS_SHIFT)
#define LORA_SET_RX_STATUS(val)     do { lora.flags = (lora.flags & ~LORA_RX_STATUS_MASK) | (((val) & 0x03) << LORA_RX_STATUS_SHIFT); } while(0)

//===========================================================================================================================
// ОБЪЕДИНЁННОЕ СОСТОЯНИЕ КАНАЛА (Last + Trigger)
// Размер: 2 + 2 + 1 + 1 + 1 = 7 байт на канал (вместо 3 отдельных массивов)
//===========================================================================================================================
typedef struct {
    int16_t  trigger_concentration; // Заморожено для первого пакета
    uint8_t  trigger_state_err;     // Заморожено для первого пакета
    uint8_t  last_in_dead_zone : 1;
    uint8_t  trigger_valid : 1;     // 1 = использовать trigger_*, 0 = использовать живые данные
    uint8_t  reserved : 6;
} channel_state_t;

static channel_state_t chan_state[COUNT_CHAN_LORA] = {0};

//===========================================================================================================================
// ТРЕКИНГ КОНТЕЙНЕРНЫХ СОБЫТИЙ v4 (п.4.8): предыдущие значения величин для
// детектора ПЕРЕХОДОВ (событие фиксируется и на взведение, и на снятие).
// Обновляется в test_state_alarm_flag_lora() (Measure Task, каждый проход).
//===========================================================================================================================
static uint32_t last_fall_cnt   = 0;      // sensors_status.cnt_fall_events на прошлом проходе
static uint8_t  last_posture    = 0;      // MOTION_POSTURE_* на прошлом проходе
static uint8_t  last_ppe_mask   = 0xFF;   // маска присутствия СИЗ (0xFF = ещё не считана)
static uint8_t  fall_latched    = 0;      // 1 = падение для бита MOTION_BIT_FALL (снимается после сборки кадра)

//===========================================================================================================================
// ОПТИМИЗИРОВАННАЯ СТРУКТУРА СОСТОЯНИЯ LORA
//===========================================================================================================================
typedef struct {
    // --- VOLATILE (прерывания/UART) ---
    volatile uint8_t  count_receive_res_tx;
    volatile uint16_t rx2_delay_ms;
    volatile uint16_t tx_busy_timer;  // Таймер занятости модема

    // --- ЕДИНАЯ БИТОВАЯ МАСКА (16 бит) ---
    volatile uint16_t flags;

    // --- СОСТОЯНИЯ И СЧЁТЧИКИ ---
    uint8_t  req_change_class;
    uint8_t  counter_err;
    uint8_t  set_param_dr;
    uint8_t  rx2_set_param_dr;
    uint32_t rx2_set_param_freq;
    uint8_t  empty_buffer;
    uint8_t  count_turnoff_system_tx;
    uint8_t  system_msg;
    uint8_t  power_state;

    uint16_t alarm_pre;
    uint16_t alarm_chan_state;
    uint8_t  alarm_pre_state_all;

    uint16_t ControlLora;         // v28/v4 п.4.2: ControlLora16 (биты enum STATUS_LORA, +EVACUATION)
    uint8_t  ControlLora2;

    uint8_t  applied_rev;         // Версия последней примененной конфигурации
    uint8_t  config_ack_pending;  // Флаг: 1 = нужно отправить ConfigAck, 0 = уже отправлено
    uint8_t  msg_ack_pending;     // v28/v4 п.7.1: 1 = нужно отправить MsgAck (бит 12)
} lora_internal_state_t;

static lora_internal_state_t lora = {0};

static uint16_t len_lora_data_set_tr;
static uint16_t len_lora_data_set;
static uint16_t len_lora_data_set_1;
static uint16_t len_lora_data_set_2;
static uint8_t port_tx = 10;

static str_lora_data_set_t      lora_data_set;
static str_lora_data_set_1_t    lora_data_set_1;
static str_lora_data_set_2_t    lora_data_set_2;
static str_lora_data_set_ack_t  lora_data_set_ack;
static str_lora_data_get_t      lora_data_get;

static const uint32_t lora_frequency_table[2][LORA_MAX_CHANNELS] = {
    [LORA_REGION_EU868] = {868100000, 868300000, 868500000, 867100000, 867300000, 867500000, 867700000, 867900000},
    [LORA_REGION_RU864] = {864100000, 864300000, 864500000, 864700000, 864900000, 868900000, 869100000}
};

static uint8_t lora_current_region = LORA_REGION_EU868;

uint8_t deveui[8] = {0};
uint8_t appeui[8] = {0x71, 0xB3, 0xD5, 0x7E, 0xD0, 0x00, 0x05, 0x91};
uint8_t appkey[16] = {0xC1, 0xFE, 0x94, 0xB0, 0xF5, 0xF6, 0xA5, 0x0E, 0x83, 0x01, 0x5B, 0x3C, 0x45, 0xC9, 0x33, 0xA9};

Message_buffer_uart_lora buffer_uart_lora = {0};

static uint8_t lora_sensor_map[COUNT_CHAN_LORA];

static uint8_t Lora_GetMaxPayloadSize(uint8_t dr)
{
    if (dr <= 2) {
        return 51;
    }
    else if (dr == 3) {
        return 115;
    }
    else if (dr == 4) {
        return 148;
    }
    else {
        return 176;
    }
}

//===========================================================================================================================
// КОНФИГУРАЦИЯ LORA В 2 БАЙТАХ
//===========================================================================================================================
static void Lora_Cfg_InitFromFlags(uint16_t flags)
{
    if (TESTBIT(flags, LORA_CFG_BIT_CLASS_C)) {
        SETBIT(lora.ControlLora, ST_LORA_BIT_CURRENT_C);
        SETBIT(lora.ControlLora2, ST_LORA2_SET_CLASS_C);
    }
    else {
        CLRBIT(lora.ControlLora, ST_LORA_BIT_CURRENT_C);
        CLRBIT(lora.ControlLora2, ST_LORA2_SET_CLASS_C);
    }

    if (TESTBIT(flags, LORA_CFG_BIT_REGION_RU)) {
        lora_current_region = LORA_REGION_RU864;
    }
    else {
        lora_current_region = LORA_REGION_EU868;
    }

    lora.set_param_dr = LORA_CFG_GET_DR(flags);
}

static uint16_t Lora_Cfg_BuildFlags(uint8_t is_saved, uint8_t is_class_c, uint8_t is_ru, uint8_t dr, uint8_t gps_fix_interval)
{
    uint16_t flags = 0;

    if (is_saved) {
        SETBIT(flags, LORA_CFG_BIT_SAVED);
    }

    if (is_class_c) {
        SETBIT(flags, LORA_CFG_BIT_CLASS_C);
    }

    if (is_ru) {
        SETBIT(flags, LORA_CFG_BIT_REGION_RU);
    }

    LORA_CFG_SET_DR(flags, dr);
    LORA_CFG_SET_GPSFIX(flags, gps_fix_interval);

    return flags;
}

static void Lora_Cfg_UpdateClass(uint8_t is_class_c)
{
    uint16_t *flags_ptr = &Sns_Cfg_struct.Config_common.Lora_Config_Flags;

    if (is_class_c) {
        SETBIT(*flags_ptr, LORA_CFG_BIT_CLASS_C);
    }
    else {
        CLRBIT(*flags_ptr, LORA_CFG_BIT_CLASS_C);
    }

    SaveConfig = 1;
}

static void Lora_Cfg_UpdateGPSFixInterval(uint8_t interval)
{
    uint16_t *flags_ptr = &Sns_Cfg_struct.Config_common.Lora_Config_Flags;
    LORA_CFG_SET_GPSFIX(*flags_ptr, interval);
    SaveConfig = 1;
}

//===========================================================================================================================
/* Карта "канал -> уплотнённый индекс в блоке Sensors" (v28: источник -
   Connected_device, газовые каналы приходят с AVIS по BLE; порядок - по
   возрастанию номера канала, совпадает с маской SensorsEnabled в TYPE_STATIC
   и с порядком channel_cfg_t[] в Cfg дескриптора SUB_TYPE_GAS).
   Вызывается перед каждой сборкой кадра (подключение AVIS может измениться). */
static void Lora_BuildSensorMap(void)
{
    uint8_t payload_idx = 0;

    for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
        if (Connected_device.Sensor[chan].channel == chan) {
            lora_sensor_map[chan] = payload_idx;
            payload_idx++;
        }
    }
}

//===========================================================================================================================
// GPS HISTORY (ЦЕПОЧКА ДЕЛЬТ)
//===========================================================================================================================
static int32_t Lora_CalcDeltaMeters(int32_t target, int32_t reference, int32_t ref_lat_for_cos)
{
    // Если ref_lat_for_cos == 0 (например, при расчете дельты по широте), cosf(0) = 1.0,
    // и формула работает как простой множитель.
    // Для долготы передаем реальную опорную широту, чтобы учесть схождение меридианов.
    float lat_rad = (float)ref_lat_for_cos * 0.0000001f * 0.01745329252f;
    float delta_m = (float)(target - reference) * 0.01113195f * cosf(lat_rad);
    return (int32_t)lround(delta_m);
}

#if CONFIG_GPS
#define MAX_GPS_HISTORY_POINTS (24)

static int32_t gps_last_valid_lat = 0;
static int32_t gps_last_valid_lon = 0;
static int32_t gps_hist_prev_lat = 0;
static int32_t gps_hist_prev_lon = 0;
static int8_t  gps_hist_dx[MAX_GPS_HISTORY_POINTS];
static int8_t  gps_hist_dy[MAX_GPS_HISTORY_POINTS];
static uint8_t gps_hist_count = 0;
static bool gps_hist_has_prev = false;
static uint8_t gps_fix_counter = 0;

void Lora_UpdateGPSTrack(int32_t cur_lat, int32_t cur_lon)
{
    // !!! НЕ ВЕДЁМ трек, пока прибор не подключен к сети
    if (!LORA_TEST_FLAG(LORA_FLAG_START)) {
        return;
    }

//    if (cur_lat != 0 && cur_lon != 0) {
        gps_last_valid_lat = cur_lat;
        gps_last_valid_lon = cur_lon;
//    }

    // !!! При потере связи GPSTrack НЕ ведётся (ведётся GPSHistory вместо него)
    if (LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST)) {
        return;
    }

    uint8_t fix_interval = LORA_CFG_GET_GPSFIX(Sns_Cfg_struct.Config_common.Lora_Config_Flags);
    if (fix_interval == 0) {
        gps_hist_has_prev = false;
        gps_hist_count = 0;
        return;
    }

    gps_fix_counter++;
    if (gps_fix_counter < fix_interval) {
        return;
    }
    gps_fix_counter = 0;

    if (cur_lat == 0 || cur_lon == 0) {
        return;
    }

//    gps_last_valid_lat = cur_lat;
//    gps_last_valid_lon = cur_lon;

    if (!gps_hist_has_prev) {
        gps_hist_prev_lat = cur_lat;
        gps_hist_prev_lon = cur_lon;
        gps_hist_has_prev = true;
        gps_hist_count = 0;
        return;
    }

    // !!! ИСПОЛЬЗУЕМ ЕДИНУЮ ФУНКЦИЮ Lora_CalcDeltaMeters
    // Для широты передаем 0 (cos(0)=1), для долготы передаем предыдущую широту
    int32_t dx_m = Lora_CalcDeltaMeters(cur_lat, gps_hist_prev_lat, 0);
    int32_t dy_m = Lora_CalcDeltaMeters(cur_lon, gps_hist_prev_lon, gps_hist_prev_lat);

    while ((dx_m > 127 || dx_m < -128 || dy_m > 127 || dy_m < -128) && (gps_hist_count < MAX_GPS_HISTORY_POINTS)) {
        int8_t chunk_x = 0;
        int8_t chunk_y = 0;

        if (dx_m > 127) {
        	chunk_x = 127;
        	dx_m -= 127;
        }
        else if (dx_m < -128) {
        	chunk_x = -128;
        	dx_m += 128;
        }
        else {
        	chunk_x = (int8_t)dx_m;
        	dx_m = 0;
        }

        if (dy_m > 127) {
        	chunk_y = 127;
        	dy_m -= 127;
        }
        else if (dy_m < -128) {
        	chunk_y = -128;
        	dy_m += 128;
        }
        else {
        	chunk_y = (int8_t)dy_m;
        	dy_m = 0;
        }

        gps_hist_dx[gps_hist_count] = chunk_x;
        gps_hist_dy[gps_hist_count] = chunk_y;
        gps_hist_count++;
    }

    if ((dx_m != 0 || dy_m != 0) && (gps_hist_count < MAX_GPS_HISTORY_POINTS)) {
        gps_hist_dx[gps_hist_count] = (int8_t)dx_m;
        gps_hist_dy[gps_hist_count] = (int8_t)dy_m;
        gps_hist_count++;
    }

    gps_hist_prev_lat = cur_lat;
    gps_hist_prev_lon = cur_lon;
}
#endif

//===========================================================================================================================
static void Lora_Switch_Region_On_The_Fly(SNS_CFG *Cfg_struct, uint8_t do_full_setup);
static void Lora_InitDeveui(SNS_CFG *Cfg_struct);
static void Lora_Control_States_Update(str_lora_data_get_t *rx_data, uint32_t port_tx_read);
static void Lora_CompleteDataSet(SNS_CFG *Cfg_struct, str_lora_data_set_t *data_set);
static void Lora_CompleteDataSet_1(SNS_CFG *Cfg_struct, str_lora_data_set_1_t *data_set);
static void Lora_CompleteDataSet_2(SNS_CFG *Cfg_struct, str_lora_data_set_2_t *data_set, TYPE_MSG_LORA type_msg);
static void Lora_CompleteDataSetAck(str_lora_data_set_ack_t *data_set);
static uint8_t Lora_Transmit(COMMAND_LORA command, uint8_t *data);
static uint8_t Lora_TransmitReceive(COMMAND_LORA command, uint8_t *data);
static uint8_t Lora_Receive_wait(COMMAND_LORA command);
static inline uint8_t HexToByte(char high, char low);
static void ParseLoraToStruct(const char* uart_buffer);

void Uart_Lora_Receive_Timer_Inc(void)
{
    if (buffer_uart_lora.TimeRX != 0) {
        buffer_uart_lora.TimeRX--;

        if (buffer_uart_lora.TimeRX == 0) {
            buffer_uart_lora.TimeFlagRX = 1;
        }
    }

    if (lora.rx2_delay_ms > 0) {
        lora.rx2_delay_ms--;
    }

    // Декремент таймера занятости модема
    if (lora.tx_busy_timer > 0) {
        lora.tx_busy_timer--;
    }
}

void Lora_ack_system_set(void)
{
//	button_block_sound_lora = 0;
    lora.count_turnoff_system_tx = 0;
    uint16_t time_data_set = (Sns_Cfg_struct.Config_common.PeriodTimeLora & 0xFF) * 10;
    START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, time_data_set);
}

void Lora_turnoff_system_set(void)
{
    lora.count_turnoff_system_tx = 0;
    lora.system_msg = 1;
    LORA_SET_FLAG(LORA_FLAG_MAC_SAVE_PENDING);
    START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, 5);
}

void Lora_system_msg_clr(void)
{
    lora.system_msg = 0;
}

uint8_t Lora_system_msg_read(void)
{
    return lora.system_msg;
}

//===========================================================================================================================
// РЕАЛИЗАЦИЯ API ДЛЯ ВНЕШНИХ ФАЙЛОВ
//===========================================================================================================================
uint8_t Lora_IsTxBusy(void)
{
    return (lora.tx_busy_timer > 0) ? 1 : 0;
}

uint16_t Lora_GetControlLora(void)
{
    return lora.ControlLora;    // v28/v4: ControlLora16
}

void Lora_SetControlLoraBit(uint8_t bit_num, uint8_t state)
{
    if (state) {
        SETBIT(lora.ControlLora, bit_num);
    }
    else {
        CLRBIT(lora.ControlLora, bit_num);
    }
}

uint8_t get_state_init_flag_lora(void)
{
    return LORA_TEST_FLAG(LORA_FLAG_INIT);
}

uint8_t get_state_start_flag_lora(void)
{
    return LORA_TEST_FLAG(LORA_FLAG_START);
}

void Lora_flag_receive_set(void)
{
	LORA_SET_FLAG(LORA_FLAG_RECEIVE);
}

void Lora_flag_receive_clr(void)
{
	LORA_CLR_FLAG(LORA_FLAG_RECEIVE);
}

uint8_t Lora_flag_receive_read(void)
{
	return LORA_TEST_FLAG(LORA_FLAG_RECEIVE);
}

uint8_t get_flag_receive_res_tx_lora(void)
{
    return (lora.count_receive_res_tx <= 2);
}

uint8_t get_state_repeat_flag_lora(void)
{
    return LORA_TEST_FLAG(LORA_FLAG_REPEAT);
}

void set_state_repeat_flag_lora(void)
{
    LORA_SET_FLAG(LORA_FLAG_REPEAT);
}

void clr_state_repeat_flag_lora(void)
{
    LORA_CLR_FLAG(LORA_FLAG_REPEAT);
}

uint8_t get_state_err_lora(void)
{
	return LORA_TEST_FLAG(LORA_FLAG_ERR);
}

void clr_state_err_lora(void)
{
    LORA_CLR_FLAG(LORA_FLAG_ERR);
    lora.counter_err = 0;
}

uint8_t get_state_alarm_flag_lora(void)
{
    return LORA_TEST_FLAG(LORA_FLAG_ALARM);
}

void set_state_alarm_flag_lora(void)
{
    LORA_SET_FLAG(LORA_FLAG_ALARM);
}

void clr_state_alarm_flag_lora(void)
{
    LORA_CLR_FLAG(LORA_FLAG_ALARM);
}

/* Концентрация канала для провода (v28/v4 п.5.3): PASS-THROUGH из
   Connected_device. Отправитель (AVIS) уже применил свою формулу
   ConcentrationVal*Discreteness*Get_Conversion_factor_rf() в своём
   Ble_CompleteDataSet (docs/Ble_mk01.c.md) - повторное масштабирование на
   трекере ЗАПРЕЩЕНО (иначе единицы уедут дважды). int16 - протокол требует
   знаковый тип (дрейф около нуля). Единицы/дискретность описаны Setting-ом
   канала в TYPE_STATIC (channel_cfg_t.Setting).                            */
static int16_t Lora_ScaleConcentration(uint8_t chan)
{
    return (int16_t)lround(Connected_device.Sensor[chan].ConcentrationVal);
}

/* Дискретность канала из Setting-байта отправителя (Avis: DeviceSetting &
   (MEASURE_MASK|MEASURE_DISK)): биты 0..1 - единицы (0 PPM, 1 мг/м3, 2 %НКПР,
   3 %об.д. - приложение v4), биты 2..3 - код дискретности. Карта дискретности
   ПРЕДВАРИТЕЛЬНАЯ (0->1, 1->10, 2->100, 3->1000) - сверить с Avis-Lora-
   protocol-v3 / заголовками Avis (автор предлагал прислать v3). Нужна только
   для мёртвой зоны O2 20.85..20.95 (границы в единицах провода = 20.9*d).   */
#define LORA_SETT_UNIT_MASK   (0x03u)
#define LORA_SETT_DISK_SHIFT  (2u)
static const uint16_t lora_discreteness_tab[4] = { 1u, 10u, 100u, 1000u };

static uint16_t Lora_ChannelDiscreteness(uint8_t setting)
{
    return lora_discreteness_tab[(setting >> LORA_SETT_DISK_SHIFT) & 0x03u];
}

/* Поза из ориентации акселерометра (v4 п.4.5: 0 неизвестно, 1 вертикальное,
   2 горизонтальное). Z_UP/Z_DOWN - прибор вертикально (человек стоит),
   X/Y UP/DOWN - горизонтально (человек лежит). Без LIS3DH - неизвестно.    */
static uint8_t Lora_PostureFromOrientation(uint8_t orient)
{
#if CONFIG_LIS3DH
    switch (orient) {
    case LIS3DH_ORIENT_Z_UP:
    case LIS3DH_ORIENT_Z_DOWN:
        return MOTION_POSTURE_VERTICAL;
    case LIS3DH_ORIENT_X_UP:
    case LIS3DH_ORIENT_X_DOWN:
    case LIS3DH_ORIENT_Y_UP:
    case LIS3DH_ORIENT_Y_DOWN:
        return MOTION_POSTURE_HORIZONTAL;
    default:
        return MOTION_POSTURE_UNKNOWN;
    }
#else
    (void)orient;
    return MOTION_POSTURE_UNKNOWN;
#endif
}

/* Добавить запись в КОНТЕЙНЕРНЫЙ журнал v4 (п.4.8). Журнал ведётся ТОЛЬКО за
   время потери связи (LORA_FLAG_CONNECTION_LOST) - как и газовый: это канал
   ВОССТАНОВЛЕНИЯ данных; в штатном режиме величины и так уходят живыми
   блоками (Motion/PPE/DeviceStatus). КОНТЕКСТ: Measure Task.               */
static void Lora_AddContainerEvent(uint8_t type, uint8_t severity, int16_t value)
{
    container_event_record_t *rec;

    if (!LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST)) {
        return;
    }
    if (container_event_log.count >= CONTAINER_EVENT_MAX_RECORDS) {
        return;    // буфер полон: новые события теряются до квитирования ECHO
    }
    rec = &container_event_log.records[container_event_log.count];
    rec->unix_time = (uint32_t)Sns_Cfg_struct.Working_data.working_hours;
#if CONFIG_GPS
    rec->lat = gps_last_valid_lat;
    rec->lon = gps_last_valid_lon;
#else
    rec->lat = 0;
    rec->lon = 0;
#endif
    rec->type     = type;
    rec->severity = severity;
    rec->value    = value;
    container_event_log.count++;
}

uint8_t test_state_alarm_flag_lora(SNS_CFG *Cfg_struct)
{
    if (!LORA_TEST_FLAG(LORA_FLAG_START)) {
		return 0;
	}
    if (TESTBIT(lora.ControlLora2, ST_LORA2_REQ_STATIC)) {
		return 0;
	}
    uint8_t state_changed = 0;

    /* v28/v4: газовые каналы - секция под-устройства AVIS; источник данных -
       Connected_device (заполняет Ble_vector.c), а НЕ собственные каналы
       SNS_CFG (у трекера газовых сенсоров нет). Cfg_struct в сигнатуре
       оставлен (совместимость вызова Lora_Run) и используется ниже для
       метки времени газового журнала.                                       */
    uint8_t gas_fresh = (Ble_telemetry.gas_online != 0u) &&
                        Lora_BleFresh(Ble_telemetry.gas_ms);

    for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
        if (!gas_fresh) break;
        if (Connected_device.Sensor[chan].channel != chan) continue;

        int16_t current_conc = Lora_ScaleConcentration(chan);
        uint8_t current_state = (uint8_t)(Connected_device.Sensor[chan].StateErr & 0x7F);

        // 1. Определяем текущее положение относительно мёртвой зоны
        // (O2: код газа LORA_GAS_ID_O2, зона 20.85..20.95 в единицах провода;
        //  дискретность - из Setting-байта отправителя, Lora_ChannelDiscreteness)
        bool current_in_dead_zone = false;
        if (current_state == 0) {
            if (Connected_device.Sensor[chan].num_formula_gas == LORA_GAS_ID_O2) {
            	uint16_t d = Lora_ChannelDiscreteness((uint8_t)Connected_device.Sensor[chan].Setting);
            	int16_t low = (int16_t)(20.85f * (float)d);
            	int16_t high = (int16_t)(20.95f * (float)d);
                current_in_dead_zone = (current_conc >= low && current_conc <= high);
            } else {
                current_in_dead_zone = (current_conc == 0);
            }
        }

        // 2. Детектируем ПЕРЕХОД (изменение состояния или пересечение границы)
        bool state_err_changed = (current_state != chan_state[chan].trigger_state_err);
        bool dead_zone_changed = (current_in_dead_zone != chan_state[chan].last_in_dead_zone);

        if (state_err_changed || dead_zone_changed) {
            state_changed = 1;

            // Обновляем историю
            chan_state[chan].last_in_dead_zone = current_in_dead_zone;

            // Замораживаем для ПЕРВОГО пакета после перехода
            chan_state[chan].trigger_concentration = current_conc;
            chan_state[chan].trigger_state_err = current_state;
            chan_state[chan].trigger_valid = 1;

            // Пишем в EventLog ТОЛЬКО если связь потеряна
            if (state_err_changed && LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST) && (event_log.count < EVENT_LOG_MAX_RECORDS)) {
                event_log_record_t *new_event = &event_log.records[event_log.count];
                new_event->unix_time = (uint32_t)Cfg_struct->Working_data.working_hours;
#if CONFIG_GPS
                new_event->lat = gps_last_valid_lat;
                new_event->lon = gps_last_valid_lon;
#else
                new_event->lat = 0;
                new_event->lon = 0;
#endif
                new_event->channel_count = 1;
                new_event->channels[0].Channel = chan;
                new_event->channels[0].StateErr = current_state;
                new_event->channels[0].Concentration = current_conc;
                event_log.count++;
            }
        }

        // 3. ГЛАВНОЕ: Управляем битом на основе ТЕКУЩЕЙ реальности
        // Канал вне мёртвой зоны = ОБЯЗАНЫ передавать (независимо от того, было ли изменение)
        if (!current_in_dead_zone) {
            SETBIT(lora.alarm_chan_state, chan);
        } else {
            CLRBIT(lora.alarm_chan_state, chan);
        }
    }

    // Глобальная кнопка SOS (v4 п.4.8: событие контейнерного журнала на
    // взведение и на снятие; взведение = критическое, снятие = инфо)
    uint8_t current_sos = TESTBIT(lora.ControlLora, ST_LORA_BIT_SOS);
    if (current_sos != lora.alarm_pre_state_all) {
        state_changed = 1;
        lora.alarm_pre_state_all = current_sos;
        if (current_sos) {
            Lora_AddContainerEvent(CONT_EV_SOS, CONT_SEV_CRITICAL, CONT_EV_VALUE_NA);
        } else {
            Lora_AddContainerEvent(CONT_EV_SOS, CONT_SEV_INFO, CONT_EV_VALUE_NA);
        }
    }

    /* v28/v4 п.4.8: ПАДЕНИЕ - дискретное событие (free-fall детектор LIS3DH,
       счётчик sensors_status.cnt_fall_events растёт в sensors_read()). Бит
       MOTION_BIT_FALL живёт до ближайшей сборки кадра (fall_latched).       */
    if (sensors_status.cnt_fall_events != last_fall_cnt) {
        last_fall_cnt = sensors_status.cnt_fall_events;
        fall_latched = 1;
        state_changed = 1;
        Lora_AddContainerEvent(CONT_EV_FALL, CONT_SEV_CRITICAL, CONT_EV_VALUE_NA);
    }

    /* v28/v4 п.4.8: ПОЗА - событие на переход (горизонтальное/вертикальное).
       Не аварийное: живой блок Motion и так уходит каждым кадром.           */
    {
        uint8_t posture = Lora_PostureFromOrientation(sensors_status.Orientation);
        if ((posture != MOTION_POSTURE_UNKNOWN) && (last_posture != MOTION_POSTURE_UNKNOWN) &&
            (posture != last_posture)) {
            if (posture == MOTION_POSTURE_HORIZONTAL) {
                Lora_AddContainerEvent(CONT_EV_POSTURE_HOR, CONT_SEV_INFO, CONT_EV_VALUE_NA);
            } else {
                Lora_AddContainerEvent(CONT_EV_POSTURE_VER, CONT_SEV_INFO, CONT_EV_VALUE_NA);
            }
        }
        if (posture != MOTION_POSTURE_UNKNOWN) {
            last_posture = posture;
        }
    }

    /* v28/v4 п.4.8: СИЗ - "не надеты" (маска снятых) / "в норме". Бит маски =
       позиция комплекта (индекс в Ble_telemetry.ppe = бит PPEEnabledMask в
       TYPE_STATIC). Первый проход (last_ppe_mask == 0xFF) - без события.    */
    if (Lora_BleFresh(Ble_telemetry.ppe_ms) && (Ble_telemetry.ppe_count > 0u)) {
        uint8_t ppe_mask_now = 0;
        for (uint8_t k = 0; (k < Ble_telemetry.ppe_count) && (k < BLE_PPE_MAX); k++) {
            if (Ble_telemetry.ppe[k].Present) {
                ppe_mask_now |= (uint8_t)(1u << k);
            }
        }
        if (last_ppe_mask == 0xFFu) {
            last_ppe_mask = ppe_mask_now;    // инициализация без события
        } else if (ppe_mask_now != last_ppe_mask) {
            uint8_t removed = (uint8_t)(last_ppe_mask & (uint8_t)~ppe_mask_now);
            if (removed != 0u) {
                Lora_AddContainerEvent(CONT_EV_PPE_OFF, CONT_SEV_WARNING, (int16_t)removed);
                state_changed = 1;
            } else if ((ppe_mask_now != 0u) && ((ppe_mask_now & last_ppe_mask) == last_ppe_mask)) {
                Lora_AddContainerEvent(CONT_EV_PPE_OK, CONT_SEV_INFO, CONT_EV_VALUE_NA);
            }
            last_ppe_mask = ppe_mask_now;
        }
    }

    if (state_changed) set_state_repeat_flag_lora();
    return state_changed;
}

void Lora_Cfg_ApplyDefaults(SNS_CFG *Cfg_struct)
{
    uint8_t is_class_c = TESTBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_CLASS_C);
    uint8_t is_ru = TESTBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_REGION_RU);
    uint8_t dr = DEFAULT_LORA_DR;
    uint8_t gps_fix = LORA_CFG_GET_GPSFIX(Cfg_struct->Config_common.Lora_Config_Flags);

    // Собираем флаги с нуля. Передаём 0 в is_saved, чтобы бит SAVED не установился.
    Cfg_struct->Config_common.Lora_Config_Flags = Lora_Cfg_BuildFlags(0, is_class_c, is_ru, dr, gps_fix);

    Cfg_struct->Config_common.Lora_freq_rx2 = RX2_DEFAULT_LORA_FR;
    Cfg_struct->Config_common.Lora_dr_rx2 = RX2_DEFAULT_LORA_DR;

    lora.rx2_set_param_dr = Cfg_struct->Config_common.Lora_dr_rx2;
    lora.rx2_set_param_freq = Cfg_struct->Config_common.Lora_freq_rx2;

    // Явно очищаем бит SAVED, чтобы гарантировать полную инициализацию при следующем старте
    CLRBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_SAVED);
}

uint8_t Lora_Init(SNS_CFG *Cfg_struct)
{
    if (get_state_init_flag_lora() == 0) {
        LORA_UART_Init();
        clr_state_err_lora();
        Init_Buffer(&InputBuffer[TYPE_LORA]);
        buffer_uart_lora.TimeRX = 0;
        buffer_uart_lora.TimeFlagRX = 0;
        LORA_CLR_FLAG(LORA_FLAG_RECEIVE);
        LORA_SET_FLAG(LORA_FLAG_INIT);
        SETBIT(lora.ControlLora2, ST_LORA2_REQ_STATIC);
        lora.req_change_class = LORA_REQ_CLASS_NONE;

        Lora_Cfg_InitFromFlags(Cfg_struct->Config_common.Lora_Config_Flags);
        Lora_BuildSensorMap();
        Lora_InitDeveui(Cfg_struct);

        if (!TESTBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_SAVED)) {
            Lora_TransmitReceive(CMD_SET_DEVEUI, deveui);
            Lora_TransmitReceive(CMD_SET_APPEUI, appeui);
            Lora_TransmitReceive(CMD_SET_APPKEY, appkey);

            Lora_TransmitReceive(CMD_GET_DR, (uint8_t*)&lora.empty_buffer);
            lora.empty_buffer = LORA_RX1_DELAY_SEC;    // v28: 3 с; значение уходит в radio_cfg_t TYPE_STATIC
            Lora_TransmitReceive(CMD_SET_RXDELAY, (uint8_t*)&lora.empty_buffer);

            lora.empty_buffer = LORA_FLAG_ON;
            Lora_TransmitReceive(CMD_LBT_ON, (uint8_t*)&lora.empty_buffer);

            lora.empty_buffer = LORA_FLAG_ON;
            Lora_TransmitReceive(CMD_SET_ADR, (uint8_t*)&lora.empty_buffer);

            Lora_TransmitReceive(CMD_SET_DR, (uint8_t*)&lora.set_param_dr);

            lora.empty_buffer = LORA_MODE_NO_CYCLE;
            Lora_TransmitReceive(CMD_SET_TX_MODE, (uint8_t*)&lora.empty_buffer);

            lora.empty_buffer = 0;
            Lora_TransmitReceive(CMD_SET_TXRETRY, (uint8_t*)&lora.empty_buffer);

            lora.empty_buffer = 3;
            Lora_TransmitReceive(CMD_SET_CR, (uint8_t*)&lora.empty_buffer);

            uint8_t default_class = DEFAULT_LORA_CLASS;

            if (TESTBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_CLASS_C)) {
                default_class = LORA_CLASS_C;
            }

            Lora_TransmitReceive(CMD_SET_CLASS, &default_class);

            lora.empty_buffer = LORA_FLAG_ON;
            Lora_TransmitReceive(CMD_SET_RT_STORE, (uint8_t*)&lora.empty_buffer);

            Lora_Switch_Region_On_The_Fly(Cfg_struct, 1);

            lora.rx2_set_param_dr = Cfg_struct->Config_common.Lora_dr_rx2;
            if(Cfg_struct->Config_common.Lora_freq_rx2 == 0){
            	Cfg_struct->Config_common.Lora_freq_rx2 = RX2_DEFAULT_LORA_FR;
            }
            lora.rx2_set_param_freq = Cfg_struct->Config_common.Lora_freq_rx2;
            lora.rx2_delay_ms = 12000;
            LORA_SET_FLAG(LORA_FLAG_RX2_SETUP_PENDING);
        }
        else {
#if(LORA_FORCE_JOIN_ON_STARTUP)
        	LORA_CLR_FLAG(LORA_FLAG_RECEIVE_TX_OK);
            lora.count_receive_res_tx = 0;
            LORA_CLR_FLAG(LORA_FLAG_START);
            Lora_Transmit(CMD_JOIN_OTAA, (uint8_t*)&lora.empty_buffer);
#else
            lora.flag_receive_tx_ok = 0;
            lora.count_receive_res_tx = 0;
            lora.start_flag = 1;
#endif
        }

        if ((Cfg_struct->Config_common.PeriodTimeLora & 0xFF) == 0) {
            Cfg_struct->Config_common.PeriodTimeLora |= TIME_RTC_LORA_DATA_SET;
        }

        if (((Cfg_struct->Config_common.PeriodTimeLora >> 8) & 0xFF) == 0) {
            Cfg_struct->Config_common.PeriodTimeLora |= (TIME_RTC_LORA_DATA_SET << 8) / 3;
        }
    }

    return get_state_init_flag_lora();
}

void Lora_DeInit(uint8_t init)
{
    if (init) {
        LORA_SET_FLAG(LORA_FLAG_INIT);
    } else {
        LORA_CLR_FLAG(LORA_FLAG_INIT);
    }
    LORA_CLR_FLAG(LORA_FLAG_START);
    clr_state_err_lora();
    RESET_TIMER_RTC(TIMER_RTC_LORA_DATA_SET);
}

static void Lora_Switch_Region_On_The_Fly(SNS_CFG *Cfg_struct, uint8_t do_full_setup)
{
	LORA_CLR_FLAG(LORA_FLAG_START);
	LORA_CLR_FLAG(LORA_FLAG_RECEIVE_TX_OK);
    lora.count_receive_res_tx = 0;

    if (do_full_setup != 0) {
        for (uint8_t ch = 0; ch < LORA_MAX_CHANNELS; ch++) {
            Lora_TransmitReceive(CMD_SET_CH_ST_OFF, &ch);
        }
    }

    if (TESTBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_REGION_RU)) {
        lora_current_region = LORA_REGION_RU864;
    }
    else {
        lora_current_region = LORA_REGION_EU868;
    }

    uint8_t channels_to_init = (lora_current_region == LORA_REGION_EU868) ? 8 : 7;

    for (uint8_t ch = 0; ch < channels_to_init; ch++) {
        Lora_TransmitReceive(CMD_SET_CH_FR, &ch);
    }

    for (uint8_t ch = 0; ch < channels_to_init; ch++) {
        Lora_TransmitReceive(CMD_SET_CH_ST_ON, &ch);
    }

    Lora_Transmit(CMD_JOIN_OTAA, (uint8_t*)&lora.empty_buffer);

    if (LORA_TEST_FLAG(LORA_FLAG_ERR)) {
    	LORA_CLR_FLAG(LORA_FLAG_START);
    }
}

static void Lora_InitDeveui(SNS_CFG *Cfg_struct)
{
    uint32_t serial;

    serial = (Cfg_struct->Config_common.SerialHi << 16) | Cfg_struct->Config_common.SerialLo;

    deveui[0] = 0x9c;
    deveui[1] = 0x65;
    deveui[2] = 0xf9;
    deveui[3] = 0xff;
    deveui[4] = serial >> 24;
    deveui[5] = serial >> 16;
    deveui[6] = serial >> 8;
    deveui[7] = serial;

    memcpy(&deveui_com[0], &deveui[0], 8);
}

static void Lora_Control_States_Update(str_lora_data_get_t *rx_data, uint32_t port_tx_read)
{
    // ==========================================================================================
    // ШАГ 1: Атомарное обновление реального состояния устройства по маскам из downlink
    // ==========================================================================================

    // 1. Control (Бит 0 FieldMask; v28/v4: Mask u16 + Value u16)
    if (rx_data->FieldMask & FIELD_MASK_DL_CONTROL) {
        // Защищаем биты SOS (0x0020) и CURRENT_C (0x0080) от перезаписи сервером
        uint16_t protected_bits = lora.ControlLora & ST_LORA16_PROTECTED_MASK;
        uint16_t allowed_mask = rx_data->ControlLoraMask & (uint16_t)~ST_LORA16_PROTECTED_MASK;

        // Формула атомарной записи: state = (state & ~Mask) | (Value & Mask)
        lora.ControlLora = (uint16_t)(protected_bits |
                           ((lora.ControlLora & (uint16_t)~allowed_mask) |
                            (rx_data->ControlLoraValue & allowed_mask)));
        // v4 бит 8 EVACUATION ("Всем на выход") приходит старшим байтом маски;
        // локальная реакция (звук/экран) - за индикацией, здесь только хранение
        // и эхо в uplink (подтверждение наблюдением, v4 п.4.2).
    }

    // 2. Mode / ControlLora2 (Бит 1 FieldMask)
    if (rx_data->FieldMask & FIELD_MASK_DL_MODE) {
        lora.ControlLora2 = (lora.ControlLora2 & ~rx_data->ControlLora2Mask) |
                            (rx_data->ControlLora2Value & rx_data->ControlLora2Mask);
    }

    // 3. PeriodNormal (Бит 2 FieldMask)
    if ((rx_data->FieldMask & FIELD_MASK_DL_PERIOD_NORM) && (rx_data->NewPeriodNorm != 0)) {
        Sns_Cfg_struct.Config_common.PeriodTimeLora &= 0xFF00;
        Sns_Cfg_struct.Config_common.PeriodTimeLora |= rx_data->NewPeriodNorm;
    }

    // 4. PeriodActive (Бит 3 FieldMask)
    if ((rx_data->FieldMask & FIELD_MASK_DL_PERIOD_ACT) && (rx_data->NewPeriodAct != 0)) {
        Sns_Cfg_struct.Config_common.PeriodTimeLora &= 0x00FF;
        Sns_Cfg_struct.Config_common.PeriodTimeLora |= ((uint16_t)rx_data->NewPeriodAct << 8);
    }

    // 5. GPSFixInterval (Бит 4 FieldMask)
    if ((rx_data->FieldMask & FIELD_MASK_DL_GPS_FIX) && (rx_data->GpsFixInterval != 0)) {
        Lora_Cfg_UpdateGPSFixInterval(rx_data->GpsFixInterval);
    }

    // 6. ConfigSeq (Бит 5 FieldMask) — Квитирование конфигурации
    if (rx_data->FieldMask & FIELD_MASK_DL_CONFIG_SEQ) {
        // Сохраняем версию в состоянии LoRa
        lora.applied_rev = rx_data->ConfigSeq;
        lora.config_ack_pending = 1;
    }

    // 7. Time (Бит 6 FieldMask) — Синхронизация RTC прибора
    if (rx_data->FieldMask & FIELD_MASK_DL_TIME) {
        // v28: working_hours - Unix-секунды календарного RTC; сверку с календарём
        // раз в секунду делает Vector_Options_System() (RTC_WriteTime) - сюда
        // пишется только значение.
        Sns_Cfg_struct.Working_data.working_hours = rx_data->UnixTime;
    }

    // 8. Message (Бит 7 FieldMask) — v28/v4 п.7.1: текст оператора на дисплей
    if ((rx_data->FieldMask & FIELD_MASK_DL_MESSAGE) && (rx_data->MsgLen > 0)) {
        Lora_Message.MsgSeq = rx_data->MsgSeq;
        Lora_Message.Length = rx_data->MsgLen;
        memcpy(Lora_Message.Text, rx_data->MsgText, rx_data->MsgLen);
        // Идемпотентность: дубль того же MsgSeq повторно НЕ показываем
        // (повтор пришёл, значит MsgAck потерялся - квитировать надо снова).
        if ((lora_msg_seen == 0u) || (rx_data->MsgSeq != lora_msg_last_seq)) {
            lora_msg_seen = 1u;
            lora_msg_last_seq = rx_data->MsgSeq;
            Lora_Message.New = 1;
        }
        lora.msg_ack_pending = 1;    // MsgAck уйдёт ближайшим uplink (бит 12)
    }

    // ==========================================================================================
    // ШАГ 2: Логика обработки флагов строго на основе ОБНОВЛЕННОГО lora.ControlLora / ControlLora2
    // ==========================================================================================
    if (TESTBIT(lora.ControlLora, ST_LORA_BIT_ECHO)) {
    	clr_state_repeat_flag_lora();
        // 1. СБРОС ЗАМОРОЗКИ: сервер подтвердил пакет, разрешаем использовать живые данные в следующих пакетах
        for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
            chan_state[chan].trigger_valid = 0;
        }

        // 2. ОЧИСТКА МАСКИ КАНАЛОВ
        // Сервер подтвердил получение изменений. Эти каналы больше не требуют срочной отправки.
        // Они будут добавлены обратно в маску (SET_BIT) только при следующем изменении состояния.
        lora.alarm_chan_state = 0;

        // Сброс EventLog (газовый, секция AVIS)
        if (event_log.last_sent_count > 0) {
            event_log.send_index += event_log.last_sent_count;
            event_log.last_sent_count = 0;
        }

        // Сброс контейнерного EventLog v4
        if (container_event_log.last_sent_count > 0) {
            container_event_log.send_index += container_event_log.last_sent_count;
            container_event_log.last_sent_count = 0;
        }

        // Сброс GPSHistory
        if (gps_history.last_sent_count > 0) {
            gps_history.send_index += gps_history.last_sent_count;
            gps_history.last_sent_count = 0;
        }

        // Сброс флага ТОЛЬКО когда ВСЕ исторические буферы пусты
        bool event_log_empty = (event_log.send_index >= event_log.count);
        bool cont_event_empty = (container_event_log.send_index >= container_event_log.count);
        bool gps_history_empty = (gps_history.send_index >= gps_history.count);

        if (event_log_empty && cont_event_empty && gps_history_empty) {
            event_log.count = 0;
            event_log.send_index = 0;
            container_event_log.count = 0;
            container_event_log.send_index = 0;
            gps_history.count = 0;
            gps_history.send_index = 0;
            event_anchor.is_valid = false;
            LORA_CLR_FLAG(LORA_FLAG_CONNECTION_LOST);
        }
    }

    if (TESTBIT(lora.ControlLora, ST_LORA_BIT_ACTIV_MOD)) {
        set_state_alarm_flag_lora();
        uint16_t time_data_set = ((Sns_Cfg_struct.Config_common.PeriodTimeLora >> 8) & 0xFF) * 10;
        START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, time_data_set);
    }
    else {
        clr_state_alarm_flag_lora();
    }

    // Бит 3: SET_CLASS_C (RW)
    if (TESTBIT(lora.ControlLora2, ST_LORA2_SET_CLASS_C)) {
        if (!TESTBIT(lora.ControlLora, ST_LORA_BIT_CURRENT_C)) {
            lora.req_change_class = LORA_REQ_CLASS_C;
        }
    }
    else {
        if (TESTBIT(lora.ControlLora, ST_LORA_BIT_CURRENT_C)) {
            lora.req_change_class = LORA_REQ_CLASS_A;
        }
    }

    // Проверка на изменение ControlLora16 для квитирования (v28/v4: маска
    // ST_LORA16_ACK_MASK = v3 0x37 + бит EVACUATION)
    if ((lora.ControlLora & ST_LORA16_ACK_MASK) != lora.alarm_pre) {
        Lora_ack_system_set();
    }
    lora.alarm_pre = lora.ControlLora & ST_LORA16_ACK_MASK;

    port_tx = port_tx_read;

    if (lora.count_turnoff_system_tx != 0) {
        Lora_system_msg_clr();
    }
}

static void Lora_CompleteDataSet(SNS_CFG *Cfg_struct, str_lora_data_set_t *data_set)
{
    /* v28/v4 п.3: Header + ContainerMask(u16) + контейнерные блоки по
       возрастанию битов + SubCount + секции под-устройств (SubID+SubMask).
       Указателей смещений нет - блоки строго по возрастанию битов маски.    */
    uint8_t header = (TYPE_MSG_DYNAMIC & 0xC0) | DATA_STRUCT_LORA_VER;
    uint16_t container_mask = 0;
    uint16_t offset = 3;
    uint8_t *buf = (uint8_t *)data_set;
    uint8_t max_payload = Lora_GetMaxPayloadSize(lora.set_param_dr);
    buf[0] = header;

    Lora_BuildSensorMap();    // v28: карта каналов AVIS перед сборкой (подключение могло измениться)

    bool gas_fresh = (Ble_telemetry.gas_online != 0u) && Lora_BleFresh(Ble_telemetry.gas_ms);
    bool has_gas_event_log  = gas_fresh && (event_log.count > event_log.send_index);
    bool has_cont_event_log = (container_event_log.count > container_event_log.send_index);
    bool has_gps_history    = (gps_history.count > gps_history.send_index);
    bool has_any_history    = has_gas_event_log || has_cont_event_log || has_gps_history;
    int32_t anchor_lat = 0, anchor_lon = 0;
    uint32_t anchor_time = 0;
    bool has_anchor_gps = false;

    if (has_any_history) {
        anchor_time = event_anchor.unix_time;
        anchor_lat  = event_anchor.lat;
        anchor_lon  = event_anchor.lon;
        if (anchor_lat != 0 || anchor_lon != 0) has_anchor_gps = true;
    } else {
        anchor_time = (uint32_t)Cfg_struct->Working_data.working_hours; // Unix-секунды (v28: календарный RTC)
#if CONFIG_GPS
        anchor_lat = gps_last_valid_lat;
        anchor_lon = gps_last_valid_lon;
        if (anchor_lat != 0 || anchor_lon != 0) has_anchor_gps = true;
#endif
    }

    // ========================================================================
    // КОНТЕЙНЕРНЫЕ БЛОКИ (биты ContainerMask по возрастанию, v4 п.3.1/п.4)
    // ========================================================================

    // БИТ 0: Battery (заряд ТРЕКЕРА)
    if ((offset + 1u) <= max_payload) {
        container_mask |= CONTAINER_MASK_BATTERY;
        buf[offset++] = (uint8_t)sensors_status.battery_charge_percent;
    }

    // БИТ 1: GPS (якорь истории или живая координата; опора для GPSTrack/EventLog/GPSHistory)
    if (has_anchor_gps && ((offset + 8u) <= max_payload)) {
        container_mask |= CONTAINER_MASK_GPS;
        memcpy(&buf[offset], &anchor_lat, 4); offset += 4;
        memcpy(&buf[offset], &anchor_lon, 4); offset += 4;
    }

    // БИТ 2: GPSTrack (только если нет истории, чтобы не смешивать дельты; v3-семантика)
    if (!has_any_history) {
#if CONFIG_GPS
        if (gps_hist_has_prev && (gps_hist_count > 0)) {
            uint16_t hist_size = (uint16_t)(1u + (2u * gps_hist_count));
            if ((offset + hist_size) <= max_payload) {
                container_mask |= CONTAINER_MASK_GPS_TRACK;
                buf[offset++] = gps_hist_count;
                for (uint8_t i = 0; i < gps_hist_count; i++) {
                    buf[offset++] = (uint8_t)gps_hist_dx[i];
                    buf[offset++] = (uint8_t)gps_hist_dy[i];
                }
                gps_hist_has_prev = false;
                gps_hist_count = 0;
            }
        }
#endif
    }

    // БИТ 3: TimeRef (обязателен при наличии любого исторического блока)
    if (has_any_history && ((offset + 4u) <= max_payload)) {
        container_mask |= CONTAINER_MASK_TIME_REF;
        memcpy(&buf[offset], &anchor_time, 4);
        offset += 4;
    }

    // БИТ 4: DeviceStatus - ControlLora16 (v28/v4 п.4.2: 2 байта LE)
    if ((offset + 2u) <= max_payload) {
        container_mask |= CONTAINER_MASK_DEV_STATUS;
        buf[offset++] = (uint8_t)(lora.ControlLora & 0xFF);
        buf[offset++] = (uint8_t)((lora.ControlLora >> 8) & 0xFF);
    }

    // БИТ 5: Environment (v4 п.4.4, 9 Б; источник BME280 int32-драйвер:
    // Temperature °C×100 -> °C×10, Humidity %×1024 -> %, Pressure УЖЕ гПа×10)
    if ((sensors_status.Sensors_ok & SENSORS_OK_BME) &&
        ((offset + (uint16_t)sizeof(environment_block_t)) <= max_payload)) {
        environment_block_t env;
        float hum;
        float prs;

        container_mask |= CONTAINER_MASK_ENVIRONMENT;
        env.AirQualityIndex = 0;    // AQI: датчика пока нет (задел, v4 п.4.4)
        env.Temperature = (int16_t)lround(sensors_status.Temperature / 10.0f);
        hum = sensors_status.Humidity / 1024.0f;
        if (hum < 0.0f) hum = 0.0f;
        if (hum > 100.0f) hum = 100.0f;
        env.Humidity = (uint8_t)lround(hum);
        prs = sensors_status.Pressure;      // гПа×10 (Па×100 / 1000)
        if (prs < 0.0f) prs = 0.0f;
        if (prs > 65535.0f) prs = 65535.0f;
        env.Pressure = (uint16_t)lround(prs);
        env.Altitude = Lora_CalcAltitudeM(sensors_status.Pressure);
        memcpy(&buf[offset], &env, sizeof(env));
        offset += (uint16_t)sizeof(env);
    }

    // БИТ 6: Motion (v4 п.4.5, 2 Б: поза/падение из LIS3DH; удар/неподвижность/
    // уровень активности - задел, источника нет)
    if ((sensors_status.Sensors_ok & SENSORS_OK_LIS3DH) && ((offset + 2u) <= max_payload)) {
        uint8_t status_bits = (uint8_t)(Lora_PostureFromOrientation(sensors_status.Orientation) & MOTION_POSTURE_MASK);

        if (fall_latched) {
            status_bits |= (uint8_t)(1u << MOTION_BIT_FALL);
        }
        container_mask |= CONTAINER_MASK_MOTION;
        buf[offset++] = status_bits;
        buf[offset++] = 0;    // ActivityLevel: задел (источник не определён, v4 п.4.5)
    }

    // БИТ 7: PPE (v4 п.4.6: PresenceMask + по байту заряда на установленный
    // бит, в порядке возрастания номеров бит; источник - Ble_telemetry.ppe)
    if (Lora_BleFresh(Ble_telemetry.ppe_ms) && (Ble_telemetry.ppe_count > 0u)) {
        uint8_t presence = 0;
        uint8_t n_present = 0;

        for (uint8_t k = 0; (k < Ble_telemetry.ppe_count) && (k < BLE_PPE_MAX); k++) {
            if (Ble_telemetry.ppe[k].Present) {
                presence |= (uint8_t)(1u << k);
                n_present++;
            }
        }
        if ((offset + 1u + (uint16_t)n_present) <= max_payload) {
            container_mask |= CONTAINER_MASK_PPE;
            buf[offset++] = presence;
            for (uint8_t k = 0; (k < Ble_telemetry.ppe_count) && (k < BLE_PPE_MAX); k++) {
                if (Ble_telemetry.ppe[k].Present) {
                    buf[offset++] = Ble_telemetry.ppe[k].Charge;
                }
            }
        }
    }

    // БИТ 8: BLEBeacons (v4 п.4.7: Count + 4 Б на маяк; раскладка ble_beacon_t
    // совпадает с проводной - memcpy; список урезается по вместимости кадра)
    if (Lora_BleFresh(Ble_telemetry.beacon_ms) && (Ble_telemetry.beacon_count > 0u)) {
        uint8_t n = Ble_telemetry.beacon_count;

        while ((n > 0u) && ((offset + 1u + (uint16_t)(4u * n)) > max_payload)) {
            n--;
        }
        if (n > 0u) {
            container_mask |= CONTAINER_MASK_BLE_BEACONS;
            buf[offset++] = n;
            for (uint8_t i = 0; i < n; i++) {
                memcpy(&buf[offset], &Ble_telemetry.beacons[i], sizeof(ble_beacon_t));
                offset += (uint16_t)sizeof(ble_beacon_t);
            }
        }
    }

    // ========================================================================
    // БИТ 9: КОНТЕЙНЕРНЫЙ EventLog v4 (п.4.8): типизированные записи 10 Б,
    // частичная отправка (send_index/last_sent_count), квита ECHO-битом
    // ========================================================================
    if (has_cont_event_log) {
        uint16_t temp_offset = (uint16_t)(offset + 1u);
        uint8_t packed_events_count = 0;

        for (uint8_t i = container_event_log.send_index; i < container_event_log.count; i++) {
            if ((temp_offset + (uint16_t)sizeof(container_event_t)) > max_payload) break;
            temp_offset += (uint16_t)sizeof(container_event_t);
            packed_events_count++;
        }

        if (packed_events_count > 0) {
            container_mask |= CONTAINER_MASK_EVENT_LOG;
            buf[offset++] = packed_events_count;

            for (uint8_t i = 0; i < packed_events_count; i++) {
                container_event_record_t *rec = &container_event_log.records[container_event_log.send_index + i];

                // Dt = секунды ВПЕРЁД от TimeRef
                uint32_t dt_sec = (rec->unix_time >= anchor_time) ? (rec->unix_time - anchor_time) : 0;
                if (dt_sec > 0xFFFF) dt_sec = 0xFFFF;
                buf[offset++] = (uint8_t)(dt_sec & 0xFF);
                buf[offset++] = (uint8_t)((dt_sec >> 8) & 0xFF);

                // Dx, Dy (0x8000 = нет фикса; LE: байты 0x00 0x80)
                if (rec->lat != 0) {
                    int32_t dx32 = Lora_CalcDeltaMeters(rec->lat, anchor_lat, 0);
                    int32_t dy32 = Lora_CalcDeltaMeters(rec->lon, anchor_lon, anchor_lat);
                    if (dx32 > 32767) dx32 = 32767;
                    if (dx32 < -32768) dx32 = -32768;
                    if (dy32 > 32767) dy32 = 32767;
                    if (dy32 < -32768) dy32 = -32768;
                    buf[offset++] = (uint8_t)(dx32 & 0xFF);
                    buf[offset++] = (uint8_t)((dx32 >> 8) & 0xFF);
                    buf[offset++] = (uint8_t)(dy32 & 0xFF);
                    buf[offset++] = (uint8_t)((dy32 >> 8) & 0xFF);
                } else {
                    buf[offset++] = 0x00; buf[offset++] = 0x80;    // Dx = 0x8000 (v28: LE-порядок исправлен)
                    buf[offset++] = 0x00; buf[offset++] = 0x80;    // Dy = 0x8000
                }

                buf[offset++] = rec->type;
                buf[offset++] = rec->severity;
                buf[offset++] = (uint8_t)(rec->value & 0xFF);
                buf[offset++] = (uint8_t)((rec->value >> 8) & 0xFF);
            }
            container_event_log.last_sent_count = packed_events_count;
        }
    }

    // ========================================================================
    // БИТ 10: GPSHistory (восстановление трека за время потери связи, v3-формат)
    // ========================================================================
    if (has_gps_history && has_anchor_gps) {
        uint8_t gps_hist_to_send = gps_history.count - gps_history.send_index;

        if (gps_hist_to_send > 0) {
            // 1. Вычисляем габарит трека для выбора Scale
            int32_t max_abs_dx = 0, max_abs_dy = 0;
            for (uint8_t i = gps_history.send_index; i < gps_history.count; i++) {
                gps_history_point_t *pt = &gps_history.points[i];
                int32_t dx_m = Lora_CalcDeltaMeters(pt->lat, anchor_lat, 0);
                int32_t dy_m = Lora_CalcDeltaMeters(pt->lon, anchor_lon, anchor_lat);
                int32_t abs_dx = (dx_m < 0) ? -dx_m : dx_m;
                int32_t abs_dy = (dy_m < 0) ? -dy_m : dy_m;
                if (abs_dx > max_abs_dx) max_abs_dx = abs_dx;
                if (abs_dy > max_abs_dy) max_abs_dy = abs_dy;
            }

            // 2. Scale = ceil(max(|dx|,|dy|) / 127), минимум 1, максимум 255
            uint8_t scale = 1;
            int32_t max_dim = (max_abs_dx > max_abs_dy) ? max_abs_dx : max_abs_dy;
            if (max_dim > 127) {
                scale = (uint8_t)((max_dim + 126) / 127);
                if (scale > 255) scale = 255;
            }

            // 3. Dt0: смещение ПЕРВОЙ точки ВПЕРЁД от TimeRef (anchor_time)
            uint32_t dt0 = (gps_history.first_point_unix_time >= anchor_time) ?
                           (gps_history.first_point_unix_time - anchor_time) : 0;
            if (dt0 > 0xFFFF) dt0 = 0xFFFF;

            // 4. Step: средний шаг между ПЕРВОЙ и ПОСЛЕДНЕЙ точкой
            uint16_t step = 60; // По умолчанию
            if (gps_hist_to_send > 1) {
                uint32_t total_span = (gps_history.last_point_unix_time >= gps_history.first_point_unix_time) ?
                                      (gps_history.last_point_unix_time - gps_history.first_point_unix_time) : 0;
                step = (uint16_t)(total_span / (gps_hist_to_send - 1));
                if (step == 0) step = 1;
                if (step > 0xFFFF) step = 0xFFFF;
            }

            // 5. Проверяем, влезет ли блок: 6 (заголовок) + 2×N (точки)
            uint16_t block_size = (uint16_t)(6u + (2u * gps_hist_to_send));
            if ((offset + block_size) <= max_payload) {
                container_mask |= CONTAINER_MASK_GPS_HISTORY;

                buf[offset++] = gps_hist_to_send;           // Count
                buf[offset++] = (uint8_t)(dt0 & 0xFF);      // Dt0 low
                buf[offset++] = (uint8_t)((dt0 >> 8) & 0xFF); // Dt0 high
                buf[offset++] = (uint8_t)(step & 0xFF);     // Step low
                buf[offset++] = (uint8_t)((step >> 8) & 0xFF); // Step high
                buf[offset++] = scale;                      // Scale

                // Точки (от старых к новым)
                for (uint8_t i = 0; i < gps_hist_to_send; i++) {
                    uint8_t idx = gps_history.send_index + i;
                    gps_history_point_t *pt = &gps_history.points[idx];

                    if (pt->lat != 0 || pt->lon != 0) {
                        int32_t dx_m = Lora_CalcDeltaMeters(pt->lat, anchor_lat, 0);
                        int32_t dy_m = Lora_CalcDeltaMeters(pt->lon, anchor_lon, anchor_lat);

                        int8_t dx_scaled = (int8_t)(dx_m / (int32_t)scale);
                        int8_t dy_scaled = (int8_t)(dy_m / (int32_t)scale);

                        if (dx_scaled > 127) dx_scaled = 127;
                        if (dx_scaled < -128) dx_scaled = -128;
                        if (dy_scaled > 127) dy_scaled = 127;
                        if (dy_scaled < -128) dy_scaled = -128;

                        buf[offset++] = (uint8_t)dx_scaled;
                        buf[offset++] = (uint8_t)dy_scaled;
                    } else {
                        buf[offset++] = 0x80;
                        buf[offset++] = 0x80;
                    }
                }
            }
            else{
            	gps_hist_to_send = 0;
            }
            gps_history.last_sent_count = gps_hist_to_send;
        }
    }

    // БИТ 11: ConfigAck
    if (lora.config_ack_pending && ((offset + 1u) <= max_payload)) {
        container_mask |= CONTAINER_MASK_CONFIG_ACK;
        buf[offset++] = lora.applied_rev;
        // Сбрасываем флаг сразу после добавления в буфер отправки.
        // Если пакет потеряется, сервер снова пришлет ConfigSeq, и мы взведем флаг заново.
        lora.config_ack_pending = 0;
    }

    // БИТ 12: MsgAck (v28/v4 п.7.1: квитирование доставки текста тем же MsgSeq)
    if (lora.msg_ack_pending && ((offset + 1u) <= max_payload)) {
        container_mask |= CONTAINER_MASK_MSG_ACK;
        buf[offset++] = Lora_Message.MsgSeq;
        lora.msg_ack_pending = 0;
    }

    // ========================================================================
    // СЕКЦИИ ПОД-УСТРОЙСТВ v4 (п.5): SubCount + [SubID + SubMask + блоки]
    // ========================================================================
    if (offset < max_payload) {
        uint16_t subcount_pos = offset++;    // резерв SubCount, заполним по факту
        uint8_t sub_count = 0;

        // --- SUB_TYPE_GAS: газоанализатор AVIS (один; данные по BLE) -------
        if (gas_fresh) {
            uint16_t sec_start = offset;
            uint8_t sub_mask = 0;

            offset += 2u;    // резерв SubID + SubMask

            // Бит 0: Battery (заряд газоанализатора, %)
            if ((offset + 1u) <= max_payload) {
                uint8_t gas_batt = (uint8_t)Connected_device.battery_charge_percent;
                sub_mask |= SUB_MASK_GAS_BATTERY;
                buf[offset++] = (gas_batt > 100u) ? 100u : gas_batt;
            }

            // Бит 1: Sensors (Count + sensor_data_t[] v3 п.4.3: SensorIndex +
            // int16 Concentration + StateErr; только каналы вне мёртвой зоны)
            uint8_t active_sensors = 0;
            for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
                if ((Connected_device.Sensor[chan].channel == chan) &&
                    TESTBIT(lora.alarm_chan_state, chan)) {
                    active_sensors++;
                }
            }

            uint16_t sensors_block_size = (uint16_t)(1u + ((uint16_t)active_sensors * 4u));

            if ((active_sensors > 0) && ((offset + sensors_block_size) <= max_payload)) {
                {
                    sub_mask |= SUB_MASK_GAS_SENSORS;
                    buf[offset++] = active_sensors;

                    for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
                        if ((Connected_device.Sensor[chan].channel == chan) &&
                            TESTBIT(lora.alarm_chan_state, chan)) {
                            int16_t conc_to_send;
                            uint8_t state_err_to_send;

                            buf[offset++] = lora_sensor_map[chan];

                            // Выбор: замороженные данные (первый пакет после
                            // события) или живые (последующие)
                            if (chan_state[chan].trigger_valid) {
                                conc_to_send = chan_state[chan].trigger_concentration;
                                state_err_to_send = chan_state[chan].trigger_state_err;
                            }
                            else {
                                conc_to_send = Lora_ScaleConcentration(chan);
                                state_err_to_send = (uint8_t)(Connected_device.Sensor[chan].StateErr & 0x7F);
                            }

                            buf[offset++] = (uint8_t)(conc_to_send & 0xFF);
                            buf[offset++] = (uint8_t)((conc_to_send >> 8) & 0xFF);
                            buf[offset++] = state_err_to_send;
                        }
                    }
                }
            }

            // Бит 2: EventLog (канальный журнал v3 п.4.8 за время потери связи;
            // якорь времени/координаты - КОНТЕЙНЕРНЫЕ TimeRef/GPS этого кадра)
            if (has_gas_event_log) {
                uint16_t temp_offset = (uint16_t)(offset + 1u);
                uint8_t packed_events_count = 0;

                for (uint8_t i = event_log.send_index; i < event_log.count; i++) {
                    event_log_record_t *rec = &event_log.records[i];
                    uint16_t record_size = (uint16_t)(7u + ((uint16_t)rec->channel_count * 4u));
                    if ((temp_offset + record_size) > max_payload) break;
                    temp_offset += record_size;
                    packed_events_count++;
                }

                if (packed_events_count > 0) {
                    sub_mask |= SUB_MASK_GAS_EVENT_LOG;
                    buf[offset++] = packed_events_count;

                    for (uint8_t i = 0; i < packed_events_count; i++) {
                        uint8_t idx = event_log.send_index + i;
                        event_log_record_t *rec = &event_log.records[idx];

                        // Dt = секунды ВПЕРЁД от TimeRef
                        uint32_t dt_sec = (rec->unix_time >= anchor_time) ? (rec->unix_time - anchor_time) : 0;
                        if (dt_sec > 0xFFFF) dt_sec = 0xFFFF;

                        buf[offset++] = (uint8_t)(dt_sec & 0xFF);
                        buf[offset++] = (uint8_t)((dt_sec >> 8) & 0xFF);

                        // Dx, Dy (0x8000 = нет фикса; LE: байты 0x00 0x80)
                        if (rec->lat != 0) {
                            int32_t dx32 = Lora_CalcDeltaMeters(rec->lat, anchor_lat, 0);
                            int32_t dy32 = Lora_CalcDeltaMeters(rec->lon, anchor_lon, anchor_lat);
                            if (dx32 > 32767) dx32 = 32767;
                            if (dx32 < -32768) dx32 = -32768;
                            if (dy32 > 32767) dy32 = 32767;
                            if (dy32 < -32768) dy32 = -32768;
                            buf[offset++] = (uint8_t)(dx32 & 0xFF);
                            buf[offset++] = (uint8_t)((dx32 >> 8) & 0xFF);
                            buf[offset++] = (uint8_t)(dy32 & 0xFF);
                            buf[offset++] = (uint8_t)((dy32 >> 8) & 0xFF);
                        } else {
                            buf[offset++] = 0x00; buf[offset++] = 0x80;    // Dx = 0x8000 (v28: LE исправлен)
                            buf[offset++] = 0x00; buf[offset++] = 0x80;    // Dy = 0x8000
                        }

                        // Channels
                        buf[offset++] = rec->channel_count;
                        for (uint8_t c = 0; c < rec->channel_count; c++) {
                            buf[offset++] = rec->channels[c].Channel;
                            buf[offset++] = rec->channels[c].StateErr;
                            buf[offset++] = (uint8_t)(rec->channels[c].Concentration & 0xFF);
                            buf[offset++] = (uint8_t)((rec->channels[c].Concentration >> 8) & 0xFF);
                        }
                    }
                    event_log.last_sent_count = packed_events_count;
                }
            }

            if (sub_mask != 0) {
                buf[sec_start]     = LORA_SUBID_GAS_AVIS;    // SubID
                buf[sec_start + 1] = sub_mask;               // SubMask
                sub_count++;
            } else {
                offset = sec_start;    // секция не собрала ни блока - откат
            }
        }

        // --- SUB_TYPE_PULSE: пульсометр - ЗАДЕЛ (v4 п.5.1): подключение по
        // BLE ещё не реализовано, секция не собирается (TODO автор).         ---

        buf[subcount_pos] = sub_count;
    }

    fall_latched = 0;    // бит падения показали в этом кадре - снять (событие в журнале живёт отдельно)

    buf[1] = (uint8_t)(container_mask & 0xFF);
    buf[2] = (uint8_t)((container_mask >> 8) & 0xFF);
    len_lora_data_set = offset;
}

static void Lora_CompleteDataSet_1(SNS_CFG *Cfg_struct, str_lora_data_set_1_t *data_set)
{
    /* v28/v4 п.6: конфигурация комплекта - паспорт трекера + ФАКТИЧЕСКИЕ
       радиопараметры (ground truth) + состав СИЗ + под-устройства с их
       идентификаторами и type-config (самоописываемо через CfgLen).         */
    uint8_t *tail = data_set->Tail;
    uint16_t t = 0;

    data_set->data_struct_ver   = DATA_STRUCT_LORA_VER | TYPE_MSG_STATIC;
    data_set->dev_id            = DEVICE_NUMBER;   // TODO автор: "заводской идентификатор трекера" - кандидат SerialLo (вопросы v28, PLAN)
    data_set->dev_hw_ver        = Cfg_struct->Config_common.HardwareVersion;
    data_set->dev_fw_ver        = FIRMWARE_VERSION;

    // Радиопараметры (v4 п.6 / v3 п.4.11.1): фактически установленные в Lora_Init
    data_set->Radio.Rx1DrOffset    = LORA_RX1_DR_OFFSET;
    data_set->Radio.Rx2DataRate    = lora.rx2_set_param_dr;
    data_set->Radio.Rx2FrequencyHz = lora.rx2_set_param_freq;
    data_set->Radio.Rx1DelaySec    = LORA_RX1_DELAY_SEC;
    data_set->Radio.TxPower        = LORA_TX_POWER_REPORT;
    data_set->Radio.MaxDutyCycle   = LORA_MAX_DUTY_CYCLE;

    // СИЗ (v4 п.6): бит k PPEEnabledMask = k-я заведённая единица (индекс в
    // Ble_telemetry.ppe), за битом следует ppe_descr_t {Type, Serial u16 LE}
    data_set->PPEEnabledMask = 0;
    if (Lora_BleFresh(Ble_telemetry.ppe_ms)) {
        for (uint8_t k = 0; (k < Ble_telemetry.ppe_count) && (k < BLE_PPE_MAX); k++) {
            ppe_descr_t d;

            data_set->PPEEnabledMask |= (uint8_t)(1u << k);
            d.Type   = Ble_telemetry.ppe[k].PpeType;
            d.Serial = Ble_telemetry.ppe[k].Serial;
            if ((t + sizeof(ppe_descr_t)) <= LORA_STATIC_TAIL_MAX) {
                memcpy(&tail[t], &d, sizeof(d));
                t = (uint16_t)(t + sizeof(ppe_descr_t));
            }
        }
    }

    // Под-устройства (v4 п.5/п.6): SubCount + дескрипторы подряд
    uint16_t subcount_pos = t;
    uint8_t sub_count = 0;
    if (t < LORA_STATIC_TAIL_MAX) {
        tail[t++] = 0;    // резерв SubCount
    }

    // SUB_TYPE_GAS: газоанализатор AVIS. Type-config (v4 п.6): uint8
    // SensorsEnabled (битовая маска каналов) + channel_cfg_t[] (GasID u16 +
    // Setting u8, как v3 п.4.11); CfgLen = 1 + 3×N.
    if ((Ble_telemetry.gas_online != 0u) && Lora_BleFresh(Ble_telemetry.gas_ms)) {
        uint8_t enabled = 0;
        uint8_t n = 0;

        for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
            if (Connected_device.Sensor[chan].channel == chan) {
                enabled |= (uint8_t)(1u << chan);
                n++;
            }
        }
        uint16_t need = (uint16_t)(5u + 1u + ((uint16_t)n * sizeof(sensor_data_lora_1_t)));
        if ((t + need) <= LORA_STATIC_TAIL_MAX) {
            uint16_t serial = (Ble_telemetry.gas_serial != 0u) ? Ble_telemetry.gas_serial
                                                               : Connected_device.dev_id;
            uint16_t cfglen_pos;

            tail[t++] = LORA_SUBID_GAS_AVIS;          // SubID
            tail[t++] = SUB_TYPE_GAS;                 // SubType
            tail[t++] = (uint8_t)(serial & 0xFF);     // Serial LE. TODO: legacy-пакет
            tail[t++] = (uint8_t)(serial >> 8);       //  Avis серийника не несёт - берём
                                                      //  dev_id; настоящий - из BLE-пакета
                                                      //  SUBDEV (docs/Mirax-Ble-protocol-v1.md)
            cfglen_pos = t;
            tail[t++] = 0;                            // резерв CfgLen
            tail[t++] = enabled;                      // SensorsEnabled
            for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
                if (Connected_device.Sensor[chan].channel == chan) {
                    sensor_data_lora_1_t c;

                    c.num_formula_gas = Connected_device.Sensor[chan].num_formula_gas;
                    c.Setting         = (uint8_t)Connected_device.Sensor[chan].Setting;
                    memcpy(&tail[t], &c, sizeof(c));
                    t = (uint16_t)(t + sizeof(c));
                }
            }
            tail[cfglen_pos] = (uint8_t)(1u + ((uint16_t)n * sizeof(sensor_data_lora_1_t)));
            sub_count++;
        }
    }
    // SUB_TYPE_PULSE: пульсометр - ЗАДЕЛ (ещё не подключается, v4 п.5.1).
    tail[subcount_pos] = sub_count;

    // Длина = фиксированная часть (всё до Tail) + собранный хвост
    len_lora_data_set_1 = (uint16_t)((sizeof(str_lora_data_set_1_t) - LORA_STATIC_TAIL_MAX) + t);
}

static void Lora_CompleteDataSet_2(SNS_CFG *Cfg_struct, str_lora_data_set_2_t *data_set, TYPE_MSG_LORA type_msg)
{
    data_set->data_struct_ver   = DATA_STRUCT_LORA_VER | type_msg;
    data_set->dev_id            = DEVICE_NUMBER;
    data_set->dev_hw_ver        = Cfg_struct->Config_common.HardwareVersion;
    data_set->dev_fw_ver        = FIRMWARE_VERSION;
    data_set->ControlLora       = lora.ControlLora;

    len_lora_data_set_2 = sizeof(str_lora_data_set_2_t);
}

static void Lora_CompleteDataSetAck(str_lora_data_set_ack_t *data_set)
{
    data_set->data_struct_ver   = DATA_STRUCT_LORA_VER | TYPE_MSG_ACK;
    data_set->ControlLora       = lora.ControlLora;
}

void Lora_DataSet(SNS_CFG *Cfg_struct)
{
	LORA_SET_FLAG(LORA_FLAG_START);
    if (LORA_TEST_FLAG(LORA_FLAG_START) && (!LORA_TEST_FLAG(LORA_FLAG_ERR))) {
#if LORA_DEBUG
        if (start_lora_read) {
            // return;
        }
#endif

        if (LORA_TEST_FLAG(LORA_FLAG_RX2_SETUP_PENDING)) {
			if (lora.rx2_delay_ms == 0) {
				LORA_CLR_FLAG(LORA_FLAG_RX2_SETUP_PENDING);
				Lora_TransmitReceive(CMD_SET_RX2, (uint8_t*)&lora.set_param_dr);
				LORA_SET_FLAG(LORA_FLAG_MAC_SAVE_PENDING);
			}
		}

		if (LORA_TEST_FLAG(LORA_FLAG_MAC_SAVE_PENDING)) {
			LORA_CLR_FLAG(LORA_FLAG_MAC_SAVE_PENDING);
			Lora_TransmitReceive(CMD_MAC_SAVE, (uint8_t*)&lora.empty_buffer);
			SETBIT(Sns_Cfg_struct.Config_common.Lora_Config_Flags, LORA_CFG_BIT_SAVED);
			SaveConfig = 1;
		}

		if (LORA_TEST_FLAG(LORA_FLAG_REQ_CHANGE_REGION)) {
			LORA_CLR_FLAG(LORA_FLAG_REQ_CHANGE_REGION);
			Lora_Switch_Region_On_The_Fly(&Sns_Cfg_struct, 1);
			LORA_SET_FLAG(LORA_FLAG_MAC_SAVE_PENDING);
			return;
		}

        if (lora.req_change_class != LORA_REQ_CLASS_NONE) {
            uint8_t class_to_set = LORA_CLASS_A;
            uint8_t current_req = lora.req_change_class;
            lora.req_change_class = LORA_REQ_CLASS_NONE;

            if (current_req == LORA_REQ_CLASS_C) {
                class_to_set = LORA_CLASS_C;
                if (Lora_TransmitReceive(CMD_SET_CLASS, &class_to_set) == 1) {
                    SETBIT(lora.ControlLora, ST_LORA_BIT_CURRENT_C);
                    Lora_Cfg_UpdateClass(1);
                }
            }
            else if (current_req == LORA_REQ_CLASS_A) {
                class_to_set = LORA_CLASS_A;
                if (Lora_TransmitReceive(CMD_SET_CLASS, &class_to_set) == 1) {
                    CLRBIT(lora.ControlLora, ST_LORA_BIT_CURRENT_C);
                    Lora_Cfg_UpdateClass(0);
                }
            }
            LORA_SET_FLAG(LORA_FLAG_MAC_SAVE_PENDING);
//            LORA_SET_FLAG(LORA_FLAG_RX2_SETUP_PENDING);
//            lora.rx2_delay_ms = 12000;
            Delay(50);
        }

        // ========================================================================
        // ФИКСАЦИЯ ANCHOR ПЕРЕД ОТПРАВКОЙ (если есть история)
        // ========================================================================
        if (!LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST) || (!event_anchor.is_valid)) {
            event_anchor.unix_time = (uint32_t)Cfg_struct->Working_data.working_hours;
#if CONFIG_GPS
        	if (gps_last_valid_lat != 0 || gps_last_valid_lon != 0) {
        		event_anchor.is_valid = true;
        	}
            event_anchor.lat = gps_last_valid_lat;
            event_anchor.lon = gps_last_valid_lon;
#else
            event_anchor.is_valid = true;
            event_anchor.lat = 0;
            event_anchor.lon = 0;
#endif
        }

        // ========================================================================
        // ФИКСАЦИЯ GPS-ТОЧКИ ДЛЯ ИСТОРИИ (только при потере связи)
        // ========================================================================
        bool has_event_log = (event_log.count > event_log.send_index);
        bool has_cont_event_log = (container_event_log.count > container_event_log.send_index);
        bool has_gps_history = (gps_history.count > gps_history.send_index);
        bool has_any_history = has_event_log || has_cont_event_log || has_gps_history;

#if CONFIG_GPS
        if (LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST) && event_anchor.is_valid) {
            uint32_t current_time = (uint32_t)Cfg_struct->Working_data.working_hours;
            bool should_add = false;

            uint16_t period_lora_sec = (Sns_Cfg_struct.Config_common.PeriodTimeLora & 0xFF) * 10;

            if (gps_history.count == 0) {
                should_add = true;
            } else {
                if ((current_time - gps_history.last_point_unix_time) >= period_lora_sec) {
                    should_add = true;
                }
            }

            if (should_add && (gps_history.count < GPS_HISTORY_MAX_POINTS)) {
                if (gps_last_valid_lat != 0 || gps_last_valid_lon != 0) {
                    if (gps_history.count == 0) {
                        gps_history.first_point_unix_time = current_time;
                    }
                    gps_history.last_point_unix_time = current_time;

                    gps_history_point_t *new_point = &gps_history.points[gps_history.count];
                    new_point->lat = gps_last_valid_lat;
                    new_point->lon = gps_last_valid_lon;
                    gps_history.count++;
                }
            }
        }
#endif

        // Установка ECHO бита: если есть repeat_flag, активный режим ИЛИ есть история
        if (get_state_repeat_flag_lora() ||
            TESTBIT(lora.ControlLora, ST_LORA_BIT_ACTIV_MOD) ||
			has_any_history) {
            SETBIT(lora.ControlLora, ST_LORA_BIT_ECHO);
        }
        else {
            CLRBIT(lora.ControlLora, ST_LORA_BIT_ECHO);
        }


        if (Lora_system_msg_read() == 1) {
            lora.count_turnoff_system_tx++;
            if (lora.count_turnoff_system_tx >= 3) {
                Lora_system_msg_clr();
            }
            else {
                Lora_CompleteDataSet_2(Cfg_struct, &lora_data_set_2, TYPE_MSG_TURN_OFF);
                len_lora_data_set_tr = len_lora_data_set_2;
                Lora_Transmit(CMD_TX_CNF, (uint8_t *)&lora_data_set_2);
            }
        }
        else if (Lora_system_msg_read() == 2) {
            Lora_CompleteDataSetAck(&lora_data_set_ack);
            len_lora_data_set_tr = sizeof(str_lora_data_set_ack_t);
            Lora_Transmit(CMD_TX_UCNF, (uint8_t *)&lora_data_set_ack);
            Lora_system_msg_clr();
        }
		else if (TESTBIT(lora.ControlLora2, ST_LORA2_REQ_STATIC)) {
			Lora_CompleteDataSet_1(Cfg_struct, &lora_data_set_1);
			len_lora_data_set_tr = len_lora_data_set_1;
			Lora_Transmit(CMD_TX_CNF, (uint8_t *)&lora_data_set_1);
		}
		else {
			// Штатная отправка динамического пакета
			Lora_CompleteDataSet(Cfg_struct, &lora_data_set);
			len_lora_data_set_tr = len_lora_data_set;
			Lora_Transmit(CMD_TX_CNF, (uint8_t *)&lora_data_set);
		}
    }
}

void Lora_SystemSleep(uint32_t time_s)
{
    if (LORA_TEST_FLAG(LORA_FLAG_START)) {
        uint32_t time_sleep = time_s;

        if (time_sleep < 20) {
            time_sleep = 20;
        }

        Lora_Transmit(CMD_SIP_SLEEP, (uint8_t*)&time_sleep);
        LORA_SET_FLAG(LORA_FLAG_SLEEP);
    }
}

void Lora_SystemWakeUp(void)
{
    if (LORA_TEST_FLAG(LORA_FLAG_SLEEP)) {
        uint8_t wake_bytes[2] = {'\r', '\n'};

#if LORA_DEBUG
        transmit_buffer(wake_bytes, 2, TYPE_USART);
#endif

        transmit_buffer(wake_bytes, 2, TYPE_LORA);
        Delay(15);
        LORA_CLR_FLAG(LORA_FLAG_SLEEP);
    }
}

static uint8_t Lora_Transmit(COMMAND_LORA command, uint8_t *data)
{
    uint8_t read_uart = 0;
    uint8_t *data_command = buffer_uart_lora.transmit;
    static const char hex_chars[] = "0123456789ABCDEF";

    Lora_SystemWakeUp();

    if (Lora_flag_receive_read() == 0) {
        if (lora.counter_err < 6) {
            lora.counter_err++;

            if (lora.counter_err == 6) {
                lora.counter_err = 0;
                LORA_SET_FLAG(LORA_FLAG_ERR);
            }
        }
    }

    Lora_flag_receive_clr();

    lora.tx_busy_timer = TX_BUSY_TIMEOUT_TX_MS;

    uint16_t len = lora_commands[command].cmd_len;
    memcpy(data_command, lora_commands[command].cmd_str, len);

    switch (command) {
        case CMD_TX_CNF: {
            if (get_flag_receive_res_tx_lora()) {
                lora.count_receive_res_tx += 1;
            }

            if (LORA_TEST_FLAG(LORA_FLAG_RECEIVE_TX_OK)) {
                // return 0;
            }

            LORA_CLR_FLAG(LORA_FLAG_RECEIVE_TX_OK);
            len += sprintf((char*)&data_command[len], "%d ", port_tx);

            for (uint16_t i = 0; i < len_lora_data_set_tr; i++) {
                data_command[len++] = hex_chars[data[i] >> 4];
                data_command[len++] = hex_chars[data[i] & 0x0F];
            }
            break;
        }

        case CMD_TX_UCNF: {
            len += sprintf((char*)&data_command[len], "%d ", port_tx);

            for (uint16_t i = 0; i < len_lora_data_set_tr; i++) {
                data_command[len++] = hex_chars[data[i] >> 4];
                data_command[len++] = hex_chars[data[i] & 0x0F];
            }
            break;
        }

        case CMD_SET_CH_FR: {
            len += sprintf((char*)&data_command[len], "%d %lu", *data, lora_frequency_table[lora_current_region][*data]);
            break;
        }

        case CMD_SET_RX2: {
            uint8_t rx2_dr = lora.rx2_set_param_dr;
            uint32_t freq_eu = lora.rx2_set_param_freq;
            len += sprintf((char*)&data_command[len], "%d %lu", rx2_dr, freq_eu);
            break;
        }

        case CMD_SET_CH_JOIN_ON:
        case CMD_SET_CH_ST_ON: {
            len += sprintf((char*)&data_command[len], "%d on", *data);
            break;
        }

        case CMD_SET_CH_JOIN_OFF:
        case CMD_SET_CH_ST_OFF: {
            len += sprintf((char*)&data_command[len], "%d off", *data);
            break;
        }

        case CMD_SET_DEVEUI:
        case CMD_SET_APPEUI: {
            for (uint16_t i = 0; i < 8; i++) {
                data_command[len++] = hex_chars[data[i] >> 4];
                data_command[len++] = hex_chars[data[i] & 0x0F];
            }
            break;
        }

        case CMD_SET_DEVADDR: {
            for (uint16_t i = 0; i < 4; i++) {
                data_command[len++] = hex_chars[data[i] >> 4];
                data_command[len++] = hex_chars[data[i] & 0x0F];
            }
            break;
        }

        case CMD_SET_APPKEY:
        case CMD_SET_NWKSKEY:
        case CMD_SET_APPSKEY: {
            for (uint16_t i = 0; i < 16; i++) {
                data_command[len++] = hex_chars[data[i] >> 4];
                data_command[len++] = hex_chars[data[i] & 0x0F];
            }
            break;
        }

        case CMD_JOIN_OTAA: {
            break;
        }

        case CMD_SET_TX_MODE: {
            uint8_t mode_idx = *data;

            if (mode_idx > 1) {
                mode_idx = 1;
            }

            memcpy(&data_command[len], lora_tx_modes[mode_idx], lora_tx_modes_len[mode_idx]);
            len += lora_tx_modes_len[mode_idx];
            break;
        }

        case CMD_GET_TX_MODE:
        case CMD_GET_UPCNT: {
            break;
        }

        case CMD_MAC_SAVE: {
            break;
        }

        case CMD_SET_RT_STORE: {
            uint8_t flag_idx = *data;

            if (flag_idx > 1) {
                flag_idx = 1;
            }

            memcpy(&data_command[len], lora_flags[flag_idx], lora_flags_len[flag_idx]);
            len += lora_flags_len[flag_idx];
            break;
        }

        case CMD_SIP_SLEEP: {
            len += sprintf((char*)&data_command[len], "%d", *data);
            memcpy(&data_command[len], UART_ON, SIZE_UART_ON);
            len += SIZE_UART_ON;
            break;
        }

        case CMD_SET_UPCNT:
        case CMD_SET_ADR: {
            uint8_t flag_idx = *data;

            if (flag_idx > 1) {
                flag_idx = 1;
            }

            memcpy(&data_command[len], lora_flags[flag_idx], lora_flags_len[flag_idx]);
            len += lora_flags_len[flag_idx];
            break;
        }

        case CMD_GET_DR: {
            break;
        }

        case CMD_SET_DR:
        case CMD_SET_TXRETRY: {
            len += sprintf((char*)&data_command[len], "%d", *data);
            break;
        }

        case CMD_SET_RXDELAY: {
            len += sprintf((char*)&data_command[len], "%d", (*data) * 1000);
            break;
        }

        case CMD_LBT_ON: {
            break;
        }

        case CMD_SET_CR: {
            uint8_t cr_idx = (*data > 3) ? 3 : *data;
            memcpy(&data_command[len], lora_coding_rates[cr_idx], 3);
            len += 3;
            break;
        }

        case CMD_SET_CLASS: {
            uint8_t class_idx = *data;

            if (class_idx > 1) {
                class_idx = 1;
            }

            memcpy(&data_command[len], lora_classes[class_idx], 1);
            len += 1;
            break;
        }

        default: {
            break;
        }
    }

    while (check_buffer(&InputBuffer[TYPE_LORA])) {
        receive_buffer(&InputBuffer[TYPE_LORA], &read_uart);
    }

#if LORA_DEBUG
    transmit_buffer(data_command, len, TYPE_USART);
#endif

    transmit_buffer(data_command, len, TYPE_LORA);
    return 1;
}

static inline uint8_t HexToByte(char high, char low)
{
    #define HEX_VAL(c) ((c >= '0' && c <= '9') ? (c - '0') : \
                       (c >= 'a' && c <= 'f') ? (c - 'a' + 10) : \
                       (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : 0)
    return (HEX_VAL(high) << 4) | HEX_VAL(low);
}

static void ParseLoraToStruct(const char* uart_buffer)
{
    if (uart_buffer == NULL) {
        return;
    }

    char* res_ptr = strstr(uart_buffer, (char*)RES_RX);
    if (res_ptr == NULL) {
        return;
    }

    char* port_ptr = res_ptr + strlen((char*)RES_RX);
    char* next_space_ptr = NULL;

    int port_tx_read = (int)strtol(port_ptr, &next_space_ptr, 10);
    if (port_tx_read == 0) {
        return;
    }

    if (next_space_ptr != NULL && *next_space_ptr == ' ') {
        char* hex_ptr = next_space_ptr + 1;
        size_t hex_len = strlen(hex_ptr);
        size_t bytes_to_parse = hex_len / 2;

        // 1. Преобразуем HEX-строку в массив байтов
        // v28: буфер 244 - downlink v4 с блоком Message до 242 байт (v4 п.7.1)
        uint8_t payload[244];
        if (bytes_to_parse > sizeof(payload)) {
            bytes_to_parse = sizeof(payload);
        }

        for (size_t i = 0; i < bytes_to_parse; i++) {
            payload[i] = HexToByte(hex_ptr[i * 2], hex_ptr[(i * 2) + 1]);
        }

        // 2. Минимальная длина: Header (1) + FieldMask (2) = 3 байта
        if (bytes_to_parse < 3) {
            return;
        }

        // 2b. v28/v4 п.2/п.7: принимаем ТОЛЬКО downlink своей версии
        // (Header = TYPE_CONFIG | 4 = 0x04). Прочее (напр. v3-кадры) - игнор.
        if (((payload[0] & 0xC0u) != TYPE_MSG_DYNAMIC) ||
            ((payload[0] & 0x0Fu) != DATA_STRUCT_LORA_VER)) {
            return;
        }

        // 3. Очищаем структуру перед заполнением
        memset(&lora_data_get, 0, sizeof(str_lora_data_get_t));

        // 4. Читаем обязательные поля
        lora_data_get.Header = payload[0];
        lora_data_get.FieldMask = payload[1] | (payload[2] << 8);

        // 5. ДИНАМИЧЕСКИЙ ПАРСИНГ: сдвигаем offset только если бит установлен в FieldMask
        uint8_t offset = 3; // Начинаем после Header и FieldMask

        if (lora_data_get.FieldMask & FIELD_MASK_DL_CONTROL) {
            // v28/v4 п.7: блок Control расширен до Mask(u16) + Value(u16)
            if (offset + 4 <= bytes_to_parse) {
                lora_data_get.ControlLoraMask  = (uint16_t)(payload[offset] | (payload[offset + 1] << 8));
                lora_data_get.ControlLoraValue = (uint16_t)(payload[offset + 2] | (payload[offset + 3] << 8));
                offset += 4;
            }
        }

        if (lora_data_get.FieldMask & FIELD_MASK_DL_MODE) {
            if (offset + 2 <= bytes_to_parse) {
                lora_data_get.ControlLora2Mask = payload[offset++];
                lora_data_get.ControlLora2Value = payload[offset++];
            }
        }

        if (lora_data_get.FieldMask & FIELD_MASK_DL_PERIOD_NORM) {
            if (offset + 1 <= bytes_to_parse) {
                lora_data_get.NewPeriodNorm = payload[offset++];
            }
        }

        if (lora_data_get.FieldMask & FIELD_MASK_DL_PERIOD_ACT) {
            if (offset + 1 <= bytes_to_parse) {
                lora_data_get.NewPeriodAct = payload[offset++];
            }
        }

        if (lora_data_get.FieldMask & FIELD_MASK_DL_GPS_FIX) {
            if (offset + 1 <= bytes_to_parse) {
                lora_data_get.GpsFixInterval = payload[offset++];
            }
        }

        if (lora_data_get.FieldMask & FIELD_MASK_DL_CONFIG_SEQ) {
            if (offset + 1 <= bytes_to_parse) {
                lora_data_get.ConfigSeq = payload[offset++];
            }
        }

        if (lora_data_get.FieldMask & FIELD_MASK_DL_TIME) {
            if (offset + 4 <= bytes_to_parse) {
                lora_data_get.UnixTime = payload[offset] |
                                        (payload[offset+1] << 8) |
                                        (payload[offset+2] << 16) |
                                        ((uint32_t)payload[offset+3] << 24);
                offset += 4;
            }
        }

        // v28/v4 п.7.1: Message - MsgSeq + Length + Text[Length] (CP1251)
        if (lora_data_get.FieldMask & FIELD_MASK_DL_MESSAGE) {
            if (offset + 2 <= bytes_to_parse) {
                lora_data_get.MsgSeq = payload[offset++];
                lora_data_get.MsgLen = payload[offset++];
                if (lora_data_get.MsgLen > LORA_MSG_TEXT_MAX) {
                    lora_data_get.MsgLen = LORA_MSG_TEXT_MAX;
                }
                if ((uint16_t)(offset + lora_data_get.MsgLen) <= bytes_to_parse) {
                    memcpy(lora_data_get.MsgText, &payload[offset], lora_data_get.MsgLen);
                    offset += lora_data_get.MsgLen;
                } else {
                    lora_data_get.MsgLen = 0;    // текст не помещается в кадр - блок игнорируем
                }
            }
        }

        // 6. Передаём корректно заполненную структуру в обработчик
        Lora_Control_States_Update(&lora_data_get, port_tx_read);
    }
}

#if LORA_DEBUG
void Test_Lora_Parsing(void)
{
    char test_uart_buffer[64] = ">> mac rx 15 4F67\r\n";
    ParseLoraToStruct(test_uart_buffer);
}
#endif

void Lora_Receive(void)
{
    uint16_t len = 0;

    if (buffer_uart_lora.TimeFlagRX && (get_state_init_flag_lora() == 1)) {
        buffer_uart_lora.TimeFlagRX = 0;

        while (check_buffer(&InputBuffer[TYPE_LORA]) &&
               (buffer_uart_lora.count_byter_r < (uint16_t)(sizeof(buffer_uart_lora.receive) - 1u))) {
            receive_buffer(&InputBuffer[TYPE_LORA], &buffer_uart_lora.receive[buffer_uart_lora.count_byter_r]);
            buffer_uart_lora.count_byter_r++;
        }

        len = buffer_uart_lora.count_byter_r;
        buffer_uart_lora.count_byter_r = 0;

        if (len < 2) {
            memset(&buffer_uart_lora.receive[0], 0, len);
            return;
        }

        char* rx_str = (char*)buffer_uart_lora.receive;
        char* has_rx     = strstr(rx_str, RES_RX);
        char* has_tx_ok  = strstr(rx_str, RES_TX_OK);

        if (has_tx_ok != NULL) {
            LORA_SET_FLAG(LORA_FLAG_RECEIVE_TX_OK);
            lora.count_receive_res_tx = 0;

            if (has_rx != NULL) {
                ParseLoraToStruct(rx_str);
            }
            lora.tx_busy_timer = TX_BUSY_RX_WINDOW_MS;
            LORA_CLR_FLAG(LORA_FLAG_CONNECTION_LOST);
            LORA_SET_RX_STATUS(LORA_STATUS_SUCCESS);
        }
        else if (has_rx != NULL) {
            lora.count_receive_res_tx = 0;
            ParseLoraToStruct(rx_str);
            LORA_SET_RX_STATUS(LORA_STATUS_SUCCESS);
        }
        else if (strstr(rx_str, (char*)RES_OK) != NULL) {
        	LORA_SET_RX_STATUS(LORA_STATUS_SUCCESS);
        }
        else if (strstr(rx_str, (char*)RES_MAC_RUN) != NULL) {
            START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, 5);
            LORA_SET_RX_STATUS(LORA_STATUS_SUCCESS);
        }
        else if (strstr(rx_str, (char*)RES_ACCEPTED) != NULL) {
        	lora.tx_busy_timer = TX_BUSY_TIMEOUT_TX_MS;
        	LORA_SET_FLAG(LORA_FLAG_START);
            LORA_SET_FLAG(LORA_FLAG_RECEIVE_TX_OK);
            LORA_CLR_FLAG(LORA_FLAG_JOINING);
            LORA_SET_RX_STATUS(LORA_STATUS_SUCCESS);
        }
        else if (strstr(rx_str, (char*)RES_NOT_JOINED) != NULL) {
        	LORA_CLR_FLAG(LORA_FLAG_START);
//            LORA_CLR_FLAG(LORA_FLAG_INIT);
            LORA_SET_FLAG(LORA_FLAG_JOINING);
            LORA_SET_RX_STATUS(LORA_STATUS_ERROR);
        }
        else if (strstr(rx_str, (char*)RES_ERR) != NULL) {
            LORA_SET_RX_STATUS(LORA_STATUS_ERROR);
        }
        else if (strstr(rx_str, (char*)RES_INV_DATA_LEN) != NULL) {
            LORA_SET_FLAG(LORA_FLAG_RECEIVE_TX_OK);
            LORA_SET_FLAG(LORA_FLAG_REQ_RESET_DR);
            LORA_SET_RX_STATUS(LORA_STATUS_ERROR);
        }
        else {
        	LORA_SET_RX_STATUS(LORA_STATUS_UNKNOWN);
        }

        if (LORA_GET_RX_STATUS() == LORA_STATUS_SUCCESS) {
            lora.counter_err = 0;
        }
        else if (LORA_GET_RX_STATUS() == LORA_STATUS_ERROR) {
            if (lora.counter_err == 0) {
                LORA_SET_FLAG(LORA_FLAG_CONNECTION_LOST);
            }

            if (lora.counter_err < 6) {
                lora.counter_err++;

                if (lora.counter_err == 6) {
                	LORA_SET_FLAG(LORA_FLAG_ERR);
                }
            }
        }

        // RX-окно: держим питание до конца окна по таймеру
        // но после получения downlink (mac rx) сразу уходим в сон — не ждём лишние 10 c.
        // Продления окна на каждый ok/tx_ok/err больше нет.
        if (has_rx != NULL) {
            START_TIMER_RTC(TIMER_RTC_LORA_DATA_SLEEP, TIME_RTC_LORA_DATA_SLEEP_AFTER_DL);
        }
        memset((void*)buffer_uart_lora.receive, 0, len);
    }
}

static uint8_t Lora_TransmitReceive(COMMAND_LORA command, uint8_t *data)
{
    uint16_t count_transmit = 0;

    if (!LORA_TEST_FLAG(LORA_FLAG_ERR)) {
        do {
        	LORA_SET_RX_STATUS(LORA_STATUS_WAITING);
            Lora_Transmit(command, data);
            count_transmit++;
        } while ((Lora_Receive_wait(command) == 1) && (count_transmit < 4));

        if (count_transmit == 4) {
        	LORA_SET_FLAG(LORA_FLAG_ERR);
            return 0;
        }
    }

    return 1;
}

static uint8_t Lora_Receive_wait(COMMAND_LORA command)
{
    uint32_t tickstart_s = GetTick();
    uint32_t timeout_delay = 2000;

    while (((GetTick() - tickstart_s) <= timeout_delay)) {
        if (Lora_flag_receive_read()) {
            timeout_delay = TIME_OUT_LORA + 2;
            tickstart_s = GetTick();
            Lora_flag_receive_clr();
        }

#if (CONFIG_FREERTOS == 0)
        Lora_Receive();
#endif

        if (LORA_GET_RX_STATUS() != LORA_STATUS_WAITING) {
            if (LORA_GET_RX_STATUS() == LORA_STATUS_SUCCESS) {
                return 0;
            }

            if (LORA_GET_RX_STATUS() == LORA_STATUS_UNKNOWN) {
                return 2;
            }

            return 1;
        }
    }

    return 1;
}

#if LORA_DEBUG
//===========================================================================================================================
// БЕЗОПАСНОЕ ДОБАВЛЕНИЕ СТРОКИ В БУФЕР (без va_list, без макросов)
//===========================================================================================================================
static void buf_append(char *buf, uint16_t buf_size, uint16_t *offset, const char *str)
{
    if (str == NULL || *offset >= buf_size) return;

    uint16_t remaining = buf_size - *offset - 1;  // -1 для '\0'
    uint16_t str_len = 0;

    // Считаем длину строки (без выхода за remaining)
    while (str[str_len] != '\0' && str_len < remaining) {
        str_len++;
    }

    // Копируем
    for (uint16_t i = 0; i < str_len; i++) {
        buf[*offset + i] = str[i];
    }
    buf[*offset + str_len] = '\0';
    *offset += str_len;
}

//===========================================================================================================================
// ОТЛАДОЧНЫЙ ВЫВОД СТАТУСА LORA
//===========================================================================================================================
void Lora_DebugPrintStatus(void)
{
    char buf[512];
    const uint16_t buf_size = sizeof(buf);
    uint16_t len = 0;
    char tmp[64];  // Промежуточный буфер для форматирования

    // Заголовок
    buf_append(buf, buf_size, &len, "\r\n==LORA==\r\n");

    // 1. Флаги
    snprintf(tmp, sizeof(tmp), "F:%04X[", lora.flags);
    buf_append(buf, buf_size, &len, tmp);

    if (LORA_TEST_FLAG(LORA_FLAG_START))           buf_append(buf, buf_size, &len, "S");
    if (LORA_TEST_FLAG(LORA_FLAG_INIT))            buf_append(buf, buf_size, &len, "I");
    if (LORA_TEST_FLAG(LORA_FLAG_ERR))             buf_append(buf, buf_size, &len, "E");
    if (LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST)) buf_append(buf, buf_size, &len, "L");
    if (LORA_TEST_FLAG(LORA_FLAG_RECEIVE_TX_OK))   buf_append(buf, buf_size, &len, "T");
    if (LORA_TEST_FLAG(LORA_FLAG_REPEAT))          buf_append(buf, buf_size, &len, "R");
    if (LORA_TEST_FLAG(LORA_FLAG_JOINING))         buf_append(buf, buf_size, &len, "J");

    buf_append(buf, buf_size, &len, "]\r\n");

    // 2. RX статус + ошибки
    snprintf(tmp, sizeof(tmp), "RX:%d ERR:%d/6\r\n",
             LORA_GET_RX_STATUS(), lora.counter_err);
    buf_append(buf, buf_size, &len, tmp);

    // 3. Таймер занятости
    snprintf(tmp, sizeof(tmp), "TX:%dms\r\n", lora.tx_busy_timer);
    buf_append(buf, buf_size, &len, tmp);

    // 4. ControlLora16
    snprintf(tmp, sizeof(tmp), "C:%04X[", lora.ControlLora);
    buf_append(buf, buf_size, &len, tmp);
    if (TESTBIT(lora.ControlLora, ST_LORA_BIT_ACTIV_MOD)) buf_append(buf, buf_size, &len, "A");
    if (TESTBIT(lora.ControlLora, ST_LORA_BIT_ECHO))      buf_append(buf, buf_size, &len, "E");
    buf_append(buf, buf_size, &len, "]\r\n");

    // 5. Data Rate
    snprintf(tmp, sizeof(tmp), "DR:%d max:%d\r\n",
             lora.set_param_dr, Lora_GetMaxPayloadSize(lora.set_param_dr));
    buf_append(buf, buf_size, &len, tmp);

    // 6. EventLog
    snprintf(tmp, sizeof(tmp), "EV:%d/%d/%d\r\n",
             event_log.count, event_log.send_index, event_log.last_sent_count);
    buf_append(buf, buf_size, &len, tmp);

    // 7. GPSHistory
    snprintf(tmp, sizeof(tmp), "GPS:%d/%d/%d\r\n",
             gps_history.count, gps_history.send_index, gps_history.last_sent_count);
    buf_append(buf, buf_size, &len, tmp);

    // 8. Якорь (нужен больший tmp для long)
    snprintf(tmp, sizeof(tmp), "AN:%d t:%lu\r\n",
             event_anchor.is_valid,
             (unsigned long)event_anchor.unix_time);
    buf_append(buf, buf_size, &len, tmp);

    // Разделитель
    buf_append(buf, buf_size, &len, "========\r\n");

    // Отправка через UART
    transmit_buffer((uint8_t*)buf, len, TYPE_USART);
}
#endif

#endif
