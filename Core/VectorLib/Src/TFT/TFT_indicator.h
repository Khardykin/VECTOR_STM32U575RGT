/**
  ******************************************************************************
  * @file    TFT_indicator.h
  * @brief   API слоя индикации (Avis): экраны, меню, LVGL-виджеты
  *
  *          Пара к TFT_indicator.c: выключен вместе с ним (CONFIG_TYPE_LCD_TFT == 2,
  *          ждёт переноса - PLAN.md п.29). Зависит от lvgl.h, shared_macros.h,
  *          shared_types.h. Из TFT.h намеренно не включается.
  ******************************************************************************
  */
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
#define SET_3G(c)
#define SET_NBIOT(c)
#define SET_GPS(c)        ui_next_state.gps     = !c
#define SET_BLE(c)        ui_next_state.ble     = !c
#define SET_EXCHANGE(c)   ui_next_state.exchange   	= !c
#define SET_LTE(c)        ui_next_state.lte     = !c
#define SET_LTE_VEC(c)    ui_next_state.lte_vec = !c

//#define SET_MUTE(c)       ui_next_state.mute    = !c
//===========================================================================================================================
//===========================================================================================================================
enum{
	ROLLER_INFO = 0,
	ROLLER_SETTING,
	ROLLER_CONNECT_DEVICE,
	ROLLER_PASSWORD,
	ROLLER_TIME,
	ROLLER_DATA,
	ROLLER_DATATIME,
	ROLLER_TURN_OFF,
	ROLLER_SOS,
	ROLLER_END
};


enum{
	ROLLER_HEAD_NEXT = 0,
	ROLLER_HEAD_SAVE,
	ROLLER_HEAD_WAIT,
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
    bool info_str;

    bool head_enter;
    bool head_out;
    bool head_timer;

    // Иконки индикации
    bool lora;
    bool gps;
    bool ble;
//    bool mute;
    bool exchange;
    bool lte;
    bool lte_vec;
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
extern uint16_t LCD_Get_Template(void);
extern uint16_t LCD_Fill_Template(SNS_CFG *pSnsCfg);
extern void LCD_Image_SET(uint8_t state);
extern void ui_reset_visibility_flags(void);
extern void ui_apply_visibility(void);
//===========================================================================================================================
extern void LCD_Menu(uint8_t num);
extern void LCD_SubMenu_Single(uint8_t num, uint16_t roller);
extern void LCD_SubMenuInfo(SNS_CFG *pSnsCfg, uint8_t num);
extern void LCD_SubMenuSetting(SNS_CFG *pSnsCfg, uint8_t num);
extern void LCD_SubMenuService(SNS_CFG *pSnsCfg, uint8_t num);
extern void LCD_SubMenuSetting_Panel_Head_Button_1(bool show_roller, int8_t rank, uint16_t enter);
extern void LCD_SubMenuSetting_Panel_Head_Button_2(bool show_roller, int8_t rank, uint16_t enter);
extern void LCD_SubMenuSetting_Panel_Head_Button_3(bool show_roller, int8_t rank, uint16_t enter);
extern void LCD_SubMenuSetting_Panel_Head_Text(bool show_roller, int8_t rank, uint16_t enter);
//===========================================================================================================================
extern void LCD_Battery(uint16_t precent, uint16_t state_flashing);
//===========================================================================================================================
extern void LCD_DataTime(struct tm*tm_ptr, uint8_t x, uint8_t y);
extern void LCD_Data(struct tm*tm_ptr, uint8_t x, uint8_t y);
extern void LCD_Time(struct tm*tm_ptr, uint8_t x, uint8_t y);
extern void LCD_TimeHead(struct tm*tm_ptr);
//===========================================================================================================================
extern void LCD_CoundownSetTimer(uint32_t data, uint16_t roller);
extern void LCD_CountdownToSOS(uint32_t data, uint32_t state);
extern void LCD_CountdownToShutdown(uint32_t data);
extern void LCD_CountdownToWarm(uint32_t data);
extern void LCD_ToWarm(SNS_CFG *pSnsCfg, uint16_t num, uint8_t chan);
extern void LCD_Firmware(void);
//===========================================================================================================================
extern void LCD_Digit_int_flashing_password(uint32_t data, uint8_t rank, uint8_t state_flashing);
//===========================================================================================================================
extern uint8_t active_channels_map[COUNT_CHAN];
extern uint8_t active_channels_count;
extern uint8_t info_total_pages;
extern ui_visibility_state_t ui_next_state;
//extern void LCD_Battery_off(uint8_t batery);
//extern void LCD_TurnOffError(uint16_t num) ;
//extern void LCD_TurnOn(void);

#endif // __TFT_IND_H__
