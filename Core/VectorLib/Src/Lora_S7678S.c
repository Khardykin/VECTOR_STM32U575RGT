#include "main.h"
#include "Vector_main.h"

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

    uint8_t  ControlLora;
    uint8_t  ControlLora2;

    uint8_t  applied_rev;         // Версия последней примененной конфигурации
    uint8_t  config_ack_pending;  // Флаг: 1 = нужно отправить ConfigAck, 0 = уже отправлено
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

uint8_t Lora_GetControlLora(void)
{
    return lora.ControlLora;
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

// Вспомогательная функция для масштабирования концентрации (используем вашу формулу)
static int16_t Lora_ScaleConcentration(SNS_CFG *Cfg_struct, uint8_t chan)
{
    // Приводим к int16_t, так как протокол требует знаковый тип (для дрейфа около нуля)
    float raw_val = Cfg_struct->Sensor[chan].ConcentrationVal *
                    Cfg_struct->Sensor[chan].Discreteness *
                    Get_Conversion_factor_rf(chan);
    return (int16_t)lround(raw_val);
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

    for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
        if (Cfg_struct->Sensor[chan].channel != chan) continue;

        int16_t current_conc = Lora_ScaleConcentration(Cfg_struct, chan);
        uint8_t current_state = Cfg_struct->Sensor[chan].StateErr & 0x7F;

        // 1. Определяем текущее положение относительно мёртвой зоны
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

        // 3. ГЛАВНОЕ: Управляем битом на основе ТЕКУЩЕЙ реальности
        // Канал вне мёртвой зоны = ОБЯЗАНЫ передавать (независимо от того, было ли изменение)
        if (!current_in_dead_zone) {
            SETBIT(lora.alarm_chan_state, chan);
        } else {
            CLRBIT(lora.alarm_chan_state, chan);
        }
    }

    // Глобальная кнопка SOS
    uint8_t current_sos = TESTBIT(lora.ControlLora, ST_LORA_BIT_SOS);
    if (current_sos != lora.alarm_pre_state_all) {
        state_changed = 1;
        lora.alarm_pre_state_all = current_sos;
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
        Lora_BuildSensorMap(Cfg_struct);
        Lora_InitDeveui(Cfg_struct);

        if (!TESTBIT(Cfg_struct->Config_common.Lora_Config_Flags, LORA_CFG_BIT_SAVED)) {
            Lora_TransmitReceive(CMD_SET_DEVEUI, deveui);
            Lora_TransmitReceive(CMD_SET_APPEUI, appeui);
            Lora_TransmitReceive(CMD_SET_APPKEY, appkey);

            Lora_TransmitReceive(CMD_GET_DR, (uint8_t*)&lora.empty_buffer);
            lora.empty_buffer = 3;
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

    // 1. Control (Бит 0 FieldMask)
    if (rx_data->FieldMask & FIELD_MASK_DL_CONTROL) {
        // Защищаем биты SOS (0x20) и CURRENT_C (0x80) от перезаписи сервером
        uint8_t protected_bits = lora.ControlLora & 0xA0;
        uint8_t allowed_mask = rx_data->ControlLoraMask & ~0xA0;

        // Формула атомарной записи: state = (state & ~Mask) | (Value & Mask)
        lora.ControlLora = protected_bits | ((lora.ControlLora & ~allowed_mask) | (rx_data->ControlLoraValue & allowed_mask));
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
        // Обновляем системное время прибора полученным от сервера Unix-временем
        Sns_Cfg_struct.Config_common.working_hours = rx_data->UnixTime;
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

        // Сброс EventLog
        if (event_log.last_sent_count > 0) {
            event_log.send_index += event_log.last_sent_count;
            event_log.last_sent_count = 0;
        }

        // Сброс GPSHistory
        if (gps_history.last_sent_count > 0) {
            gps_history.send_index += gps_history.last_sent_count;
            gps_history.last_sent_count = 0;
        }

        // Сброс флага ТОЛЬКО когда оба буфера пусты
        bool event_log_empty = (event_log.send_index >= event_log.count);
        bool gps_history_empty = (gps_history.send_index >= gps_history.count);

        if (event_log_empty && gps_history_empty) {
            event_log.count = 0;
            event_log.send_index = 0;
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

    // Проверка на изменение ControlLora для квитирования
    if ((lora.ControlLora & 0x37) != lora.alarm_pre) {
        Lora_ack_system_set();
    }
    lora.alarm_pre = lora.ControlLora & 0x37;

    port_tx = port_tx_read;

    if (lora.count_turnoff_system_tx != 0) {
        Lora_system_msg_clr();
    }
}

static void Lora_CompleteDataSet(SNS_CFG *Cfg_struct, str_lora_data_set_t *data_set)
{
    uint8_t header = (TYPE_MSG_DYNAMIC & 0xC0) | 0x03;
    uint16_t field_mask = 0;
    uint8_t offset = 3;
    uint8_t *buf = (uint8_t *)data_set;
    uint8_t max_payload = Lora_GetMaxPayloadSize(lora.set_param_dr);
    buf[0] = header;

    bool has_event_log = (event_log.count > event_log.send_index);
    bool has_gps_history = (gps_history.count > gps_history.send_index);
    bool has_any_history = has_event_log || has_gps_history;
    int32_t anchor_lat = 0, anchor_lon = 0;
    uint32_t anchor_time = 0;
    bool has_anchor_gps = false;

    if (has_any_history) {
        anchor_time = event_anchor.unix_time;
        anchor_lat  = event_anchor.lat;
        anchor_lon  = event_anchor.lon;
        if (anchor_lat != 0 || anchor_lon != 0) has_anchor_gps = true;
    } else {
        anchor_time = (uint32_t)Cfg_struct->Config_common.working_hours; // ВАШЕ ВРЕМЯ
#if CONFIG_GPS
        anchor_lat = gps_last_valid_lat;
        anchor_lon = gps_last_valid_lon;
        if (anchor_lat != 0 || anchor_lon != 0) has_anchor_gps = true;
#endif
    }

    // БИТ 0: Battery
    field_mask |= FIELD_MASK_BATTERY;
    buf[offset++] = Cfg_struct->Config_common.battery_charge_percent;

    // БИТ 1: GPS (Якорь или живой)
    if (has_anchor_gps && ((offset + 8) <= max_payload)) {
        field_mask |= FIELD_MASK_GPS;
        memcpy(&buf[offset], &anchor_lat, 4); offset += 4;
        memcpy(&buf[offset], &anchor_lon, 4); offset += 4;
    }

    // БИТ 2: Sensors
    uint8_t active_sensors = 0;
    for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
        if (TESTBIT(lora.alarm_chan_state, chan)) {
            active_sensors++;
        }
    }

    uint8_t sensors_block_size = 1 + (active_sensors * 4);

    if ((active_sensors > 0) && ((offset + sensors_block_size) <= max_payload)) {
        field_mask |= FIELD_MASK_SENSORS;
        buf[offset++] = active_sensors;

        for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
            if (TESTBIT(lora.alarm_chan_state, chan)) {
                buf[offset++] = lora_sensor_map[chan];

                // Выбор: замороженные данные (первый пакет) или живые (последующие)
                int16_t conc_to_send;
                uint8_t state_err_to_send;

                if (chan_state[chan].trigger_valid) {
                    // 1. ПЕРВЫЙ ПАКЕТ после события: шлем то, что вызвало тревогу
                    conc_to_send = chan_state[chan].trigger_concentration;
                    state_err_to_send = chan_state[chan].trigger_state_err;
                }
                else {
                    // 3. ШТАТНЫЙ РЕЖИМ: шлем абсолютно текущее живое значение
                    conc_to_send = Lora_ScaleConcentration(Cfg_struct, chan);
                    state_err_to_send = Cfg_struct->Sensor[chan].StateErr & 0x7F;
                }

                buf[offset++] = (uint8_t)(conc_to_send & 0xFF);
                buf[offset++] = (uint8_t)((conc_to_send >> 8) & 0xFF);
                buf[offset++] = state_err_to_send;
            }
        }
    }

    // БИТ 3: DeviceStatus
    if ((offset + 1) <= max_payload) {
        field_mask |= FIELD_MASK_DEV_STATUS;
        buf[offset++] = lora.ControlLora;
    }

    // БИТ 4: GPSTrack (Только если нет истории, чтобы не смешивать дельты)
    if (!has_any_history) {
#if CONFIG_GPS
        if (gps_hist_has_prev && (gps_hist_count > 0)) {
            uint8_t hist_size = 1 + (2 * gps_hist_count);
            if ((offset + hist_size) <= max_payload) {
                field_mask |= FIELD_MASK_GPS_TRACK; // Используем новое имя маски
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

    // БИТ 5: TimeRef (Обязателен для истории)
    if (has_any_history && ((offset + 4) <= max_payload)) {
        field_mask |= FIELD_MASK_TIME_REF;
        memcpy(&buf[offset], &anchor_time, 4);
        offset += 4;
    }

    // БИТ 6: EventLog
    if (has_event_log) {
        uint8_t temp_offset = offset + 1;
        uint8_t packed_events_count = 0;
        for (uint8_t i = event_log.send_index; i < event_log.count; i++) {
            event_log_record_t *rec = &event_log.records[i];
            uint8_t record_size = 7 + (rec->channel_count * 4);
            if ((temp_offset + record_size) > max_payload) break;
            temp_offset += record_size;
            packed_events_count++;
        }

        if (packed_events_count > 0) {
            field_mask |= FIELD_MASK_EVENT_LOG;
            buf[offset++] = packed_events_count;

			for (uint8_t i = 0; i < packed_events_count; i++) {
				uint8_t idx = event_log.send_index + i;
				event_log_record_t *rec = &event_log.records[idx];

				// Dt = секунды ВПЕРЁД от TimeRef
				uint32_t dt_sec = (rec->unix_time >= anchor_time) ? (rec->unix_time - anchor_time) : 0;
				if (dt_sec > 0xFFFF) dt_sec = 0xFFFF;

				buf[offset++] = (uint8_t)(dt_sec & 0xFF);
				buf[offset++] = (uint8_t)((dt_sec >> 8) & 0xFF);

				// Dx, Dy
				if (rec->lat != 0) {
					int16_t dx = Lora_CalcDeltaMeters(rec->lat, anchor_lat, 0);
					int16_t dy = Lora_CalcDeltaMeters(rec->lon, anchor_lon, anchor_lat);
					buf[offset++] = (uint8_t)(dx & 0xFF);
					buf[offset++] = (uint8_t)((dx >> 8) & 0xFF);
					buf[offset++] = (uint8_t)(dy & 0xFF);
					buf[offset++] = (uint8_t)((dy >> 8) & 0xFF);
				} else {
					buf[offset++] = 0x80; buf[offset++] = 0x00;
					buf[offset++] = 0x80; buf[offset++] = 0x00;
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

    // ========================================================================
    // БИТ 7: GPSHistory (восстановление трека за время потери связи)
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
            uint8_t block_size = 6 + (2 * gps_hist_to_send);
            if ((offset + block_size) <= max_payload) {
                field_mask |= FIELD_MASK_GPS_HISTORY;

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

    // БИТ 8: ConfigAck
    if (lora.config_ack_pending && ((offset + 1) <= max_payload)) {
        field_mask |= FIELD_MASK_CONFIG_ACK;
        buf[offset++] = lora.applied_rev;
        // Сбрасываем флаг сразу после добавления в буфер отправки.
        // Если пакет потеряется, сервер снова пришлет ConfigSeq, и мы взведем флаг заново.
        lora.config_ack_pending = 0;
    }


    buf[1] = (uint8_t)(field_mask & 0xFF);
    buf[2] = (uint8_t)((field_mask >> 8) & 0xFF);
    len_lora_data_set = offset;
}

static void Lora_CompleteDataSet_1(SNS_CFG *Cfg_struct, str_lora_data_set_1_t *data_set)
{
    uint8_t chan_data_set = 0;
    uint8_t active_channels = 0;

    data_set->data_struct_ver   = DATA_STRUCT_LORA_VER | TYPE_MSG_STATIC;
    data_set->dev_id            = DEVICE_NUMBER;
    data_set->dev_hw_ver        = Cfg_struct->Config_common.HardwareVersion;
    data_set->dev_fw_ver        = FIRMWARE_VERSION;
    data_set->sensors_enabled   = 0;

    for (uint8_t chan = 0; chan < COUNT_CHAN_LORA; chan++) {
        if (Cfg_struct->Sensor[chan].channel == chan) {
            data_set->sensors_enabled |= (1 << chan);
            data_set->sensors[chan_data_set].Setting = Cfg_struct->Sensor[chan].DeviceSetting;
            data_set->sensors[chan_data_set].num_formula_gas = Cfg_struct->Sensor[chan].num_formula_gas;
            chan_data_set++;
            active_channels++;
        }
    }

    len_lora_data_set_1 = 6 + (active_channels * sizeof(sensor_data_lora_1_t));
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
            event_anchor.unix_time = (uint32_t)Cfg_struct->Config_common.working_hours;
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
        bool has_gps_history = (gps_history.count > gps_history.send_index);
        bool has_any_history = has_event_log || has_gps_history;

#if CONFIG_GPS
        if (LORA_TEST_FLAG(LORA_FLAG_CONNECTION_LOST) && event_anchor.is_valid) {
            uint32_t current_time = (uint32_t)Cfg_struct->Config_common.working_hours;
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
        uint8_t payload[64]; // С запасом (макс. downlink обычно меньше)
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

        // 3. Очищаем структуру перед заполнением
        memset(&lora_data_get, 0, sizeof(str_lora_data_get_t));

        // 4. Читаем обязательные поля
        lora_data_get.Header = payload[0];
        lora_data_get.FieldMask = payload[1] | (payload[2] << 8);

        // 5. ДИНАМИЧЕСКИЙ ПАРСИНГ: сдвигаем offset только если бит установлен в FieldMask
        uint8_t offset = 3; // Начинаем после Header и FieldMask

        if (lora_data_get.FieldMask & FIELD_MASK_DL_CONTROL) {
            if (offset + 2 <= bytes_to_parse) {
                lora_data_get.ControlLoraMask = payload[offset++];
                lora_data_get.ControlLoraValue = payload[offset++];
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
                                        (payload[offset+3] << 24);
                offset += 4;
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

    // 4. ControlLora
    snprintf(tmp, sizeof(tmp), "C:%02X[", lora.ControlLora);
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
