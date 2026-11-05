/**
  ******************************************************************************
  * @file    TFT_indicator.h
  * @brief   API слоя индикации (Avis): экраны, меню, LVGL-виджеты
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
#define BUF_SIZE ((TFT_WIDTH * TFT_HEIGHT / 4))
#define MAX_ERROR_QUEUE 20
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

//===========================================================================================================================
enum {
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

enum {
	ROLLER_HEAD_NEXT = 0,
	ROLLER_HEAD_SAVE,
	ROLLER_HEAD_WAIT,
	ROLLER_HEAD_LANGUAGE,
	ROLLER_HEAD_END
};

enum {
	MEAS_NONE = 0,		/* панель не участвует в этой странице (скрыта)        */
	MEAS_DEFAULT,		/* первоначальная настройка (раскладка из дизайна)     */
	MEAS_TEMP,		/* температура воздуха                                 */
	MEAS_HUM,		/* влажность                                           */
	MEAS_PRESS,		/* атмосферное давление                                */
	MEAS_COORD,		/* координаты: широта + долгота, панель на весь экран  */
	MEAS_AQI,		/* индекс качества воздуха                             */
	MEAS_HEART,		/* пульс, панель на весь экран                         */
	MEAS_VOICE,		/* голосовые сообщения (только иконка)                 */
	MEAS_CHAT,		/* текстовые сообщения/чат (только иконка)             */
	MEAS_RSSI,		/* уровень сигнала                                     */
	MEAS_SAT,		/* количество спутников                                */
	MEAS_ERR,		/* строка текущей ошибки, панель на весь экран         */
	MEAS_MODE_END
};

//--------------------------------------------------------------------------------------------------------------
typedef struct {
    bool mirax;
    bool info_str;
    bool head_enter;
    bool head_out;
    bool head_timer;
    bool battery_charge;

    // Иконки индикации
    bool lora;
    bool gps;
    bool ble;
    bool exchange;
    bool lte;
    bool lte_vec;
} ui_visibility_state_t;

typedef struct {
    uint8_t channel;      // 0xFF для системных, 0xFE для LORA, 0xFD для тревог
    uint8_t category;     // 1=системная, 2=LORA, 3=тревога
    uint8_t error_bit;    // Номер бита ошибки или статус LORA
    uint16_t key;         // Уникальный ключ: (category<<8)|(channel<<4)|bit
} error_queue_item_t;

/* Структуризация глобального контекста индикации */
typedef struct {
    error_queue_item_t queue[MAX_ERROR_QUEUE];
    char current_text[40];
    uint8_t count;
    uint8_t display_index;
    uint32_t last_hash;
} lcd_error_context_t;

typedef struct {
    uint8_t last_lang;
    uint8_t rotation;
    uint8_t rotation_old;
    uint8_t total_info_pages;
} lcd_system_state_t;

typedef struct {
    uint16_t charge_count;
    uint16_t charge_flag;
} lcd_battery_anim_t;

//===========================================================================================================================
/* Экспорт сгруппированных контекстов */
extern ui_visibility_state_t ui_next_state;
extern lcd_error_context_t     lcd_err;
extern lcd_system_state_t    lcd_sys;
extern lcd_battery_anim_t   lcd_bat;

//===========================================================================================================================
// Функции работы с очередью ошибок
extern void LCD_UpdateErrorDisplay(SNS_CFG *pSnsCfg, uint8_t state_flashing);
extern void LCD_BuildErrorQueue(SNS_CFG *pSnsCfg);
//===========================================================================================================================
extern void ui_lv_init(void);
extern void ui_set_obj_hidden(lv_obj_t * obj, bool hide);
extern uint16_t LCD_Fill_Template(SNS_CFG *pSnsCfg);
extern void LCD_Image_SET(uint8_t state);
extern void ui_reset_visibility_flags(void);
extern void ui_apply_visibility(void);
extern void LCD_RadioIcon(void);

//===========================================================================================================================
extern void LCD_Menu(uint8_t num);
extern void LCD_SubMenu_Single(uint8_t num, uint16_t roller);
extern void LCD_SubMenuInfo(SNS_CFG *pSnsCfg, uint8_t num);
extern void LCD_SubMenuSetting(SNS_CFG *pSnsCfg, uint8_t num);

//===========================================================================================================================
extern void LCD_Measure(uint8_t panel, uint8_t mode, const char *value, const char *name, const char *units);
extern void LCD_MeasureScreen(uint8_t num);
extern void LCD_Battery(uint16_t precent, uint16_t state_flashing);

//===========================================================================================================================
/* Синхронизированные сигнатуры функций ввода */
extern void LCD_DataTime(struct tm *tm_ptr, uint8_t rank, uint8_t state_flashing);
extern void LCD_Data(struct tm *tm_ptr, uint8_t rank, uint8_t state_flashing);
extern void LCD_Time(struct tm *tm_ptr, uint8_t rank, uint8_t state_flashing);
extern void LCD_TimeHead(struct tm *tm_ptr);

//===========================================================================================================================
extern void LCD_CountdownSetTimer(uint32_t data, uint16_t roller);
extern void LCD_CountdownToSOS(uint32_t data, uint32_t state);
extern void LCD_CountdownToShutdown(uint32_t data);
extern void LCD_CountdownToWarm(uint32_t data);
extern void LCD_ToWarm(SNS_CFG *pSnsCfg, uint16_t num, uint8_t chan);
extern void LCD_Firmware(void);
extern void LCD_Digit_int_flashing_password(uint32_t data, uint8_t rank, uint8_t state_flashing);

#endif // __TFT_IND_H__
