/*
 * Avis_Menu_Functionality.h
 *
 *  Created on: 2025 Oct 22
 *      Author: Dmitriy
 */

#ifndef USER_AVIS_LIB_INC_AVIS_MENU_FUNCTIONALITY_H_
#define USER_AVIS_LIB_INC_AVIS_MENU_FUNCTIONALITY_H_

//===========================================================================================================================
enum PASSWORD_OPTION
{
	PASSWORD_CALIB_NUM = 0,
	PASSWORD_CALIB = 555,
	PASSWORD_SETTING_NUM = 1,
	PASSWORD_SETTING = 534,
};
#define FLASHING_CALIB_COUNT	(10)
#if (CONFIG_TYPE_LCD == 0)
#define DISP_PARAM_INPUT_DEC_MAX (4)
#elif (CONFIG_TYPE_LCD == 1)
#define DISP_PARAM_INPUT_DEC_MAX (4)
#elif (CONFIG_TYPE_LCD == 2)
#define DISP_PARAM_INPUT_DEC_MAX (7)
#endif
#define DISP_PARAM_INPUT_DATA_MAX (8)
#define DISP_PARAM_INPUT_TIME_MAX (12)
//===========================================================================================================================
extern float    conversion_factor[COUNT_CHAN];  // Коэффициент перевода в отображаемую единицу измерения
//------------------------------------------------------------------------------
extern uint8_t  state_flip;                		// Переключение отображений экрана при вкл
//------------------------------------------------------------------------------
extern time_t   tmp_current;                	// Время текущее
extern uint8_t  tmp_calib_flag;             	// Время до калибровки
extern time_t   tmp_calib;                  	// Время до калибровки
extern time_t  	tmp_calib_ch[COUNT_CHAN]; 		// Время до калибровки
extern time_t   tmp_calib_pre;              	// Вреям до калибровки предыдущее
extern time_t   tmp_bt;                     	// Время до БТ
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
extern uint8_t 	state_auto_calib_zero[COUNT_CHAN];
//------------------------------------------------------------------------------
extern uint8_t  ChanOne;                		// Выбранный канал во время калибровки
//===========================================================================================================================
extern void 	DisplayIndicationConc(SNS_CFG_Type *pSnsCfg);
extern float 	GetDisplayIndicationConc(uint8_t chan);
extern void 	DisplayConversionUnitMasure(SNS_CFG_Type *pSnsCfg);
extern void 	DisplayTimeConversion(SNS_CFG *pSnsCfg);
//------------------------------------------------------------------------------
extern float    calculating_conversion_factor(SNS_CFG_Type *pSnsCfg);
extern uint16_t calculating_the_discreteness(SNS_CFG_Type *pSnsCfg, uint16_t num1);
extern float 	Get_Conversion_factor(uint8_t chan);
extern float 	Get_Conversion_factor_rf(uint8_t chan);
//------------------------------------------------------------------------------
extern void     SystemDiagnostics(void);
extern uint8_t 	ParamStelState(void);
extern uint8_t 	ParamTwaState(void);
extern void 	Turn_FID_Sensor_To_Menu(uint8_t chan, uint8_t state);
extern uint8_t 	Check_status_before_action(uint8_t chan);
extern uint8_t  Check_SerialNumberChannel(uint8_t chan);
//------------------------------------------------------------------------------
extern time_t 	Setting_data_time(struct tm*tm_ptr);
extern time_t 	Setting_data_time_dec(struct tm* tm_ptr);
extern time_t 	Get_data_time(void);
extern void 	Display_data_time_save(void);
//------------------------------------------------------------------------------
extern void 	Init_Setting_data_time_rank(uint8_t rank, uint8_t rank_max, uint8_t rank_min);
extern uint8_t 	Check_Setting_data_time_rank(void);
//------------------------------------------------------------------------------
extern uint8_t 	Init_password(uint8_t num);
extern uint8_t 	Enter_password(void);
extern uint8_t 	Inc_password(uint8_t inc_rank);
extern uint8_t 	Check_password_rank(void);
extern uint16_t Check_password_num(uint8_t num);
//------------------------------------------------------------------------------
extern uint8_t 	InitRankDisplayParameterInput(float *data_input, uint8_t chan);
extern void 	InitRankDisplayParameterInputLimit(void);
extern void 	SetIncDisplayParameterInput(void);
extern void 	SetDecDisplayParameterInput(void);
extern uint8_t 	SwitchingRankDisplayParameterInput(float *data_input, uint8_t chan);
extern void 	DisplayParameterInputSave(uint8_t chan);
//------------------------------------------------------------------------------
extern float 	GetDisplayParameterInput(uint8_t chan);
extern uint8_t 	CheckRankDisplayParameterInput(void);
extern uint8_t 	SwitchingRankIncDisplayParameterInput(void);
extern uint8_t 	SwitchingRankDecDisplayParameterInput(void);
//------------------------------------------------------------------------------
extern void 	InitDisplayDeviceSettingInput(uint32_t data_input);
extern float 	GetDisplayDeviceSettingInput(void);
//------------------------------------------------------------------------------
extern void 	CountdownToShutdown(void);
extern void 	CountdownToShutdownImit(void);
extern void 	CountdownToSOS(void);
extern void		TurnOffSystem(void);
extern void     TurnOnPower(void);
extern void 	TurnOffPower(void);
extern void 	Button_Run_Backlight(void);
extern void 	Button_Run_Start(void);
extern void 	Button_Run(void);
//------------------------------------------------------------------------------
extern void 	Button_Sos(void);
extern void 	Button_Sos_Set(void);
extern void 	Button_Sos_Timer_Clr(void);

#endif /* USER_AVIS_LIB_INC_AVIS_MENU_FUNCTIONALITY_H_ */
