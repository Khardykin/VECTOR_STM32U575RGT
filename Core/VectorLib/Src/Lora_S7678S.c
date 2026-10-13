#include "main.h"
#include "Avis_main.h"

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

#define DEFAULT_RX1_DELAY_SEC   (3)
#define DEFAULT_RX1_DR_OFFSET   (0)
#define DEFAULT_TX_POWER        (0)
#define DEFAULT_MAX_DUTY_CYCLE  (0)
#define DEFAULT_TX_RETRY        (0)
#define DEFAULT_CODING_RATE     (3)
#define RX2_SETUP_DELAY_MS      (12000)

static const char* const lora_classes[] = { "A", "C" };
static const char* const lora_coding_rates[] = {"4/5", "4/6", "4/7", "4/8"};

//===========================================================================================================================
// ЯКОРЬ ДЛЯ ИСТОРИИ
//===========================================================================================================================
typedef struct {
    bool     is_valid;
    uint32_t unix_time;
    int32_t  lat;
    int32_t  lon;
} event_log_anchor_t;

static event_log_anchor_t event_anchor = {0};

//===========================================================================================================================
// GAS EVENT LOG
//===========================================================================================================================
#define EVENT_LOG_MAX_RECORDS  64

typedef struct {
    uint8_t  Channel;
    uint8_t  StateErr;
    int16_t  Concentration;
} event_channel_t;

typedef struct {
    uint32_t unix_time;
    int32_t  lat;
    int32_t  lon;
    uint8_t  channel_count;
    event_channel_t channels[COUNT_CHAN_LORA];
} event_log_record_t;

typedef struct {
    uint8_t count;
    uint8_t send_index;
    uint8_t last_sent_count;
    event_log_record_t records[EVENT_LOG_MAX_RECORDS];
} event_log_buffer_t;

static event_log_buffer_t event_log = {0};

//===========================================================================================================================
// GPS HISTORY
//===========================================================================================================================
#define GPS_HISTORY_MAX_POINTS  32

typedef struct {
    int32_t lat;
    int32_t lon;
} gps_history_point_t;

typedef struct {
    uint8_t count;
    uint8_t send_index;
    uint8_t last_sent_count;
    uint32_t first_point_unix_time;
    uint32_t last_point_unix_time;
    gps_history_point_t points[GPS_HISTORY_MAX_POINTS];
} gps_history_buffer_t;

static gps_history_buffer_t gps_history = {0};

//===========================================================================================================================
// CONTAINER EVENT LOG
//===========================================================================================================================
typedef struct {
    uint32_t unix_time;
    int32_t  lat;
    int32_t  lon;
    uint8_t  EventType;
    int16_t  Value;
} container_event_record_t;

typedef struct {
    uint8_t count;
    uint8_t send_index;
    uint8_t last_sent_count;
    container_event_record_t records[LORA_CONTAINER_EVENT_MAX];
} container_event_buffer_t;

static container_event_buffer_t container_event_log = {0};

//===========================================================================================================================
// ФЛАГИ И СОСТОЯНИЕ
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
    LORA_FLAG_RECEIVE           = 10,
    LORA_FLAG_RECEIVE_TX_OK     = 11,
    LORA_FLAG_ERR               = 12,
    LORA_FLAG_JOINING           = 13,
    LORA_RX_STATUS_SHIFT        = 14,
} LORA_FLAG_T;

#define LORA_SET_FLAG(flag)         SETBIT(lora.flags, flag)
#define LORA_CLR_FLAG(flag)         CLRBIT(lora.flags, flag)
#define LORA_TEST_FLAG(flag)        (TESTBIT(lora.flags, flag) ? 1 : 0)

#define LORA_RX_STATUS_MASK         (0x03 << LORA_RX_STATUS_SHIFT)
#define LORA_GET_RX_STATUS()        ((lora.flags & LORA_RX_STATUS_MASK) >> LORA_RX_STATUS_SHIFT)
#define LORA_SET_RX_STATUS(val)     do { lora.flags = (lora.flags & ~LORA_RX_STATUS_MASK) | (((val) & 0x03) << LORA_RX_STATUS_SHIFT); } while(0)

//===========================================================================================================================
// СОСТОЯНИЕ КАНАЛОВ
//===========================================================================================================================
typedef struct {
    int16_t  trigger_concentration;
    uint8_t  trigger_state_err;
    uint8_t  last_in_dead_zone : 1;
    uint8_t  trigger_valid : 1;
    uint8_t  reserved : 6;
} channel_state_t;

static channel_state_t chan_state[COUNT_CHAN_LORA] = {0};

//===========================================================================================================================
// PULSE STATE
//===========================================================================================================================
typedef struct {
    uint8_t  present;
    uint8_t  battery;
    uint8_t  heart_rate;
    uint8_t  spo2;
    uint16_t serial;
} lora_pulse_state_t;

static lora_pulse_state_t pulse_state = {0};

//===========================================================================================================================
// LORA INTERNAL STATE
//===========================================================================================================================
typedef struct {
    volatile uint8_t  count_receive_res_tx;
    volatile uint16_t rx2_delay_ms;
    volatile uint16_t tx_busy_timer;

    volatile uint16_t flags;

    uint8_t  req_change_class;
    uint8_t  counter_err;
    uint8_t  set_param_dr;
    uint8_t  rx2_set_param_dr;
    uint32_t rx2_set_param_freq;

    uint8_t  rx1_delay_sec;
    uint8_t  rx1_dr_offset;
    uint8_t  tx_power;
    uint8_t  max_duty_cycle;
    uint8_t  tx_retry;
    uint8_t  coding_rate;

    uint8_t  empty_buffer;
    uint8_t  count_turnoff_system_tx;
    uint8_t  system_msg;
    uint8_t  power_state;

    uint16_t alarm_pre;
    uint16_t alarm_chan_state;
    uint8_t  alarm_pre_state_all;

    uint16_t ControlLora;       // ControlLora16
    uint8_t  ControlLora2;

    uint8_t  applied_rev;
    uint8_t  config_ack_pending;

    uint8_t  msg_ack_pending;
    uint8_t  msg_seq;

    uint8_t  motion_state;
    uint8_t  motion_event;
    uint8_t  motion_activity;
    uint8_t  motion_valid;

} lora_internal_state_t;

static lora_internal_state_t lora = {0};

//===========================================================================================================================
// СТАТИЧЕСКИЕ БУФЕРЫ И ДАННЫЕ
//===========================================================================================================================
static uint16_t len_lora_data_set_tr;
static uint16_t len_lora_data_set;
static uint16_t len_lora_data_set_1;
static uint16_t len_lora_data_set_2;

static uint8_t port_tx = 10;

static str_lora_data_set_t      lora_data_set;
static str_lora_data_set_2_t    lora_data_set_2;
static str_lora_data_set_ack_t  lora_data_set_ack;
static str_lora_data_get_t      lora_data_get;

static uint8_t lora_static_payload[256];

static uint8_t lora_message_text[LORA_MESSAGE_MAX_LEN];
static uint8_t lora_message_len = 0;
static uint8_t lora_message_seq = 0;
static uint8_t lora_message_new = 0;
static uint8_t lora_message_received_once = 0;

static environment_block_t lora_env = {0};
static uint8_t lora_env_valid = 0;

static uint8_t ppe_enabled_mask = 0;
static ppe_descr_t ppe_desc[LORA_MAX_PPE] = {0};
static uint8_t ppe_presence_mask = 0;
static uint8_t ppe_charge[LORA_MAX_PPE] = {0};
static uint8_t ppe_state_valid = 0;

static ble_beacon_t ble_beacons[LORA_MAX_BLE_BEACONS];
static uint8_t ble_beacon_count = 0;

static uint8_t lora_time_valid = 1;

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

//===========================================================================================================================
static uint16_t Lora_GetMaxPayloadSize(uint8_t dr)
{
    if (dr <= 2) {
        return 51;
    }
    else if (dr == 3) {
        return 115;
    }
    else {
        return 242;
    }
}

//===========================================================================================================================
static uint8_t Lora_PopCount8(uint8_t x)
{
    uint8_t count = 0;

    while (x != 0) {
        count++;
        x &= (uint8_t)(x - 1);
    }

    return count;
}

//===========================================================================================================================
// КОНФИГУРАЦИЯ
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
static void Lora_BuildSensorMap(SNS_CFG *Cfg_struct)
{
    uint8_t payload_idx = 0;

    for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
        if (Cfg_struct->Sensor[chan].channel == chan) {
            lora_sensor_map[chan] = payload_idx;
            payload_idx++;
        }
    }
}

//===========================================================================================================================
static int32_t Lora_CalcDeltaMeters(int32_t target, int32_t reference, int32_t ref_lat_for_cos)
{
    float lat_rad = (float)ref_lat_for_cos * 0.0000001f * 0.01745329252f;
    float delta_m = (float)(target - reference) * 0.01113195f * cosf(lat_rad);
    return (int32_t)lround(delta_m);
}

//===========================================================================================================================
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
    if (!LORA_TEST_FLAG(LORA_FLAG_START)) {
        return;
    }

    gps_last_valid_lat = cur_lat;
    gps_last_valid_lon = cur_lon;

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

    if (!gps_hist_has_prev) {
        gps_hist_prev_lat = cur_lat;
        gps_hist_prev_lon = cur_lon;
        gps_hist_has_prev = true;
        gps_hist_count = 0;
        return;
    }

    int32_t dx_m = Lora_CalcDeltaMeters(cur_lat, gps_hist_prev_lat, 0);
    int32_t dy_m = Lora_CalcDeltaMeters(cur_lon, gps_hist_prev_lon, gps_hist_prev_lat);

    while ((dx_m > 127 || dx_m < -128 || dy_m > 127 || dy_m < -128) &&
           (gps_hist_count < MAX_GPS_HISTORY_POINTS)) {
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
static void Lora_CompleteDataSet_1(SNS_CFG *Cfg_struct);
static void Lora_CompleteDataSet_2(SNS_CFG *Cfg_struct, str_lora_data_set_2_t *data_set, TYPE_MSG_LORA type_msg);
static void Lora_CompleteDataSetAck(str_lora_data_set_ack_t *data_set);
static uint8_t Lora_Transmit(COMMAND_LORA command, uint8_t *data);
static uint8_t Lora_TransmitReceive(COMMAND_LORA command, uint8_t *data);
static uint8_t Lora_Receive_wait(COMMAND_LORA command);
static inline uint8_t HexToByte(char high, char low);
static uint8_t Lora_IsHexChar(char c);
static uint16_t Lora_HexStringToBytes(const char *hex_str, uint8_t *out, uint16_t out_size);
static void Lora_HandleMessage(uint8_t seq, uint8_t len, const uint8_t *text);
static void ParseLoraToStruct(const char* uart_buffer);
static void Lora_ParseMacCommand(const char *cmd_str);

//===========================================================================================================================
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

    if (lora.tx_busy_timer > 0) {
        lora.tx_busy_timer--;
    }
}

//===========================================================================================================================
void Lora_ack_system_set(void)
{
    button_block_sound_lora = 0;
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
// PUBLIC API: CONTROL / TIME / MESSAGE / MOTION / ENV / PPE / BLE / EVENTS / PULSE
//===========================================================================================================================
uint8_t Lora_IsTxBusy(void)
{
    return (lora.tx_busy_timer > 0) ? 1 : 0;
}

uint16_t Lora_GetControlLora(void)
{
    return lora.ControlLora;
}

void Lora_SetControlLoraBit(uint8_t bit_num, uint8_t state)
{
    if (bit_num > 15) {
        return;
    }

    if (state) {
        SETBIT(lora.ControlLora, bit_num);
    }
    else {
        CLRBIT(lora.ControlLora, bit_num);
    }
}

void Lora_SetTimeValid(uint8_t valid)
{
    lora_time_valid = valid ? 1 : 0;
}

uint8_t Lora_IsTimeValid(void)
{
    return lora_time_valid;
}

uint8_t Lora_MessageIsNew(void)
{
    return lora_message_new;
}

void Lora_MessageDisplayed(void)
{
    lora_message_new = 0;
}

uint8_t Lora_GetMessage(uint8_t **text, uint8_t *len, uint8_t *seq)
{
    if ((text == NULL) || (len == NULL) || (seq == NULL)) {
        return 0;
    }

    if (!lora_message_received_once) {
        return 0;
    }

    *text = lora_message_text;
    *len  = lora_message_len;
    *seq  = lora_message_seq;

    return 1;
}

void Lora_SetMotionState(uint8_t posture, uint8_t inactive, uint8_t activity)
{
    lora.motion_state = (posture & MOTION_POSTURE_MASK);

    if (inactive) {
        lora.motion_state |= MOTION_BIT_INACTIVE;
    }

    lora.motion_activity = activity;
    lora.motion_valid = 1;
}

void Lora_SetMotionEvent(uint8_t event_bits)
{
    uint8_t ev = event_bits & (MOTION_BIT_FALL | MOTION_BIT_IMPACT);

    if (ev == 0) {
        return;
    }

    lora.motion_event |= ev;

    // Если связь потеряна, дублируем fall/impact в контейнерный EventLog.
    if (LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST)) {
        if (ev & MOTION_BIT_FALL) {
            Lora_AddContainerEvent(CONTAINER_EVENT_FALL, (int16_t)0x8000);
        }

        if (ev & MOTION_BIT_IMPACT) {
            Lora_AddContainerEvent(CONTAINER_EVENT_IMPACT, (int16_t)0x8000);
        }
    }

    if (LORA_TEST_FLAG(LORA_FLAG_START)) {
        set_state_repeat_flag_lora();
        START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, 5);
    }
}

void Lora_ClearMotionEvent(uint8_t event_bits)
{
    lora.motion_event &= (uint8_t)~event_bits;
}

void Lora_SetEnvironment(uint16_t aqi,
                         int16_t temperature_x10,
                         uint8_t humidity,
                         uint16_t pressure_hpa_x10)
{
    lora_env.AirQualityIndex = aqi;
    lora_env.Temperature     = temperature_x10;
    lora_env.Humidity        = humidity;
    lora_env.Pressure        = pressure_hpa_x10;

    lora_env_valid = 1;
}

void Lora_ClearEnvironment(void)
{
    lora_env_valid = 0;
}

void Lora_SetPPEEnabledMask(uint8_t mask)
{
    ppe_enabled_mask = mask;
}

void Lora_SetPPEDescriptor(uint8_t bit_index, uint8_t type, uint16_t serial)
{
    if (bit_index >= LORA_MAX_PPE) {
        return;
    }

    ppe_desc[bit_index].Type   = type;
    ppe_desc[bit_index].Serial = serial;
}

void Lora_SetPPEState(uint8_t presence_mask, const uint8_t *charge_by_bit)
{
    uint8_t i;

    ppe_presence_mask = presence_mask & ppe_enabled_mask;

    for (i = 0; i < LORA_MAX_PPE; i++) {
        if (charge_by_bit != NULL) {
            ppe_charge[i] = charge_by_bit[i];
        }
        else {
            ppe_charge[i] = PPE_CHARGE_OK;
        }
    }

    ppe_state_valid = 1;
}

void Lora_ClearPPEState(void)
{
    ppe_presence_mask = 0;
    ppe_state_valid = 0;
}

void Lora_ClearBleBeacons(void)
{
    ble_beacon_count = 0;
}

uint8_t Lora_AddBleBeacon(uint16_t short_id, int8_t rssi, uint8_t battery_state)
{
    if (ble_beacon_count >= LORA_MAX_BLE_BEACONS) {
        return 0;
    }

    ble_beacons[ble_beacon_count].ShortID = short_id;
    ble_beacons[ble_beacon_count].Rssi    = rssi;
    ble_beacons[ble_beacon_count].Battery = battery_state;

    ble_beacon_count++;

    return 1;
}

void Lora_AddContainerEvent(uint8_t event_type, int16_t value)
{
    container_event_record_t *rec;

    if (container_event_log.count >= LORA_CONTAINER_EVENT_MAX) {
        return;
    }

    rec = &container_event_log.records[container_event_log.count];

    rec->unix_time = (uint32_t)Sns_Cfg_struct.Config_common.working_hours;

#if CONFIG_GPS
    rec->lat = gps_last_valid_lat;
    rec->lon = gps_last_valid_lon;
#else
    rec->lat = 0;
    rec->lon = 0;
#endif

    rec->EventType = event_type;
    rec->Value     = value;

    container_event_log.count++;
}

void Lora_SetPulsePresent(uint8_t present)
{
    pulse_state.present = present ? 1 : 0;
}

void Lora_SetPulseSerial(uint16_t serial)
{
    pulse_state.serial = serial;
}

void Lora_SetPulseData(uint8_t battery_percent, uint8_t heart_rate, uint8_t spo2_percent)
{
    pulse_state.battery    = battery_percent;
    pulse_state.heart_rate = heart_rate;
    pulse_state.spo2       = spo2_percent;
}

//===========================================================================================================================
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

//===========================================================================================================================
static int16_t Lora_ScaleConcentration(SNS_CFG *Cfg_struct, uint8_t chan)
{
    float raw_val = Cfg_struct->Sensor[chan].ConcentrationVal *
                    Cfg_struct->Sensor[chan].Discreteness *
                    Get_Conversion_factor_rf(chan);

    return (int16_t)lround(raw_val);
}

//===========================================================================================================================
uint8_t test_state_alarm_flag_lora(SNS_CFG *Cfg_struct)
{
    if (!LORA_TEST_FLAG(LORA_FLAG_START)) {
        return 0;
    }

    if (TESTBIT(lora.ControlLora2, ST_LORA2_REQ_STATIC)) {
        return 0;
    }

    uint8_t state_changed = 0;

    for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
        if (Cfg_struct->Sensor[chan].channel != chan) continue;

        int16_t current_conc = Lora_ScaleConcentration(Cfg_struct, chan);
        uint8_t current_state = Cfg_struct->Sensor[chan].StateErr & 0x7F;

        bool current_in_dead_zone = false;

        if (current_state == 0) {
            if (TESTBIT(Cfg_struct->Sensor[chan].DeviceSetting, BIT_SET_OXYGEN_SENSOR)) {
                int16_t low = 20.85 * Cfg_struct->Sensor[chan].Discreteness;
                int16_t high = 20.95 * Cfg_struct->Sensor[chan].Discreteness;
                current_in_dead_zone = (current_conc >= low && current_conc <= high);
            } else {
                current_in_dead_zone = (current_conc == 0);
            }
        }

        bool state_err_changed = (current_state != chan_state[chan].trigger_state_err);
        bool dead_zone_changed = (current_in_dead_zone != chan_state[chan].last_in_dead_zone);

        if (state_err_changed || dead_zone_changed) {
            state_changed = 1;

            chan_state[chan].last_in_dead_zone = current_in_dead_zone;

            chan_state[chan].trigger_concentration = current_conc;
            chan_state[chan].trigger_state_err = current_state;
            chan_state[chan].trigger_valid = 1;

            if (state_err_changed &&
                LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST) &&
                (event_log.count < EVENT_LOG_MAX_RECORDS)) {

                event_log_record_t *new_event = &event_log.records[event_log.count];

                new_event->unix_time = (uint32_t)Cfg_struct->Config_common.working_hours;

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

        if (!current_in_dead_zone) {
            SETBIT(lora.alarm_chan_state, chan);
        } else {
            CLRBIT(lora.alarm_chan_state, chan);
        }
    }

    uint8_t current_sos = TESTBIT(lora.ControlLora, ST_LORA_BIT_SOS);

    if (current_sos != lora.alarm_pre_state_all) {
        state_changed = 1;
        lora.alarm_pre_state_all = current_sos;
    }

    if (state_changed) set_state_repeat_flag_lora();

    return state_changed;
}

//===========================================================================================================================
void Lora_Cfg_ApplyDefaults(SNS_CFG *Cfg_struct)
{
    uint8_t is_class_c = TESTBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_CLASS_C);
    uint8_t is_ru = TESTBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_REGION_RU);
    uint8_t dr = DEFAULT_LORA_DR;
    uint8_t gps_fix = LORA_CFG_GET_GPSFIX(Cfg_struct->Config_common.Lora_Config_Flags);

    Cfg_struct->Config_common.Lora_Config_Flags = Lora_Cfg_BuildFlags(0, is_class_c, is_ru, dr, gps_fix);

    Cfg_struct->Config_common.Lora_freq_rx2 = RX2_DEFAULT_LORA_FR;
    Cfg_struct->Config_common.Lora_dr_rx2 = RX2_DEFAULT_LORA_DR;

    lora.rx2_set_param_dr = Cfg_struct->Config_common.Lora_dr_rx2;
    lora.rx2_set_param_freq = Cfg_struct->Config_common.Lora_freq_rx2;

    CLRBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_SAVED);
}

//===========================================================================================================================
static void Lora_RadioCfg_InitDefaults(SNS_CFG *Cfg_struct)
{
    lora.rx1_delay_sec  = DEFAULT_RX1_DELAY_SEC;
    lora.rx1_dr_offset  = DEFAULT_RX1_DR_OFFSET;
    lora.tx_power       = DEFAULT_TX_POWER;
    lora.max_duty_cycle = DEFAULT_MAX_DUTY_CYCLE;
    lora.tx_retry       = DEFAULT_TX_RETRY;
    lora.coding_rate    = DEFAULT_CODING_RATE;

    lora.rx2_delay_ms = RX2_SETUP_DELAY_MS;

    lora.rx2_set_param_dr = Cfg_struct->Config_common.Lora_dr_rx2;

    if (Cfg_struct->Config_common.Lora_freq_rx2 == 0) {
        Cfg_struct->Config_common.Lora_freq_rx2 = RX2_DEFAULT_LORA_FR;
    }

    lora.rx2_set_param_freq = Cfg_struct->Config_common.Lora_freq_rx2;
}

//===========================================================================================================================
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
        Lora_BuildSensorMap(Cfg_struct);
        Lora_InitDeveui(Cfg_struct);
        Lora_RadioCfg_InitDefaults(Cfg_struct);

        if (!TESTBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_SAVED)) {
            Lora_TransmitReceive(CMD_SET_DEVEUI, deveui);
            Lora_TransmitReceive(CMD_SET_APPEUI, appeui);
            Lora_TransmitReceive(CMD_SET_APPKEY, appkey);

            Lora_TransmitReceive(CMD_GET_DR, (uint8_t*)&lora.empty_buffer);

            Lora_TransmitReceive(CMD_SET_RXDELAY, &lora.rx1_delay_sec);

            lora.empty_buffer = LORA_FLAG_ON;
            Lora_TransmitReceive(CMD_LBT_ON, (uint8_t*)&lora.empty_buffer);

            lora.empty_buffer = LORA_FLAG_ON;
            Lora_TransmitReceive(CMD_SET_ADR, (uint8_t*)&lora.empty_buffer);

            Lora_TransmitReceive(CMD_SET_DR, (uint8_t*)&lora.set_param_dr);

            lora.empty_buffer = LORA_MODE_NO_CYCLE;
            Lora_TransmitReceive(CMD_SET_TX_MODE, (uint8_t*)&lora.empty_buffer);

            Lora_TransmitReceive(CMD_SET_TXRETRY, &lora.tx_retry);
            Lora_TransmitReceive(CMD_SET_CR, &lora.coding_rate);

            uint8_t default_class = DEFAULT_LORA_CLASS;

            if (TESTBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_CLASS_C)) {
                default_class = LORA_CLASS_C;
            }

            Lora_TransmitReceive(CMD_SET_CLASS, &default_class);

            lora.empty_buffer = LORA_FLAG_ON;
            Lora_TransmitReceive(CMD_SET_RT_STORE, (uint8_t*)&lora.empty_buffer);

            Lora_Switch_Region_On_The_Fly(Cfg_struct, 1);

            LORA_SET_FLAG(LORA_FLAG_RX2_SETUP_PENDING);
        }
        else {
#if(LORA_FORCE_JOIN_ON_STARTUP)
            LORA_CLR_FLAG(LORA_FLAG_RECEIVE_TX_OK);
            lora.count_receive_res_tx = 0;
            LORA_CLR_FLAG(LORA_FLAG_START);

            Lora_Transmit(CMD_JOIN_OTAA, (uint8_t*)&lora.empty_buffer);
#else
            LORA_CLR_FLAG(LORA_FLAG_RECEIVE_TX_OK);
            lora.count_receive_res_tx = 0;
            LORA_SET_FLAG(LORA_FLAG_START);
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

//===========================================================================================================================
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

//===========================================================================================================================
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

//===========================================================================================================================
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

//===========================================================================================================================
static void Lora_HandleMessage(uint8_t seq, uint8_t len, const uint8_t *text)
{
    if (text == NULL) {
        return;
    }

    if ((!lora_message_received_once) || (seq != lora_message_seq)) {
        uint8_t copy_len = len;

        if (copy_len > LORA_MESSAGE_MAX_LEN) {
            copy_len = LORA_MESSAGE_MAX_LEN;
        }

        memcpy(lora_message_text, text, copy_len);

        lora_message_len = copy_len;
        lora_message_seq = seq;
        lora_message_new = 1;
        lora_message_received_once = 1;
    }

    lora.msg_ack_pending = 1;

    if (LORA_TEST_FLAG(LORA_FLAG_START)) {
        START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, 5);
    }
}

//===========================================================================================================================
static void Lora_Control_States_Update(str_lora_data_get_t *rx_data, uint32_t port_tx_read)
{
    // Control (16 бит)
    if (rx_data->FieldMask & FIELD_MASK_DL_CONTROL) {
        // Запрещаем серверу писать в SOS (бит 5) и CURRENT_C (бит 7)
        uint16_t allowed_mask = rx_data->ControlLoraMask & (uint16_t)~0x00A0u;

        lora.ControlLora =
            (lora.ControlLora & (uint16_t)~allowed_mask) |
            (rx_data->ControlLoraValue & allowed_mask);
    }

    // Mode / ControlLora2
    if (rx_data->FieldMask & FIELD_MASK_DL_MODE) {
        lora.ControlLora2 = (lora.ControlLora2 & ~rx_data->ControlLora2Mask) |
                            (rx_data->ControlLora2Value & rx_data->ControlLora2Mask);
    }

    // PeriodNormal
    if ((rx_data->FieldMask & FIELD_MASK_DL_PERIOD_NORM) && (rx_data->NewPeriodNorm != 0)) {
        Sns_Cfg_struct.Config_common.PeriodTimeLora &= 0xFF00;
        Sns_Cfg_struct.Config_common.PeriodTimeLora |= rx_data->NewPeriodNorm;
    }

    // PeriodActive
    if ((rx_data->FieldMask & FIELD_MASK_DL_PERIOD_ACT) && (rx_data->NewPeriodAct != 0)) {
        Sns_Cfg_struct.Config_common.PeriodTimeLora &= 0x00FF;
        Sns_Cfg_struct.Config_common.PeriodTimeLora |= ((uint16_t)rx_data->NewPeriodAct << 8);
    }

    // GPSFixInterval
    if ((rx_data->FieldMask & FIELD_MASK_DL_GPS_FIX) && (rx_data->GpsFixInterval != 0)) {
        Lora_Cfg_UpdateGPSFixInterval(rx_data->GpsFixInterval);
    }

    // ConfigSeq
    if (rx_data->FieldMask & FIELD_MASK_DL_CONFIG_SEQ) {
        lora.applied_rev = rx_data->ConfigSeq;
        lora.config_ack_pending = 1;
    }

    // Time
    if (rx_data->FieldMask & FIELD_MASK_DL_TIME) {
        Sns_Cfg_struct.Config_common.working_hours = rx_data->UnixTime;
        lora_time_valid = 1;
    }

    // ECHO
    if (TESTBIT(lora.ControlLora, ST_LORA_BIT_ECHO)) {
        clr_state_repeat_flag_lora();

        for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
            chan_state[chan].trigger_valid = 0;
        }

        lora.alarm_chan_state = 0;

        // Сбрасываем одноразовые события движения
        lora.motion_event = 0;

        if (event_log.last_sent_count > 0) {
            event_log.send_index += event_log.last_sent_count;
            event_log.last_sent_count = 0;
        }

        if (gps_history.last_sent_count > 0) {
            gps_history.send_index += gps_history.last_sent_count;
            gps_history.last_sent_count = 0;
        }

        if (container_event_log.last_sent_count > 0) {
            container_event_log.send_index += container_event_log.last_sent_count;
            container_event_log.last_sent_count = 0;
        }

        bool event_log_empty = (event_log.send_index >= event_log.count);
        bool gps_history_empty = (gps_history.send_index >= gps_history.count);
        bool container_event_empty = (container_event_log.send_index >= container_event_log.count);

        if (event_log_empty && gps_history_empty && container_event_empty) {
            event_log.count = 0;
            event_log.send_index = 0;

            gps_history.count = 0;
            gps_history.send_index = 0;

            container_event_log.count = 0;
            container_event_log.send_index = 0;

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

    // ACK при изменении значимых битов, включая EVACUATION
    if ((lora.ControlLora & 0x0137u) != lora.alarm_pre) {
        Lora_ack_system_set();
    }

    lora.alarm_pre = lora.ControlLora & 0x0137u;

    port_tx = port_tx_read;

    if (lora.count_turnoff_system_tx != 0) {
        Lora_system_msg_clr();
    }
}

//===========================================================================================================================
// TYPE_DYNAMIC, MIRAX v4
//===========================================================================================================================
static void Lora_CompleteDataSet(SNS_CFG *Cfg_struct, str_lora_data_set_t *data_set)
{
    uint8_t *buf = (uint8_t *)data_set;

    uint16_t container_mask = 0;
    uint16_t offset = 3;
    uint16_t max_payload = Lora_GetMaxPayloadSize(lora.set_param_dr);

    if (max_payload > sizeof(str_lora_data_set_t)) {
        max_payload = sizeof(str_lora_data_set_t);
    }

    buf[0] = (TYPE_MSG_DYNAMIC & 0xC0) | DATA_STRUCT_LORA_VER;

    uint8_t battery = Cfg_struct->Config_common.battery_charge_percent;

    bool has_gas_event = (event_log.count > event_log.send_index);
    bool has_gps_history = (gps_history.count > gps_history.send_index);
    bool has_container_event = (container_event_log.count > container_event_log.send_index);

    bool has_any_history_raw = has_gas_event || has_gps_history || has_container_event;
    bool has_sendable_history = has_any_history_raw && lora_time_valid;

    int32_t anchor_lat = 0;
    int32_t anchor_lon = 0;
    uint32_t anchor_time = 0;
    bool has_anchor_gps = false;

    if (has_sendable_history) {
        anchor_time = event_anchor.unix_time;
        anchor_lat  = event_anchor.lat;
        anchor_lon  = event_anchor.lon;

        if ((anchor_lat != 0) || (anchor_lon != 0)) {
            has_anchor_gps = true;
        }
    }
    else {
        anchor_time = (uint32_t)Cfg_struct->Config_common.working_hours;

#if CONFIG_GPS
        anchor_lat = gps_last_valid_lat;
        anchor_lon = gps_last_valid_lon;

        if ((anchor_lat != 0) || (anchor_lon != 0)) {
            has_anchor_gps = true;
        }
#endif
    }

    uint8_t active_sensors = 0;

    for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
        if (TESTBIT(lora.alarm_chan_state, chan)) {
            active_sensors++;
        }
    }

    bool gas_sub_possible = false;

#if LORA_INCLUDE_GAS_SUB_BATTERY
    gas_sub_possible = true;
#else
    gas_sub_possible = (active_sensors > 0) || has_gas_event;
#endif

#if LORA_INCLUDE_PULSE_SUBDEVICE
    bool pulse_sub_possible = (pulse_state.present != 0);
#else
    bool pulse_sub_possible = false;
#endif

    uint16_t tail_reserve = 1; // SubCount

    if (gas_sub_possible) {
        tail_reserve += 2; // SubID + SubMask

#if LORA_INCLUDE_GAS_SUB_BATTERY
        tail_reserve += 1;
#endif

        if (active_sensors > 0) {
            tail_reserve += (1 + (4 * active_sensors));
        }

        if (has_gas_event) {
            tail_reserve += 1;
        }
    }

    if (pulse_sub_possible) {
        tail_reserve += 2 + 3; // SubID + SubMask + battery + HR + SpO2
    }

    uint16_t end_reserve = tail_reserve + 1; // ConfigAck

    if (lora.msg_ack_pending) {
        end_reserve += 1;
    }

    // BIT 0: Battery
    if ((offset + 1 + end_reserve) <= max_payload) {
        container_mask |= CONTAINER_MASK_BATTERY;
        buf[offset++] = battery;
    }

    // BIT 1: GPS
    bool gps_added = false;

    if (has_anchor_gps && ((offset + 8 + end_reserve) <= max_payload)) {
        container_mask |= CONTAINER_MASK_GPS;

        memcpy(&buf[offset], &anchor_lat, 4);
        offset += 4;

        memcpy(&buf[offset], &anchor_lon, 4);
        offset += 4;

        gps_added = true;
    }

    // BIT 2: GPSTrack
    if (!has_sendable_history && gps_added) {
#if CONFIG_GPS
        if (gps_hist_has_prev && (gps_hist_count > 0)) {
            uint16_t hist_size = 1 + (2 * gps_hist_count);

            if ((offset + hist_size + end_reserve) <= max_payload) {
                container_mask |= CONTAINER_MASK_GPS_TRACK;

                buf[offset++] = gps_hist_count;

                for (uint8_t i = 0; i < gps_hist_count; i++) {
                    buf[offset++] = gps_hist_dx[i];
                    buf[offset++] = gps_hist_dy[i];
                }

                gps_hist_has_prev = false;
                gps_hist_count = 0;
            }
        }
#endif
    }

    // BIT 3: TimeRef
    bool time_ref_added = false;

    if (has_sendable_history && ((offset + 4 + end_reserve) <= max_payload)) {
        container_mask |= CONTAINER_MASK_TIME_REF;

        memcpy(&buf[offset], &anchor_time, 4);
        offset += 4;

        time_ref_added = true;
    }

    // BIT 4: DeviceStatus, ControlLora16
    if ((offset + 2 + end_reserve) <= max_payload) {
        container_mask |= CONTAINER_MASK_DEV_STATUS;

        buf[offset++] = (uint8_t)(lora.ControlLora & 0x00FF);
        buf[offset++] = (uint8_t)((lora.ControlLora >> 8) & 0x00FF);
    }

    // BIT 5: Environment
    if (lora_env_valid && ((offset + 7 + end_reserve) <= max_payload)) {
        container_mask |= CONTAINER_MASK_ENVIRONMENT;

        buf[offset++] = (uint8_t)(lora_env.AirQualityIndex & 0x00FF);
        buf[offset++] = (uint8_t)((lora_env.AirQualityIndex >> 8) & 0x00FF);

        buf[offset++] = (uint8_t)(lora_env.Temperature & 0x00FF);
        buf[offset++] = (uint8_t)((lora_env.Temperature >> 8) & 0x00FF);

        buf[offset++] = lora_env.Humidity;

        buf[offset++] = (uint8_t)(lora_env.Pressure & 0x00FF);
        buf[offset++] = (uint8_t)((lora_env.Pressure >> 8) & 0x00FF);
    }

    // BIT 6: Motion
    if ((lora.motion_valid || (lora.motion_event != 0)) &&
        ((offset + 2 + end_reserve) <= max_payload)) {

        uint8_t status_bits = (lora.motion_state & (MOTION_POSTURE_MASK | MOTION_BIT_INACTIVE)) |
                              (lora.motion_event & (MOTION_BIT_FALL | MOTION_BIT_IMPACT));

        container_mask |= CONTAINER_MASK_MOTION;

        buf[offset++] = status_bits;
        buf[offset++] = lora.motion_activity;
    }

    // BIT 7: PPE
    if (ppe_state_valid) {
        uint8_t presence = ppe_presence_mask & ppe_enabled_mask;
        uint8_t charge_count = Lora_PopCount8(presence);
        uint16_t ppe_size = 1 + charge_count;

        if ((offset + ppe_size + end_reserve) <= max_payload) {
            container_mask |= CONTAINER_MASK_PPE;

            buf[offset++] = presence;

            for (uint8_t i = 0; i < LORA_MAX_PPE; i++) {
                if (presence & (1u << i)) {
                    buf[offset++] = ppe_charge[i];
                }
            }
        }
    }

    // BIT 8: BLEBeacons
    if (ble_beacon_count > 0) {
        uint16_t ble_size = 1 + (4 * ble_beacon_count);

        if ((offset + ble_size + end_reserve) <= max_payload) {
            container_mask |= CONTAINER_MASK_BLE_BEACONS;

            buf[offset++] = ble_beacon_count;

            for (uint8_t i = 0; i < ble_beacon_count; i++) {
                buf[offset++] = (uint8_t)(ble_beacons[i].ShortID & 0x00FF);
                buf[offset++] = (uint8_t)((ble_beacons[i].ShortID >> 8) & 0x00FF);

                buf[offset++] = (uint8_t)ble_beacons[i].Rssi;
                buf[offset++] = ble_beacons[i].Battery;
            }
        }
    }

    // BIT 9: Container EventLog
    container_event_log.last_sent_count = 0;

    if (has_container_event && time_ref_added) {
        uint16_t temp_offset = offset + 1;
        uint8_t packed_events_count = 0;

        for (uint8_t i = container_event_log.send_index; i < container_event_log.count; i++) {
            if ((temp_offset + 9 + end_reserve) > max_payload) {
                break;
            }

            temp_offset += 9;
            packed_events_count++;
        }

        if (packed_events_count > 0) {
            container_mask |= CONTAINER_MASK_EVENT_LOG;

            buf[offset++] = packed_events_count;

            for (uint8_t i = 0; i < packed_events_count; i++) {
                uint8_t idx = container_event_log.send_index + i;
                container_event_record_t *rec = &container_event_log.records[idx];

                uint32_t dt_sec = 0;

                if (rec->unix_time >= anchor_time) {
                    dt_sec = rec->unix_time - anchor_time;
                }

                if (dt_sec > 0xFFFF) {
                    dt_sec = 0xFFFF;
                }

                buf[offset++] = (uint8_t)(dt_sec & 0x00FF);
                buf[offset++] = (uint8_t)((dt_sec >> 8) & 0x00FF);

                if (gps_added && (rec->lat != 0)) {
                    int16_t dx = Lora_CalcDeltaMeters(rec->lat, anchor_lat, 0);
                    int16_t dy = Lora_CalcDeltaMeters(rec->lon, anchor_lon, anchor_lat);

                    buf[offset++] = (uint8_t)(dx & 0x00FF);
                    buf[offset++] = (uint8_t)((dx >> 8) & 0x00FF);

                    buf[offset++] = (uint8_t)(dy & 0x00FF);
                    buf[offset++] = (uint8_t)((dy >> 8) & 0x00FF);
                }
                else {
                    buf[offset++] = 0x80;
                    buf[offset++] = 0x00;

                    buf[offset++] = 0x80;
                    buf[offset++] = 0x00;
                }

                buf[offset++] = rec->EventType;
                buf[offset++] = rec->Severity;

                buf[offset++] = (uint8_t)(rec->Value & 0x00FF);
                buf[offset++] = (uint8_t)((rec->Value >> 8) & 0x00FF);
            }

            container_event_log.last_sent_count = packed_events_count;
        }
    }

    // BIT 10: GPSHistory
    gps_history.last_sent_count = 0;

    if (has_gps_history && gps_added && time_ref_added) {
        uint8_t gps_hist_to_send = gps_history.count - gps_history.send_index;

        if (gps_hist_to_send > 0) {
            int32_t max_abs_dx = 0;
            int32_t max_abs_dy = 0;

            for (uint8_t i = gps_history.send_index; i < gps_history.count; i++) {
                gps_history_point_t *pt = &gps_history.points[i];

                int32_t dx_m = Lora_CalcDeltaMeters(pt->lat, anchor_lat, 0);
                int32_t dy_m = Lora_CalcDeltaMeters(pt->lon, anchor_lon, anchor_lat);

                int32_t abs_dx = (dx_m < 0) ? -dx_m : dx_m;
                int32_t abs_dy = (dy_m < 0) ? -dy_m : dy_m;

                if (abs_dx > max_abs_dx) max_abs_dx = abs_dx;
                if (abs_dy > max_abs_dy) max_abs_dy = abs_dy;
            }

            uint8_t scale = 1;
            int32_t max_dim = (max_abs_dx > max_abs_dy) ? max_abs_dx : max_abs_dy;

            if (max_dim > 127) {
                scale = (uint8_t)((max_dim + 126) / 127);

                if (scale > 255) {
                    scale = 255;
                }
            }

            uint32_t dt0 = 0;

            if (gps_history.first_point_unix_time >= anchor_time) {
                dt0 = gps_history.first_point_unix_time - anchor_time;
            }

            if (dt0 > 0xFFFF) {
                dt0 = 0xFFFF;
            }

            uint16_t step = 60;

            if (gps_hist_to_send > 1) {
                uint32_t total_span = 0;

                if (gps_history.last_point_unix_time >= gps_history.first_point_unix_time) {
                    total_span = gps_history.last_point_unix_time - gps_history.first_point_unix_time;
                }

                step = (uint16_t)(total_span / (gps_hist_to_send - 1));

                if (step == 0) {
                    step = 1;
                }

                if (step > 0xFFFF) {
                    step = 0xFFFF;
                }
            }

            uint16_t block_size = 6 + (2 * gps_hist_to_send);

            if ((offset + block_size + end_reserve) <= max_payload) {
                container_mask |= CONTAINER_MASK_GPS_HISTORY;

                buf[offset++] = gps_hist_to_send;

                buf[offset++] = (uint8_t)(dt0 & 0x00FF);
                buf[offset++] = (uint8_t)((dt0 >> 8) & 0x00FF);

                buf[offset++] = (uint8_t)(step & 0x00FF);
                buf[offset++] = (uint8_t)((step >> 8) & 0x00FF);

                buf[offset++] = scale;

                for (uint8_t i = 0; i < gps_hist_to_send; i++) {
                    uint8_t idx = gps_history.send_index + i;
                    gps_history_point_t *pt = &gps_history.points[idx];

                    if ((pt->lat != 0) || (pt->lon != 0)) {
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
                    }
                    else {
                        buf[offset++] = 0x80;
                        buf[offset++] = 0x80;
                    }
                }

                gps_history.last_sent_count = gps_hist_to_send;
            }
        }
    }

    // BIT 11: ConfigAck
    if ((offset + 1 + tail_reserve) <= max_payload) {
        container_mask |= CONTAINER_MASK_CONFIG_ACK;
        buf[offset++] = lora.applied_rev;
        lora.config_ack_pending = 0;
    }

    // BIT 12: MsgAck
    if (lora.msg_ack_pending && ((offset + 1 + tail_reserve) <= max_payload)) {
        container_mask |= CONTAINER_MASK_MSG_ACK;
        buf[offset++] = lora.msg_seq;
        lora.msg_ack_pending = 0;
    }

    // SubCount
    if ((offset + 1) > max_payload) {
        buf[1] = (uint8_t)(container_mask & 0x00FF);
        buf[2] = (uint8_t)((container_mask >> 8) & 0x00FF);

        len_lora_data_set = offset;
        return;
    }

    uint16_t sub_count_pos = offset++;
    uint8_t sub_count = 0;

    event_log.last_sent_count = 0;

#if LORA_INCLUDE_PULSE_SUBDEVICE
    uint16_t pulse_reserve = pulse_sub_possible ? (2 + 3) : 0;
#else
    uint16_t pulse_reserve = 0;
#endif

    // GAS SUBDEVICE
    if (gas_sub_possible && ((offset + 2) <= max_payload)) {
        uint16_t sub_id_pos = offset++;
        uint16_t sub_mask_pos = offset++;

        uint8_t sub_mask = 0;

#if LORA_INCLUDE_GAS_SUB_BATTERY
        if ((offset + 1 + pulse_reserve) <= max_payload) {
            sub_mask |= SUB_MASK_GAS_BATTERY;
            buf[offset++] = battery;
        }
#endif

        // GAS Sensors
        if ((active_sensors > 0) &&
            ((offset + 1 + (4 * active_sensors) + pulse_reserve) <= max_payload)) {

            sub_mask |= SUB_MASK_GAS_SENSORS;

            buf[offset++] = active_sensors;

            for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
                if (TESTBIT(lora.alarm_chan_state, chan)) {
                    buf[offset++] = lora_sensor_map[chan];

                    int16_t conc_to_send;
                    uint8_t state_err_to_send;

                    if (chan_state[chan].trigger_valid) {
                        conc_to_send = chan_state[chan].trigger_concentration;
                        state_err_to_send = chan_state[chan].trigger_state_err;
                    }
                    else {
                        conc_to_send = Lora_ScaleConcentration(Cfg_struct, chan);
                        state_err_to_send = Cfg_struct->Sensor[chan].StateErr & 0x7F;
                    }

                    buf[offset++] = (uint8_t)(conc_to_send & 0x00FF);
                    buf[offset++] = (uint8_t)((conc_to_send >> 8) & 0x00FF);
                    buf[offset++] = state_err_to_send;
                }
            }
        }

        // GAS EventLog
        if (has_gas_event && time_ref_added && ((offset + 1 + pulse_reserve) <= max_payload)) {
            uint16_t temp_offset = offset + 1;
            uint8_t packed_events_count = 0;

            for (uint8_t i = event_log.send_index; i < event_log.count; i++) {
                event_log_record_t *rec = &event_log.records[i];

                uint16_t record_size = 7 + (rec->channel_count * 4);

                if ((temp_offset + record_size + pulse_reserve) > max_payload) {
                    break;
                }

                temp_offset += record_size;
                packed_events_count++;
            }

            if (packed_events_count > 0) {
                sub_mask |= SUB_MASK_GAS_EVENT_LOG;

                buf[offset++] = packed_events_count;

                for (uint8_t i = 0; i < packed_events_count; i++) {
                    uint8_t idx = event_log.send_index + i;
                    event_log_record_t *rec = &event_log.records[idx];

                    uint32_t dt_sec = 0;

                    if (rec->unix_time >= anchor_time) {
                        dt_sec = rec->unix_time - anchor_time;
                    }

                    if (dt_sec > 0xFFFF) {
                        dt_sec = 0xFFFF;
                    }

                    buf[offset++] = (uint8_t)(dt_sec & 0x00FF);
                    buf[offset++] = (uint8_t)((dt_sec >> 8) & 0x00FF);

                    if (gps_added && (rec->lat != 0)) {
                        int16_t dx = Lora_CalcDeltaMeters(rec->lat, anchor_lat, 0);
                        int16_t dy = Lora_CalcDeltaMeters(rec->lon, anchor_lon, anchor_lat);

                        buf[offset++] = (uint8_t)(dx & 0x00FF);
                        buf[offset++] = (uint8_t)((dx >> 8) & 0x00FF);

                        buf[offset++] = (uint8_t)(dy & 0x00FF);
                        buf[offset++] = (uint8_t)((dy >> 8) & 0x00FF);
                    }
                    else {
                        buf[offset++] = 0x80;
                        buf[offset++] = 0x00;

                        buf[offset++] = 0x80;
                        buf[offset++] = 0x00;
                    }

                    buf[offset++] = rec->channel_count;

                    for (uint8_t c = 0; c < rec->channel_count; c++) {
                        buf[offset++] = rec->channels[c].Channel;
                        buf[offset++] = rec->channels[c].StateErr;

                        buf[offset++] = (uint8_t)(rec->channels[c].Concentration & 0x00FF);
                        buf[offset++] = (uint8_t)((rec->channels[c].Concentration >> 8) & 0x00FF);
                    }
                }

                event_log.last_sent_count = packed_events_count;
            }
        }

        if (sub_mask != 0) {
            buf[sub_id_pos] = SUB_ID_GAS_DEFAULT;
            buf[sub_mask_pos] = sub_mask;
            sub_count++;
        }
        else {
            offset = sub_id_pos;
        }
    }

#if LORA_INCLUDE_PULSE_SUBDEVICE
    // PULSE SUBDEVICE
    if (pulse_sub_possible && ((offset + 2 + 3) <= max_payload)) {
        uint16_t sub_id_pos = offset++;
        uint16_t sub_mask_pos = offset++;

        uint8_t sub_mask = 0;

        sub_mask |= SUB_MASK_PULSE_BATTERY;
        buf[offset++] = pulse_state.battery;

        sub_mask |= SUB_MASK_PULSE_HEART_RATE;
        buf[offset++] = pulse_state.heart_rate;

        sub_mask |= SUB_MASK_PULSE_SPO2;
        buf[offset++] = pulse_state.spo2;

        buf[sub_id_pos] = SUB_ID_PULSE_DEFAULT;
        buf[sub_mask_pos] = sub_mask;

        sub_count++;
    }
#endif

    buf[sub_count_pos] = sub_count;

    buf[1] = (uint8_t)(container_mask & 0x00FF);
    buf[2] = (uint8_t)((container_mask >> 8) & 0x00FF);

    len_lora_data_set = offset;
}

//===========================================================================================================================
// TYPE_STATIC, MIRAX v4
//===========================================================================================================================
static void Lora_CompleteDataSet_1(SNS_CFG *Cfg_struct)
{
    uint8_t *buf = lora_static_payload;
    uint16_t offset = 0;

    uint8_t active_channels = 0;
    uint8_t sensors_enabled = 0;

    for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
        if (Cfg_struct->Sensor[chan].channel == chan) {
            sensors_enabled |= (1u << chan);
            active_channels++;
        }
    }

    buf[offset++] = DATA_STRUCT_LORA_VER | TYPE_MSG_STATIC;

    buf[offset++] = (uint8_t)(DEVICE_NUMBER & 0x00FF);
    buf[offset++] = (uint8_t)((DEVICE_NUMBER >> 8) & 0x00FF);

    buf[offset++] = Cfg_struct->Config_common.HardwareVersion;
    buf[offset++] = FIRMWARE_VERSION;

    radio_cfg_t radio;

    radio.Rx1DrOffset    = lora.rx1_dr_offset;
    radio.Rx2DataRate    = lora.rx2_set_param_dr;
    radio.Rx2FrequencyHz = lora.rx2_set_param_freq;
    radio.Rx1DelaySec    = lora.rx1_delay_sec;
    radio.TxPower        = lora.tx_power;
    radio.MaxDutyCycle   = lora.max_duty_cycle;

    if ((offset + sizeof(radio_cfg_t)) <= sizeof(lora_static_payload)) {
        memcpy(&buf[offset], &radio, sizeof(radio_cfg_t));
        offset += sizeof(radio_cfg_t);
    }

    // PPEEnabledMask
    buf[offset++] = ppe_enabled_mask;

    // PPE descriptors
    if (ppe_enabled_mask != 0) {
        for (uint8_t i = 0; i < LORA_MAX_PPE; i++) {
            if ((ppe_enabled_mask & (1u << i)) &&
                ((offset + sizeof(ppe_descr_t)) <= sizeof(lora_static_payload))) {

                memcpy(&buf[offset], &ppe_desc[i], sizeof(ppe_descr_t));
                offset += sizeof(ppe_descr_t);
            }
        }
    }

    uint16_t sub_count_pos = offset++;
    uint8_t sub_count = 0;

#if LORA_INCLUDE_PULSE_SUBDEVICE
    // Pulse subdevice
    if (pulse_state.present && ((offset + 5) <= sizeof(lora_static_payload))) {
        buf[offset++] = SUB_ID_PULSE_DEFAULT;
        buf[offset++] = SUB_TYPE_PULSE;

        buf[offset++] = (uint8_t)(pulse_state.serial & 0x00FF);
        buf[offset++] = (uint8_t)((pulse_state.serial >> 8) & 0x00FF);

        buf[offset++] = 0; // CfgLen

        sub_count++;
    }
#endif

    // Gas subdevice
    if ((active_channels > 0) && ((offset + 5) <= sizeof(lora_static_payload))) {
        uint8_t cfg_len = 1 + (3 * active_channels);

        if ((offset + 5 + cfg_len) <= sizeof(lora_static_payload)) {
            buf[offset++] = SUB_ID_GAS_DEFAULT;
            buf[offset++] = SUB_TYPE_GAS;

            uint16_t sub_serial = (uint16_t)(DEVICE_NUMBER & 0xFFFF);

            buf[offset++] = (uint8_t)(sub_serial & 0x00FF);
            buf[offset++] = (uint8_t)((sub_serial >> 8) & 0x00FF);

            buf[offset++] = cfg_len;

            buf[offset++] = sensors_enabled;

            for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
                if (Cfg_struct->Sensor[chan].channel == chan) {
                    uint16_t gas_id = Cfg_struct->Sensor[chan].num_formula_gas;

                    buf[offset++] = (uint8_t)(gas_id & 0x00FF);
                    buf[offset++] = (uint8_t)((gas_id >> 8) & 0x00FF);

                    buf[offset++] = Cfg_struct->Sensor[chan].DeviceSetting;
                }
            }

            sub_count++;
        }
    }

    buf[sub_count_pos] = sub_count;

    len_lora_data_set_1 = offset;
}

//===========================================================================================================================
static void Lora_CompleteDataSet_2(SNS_CFG *Cfg_struct, str_lora_data_set_2_t *data_set, TYPE_MSG_LORA type_msg)
{
    data_set->data_struct_ver = DATA_STRUCT_LORA_VER | type_msg;
    data_set->dev_id          = DEVICE_NUMBER;
    data_set->dev_hw_ver      = Cfg_struct->Config_common.HardwareVersion;
    data_set->dev_fw_ver      = FIRMWARE_VERSION;
    data_set->ControlLora     = lora.ControlLora;

    len_lora_data_set_2 = sizeof(str_lora_data_set_2_t);
}

//===========================================================================================================================
static void Lora_CompleteDataSetAck(str_lora_data_set_ack_t *data_set)
{
    data_set->data_struct_ver = DATA_STRUCT_LORA_VER | TYPE_MSG_ACK;
    data_set->ControlLora     = lora.ControlLora;
}

//===========================================================================================================================
void Lora_DataSet(SNS_CFG *Cfg_struct)
{
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

            Delay(50);
        }

        if (!LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST) || (!event_anchor.is_valid)) {
            event_anchor.unix_time = (uint32_t)Cfg_struct->Config_common.working_hours;

#if CONFIG_GPS
            if ((gps_last_valid_lat != 0) || (gps_last_valid_lon != 0)) {
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

        bool has_event_log = (event_log.count > event_log.send_index);
        bool has_gps_history = (gps_history.count > gps_history.send_index);
        bool has_container_event = (container_event_log.count > container_event_log.send_index);

        bool has_any_history = (has_event_log || has_gps_history || has_container_event) && lora_time_valid;

#if CONFIG_GPS
        if (LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST) && event_anchor.is_valid) {
            uint32_t current_time = (uint32_t)Cfg_struct->Config_common.working_hours;

            bool should_add = false;

            uint16_t period_lora_sec = (Sns_Cfg_struct.Config_common.PeriodTimeLora & 0xFF) * 10;

            if (gps_history.count == 0) {
                should_add = true;
            }
            else {
                if ((current_time - gps_history.last_point_unix_time) >= period_lora_sec) {
                    should_add = true;
                }
            }

            if (should_add && (gps_history.count < GPS_HISTORY_MAX_POINTS)) {
                if ((gps_last_valid_lat != 0) || (gps_last_valid_lon != 0)) {
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

        if (get_state_repeat_flag_lora() ||
            TESTBIT(lora.ControlLora, ST_LORA_BIT_ACTIV_MOD) ||
            has_any_history ||
            (lora.motion_event != 0)) {
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
            Lora_CompleteDataSet_1(Cfg_struct);

            len_lora_data_set_tr = len_lora_data_set_1;

            Lora_Transmit(CMD_TX_CNF, lora_static_payload);
        }
        else {
            Lora_CompleteDataSet(Cfg_struct, &lora_data_set);

            len_lora_data_set_tr = len_lora_data_set;

            Lora_Transmit(CMD_TX_CNF, (uint8_t *)&lora_data_set);
        }
    }
}

//===========================================================================================================================
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

//===========================================================================================================================
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

//===========================================================================================================================
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
            uint32_t sleep_time = 0;
            memcpy(&sleep_time, data, sizeof(uint32_t));

            len += sprintf((char*)&data_command[len], "%lu", (unsigned long)sleep_time);

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

//===========================================================================================================================
static inline uint8_t HexToByte(char high, char low)
{
#define HEX_VAL(c) ((c >= '0' && c <= '9') ? (c - '0') : \
                    (c >= 'a' && c <= 'f') ? (c - 'a' + 10) : \
                    (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : 0)

    return (HEX_VAL(high) << 4) | HEX_VAL(low);
}

//===========================================================================================================================
static uint8_t Lora_IsHexChar(char c)
{
    if ((c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'F') ||
        (c >= 'a' && c <= 'f')) {
        return 1U;
    }

    return 0U;
}

//===========================================================================================================================
static uint16_t Lora_HexStringToBytes(const char *hex_str, uint8_t *out, uint16_t out_size)
{
    uint16_t count = 0U;

    while ((count < out_size) &&
           (Lora_IsHexChar(hex_str[0]) != 0U) &&
           (Lora_IsHexChar(hex_str[1]) != 0U)) {

        out[count] = HexToByte(hex_str[0], hex_str[1]);
        count++;

        hex_str += 2;
    }

    return count;
}

//===========================================================================================================================
// DOWNLINK PARSER
//===========================================================================================================================
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

        static uint8_t payload[256];

        uint16_t bytes_to_parse = Lora_HexStringToBytes(hex_ptr, payload, sizeof(payload));

        if (bytes_to_parse < 3) {
            return;
        }

        memset(&lora_data_get, 0, sizeof(str_lora_data_get_t));

        lora_data_get.Header = payload[0];
        lora_data_get.FieldMask = payload[1] | ((uint16_t)payload[2] << 8);

        uint16_t offset = 3;

        // Control: Mask(2) + Value(2)
        if (lora_data_get.FieldMask & FIELD_MASK_DL_CONTROL) {
            if ((offset + 4) <= bytes_to_parse) {
                lora_data_get.ControlLoraMask =
                    payload[offset] | ((uint16_t)payload[offset + 1] << 8);
                offset += 2;

                lora_data_get.ControlLoraValue =
                    payload[offset] | ((uint16_t)payload[offset + 1] << 8);
                offset += 2;
            }
        }

        // Mode
        if (lora_data_get.FieldMask & FIELD_MASK_DL_MODE) {
            if ((offset + 2) <= bytes_to_parse) {
                lora_data_get.ControlLora2Mask  = payload[offset++];
                lora_data_get.ControlLora2Value = payload[offset++];
            }
        }

        // PeriodNormal
        if (lora_data_get.FieldMask & FIELD_MASK_DL_PERIOD_NORM) {
            if ((offset + 1) <= bytes_to_parse) {
                lora_data_get.NewPeriodNorm = payload[offset++];
            }
        }

        // PeriodActive
        if (lora_data_get.FieldMask & FIELD_MASK_DL_PERIOD_ACT) {
            if ((offset + 1) <= bytes_to_parse) {
                lora_data_get.NewPeriodAct = payload[offset++];
            }
        }

        // GPSFixInterval
        if (lora_data_get.FieldMask & FIELD_MASK_DL_GPS_FIX) {
            if ((offset + 1) <= bytes_to_parse) {
                lora_data_get.GpsFixInterval = payload[offset++];
            }
        }

        // ConfigSeq
        if (lora_data_get.FieldMask & FIELD_MASK_DL_CONFIG_SEQ) {
            if ((offset + 1) <= bytes_to_parse) {
                lora_data_get.ConfigSeq = payload[offset++];
            }
        }

        // Time
        if (lora_data_get.FieldMask & FIELD_MASK_DL_TIME) {
            if ((offset + 4) <= bytes_to_parse) {
                lora_data_get.UnixTime =
                    payload[offset] |
                    ((uint32_t)payload[offset + 1] << 8) |
                    ((uint32_t)payload[offset + 2] << 16) |
                    ((uint32_t)payload[offset + 3] << 24);

                offset += 4;
            }
        }

        // Message
        if (lora_data_get.FieldMask & FIELD_MASK_DL_MESSAGE) {
            if ((offset + 2) <= bytes_to_parse) {
                uint8_t msg_seq = payload[offset++];
                uint8_t msg_len = payload[offset++];

                if ((offset + msg_len) <= bytes_to_parse) {
                    lora_data_get.MsgPresent = 1U;
                    lora_data_get.MsgSeq     = msg_seq;
                    lora_data_get.MsgLength  = msg_len;

                    Lora_HandleMessage(msg_seq, msg_len, &payload[offset]);

                    offset += msg_len;
                }
            }
        }

        Lora_Control_States_Update(&lora_data_get, port_tx_read);
    }
}

//===========================================================================================================================
// MAC COMMAND PARSER
//===========================================================================================================================
static void Lora_ParseMacCommand(const char *cmd_str)
{
    uint8_t mac_cmd[64];
    uint16_t len;
    uint16_t i = 0;
    uint8_t parsed_any = 0;

    if (cmd_str == NULL) {
        return;
    }

    cmd_str += strlen(RES_MAC_CMD);

    while ((*cmd_str == ' ') || (*cmd_str == ':') || (*cmd_str == '\t')) {
        cmd_str++;
    }

    len = Lora_HexStringToBytes(cmd_str, mac_cmd, sizeof(mac_cmd));

    while (i < len) {
        uint8_t cid = mac_cmd[i];

        // LinkADRReq
        if ((cid == 0x03) && ((i + 5) <= len)) {
            uint8_t dr_tx = mac_cmd[i + 1];

            lora.set_param_dr = (dr_tx >> 4) & 0x0F;
            lora.tx_power = dr_tx & 0x0F;

            LORA_CFG_SET_DR(Sns_Cfg_struct.Config_common.Lora_Config_Flags, lora.set_param_dr);

            parsed_any = 1;
            i += 5;
        }
        // DutyCycleReq
        else if ((cid == 0x04) && ((i + 2) <= len)) {
            lora.max_duty_cycle = mac_cmd[i + 1] & 0x0F;

            parsed_any = 1;
            i += 2;
        }
        // RXParamSetupReq
        else if ((cid == 0x05) && ((i + 5) <= len)) {
            uint8_t param = mac_cmd[i + 1];

            uint8_t rx1_dr_offset = (param >> 4) & 0x07U;
            uint8_t rx2_data_rate = param & 0x0FU;

            uint32_t freq_raw = ((uint32_t)mac_cmd[i + 4] << 16) |
                                ((uint32_t)mac_cmd[i + 3] << 8) |
                                ((uint32_t)mac_cmd[i + 2]);

            uint32_t freq_hz = freq_raw * 100U;

            lora.rx1_dr_offset      = rx1_dr_offset;
            lora.rx2_set_param_dr   = rx2_data_rate;
            lora.rx2_set_param_freq = freq_hz;

            Sns_Cfg_struct.Config_common.Lora_dr_rx2   = rx2_data_rate;
            Sns_Cfg_struct.Config_common.Lora_freq_rx2 = freq_hz;

            lora.rx2_delay_ms = 0;
            LORA_SET_FLAG(LORA_FLAG_RX2_SETUP_PENDING);

            parsed_any = 1;
            i += 5;
        }
        // RXTimingSetupReq
        else if ((cid == 0x08) && ((i + 2) <= len)) {
            uint8_t delay = mac_cmd[i + 1] & 0x0F;

            if (delay == 0) {
                delay = 1;
            }

            lora.rx1_delay_sec = delay;

            parsed_any = 1;
            i += 2;
        }
        else {
            break;
        }
    }

    if (parsed_any) {
        SaveConfig = 1;
        LORA_SET_FLAG(LORA_FLAG_MAC_SAVE_PENDING);
    }
}

//===========================================================================================================================
#if LORA_DEBUG
void Test_Lora_Parsing(void)
{
    char test_uart_buffer[64] = ">> mac rx 15 4F67\r\n";

    ParseLoraToStruct(test_uart_buffer);
}
#endif

//===========================================================================================================================
void Lora_Receive(void)
{
    uint16_t len = 0;

    if (buffer_uart_lora.TimeFlagRX && (get_state_init_flag_lora() == 1)) {
        buffer_uart_lora.TimeFlagRX = 0;

        while (check_buffer(&InputBuffer[TYPE_LORA])) {
            receive_buffer(&InputBuffer[TYPE_LORA], &buffer_uart_lora.receive[buffer_uart_lora.count_byter_r]);
            buffer_uart_lora.count_byter_r++;
        }

        len = buffer_uart_lora.count_byter_r;
        buffer_uart_lora.count_byter_r = 0;

        if (len < 2) {
            memset(&buffer_uart_lora.receive[0], 0, len);
            return;
        }

        if (len < sizeof(buffer_uart_lora.receive)) {
            buffer_uart_lora.receive[len] = '\0';
        }
        else {
            buffer_uart_lora.receive[sizeof(buffer_uart_lora.receive) - 1] = '\0';
        }

        char* rx_str = (char*)buffer_uart_lora.receive;

        char* has_rx      = strstr(rx_str, RES_RX);
        char* has_tx_ok   = strstr(rx_str, RES_TX_OK);
        char* has_mac_cmd = strstr(rx_str, RES_MAC_CMD);

        if (has_mac_cmd != NULL) {
            Lora_ParseMacCommand(has_mac_cmd);
        }

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
        else if (has_mac_cmd != NULL) {
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
            LORA_CLR_FLAG(LORA_FLAG_INIT);
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

        if (has_rx != NULL) {
            START_TIMER_RTC(TIMER_RTC_LORA_DATA_SLEEP, TIME_RTC_LORA_DATA_SLEEP_AFTER_DL);
        }

        memset((void*)buffer_uart_lora.receive, 0, len);
    }
}

//===========================================================================================================================
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

//===========================================================================================================================
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

//===========================================================================================================================
#if LORA_DEBUG

static void buf_append(char *buf, uint16_t buf_size, uint16_t *offset, const char *str)
{
    if (str == NULL || *offset >= buf_size) return;

    uint16_t remaining = buf_size - *offset - 1;
    uint16_t str_len = 0;

    while (str[str_len] != '\0' && str_len < remaining) {
        str_len++;
    }

    for (uint16_t i = 0; i < str_len; i++) {
        buf[*offset + i] = str[i];
    }

    buf[*offset + str_len] = '\0';
    *offset += str_len;
}

void Lora_DebugPrintStatus(void)
{
    char buf[512];
    const uint16_t buf_size = sizeof(buf);
    uint16_t len = 0;

    char tmp[64];

    buf_append(buf, buf_size, &len, "\r\n==LORA==\r\n");

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

    snprintf(tmp, sizeof(tmp), "RX:%d ERR:%d/6\r\n",
             LORA_GET_RX_STATUS(), lora.counter_err);
    buf_append(buf, buf_size, &len, tmp);

    snprintf(tmp, sizeof(tmp), "TX:%dms\r\n", lora.tx_busy_timer);
    buf_append(buf, buf_size, &len, tmp);

    snprintf(tmp, sizeof(tmp), "C:%04X[", (unsigned)lora.ControlLora);
    buf_append(buf, buf_size, &len, tmp);

    if (TESTBIT(lora.ControlLora, ST_LORA_BIT_ACTIV_MOD))  buf_append(buf, buf_size, &len, "A");
    if (TESTBIT(lora.ControlLora, ST_LORA_BIT_ECHO))       buf_append(buf, buf_size, &len, "E");
    if (TESTBIT(lora.ControlLora, ST_LORA_BIT_EVACUATION)) buf_append(buf, buf_size, &len, "V");

    buf_append(buf, buf_size, &len, "]\r\n");

    snprintf(tmp, sizeof(tmp), "ENV:%d MOT:%02X EV:%02X\r\n",
             lora_env_valid,
             lora.motion_state,
             lora.motion_event);
    buf_append(buf, buf_size, &len, tmp);

    snprintf(tmp, sizeof(tmp), "PPE:%02X BLE:%d CEV:%d\r\n",
             ppe_presence_mask,
             ble_beacon_count,
             container_event_log.count);
    buf_append(buf, buf_size, &len, tmp);

    buf_append(buf, buf_size, &len, "========\r\n");

    transmit_buffer((uint8_t*)buf, len, TYPE_USART);
}

#endif

#endif
