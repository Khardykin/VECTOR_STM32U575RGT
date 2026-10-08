#ifndef __TFT_IND_H__
#define __TFT_IND_H__
#include <time.h>
#include <stdbool.h>
#include "lvgl.h"
#include "shared_macros.h"
#include "shared_types.h"
//===========================================================================================================================
#define BUF_SIZE ((TFT_WIDTH * TFT_HEIGHT / 4))//todo было 5
//===========================================================================================================================
#define SET_MIRAX(c)      ui_next_state.mirax   = !c
#define SET_BAT_CHARGE(c) ui_next_state.battery_charge   = !c

#define SET_LORA(c)       ui_next_state.lora    = !c
#define SET_GPS(c)        ui_next_state.gps     = !c
#define SET_BLE(c)        ui_next_state.ble     = !c
#define SET_MUTE(c)       ui_next_state.mute    = !c
#define SET_EXCHANGE(c)   ui_next_state.exchange   	= !c
#define SET_LTE(c)        ui_next_state.lte     = !c
#define SET_LTE_VEC(c)    ui_next_state.lte_vec = !c
#define SET_FAN(c)        ui_next_state.fan     = !c
//===========================================================================================================================
//===========================================================================================================================
enum{
	LCD_STR_UNIT_MG_M3 = 0,
	LCD_STR_UNIT_PPM,
	LCD_STR_UNIT_NKPR,
	LCD_STR_UNIT_OB_D,
	LCD_STR_UNIT_END
};

enum{
	ROLLER_MENU_INFO = 0,
	ROLLER_MENU_SETTING,
	ROLLER_MENU_SETTING_LIMIT,
	ROLLER_MENU_SETTING_DATA_TIME,
	ROLLER_MENU_SETTING_RESET_STH,
	ROLLER_MENU_SETTING_LANG,
	ROLLER_MENU_SETTING_FACTORY,
	ROLLER_MENU_SERVICE,
	ROLLER_MENU_SERVICE_CALIB_ZERO,
	ROLLER_MENU_SERVICE_CALIB_RANGE,
	ROLLER_MENU_SERVICE_RESTART,
	ROLLER_MENU_END
};

enum{
	ROLLER_TIMER_WARM = 0,
	ROLLER_TIMER_TURN_OFF,
	ROLLER_TIMER_SOS,
	ROLLER_TIMER_END
};

enum{
	ROLLER_ENTER_PASSWORD = 0,
	ROLLER_ENTER_TIME,
	ROLLER_ENTER_DATA,
	ROLLER_ENTER_DATATIME,
	ROLLER_ENTER_MEAS,
	ROLLER_ENTER_DISK,
	ROLLER_ENTER_CONC,
	ROLLER_ENTER_LIMIT1,
	ROLLER_ENTER_LIMIT2,
	ROLLER_ENTER_LIMIT3,
	ROLLER_ENTER_END
};

enum{
	ROLLER_HEAD_RANGE_GAS = 0,
	ROLLER_HEAD_ZERO_GAS,
	ROLLER_HEAD_CALIB_GAS,
	ROLLER_HEAD_CALIB_WAIT,
	ROLLER_HEAD_LANGUAGE,
	ROLLER_HEAD_COUNT_CALIB,
	ROLLER_HEAD_CONFIRM,
	ROLLER_HEAD_RESTART,
	ROLLER_HEAD_END
};
//--------------------------------------------------------------------------------------------------------------
typedef struct
{
	uint8_t type_calib;
	uint16_t calib_flag_start;
	uint16_t calib_flag;
	uint16_t calib_flag_disp;
	uint16_t calib_flag_state;
	uint32_t calib_time;
} CALIB_CFG;
typedef struct
{
	uint8_t state_menu;
	uint8_t state_submenu;
	uint8_t state_submenu1;
	uint8_t state_submenu2;
	uint8_t state_submenu3;
	uint8_t state_set_input;
	uint8_t dirty_data;		// Данные изменены, сохранить
	CALIB_CFG calib;
} STATE_MENU;

typedef struct {
    bool mirax;
    bool battery_charge;
    bool info_str;
    bool info_temp;
    bool head_enter;
    bool head_out;
    bool head_menu;
    bool head_menu_up;
    bool head_timer;

    // Иконки индикации
    bool lora;
    bool gps;
    bool ble;
    bool mute;
    bool exchange;
    bool lte;
    bool lte_vec;
    bool fan;
} ui_visibility_state_t;
//===========================================================================================================================
// Структура для очереди ошибок (без текста - экономия RAM)
typedef struct {
    uint8_t channel;      // 0..7 для каналов, 0xFF для системных, 0xFE для LORA
    uint8_t category;     // 0=канал, 1=системная, 2=LORA
    uint8_t error_bit;    // Номер бита ошибки или статус LORA
    uint16_t key;         // Уникальный ключ: (category<<8)|(channel<<4)|bit
} error_queue_item_t;

#define MAX_ERROR_QUEUE 20
extern error_queue_item_t error_queue[MAX_ERROR_QUEUE];
extern uint8_t error_queue_count;
extern uint8_t error_display_index;
extern uint32_t last_error_hash;
//===========================================================================================================================
// Функции работы с очередью ошибок
extern void LCD_UpdateErrorDisplay(SNS_CFG *pSnsCfg, uint8_t state_flashing);
extern void LCD_BuildErrorQueue(SNS_CFG *pSnsCfg);
//===========================================================================================================================
extern void ui_lv_init(void);
//===========================================================================================================================
extern void ui_set_obj_hidden(lv_obj_t * obj, bool hide);
//===========================================================================================================================
extern void LCD_float_measure(SNS_CFG_Type *pSnsCfg, float data, uint8_t state_flashing, uint8_t num_channel);
extern void LCD_float_chan_calib(SNS_CFG_Type *pSnsCfg, float data, CALIB_CFG *calib, uint8_t state);
//extern void LCD_float_flashing(SNS_CFG_Type *pSnsCfg, float data_f, uint8_t rank, uint8_t state_flashing);
//===========================================================================================================================
extern void LCD_PosChannel_init(SNS_CFG *pSnsCfg);
extern uint8_t LCD_GetActiveChannelsCount(SNS_CFG *pSnsCfg);
extern void LCD_init_DisplayChannelScreenMeasurements(SNS_CFG *pSnsCfg, uint8_t tid);
extern uint16_t LCD_Get_Template(void);
extern uint16_t LCD_Fill_Template(SNS_CFG *pSnsCfg);
//extern uint16_t LCD_PosChannel(SNS_CFG_Type *pSnsCfg, uint8_t *pos_x, uint8_t *pos_y);
extern void LCD_Image_SET(uint8_t state);
extern void ui_reset_visibility_flags(void);
extern void ui_apply_visibility(void);
//===========================================================================================================================
extern void LCD_Menu(uint8_t num);
extern void LCD_SubMenu_Single(uint8_t num, uint16_t roller);
extern void LCD_SubMenu_Double(uint8_t num, uint16_t roller);
extern void LCD_SubMenuInfo(SNS_CFG *pSnsCfg, uint8_t num);
extern void LCD_SubMenuSetting(SNS_CFG *pSnsCfg, uint8_t num);
extern void LCD_SubMenuService(SNS_CFG *pSnsCfg, uint8_t num);
extern void LCD_SubMenuSettingSet(SNS_CFG *pSnsCfg, uint8_t config, uint8_t activrank, uint8_t num, uint8_t rank, uint8_t state_flashing, uint8_t chan, float data_f, struct tm*tm_ptr);
extern void LCD_SubMenuSettingSetCalib(SNS_CFG *pSnsCfg, float data_f, uint8_t chan, uint8_t rank, uint8_t state_flashing, uint16_t enter);
extern void LCD_SubMenuSettingChan(SNS_CFG *pSnsCfg, uint8_t num, uint16_t enter, CALIB_CFG *calib);
extern void LCD_SubMenuSetting_Panel_Head_Button_1(bool show_roller, int8_t rank, uint16_t enter);
extern void LCD_SubMenuSetting_Panel_Head_Button_2(bool show_roller, int8_t rank, uint16_t enter);
extern void LCD_SubMenuSetting_Panel_Head_Button_3(bool show_roller, int8_t rank, uint16_t enter);
extern void LCD_SubMenuSetting_Panel_Head_Button_Meas_1(SNS_CFG_Type *pSnsCfg, uint32_t t1, uint8_t state, int8_t rank, uint16_t enter);
extern void LCD_SubMenuSetting_Panel_Head_Button_Meas_2(SNS_CFG_Type *pSnsCfg, uint32_t t1, uint8_t state, int8_t rank, uint16_t enter);
extern void LCD_SubMenuSetting_Panel_Head_Text(bool show_roller, int8_t rank, uint16_t enter);
//===========================================================================================================================
extern void LCD_Battery(uint16_t precent, uint16_t state_flashing);
//===========================================================================================================================
extern void LCD_DataTime(struct tm*tm_ptr, uint8_t x, uint8_t y);
extern void LCD_Data(struct tm*tm_ptr, uint8_t x, uint8_t y);
extern void LCD_Time(struct tm*tm_ptr, uint8_t x, uint8_t y);
extern void LCD_TimeHead(struct tm*tm_ptr);
//===========================================================================================================================
//extern void LCD_AboutTheDevice(SNS_CFG *pSnsCfg, uint8_t num, uint8_t chan);
extern void LCD_CoundownSetTimer(uint32_t data, uint16_t roller);
extern void LCD_CountdownToSOS(uint32_t data, uint32_t state);
extern void LCD_CountdownToShutdown(uint32_t data);
extern void LCD_CountdownToWarm(uint32_t data);
extern void LCD_ToWarm(SNS_CFG *pSnsCfg, uint16_t num, uint8_t chan);
extern void LCD_Firmware(void);
//===========================================================================================================================
extern void LCD_Digit_int_flashing_password(uint32_t data, uint8_t rank, uint8_t state_flashing);
extern void LCD_Digit_Measure(uint32_t data, uint8_t rank, uint8_t state_flashing);
extern void LCD_Digit_Discretenes(uint32_t data, uint8_t rank, uint8_t state_flashing);
extern void LCD_Digit_flashing(SNS_CFG_Type *pSnsCfg, float data, uint8_t rank, uint8_t state_flashing, uint16_t enter);
extern void LCD_CalibrationZeroOrSpanMulti(SNS_CFG *pSnsCfg, CALIB_CFG *calib, uint32_t t1, uint16_t enter);
extern void LCD_CalibrationZeroOrSpan(SNS_CFG_Type *pSnsCfg, CALIB_CFG *calib, uint32_t t1, int8_t rank, uint16_t enter);
extern uint8_t LCD_EndCalibrationZeroOrSpan(SNS_CFG_Type *pSnsCfg, CALIB_CFG *calib);
//===========================================================================================================================
extern void LCD_BumpTest(SNS_CFG *pSnsCfg, uint8_t num, uint8_t state_flashing);
extern void LCD_BumpTestStatus(SNS_CFG *pSnsCfg, uint8_t num, uint8_t state_flashing);
//===========================================================================================================================
extern uint8_t active_channels_map[COUNT_CHAN];
extern uint8_t active_channels_count;
extern uint8_t info_total_pages;
extern ui_visibility_state_t ui_next_state;
//extern void LCD_Battery_off(uint8_t batery);
//extern void LCD_TurnOffError(uint16_t num) ;
//extern void LCD_TurnOn(void);

#endif // __TFT_IND_H__
