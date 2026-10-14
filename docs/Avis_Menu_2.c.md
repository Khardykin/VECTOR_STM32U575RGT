#include "main.h"
#include "Avis_main.h"
#if ((CONFIG_TYPE_LCD == 2))
//===========================================================================================================================
#define TIMER_SERVICE_DEVICE_END	(3000)//ms
//------------------------------------------------------------------------------
static uint8_t  flashing_calib = 0;             // Инкремент миганий(Количество миганий обычно)
static uint8_t  state_flashing_calib = 0;       // Состояние отображения
#define PARAM_FLASHING(x, y)  {flashing_calib = x; state_flashing_calib = y;}
static uint8_t  state_flashing_system = 0;      // Состояние отображения зарядки
//------------------------------------------------------------------------------
static uint8_t  count_flashing_lora = 0;      	// Счетчик отображения lora
static uint8_t  state_flashing_lora = 0;      	// Состояние отображения lora
//------------------------------------------------------------------------------
//static uint8_t 	flashing_err = 0;
static uint8_t 	state_err = 0;
//------------------------------------------------------------------------------
static STATE_MENU state_menu_str = {0};
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
extern SNS_CFG  Cfg_structdef;					// Вспомагательная структура
//------------------------------------------------------------------------------
//===========================================================================================================================
static uint8_t 	scroll_up(uint8_t current, uint8_t max);
#if (CONFIG_MODEL_LCD == 0)
static uint8_t 	scroll_down(uint8_t current, uint8_t max);
#endif
static uint16_t Get_Max_Count_Chan_Calib(void);
static void 	DisplayTimeCurrent(SNS_CFG *pSnsCfg);
static void 	DisplayErrIndication(SNS_CFG *pSnsCfg);
static void 	DisplayChargeIndication(SNS_CFG *pSnsCfg);
static void 	Screen_status_flip(SNS_CFG *pSnsCfg, uint8_t *state);
static void     Warm_Screen_statuses(SNS_CFG *pSnsCfg, uint8_t *state);
//===========================================================================================================================
// Функция для прокрутки значения вниз (декремент с кольцом)
static uint8_t scroll_up(uint8_t current, uint8_t max)
{
    return (current + 1) % max;
}

#if (CONFIG_MODEL_LCD == 0)
// Функция для прокрутки значения вниз (декремент с кольцом)
static uint8_t scroll_down(uint8_t current, uint8_t max)
{
	return (current + max - 1) % max;
}
#endif

//===========================================================================================================================
static uint16_t Get_Max_Count_Chan_Calib(void)
{
	uint16_t count = 0;
	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
		if(Check_status_before_action(chan)){
			count++;
		}
	}
	return count;
}

//===========================================================================================================================
// Вывод на дисплей индикацию ошибок
static void DisplayTimeCurrent(SNS_CFG *pSnsCfg)
{
	LCD_TimeHead(gmtime(&tmp_current));
}

//===========================================================================================================================
// Вывод на дисплей индикацию ошибок
static void DisplayErrIndication(SNS_CFG *pSnsCfg)
{
	if(IS_STATE_SCREEN() != STATE_SCREEN_MEASURE){
		return;
	}
    // Вызываем функцию обновления ошибок из TFT_indicator.c
    LCD_UpdateErrorDisplay(pSnsCfg, state_flashing_lora);
}

//===========================================================================================================================
// Вывод на дисплей заряд батареи
static void DisplayChargeIndication(SNS_CFG *pSnsCfg)
{
#if CONFIG_CHARGE
	//------------------------------------------------
	// Вывод заряда батареи
	LCD_Battery(pSnsCfg->Config_common.battery_charge_percent, state_flashing_system);
	//------------------------------------------------
#endif
}

//===========================================================================================================================
// Вывод на дисплей заряд батареи в выключенном состоянии
void DisplayChargeIndicationTurnOff(void)
{
#if CONFIG_CHARGE

  if(TEST_TIMER_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM)){
	  RESET_TIMER_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM);
	  state_flashing_system = (~state_flashing_system);
  }
  //------------------------------------------------
  // Копируем рабочую структуру в вспомогательную
  Cfg_structdef = Sns_Cfg_struct;
  SET_BAT_CHARGE(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE));
  //------------------------------------------------
  // Вывод заряда батареи
  if(TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_CRITICAL_BATTERY) && TEST_TIMER_RUN_RTC(TIMER_RTC_OFF_BAT_CRITICAL_LOW)){
	  // Включение(питание) платы
	  TurnOnPower();
	  //------------------------------------------------------------------------
	  if(LCD_Get_flag_init() == 0){
		  // Инициализация LCD
		  LCD_Init();
	  }
	  //------------------------------------------------------------------------
	  // Идет зарядка батареи
	  LCD_Fill_Template(&Cfg_structdef);
	  DisplayTimeConversion(&Cfg_structdef);
	  DisplayTimeCurrent(&Cfg_structdef);
	  DisplayChargeIndication(&Cfg_structdef);
	  ui_apply_visibility();
	  LCD_UpdateScreen();
	  //------------------------------------------------
	  if((!TEST_TIMER_RUN_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM)) && (!TEST_TIMER_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM))){
		  START_TIMER_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM, TIME_RTC_DISP_FLASHING_ERR);
	  }
  }
  else if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE)){
	  // Включение(питание) платы
	  TurnOnPower();
	  if(LCD_Get_flag_init() == 0){
		  //------------------------------------------------------------------------
		  // Инициализация LCD
		  LCD_Init();
		  //------------------------------------------------------------------------
    	  // Включение(питание) платы
//    	  if(!READ_PIN_OUT(POWER_ON)){
//    		  SET_ON(POWER_ON);
    		  Delay(50);
    		  SOUND_ON();//sound
    		  VIBRO_ON();	//vibro
    		  Delay(200);
    		  SOUND_OFF();	//sound
    		  VIBRO_OFF();	//vibro
//          }
	  }
	  RESET_TIMER(TIMER_CHARGE_SOUND_VIBRO);
	  // Идет зарядка батареи
	  LCD_Fill_Template(&Cfg_structdef);
	  DisplayTimeConversion(&Cfg_structdef);
	  DisplayTimeCurrent(&Cfg_structdef);
	  DisplayChargeIndication(&Cfg_structdef);
	  ui_apply_visibility();
	  LCD_UpdateScreen();
	  //------------------------------------------------
	  if((!TEST_TIMER_RUN_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM)) && (!TEST_TIMER_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM))){
		  START_TIMER_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM, TIME_RTC_DISP_FLASHING_ERR);
	  }
  }
  else{
	  if(TEST_TIMER_RTC(TIMER_RTC_OFF_BAT_CRITICAL_LOW)){
		  RESET_TIMER_RTC(TIMER_RTC_OFF_BAT_CRITICAL_LOW);
	  }
	  if(LCD_Get_flag_init() == 1){
		  SOUND_OFF();
		  VIBRO_OFF();
		  LCD_UpdateScreen();
		  LCD_DeInit();
	  }
	  // Выключение(питание) платы
	  if(button.button_flag == 0){
		  TurnOffPower();
      }
  }
#endif
}

//===========================================================================================================================
static void Screen_status_flip(SNS_CFG *pSnsCfg, uint8_t *state)
{
	uint8_t state_calib = 0;
	switch(*state){
	case 0:
		*state = 1;
		state_menu_str.state_submenu = 0;
		TO_GO_STATE_SCREEN(STATE_SCREEN_WARM_INFO_DEVICE);
		break;
	case 1:
		state_menu_str.state_submenu = scroll_up(state_menu_str.state_submenu, info_total_pages);
		if(state_menu_str.state_submenu == 0){
			*state = 3;
			TO_GO_STATE_SCREEN(STATE_SCREEN_WARM);
		}
		else{
			*state = 1;
		}
		break;
	case 2:
		*state = 3;
		break;
	case 3:
		for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
			if(!state_auto_calib_zero[chan]){
				state_calib = 0;
			}
			else{
				state_calib = 1;
				chan = COUNT_CHAN;
			}
		}
		if(state_calib == 0){
			*state = 5;
			TO_GO_STATE_SCREEN(STATE_SCREEN_WARM);
		}
		else{
			*state = 4;
			TO_GO_STATE_SCREEN(STATE_SCREEN_WARM_AUTO_CALIB);
		}
    	break;
    case 4:
    	*state = 5;
    	TO_GO_STATE_SCREEN(STATE_SCREEN_WARM);
    	break;
	case 5:
		SystemDiagnostics();
		*state = 6;
		break;
	case 6:
		START_TIMER_RTC(TIMER_RTC_WARM, 1);
		*state = 7;
		break;
	case 7:
        RESET_TIMER_RTC(TIMER_RTC_WARM);
        //----------------------------------------------------------------------
        //Сброс STEL, TWA, HIGH
        for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
        	CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT_STEL, chan);
        	RESET_TIMER_RTC_CH(TIMER_RTC_STEL, chan);
        	clearCalculationTwa(chan);
        	pSnsCfg->Sensor[chan].ConcentrationValHigh = 0;
        }
        //----------------------------------------------------------------------
        TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);
        device_turn = DEVICE_TURNED_ON;
        //----------------------------------------------------------------------
		break;
	default:
		*state = 0;
		break;
	}

	if(*state == 3){
		for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
			state_auto_calib_zero[chan] = 0;
			//Бит о запуске калибровки нуля
			if(Check_status_before_action(chan)){
				if(TEST_STATUS_BIT(ST_BIT_AUTO_ZERO_TURN_ON, chan)){
					state_auto_calib_zero[chan] = 1;
				}
			}
		}
		START_TIMER_RTC(TIMER_RTC_WARM, TIME_RTC_WARM_SENS);
	}
}

//===========================================================================================================================
static void Warm_Screen_statuses(SNS_CFG *pSnsCfg, uint8_t *state)
{
	uint8_t state_calib = 0;
	//------------------------------------------------------------------------

	//------------------------------------------------------------------------
	switch(*state){
	case 0:
		//Картинка миракс
		LCD_ToWarm(pSnsCfg, 0, NOCHAN);
		break;
	case 1:
		// Общая инфомрация
		LCD_ToWarm(pSnsCfg, 1, state_menu_str.state_submenu);
		break;
	case 2:
		// Информация о включенных каналах
//		LCD_ToWarm(pSnsCfg, 2, ChanOne);
	break;
	case 3:
		if((TEST_TIMER_RUN_RTC(TIMER_RTC_WARM) >= 3)){
			// Обратный отсчет
			LCD_ToWarm(pSnsCfg, 3, NOCHAN);
			if(TEST_TIMER_RUN_RTC(TIMER_RTC_WARM) <= 4){
				END_TIMER_RTC(TIMER_RTC_WARM);
			}
		}
    	break;
    case 4:
		// Автокалибрвка
		LCD_ToWarm(pSnsCfg, 4, NOCHAN);
		for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
			if(state_auto_calib_zero[chan_r]){
				state_auto_calib_zero[chan_r] = 0;
				CalibZeroAutoStart[chan_r] = 1;
			}
		}
      break;
    case 5:
		// Тест
		LCD_ToWarm(pSnsCfg, 5, NOCHAN);
		break;
    case 6:
    	// Результаты тест
		for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
			if((pSnsCfg->Sensor[chan].StateErr & ERROR_MASK_ST_1) ||
					(pSnsCfg->Config_common.StateErr & ERROR_MASK_ST_2) ||
					(ErrorCalibZeroCount[chan] != 0)){
//				SET_DANGER(1);
				state_calib = 0;
				chan = COUNT_CHAN;
			}
			else{
				state_calib = 1;
			}
		}
		LCD_ToWarm(pSnsCfg, 6, state_calib);
		break;
	case 7:
		//Картинка миракс
		LCD_ToWarm(pSnsCfg, 7, NOCHAN);
		break;
	default:
		// Заполнение буферной структуры данных
		//Картинка миракс
		LCD_ToWarm(pSnsCfg, 7, NOCHAN);
		break;
	}
}

//===========================================================================================================================
void DisplayIndication(void)
{
	  //===========================================================================================================================
	  // Проверка таймера для мигания в режимах калибровки
	  if(TEST_TIMER(TIMER_DISP_FLASHING)){
		  PARAM_FLASHING(flashing_calib, ~state_flashing_calib);
		  START_TIMER(TIMER_DISP_FLASHING, TIME_DISP_FLASH_CALIB);
	  }
	  // Таймер переключения ошибок на экране и мигание сегмента передачи
//	  if(TEST_TIMER_RTC(TIMER_RTC_DISP_FLASHING_ERR)){
//		  PARAM_FLASHING(0, ~state_flashing_calib);
//		  START_TIMER_RTC(TIMER_RTC_DISP_FLASHING_ERR, TIME_RTC_DISP_FLASHING_ERR);
//	  }
	  // Таймер мигания сегментов на экране
	  if(TEST_TIMER_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM)){
		  RESET_TIMER_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM);
		  state_flashing_system = (~state_flashing_system);
		  count_flashing_lora++;
		  if(count_flashing_lora > 3){
			  count_flashing_lora = 0;
			  state_flashing_lora = (~state_flashing_lora);
		  }
	  }
	  if((!TEST_TIMER_RUN_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM)) && (!TEST_TIMER_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM))){
		  START_TIMER_RTC(TIMER_RTC_DISP_FLASHING_SYSTEM, TIME_RTC_DISP_FLASHING_ERR);
	  }
	  //===========================================================================================================================
	  // Копируем рабочую структуру в вспомогательную
	  Cfg_structdef = Sns_Cfg_struct;
	  //------------------------------------------------
	  if((IS_STATE_SCREEN() == STATE_SCREEN_WARM) ||
			  (IS_STATE_SCREEN() == STATE_SCREEN_WARM_AUTO_CALIB) ||
			  (IS_STATE_SCREEN() == STATE_SCREEN_WARM_INFO_DEVICE)){
		  if(TEST_TIMER_RTC(TIMER_RTC_WARM)){
			  START_TIMER_RTC(TIMER_RTC_WARM, TIME_RTC_WARM);
			  Screen_status_flip(&Cfg_structdef, &state_flip);
		  }
	  }
	  //------------------------------------------------
	  LCD_Fill_Template(&Cfg_structdef);
	  //------------------------------------------------
	  // Перевод времени в другой формат и проверка на истечение интервалов времени
	  DisplayTimeConversion(&Cfg_structdef);
	  //------------------------------------------------
	  // Вывод на дисплей real time
	  DisplayTimeCurrent(&Cfg_structdef);
	  //------------------------------------------------
	  // Вывод на дисплей индикацию ошибок
	  DisplayErrIndication(&Sns_Cfg_struct);
	  //------------------------------------------------
	  // Вывод на дисплей заряд батареи
	  DisplayChargeIndication(&Cfg_structdef);
	  SET_BAT_CHARGE(0);
	  //------------------------------------------------
	  if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_DATA_EXCHANGE)){
		  SET_EXCHANGE(1);
	  }
	  //------------------------------------------------
	  if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND)){
		  SET_MUTE(1);
	  }
	  //------------------------------------------------
	  if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_BLE)){
		  SET_BLE(1);
	  }
	  //------------------------------------------------
#if CONFIG_GPS
	  if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_GPS)){
		  if(Gps_Receive_flag_navigation() == 1){
			  SET_GPS(1);
		  }
		  else{
			  SET_GPS(state_flashing_system);
		  }
	  }
#endif
	  //------------------------------------------------
#if CONFIG_LORA
	  if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA)){
		  if((get_state_start_flag_lora() == 1) && (get_flag_receive_res_tx_lora() == 1)){
			  SET_LORA(1);
		  }
		  else{
			  SET_LORA(state_flashing_system);
		  }
	  }
#endif
#if CONFIG_LORA_G
	  if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA)){
		  if((get_state_start_flag_lora_g() == 1) && (get_flag_receive_res_tx_lora_g() == 1)){
			  SET_LORA(1);
		  }
		  else{
			  SET_LORA(state_flashing_system);
		  }
	  }
#endif
	  LCD_GetActiveChannelsCount(&Cfg_structdef);
	  //===========================================================================================================================
	  for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
		  Cfg_structdef.Sensor[chan].channel = chan;
		  //------------------------------------------------
		  // Перевод единицы измерения у сенсоров
		  DisplayConversionUnitMasure(&Cfg_structdef.Sensor[chan]);
		  //------------------------------------------------
		  //Рассчет коэффициента перевода в отображаемую единицу измерения
		  conversion_factor[chan] = calculating_conversion_factor(&Cfg_structdef.Sensor[chan]);
	  }
	//===========================================================================================================================
	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
		switch(IS_STATE_SCREEN())
		{
		//===========================================================================================================================
		// Навигация по меню
		case STATE_SCREEN_MEASURE:
//			LCD_AboutTheDevice(&Cfg_structdef, 2, chan);
//			chan = COUNT_CHAN;
			LCD_float_measure(&Cfg_structdef.Sensor[chan], GetDisplayIndicationConc(chan), state_err, NOCHAN);
			break;
//		case STATE_SCREEN_MEASURE_TO_MENU:
//	        LCD_float_measure(&Cfg_structdef.Sensor[chan], GetDisplayIndicationConc(chan), state_err, NOCHAN);
//	        break;
//		{// Калибровка нуля всех каналов
//		case STATE_SCREEN_CALIB_ZERO_END_MULTI:
//			uint32_t calib_time = GetTick() - state_menu_str.calib.calib_time;
//			LCD_CalibrationZeroOrSpanMulti(&Cfg_structdef.Sensor[chan], GetDisplayIndicationConc(chan), state_menu_str.calib, ROLLER_MENU_SERVICE_CALIB_ZERO);
//			break;
//		}
//        {// Калибровки диапазона всех каналов
//		case STATE_SCREEN_CALIB_SPAN_END_MULTI:
//			uint32_t calib_time = GetTick() - state_menu_str.calib.calib_time;
//			LCD_CalibrationZeroOrSpanMulti(&Cfg_structdef.Sensor[chan], GetDisplayIndicationConc(chan), state_menu_str.calib, calib_time, ROLLER_MENU_SERVICE_CALIB_RANGE);
//			break;
//        }
		default:
			goto next_iteration;
			break;
		}
	}
	goto update_and_next;

	next_iteration:
	switch(IS_STATE_SCREEN())
	{
		case STATE_SCREEN_MEASURE_TO_COUNTDOWN:
			CountdownToShutdown();
			break;
		case STATE_SCREEN_COUNTDOWN_IMIT:
			CountdownToShutdownImit();
			break;
		//===========================================================================================================================

		//===========================================================================================================================
		{// Меню по второму короткому нажатию
		case STATE_SCREEN_MENU:
			LCD_Menu(state_menu_str.state_menu);
			break;
		case STATE_SCREEN_PASSWORD:
#if (CONFIG_MODEL_LCD == 0)
#elif (CONFIG_MODEL_LCD == 1)
			if(state_menu_str.state_set_input == 0){
				PARAM_FLASHING(0, 0xFF);
			}
#endif
			if(state_menu_str.state_menu == 1){
				LCD_Digit_int_flashing_password(Check_password_num(PASSWORD_SETTING_NUM), Check_password_rank(), state_flashing_calib);
			}
			else if(state_menu_str.state_menu == 2){
				LCD_Digit_int_flashing_password(Check_password_num(PASSWORD_CALIB_NUM), Check_password_rank(), state_flashing_calib);
			}
			break;
		case STATE_SCREEN_INFO_DEVICE:
			LCD_SubMenuInfo(&Cfg_structdef, state_menu_str.state_submenu);
			break;
		case STATE_SCREEN_SETTING_DEVICE:
			LCD_SubMenuSetting(&Cfg_structdef, state_menu_str.state_submenu);
			break;
		case STATE_SCREEN_SERVICE_DEVICE:
			LCD_SubMenuService(&Cfg_structdef, state_menu_str.state_submenu);
			break;
		}
		//===========================================================================================================================
		{// Меню STATE_SCREEN_SETTING_DEVICE
		case STATE_SCREEN_SETTING_CHAN:
			LCD_SubMenuSettingChan(&Cfg_structdef, state_menu_str.state_submenu1, ROLLER_MENU_SETTING_LIMIT, NULL);
			break;
		case STATE_SCREEN_SETTING_DATA:
			LCD_SubMenuSettingSet(&Cfg_structdef, 0, 0, state_menu_str.state_submenu1, CheckRankDisplayParameterInput(), state_flashing_calib, 0, 0.0, gmtime(&tmp_current));
			break;
		case STATE_SCREEN_SETTING_RESET_LIMIT:
			LCD_SubMenuSetting_Panel_Head_Button_1(0, state_menu_str.state_submenu1, ROLLER_MENU_SETTING_RESET_STH);
			break;
		case STATE_SCREEN_SETTING_LANGUAGE:
			LCD_SubMenuSetting_Panel_Head_Button_1(0, state_menu_str.state_submenu1, ROLLER_MENU_SETTING_LANG);
			break;
		}
		{
		case STATE_SCREEN_SETTING_CHAN_SET:
			LCD_SubMenuSettingSet(&Cfg_structdef, 1, 0, state_menu_str.state_submenu2, CheckRankDisplayParameterInput(), state_flashing_calib, ChanOne, 0.0, gmtime(&tmp_current));
			break;
		case STATE_SCREEN_SETTING_CHAN_SET_P:
#if (CONFIG_MODEL_LCD == 0)
#elif (CONFIG_MODEL_LCD == 1)
			if(state_menu_str.state_set_input == 0){
				PARAM_FLASHING(0, 0xFF);
			}
#endif
			LCD_SubMenuSettingSet(&Cfg_structdef, 1, 1, state_menu_str.state_submenu2, CheckRankDisplayParameterInput(), state_flashing_calib, ChanOne, GetDisplayParameterInput(ChanOne), gmtime(&tmp_current));
			break;
		case STATE_SCREEN_SETTING_CHAN_SET_M_D:
#if (CONFIG_MODEL_LCD == 0)
#elif (CONFIG_MODEL_LCD == 1)
			if(state_menu_str.state_set_input == 0){
				PARAM_FLASHING(0, 0xFF);
			}
#endif
			LCD_SubMenuSettingSet(&Cfg_structdef, 1, 1, state_menu_str.state_submenu2, state_menu_str.state_submenu3, state_flashing_calib, ChanOne, GetDisplayDeviceSettingInput(), gmtime(&tmp_current));
			break;
		}
		{// Меню выбора и установки даты и времени
		case STATE_SCREEN_SETTING_DATA_SET:
		case STATE_SCREEN_SETTING_TIME_SET:
		{
#if (CONFIG_MODEL_LCD == 0)
#elif (CONFIG_MODEL_LCD == 1)
			if(state_menu_str.state_set_input == 0){
				PARAM_FLASHING(0, 0xFF);
			}
#endif
			time_t tmp_setting_data = Get_data_time();
			LCD_SubMenuSettingSet(&Cfg_structdef, 0, 1, state_menu_str.state_submenu1, CheckRankDisplayParameterInput(), state_flashing_calib, 0, 0.0, gmtime(&tmp_setting_data));
			break;
		}
		}
//		{
//		case STATE_SCREEN_SETTING_MEAS_CHAN_SET:
//			LCD_SubMenuSettingSet(&Cfg_structdef, 2, 0, state_menu_str.state_submenu2, CheckRankDisplayParameterInput(), state_flashing_calib, ChanOne, 0.0, gmtime(&tmp_current));
//			break;
//		}
		//===========================================================================================================================
		{// Меню STATE_SCREEN_SERVICE_DEVICE
		case STATE_SCREEN_CALIB_ZERO:
			LCD_SubMenuSetting_Panel_Head_Button_1(0, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_ZERO);
			break;
		case STATE_SCREEN_CALIB_SPAN:
			LCD_SubMenuSetting_Panel_Head_Button_1(0, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_RANGE);
			break;
		case STATE_SCREEN_SERVICE_RESET_ERR:
			LCD_SubMenuSetting_Panel_Head_Button_1(0, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_RESTART);
			break;
		}
		//========================================================================
		{// Калибровка нуля одного канала
			case STATE_SCREEN_CALIB_ZERO_SELECT_CHAN:
				LCD_SubMenuSettingChan(&Cfg_structdef, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_ZERO, NULL);
				break;
			case STATE_SCREEN_CALIB_ZERO_START:
				LCD_SubMenuSetting_Panel_Head_Button_2(1, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_ZERO);
				break;
			case STATE_SCREEN_CALIB_ZERO_END:
			{
				uint32_t calib_time = GetTick() - state_menu_str.calib.calib_time;
				LCD_CalibrationZeroOrSpan(&Cfg_structdef.Sensor[ChanOne], &state_menu_str.calib, calib_time, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_ZERO);
				break;
			}

			{// Калибровка нуля всех каналов
				case STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI:
					LCD_SubMenuSettingChan(&Cfg_structdef, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_ZERO, &state_menu_str.calib);
					break;
				case STATE_SCREEN_CALIB_ZERO_START_MULTI:
					LCD_SubMenuSetting_Panel_Head_Button_2(1, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_ZERO);
					break;
				case STATE_SCREEN_CALIB_ZERO_END_MULTI:
				{
					uint32_t calib_time = GetTick() - state_menu_str.calib.calib_time;
					LCD_CalibrationZeroOrSpanMulti(&Cfg_structdef, &state_menu_str.calib, calib_time, ROLLER_MENU_SERVICE_CALIB_ZERO);
					break;
				}
				case STATE_SCREEN_CALIB_ZERO_END_MULTI_ACK:
					LCD_SubMenuSetting_Panel_Head_Button_3(1, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_ZERO);
					break;
			}
		}
        //====================================================================
        {// Калибровки диапазона одного канала
			case STATE_SCREEN_CALIB_SPAN_SELECT_CHAN:
				LCD_SubMenuSettingChan(&Cfg_structdef, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_RANGE, NULL);
				break;
			case STATE_SCREEN_CALIB_SPAN_CONC_P:
#if (CONFIG_MODEL_LCD == 0)
#elif (CONFIG_MODEL_LCD == 1)
			if(state_menu_str.state_set_input == 0){
				PARAM_FLASHING(0, 0xFF);
			}
#endif
				LCD_SubMenuSettingSetCalib(&Cfg_structdef, GetDisplayParameterInput(ChanOne), ChanOne, CheckRankDisplayParameterInput(), state_flashing_calib, ROLLER_MENU_SERVICE_CALIB_RANGE);
				break;
			case STATE_SCREEN_CALIB_SPAN_START:
				LCD_SubMenuSetting_Panel_Head_Button_2(1, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_RANGE);
				break;
			case STATE_SCREEN_CALIB_SPAN_END:
			{
				uint32_t calib_time = GetTick() - state_menu_str.calib.calib_time;
				LCD_CalibrationZeroOrSpan(&Cfg_structdef.Sensor[ChanOne], &state_menu_str.calib, calib_time, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_RANGE);
				break;
			}
        }
        {// Калибровки диапазона всех каналов
			case STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI:
				LCD_SubMenuSettingChan(&Cfg_structdef, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_RANGE, &state_menu_str.calib);
				break;
			case STATE_SCREEN_CALIB_SPAN_START_MULTI:
				LCD_SubMenuSetting_Panel_Head_Button_2(1, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_RANGE);
				break;
			case STATE_SCREEN_CALIB_SPAN_END_MULTI:
			{
				uint32_t calib_time = GetTick() - state_menu_str.calib.calib_time;
				LCD_CalibrationZeroOrSpanMulti(&Cfg_structdef, &state_menu_str.calib, calib_time, ROLLER_MENU_SERVICE_CALIB_RANGE);
				break;
			}
			case STATE_SCREEN_CALIB_SPAN_END_MULTI_ACK:
				LCD_SubMenuSetting_Panel_Head_Button_3(1, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_CALIB_RANGE);
				break;
        }
        //========================================================================
        {// Подтверждение сброса сенсоров или возврата на заводские настройки
	        case STATE_SCREEN_SERVICE_DEVICE_END:
	        	LCD_SubMenuSetting_Panel_Head_Text(1, state_menu_str.state_submenu1, ROLLER_MENU_SERVICE_RESTART);
	        	uint32_t calib_time = GetTick() - state_menu_str.calib.calib_time;
	        	if(calib_time >= TIMER_SERVICE_DEVICE_END){
	        		TO_GO_STATE_SCREEN(STATE_SCREEN_SERVICE_DEVICE);
	        	}
	        	break;
        }
        //========================================================================
        {// BUMP TEST
        case STATE_SCREEN_BUMP_TEST:
        	LCD_BumpTest(&Cfg_structdef, state_menu_str.state_submenu, state_flashing_calib);
          break;
//        case STATE_SCREEN_BUMP_TEST_START:
//        	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
//        		LCD_float_measure(&Cfg_structdef.Sensor[chan], GetDisplayIndicationConc(chan), 1, NOCHAN);
//        	}
////        	SET_BALLOON;
//        	if(chan >= (COUNT_CHAN-1)){
//				if(!TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BUMP_TEST_RUN)){
//					TO_GO_STATE_SCREEN(STATE_SCREEN_BUMP_TEST_END);
//				}
//			}
//        	break;
//        case STATE_SCREEN_BUMP_TEST_END:
//        	LCD_BumpTestStatus(&Cfg_structdef, state_menu_str.state_submenu, state_flashing_calib);
//        	chan = COUNT_CHAN;
//        	break;
        }
		//===========================================================================================================================
		{//Прогрев
		case STATE_SCREEN_WARM_AUTO_CALIB:
		case STATE_SCREEN_WARM_INFO_DEVICE:
		case STATE_SCREEN_WARM:
			Warm_Screen_statuses(&Cfg_structdef, &state_flip);
			break;
		}
		//===========================================================================================================================
//		{// Firmware
//		case STATE_SCREEN_FIRMWARE:
//			LCD_Firmware();
//			break;
//		}
		default:
			break;
	}

	update_and_next:
	//------------------------------------------------------------------------
	if(StateProg == 0){
		LCD_Firmware();
	}
	ui_apply_visibility();
	LCD_UpdateScreen();
}

//===========================================================================================================================
void Button_Menu(void)
{
	Button_1_Menu();
#if (CONFIG_MODEL_LCD == 0)
	Button_2_Menu();
	Button_3_Menu();
#endif
	if(IS_STATE_SCREEN() == STATE_SCREEN_MEASURE){
		if(state_menu_str.dirty_data) {
			SaveConfig = 1;
			state_menu_str.dirty_data = 0; // Сброс флага
		}
	}
}

//===========================================================================================================================
#if (CONFIG_MODEL_LCD == 0)
void Button_1_Menu(void)
{
//	if(button.button_count){
		//----------------------------------------------------------------------
		SET_STATE_BUTTON1(STATE_BUTTON_CLR);
		if(button.button_count >= BUTTON_PRESS_LONG){
//#if CONFIG_FREERTOS
//	        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
//#else
	        uint32_t now = GetTick();
//#endif
	        if((now - button.last_short_click_time < TIME_DOUBLE_CLICK) && (button.last_short_click_time != 0)){
	            SET_STATE_BUTTON1(STATE_BUTTON_SHORT_LONG);
	        }else{
	            SET_STATE_BUTTON1(STATE_BUTTON_LONG);
	        }
	        button.button_flag = 0;
	        button.last_short_click_time = 0;
		}
		else if(button.button_count > BUTTON_PRESS_SHORT){
			if(!button.button_flag){
//#if CONFIG_FREERTOS
//				uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
//#else
				uint32_t now = GetTick();
//#endif
		        if((now - button.last_short_click_time) < TIME_DOUBLE_CLICK){
		            SET_STATE_BUTTON1(STATE_BUTTON_DOUBLE);
		            button.last_short_click_time = 0;
		        }else{
		            SET_STATE_BUTTON1(STATE_BUTTON_SHORT);
		            button.last_short_click_time = now;
		        }
			}
		}
		else if((button.button_count > 0) && !button.button_flag){
			SET_STATE_BUTTON1(STATE_BUTTON_FAKE);
		}
		if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG) ||
				TEST_STATE_BUTTON1(STATE_BUTTON_SHORT) ||
				TEST_STATE_BUTTON1(STATE_BUTTON_DOUBLE) ||
				TEST_STATE_BUTTON1(STATE_BUTTON_SHORT_LONG) ||
				TEST_STATE_BUTTON1(STATE_BUTTON_FAKE))
		{
			START_TIMER_RTC(TIMER_RTC_MAIN_RESET, TIME_RTC_MAIN_RESET);// Запуск таймера для возвращения на главный экран
			button.button_count = 0;
		}

		//----------------------------------------------------------------------
		switch(IS_STATE_SCREEN())
		{
		//===========================================================================================================================
		{// Навигация по меню
		case STATE_SCREEN_MEASURE:
			  if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
//				  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE_TO_MENU);                 // Переключение экрана
//				  START_TIMER(TIMER_MAIN_SCREEN, TIME_MAIN_SCREEN);            		// Запуск таймера для возвращения на главный экран
			  }
			  else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
	#if CONFIG_CHARGE
				  if((!TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_TURN_OFF)) ||
						  (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE) && TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_TURN_OFF)))
	#endif
				  {
					  START_TIMER(TIMER_SHUTDOWN, TIME_SHUTDOWN);                		// Запуск таймера на обратный отсчет
					  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE_TO_COUNTDOWN);            // Переключение экрана
				  }
			  }
			  else if(TEST_STATE_BUTTON1(STATE_BUTTON_DOUBLE)){
				  TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);                  	// Переключение экрана
				  state_menu_str.state_menu = 0;
				  START_TIMER(TIMER_DISP_FLASHING, TIME_DISP_FLASH_CALIB);
			  }
			  else if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT_LONG)){

			  }
			break;
//		case STATE_SCREEN_MEASURE_TO_MENU:
//			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
//				TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);                  	// Переключение экрана
//				state_menu_str.state_menu = 0;
//				RESET_TIMER(TIMER_MAIN_SCREEN);                             // Сброс таймера для возвращения на главный экран
//				START_TIMER(TIMER_DISP_FLASHING, TIME_DISP_FLASH_CALIB);
//			}
//			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
//				TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);                  	// Переключение экрана
//#if CONFIG_CHARGE
//				if((!TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_TURN_OFF)) ||
//						(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE) && TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_TURN_OFF)))
//#endif
//				{
//					START_TIMER(TIMER_SHUTDOWN, TIME_SHUTDOWN);                		// Запуск таймера на обратный отсчет
//					TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE_TO_COUNTDOWN);            // Переключение экрана
//				}
//				RESET_TIMER(TIMER_MAIN_SCREEN);                            	// Сброс таймера для возвращения на главный экран
//			}
//			break;
		}
		//===========================================================================================================================

		//===========================================================================================================================
		{// Меню по второму короткому нажатию state_menu
			case STATE_SCREEN_MENU:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu = 0;
					TO_GO_STATE_SCREEN(STATE_SCREEN_PASSWORD);
					if(state_menu_str.state_menu == 0){
						TO_GO_STATE_SCREEN(STATE_SCREEN_INFO_DEVICE);
					}
					else if(state_menu_str.state_menu == 1){
						Init_password(PASSWORD_SETTING_NUM);
						if(Check_password_num(PASSWORD_SETTING_NUM) == PASSWORD_SETTING){// Настройка
							TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DEVICE);
						}
					}
					else if(state_menu_str.state_menu == 2){
						Init_password(PASSWORD_CALIB_NUM);
						if(Check_password_num(PASSWORD_CALIB_NUM) == PASSWORD_CALIB){// Сервисное меню
							TO_GO_STATE_SCREEN(STATE_SCREEN_SERVICE_DEVICE);
						}
					}
					else if(state_menu_str.state_menu == 3){
						TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);                  	// Переключение экрана
						RESET_TIMER(TIMER_DISP_FLASHING);
					}
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);                  	// Переключение экрана
					RESET_TIMER(TIMER_DISP_FLASHING);
				}
				break;
			case STATE_SCREEN_PASSWORD:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu = 0;
					if((Check_password_rank() >= 2) && (Check_password_rank() <= 4)){
						if(state_menu_str.state_menu == 1){
							Enter_password();
						}
						else if(state_menu_str.state_menu == 2){
							Enter_password();
						}
					}
					else if(Check_password_rank() == 5){
						TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
						if(state_menu_str.state_menu == 1){	// Настройка
							if(Check_password_num(PASSWORD_SETTING_NUM) == PASSWORD_SETTING){
								TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DEVICE);
							}
						}
						else if(state_menu_str.state_menu == 2){	// Сервисное меню
							if(Check_password_num(PASSWORD_CALIB_NUM) == PASSWORD_CALIB){
								TO_GO_STATE_SCREEN(STATE_SCREEN_SERVICE_DEVICE);
							}
						}
					}
					else if(Check_password_rank() == 6){
						TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
					}
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){

				}
				break;
		}
		//===========================================================================================================================
		{// Меню инфо state_submenu
			case STATE_SCREEN_INFO_DEVICE:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
				}
				break;
		}
		//===========================================================================================================================
		{// Меню настройки state_submenu
		case STATE_SCREEN_SETTING_DEVICE:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
			    const uint8_t setting_screens[] = {
			        STATE_SCREEN_MENU,                // 0
			        STATE_SCREEN_SETTING_CHAN,         // 1
			        STATE_SCREEN_SETTING_DATA,         // 2
			        STATE_SCREEN_SETTING_RESET_LIMIT,  // 3
			        STATE_SCREEN_SETTING_LANGUAGE,     // 4
			    };
			    uint8_t sub = state_menu_str.state_submenu;
			    state_menu_str.state_submenu1 = 0;
				if(sub <= 4){
					TO_GO_STATE_SCREEN(setting_screens[sub]);
				}
				else if(sub == 5){
					if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_PUMP)){
						CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_PUMP);
					}
					else{
						SET_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_PUMP);
					}
//					state_menu_str.dirty_data = 1;
				}
			}
			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
				TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
			}
			break;
		}
		//===========================================================================================================================
		{// Меню сервис state_submenu
		case STATE_SCREEN_SERVICE_DEVICE:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
			    const uint8_t setting_screens[] = {
			        STATE_SCREEN_MENU,                // 0
					STATE_SCREEN_CALIB_ZERO,         // 1
					STATE_SCREEN_CALIB_SPAN,         // 2
					STATE_SCREEN_SERVICE_RESET_ERR,  // 3
			    };
			    const uint32_t feature_bits[] = {
			        ST_COMMON_BIT_TURN_ON_BLE,  // для sub 4 (индекс 0)
			        ST_COMMON_BIT_TURN_ON_LORA, // для sub 5 (индекс 1)
			        ST_COMMON_BIT_TURN_ON_GPS   // для sub 6 (индекс 2)
			    };
			    uint8_t sub = state_menu_str.state_submenu;
			    state_menu_str.state_submenu1 = 0;
				if(sub <= 3){
					TO_GO_STATE_SCREEN(setting_screens[sub]);
					if((sub == 1) || (sub == 2)){
						if(Get_Max_Count_Chan_Calib() == 0){
							TO_GO_STATE_SCREEN(STATE_SCREEN_SERVICE_DEVICE);
						}
					}
				}
				else if (sub >= 4 && sub <= 6) {
				    // Универсальное переключение бита
				    uint32_t bit = feature_bits[sub - 4];
				    if (TEST_STATUS_COMMON_BIT(bit)){
				        CLEAR_STATUS_COMMON_BIT(bit);
				    }
				    else{
				        SET_STATUS_COMMON_BIT(bit);
				    }
				    state_menu_str.dirty_data = 1;
				}
			}
			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
				TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
			}
			break;
		}
		//===========================================================================================================================
		{// Меню STATE_SCREEN_SETTING_DEVICE state_submenu1
		case STATE_SCREEN_SETTING_CHAN:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
				const uint8_t setting_screens[] = {
					STATE_SCREEN_SETTING_DEVICE,           // 0
					STATE_SCREEN_SETTING_CHAN_SET,         // 1
				};
				uint8_t sub = state_menu_str.state_submenu1;
				state_menu_str.state_submenu2 = 0;
				if(sub == 0){
					TO_GO_STATE_SCREEN(setting_screens[sub]);
				}
				else if(sub <= 9){
					TO_GO_STATE_SCREEN(setting_screens[1]);
				}
				if(sub >= 1){
					ChanOne = active_channels_map[sub - 1];
				}
			}
			break;
		case STATE_SCREEN_SETTING_DATA:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
				const uint8_t setting_screens[] = {
					STATE_SCREEN_SETTING_DEVICE,                // 0
					STATE_SCREEN_SETTING_DATA_SET,         // 1
					STATE_SCREEN_SETTING_TIME_SET,         // 2
				};
				uint8_t sub = state_menu_str.state_submenu1;
				state_menu_str.state_submenu2 = 0;
				if(sub <= 2){
					TO_GO_STATE_SCREEN(setting_screens[sub]);
				}
				if(sub == 1){
					Init_Setting_data_time_rank(DISP_PARAM_INPUT_DATA_MAX - 1, DISP_PARAM_INPUT_DATA_MAX, 0);
				}
				else if(sub == 2){
					Init_Setting_data_time_rank(DISP_PARAM_INPUT_TIME_MAX - 1, DISP_PARAM_INPUT_TIME_MAX, DISP_PARAM_INPUT_DATA_MAX);
				}
			}
			break;
		case STATE_SCREEN_SETTING_RESET_LIMIT:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
				TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DEVICE);
				if(state_menu_str.state_submenu1 == 1){
					for(uint8_t chan = 0; chan < COUNT_CHAN; chan++){
						Sns_Cfg_struct.Sensor[chan].StateErr = 0;
						CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT_STEL, chan);
						RESET_TIMER_RTC_CH(TIMER_RTC_STEL, chan);
						clearCalculationTwa(chan);
						Sns_Cfg_struct.Sensor[chan].ConcentrationValHigh = 0;
						START_TIMER_RTC(TIMER_RTC_RESET_ERR, 2);
//						state_menu_str.dirty_data = 1;
					}
				}
			}
			break;
		case STATE_SCREEN_SETTING_LANGUAGE:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
				if(state_menu_str.state_submenu1 == 0){
					TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DEVICE);
				}
				else if(state_menu_str.state_submenu1 == 1){
					APPLANG = RUS;
					state_menu_str.dirty_data = 1;
				}
				else if(state_menu_str.state_submenu1 == 2){
					APPLANG = EN;
					state_menu_str.dirty_data = 1;
				}
			}
			break;
//		case STATE_SCREEN_SETTING_MEAS_CHAN:
//			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
//				const uint8_t setting_screens[] = {
//					STATE_SCREEN_SETTING_DEVICE,           // 0
//					STATE_SCREEN_SETTING_MEAS_CHAN_SET,    // 1
//				};
//				uint8_t sub = state_menu_str.state_submenu1;
//				state_menu_str.state_submenu2 = 0;
//				if(sub == 0){
//					TO_GO_STATE_SCREEN(setting_screens[sub]);
//				}
//				else if(sub <= 9){
//					TO_GO_STATE_SCREEN(setting_screens[1]);
//				}
//				if(sub >= 1){
//					ChanOne = active_channels_map[sub - 1];
//				}
//			}
//			break;
		}
		{
			case STATE_SCREEN_SETTING_CHAN_SET:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					uint8_t sub = state_menu_str.state_submenu2;
					state_menu_str.state_submenu3 = 0;
					if(sub == 0){
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN);
					}
					else if(sub == 1){
						uint32_t val = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting) & MEASURE_MASK;
						InitDisplayDeviceSettingInput(val);
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET_M_D);
					}
					else if(sub == 2){
						uint32_t val = (((Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & MEASURE_DISK) >> OFFSET_DISK)) & (MEASURE_DISK >> OFFSET_DISK);
						InitDisplayDeviceSettingInput(val);
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET_M_D);
					}
					else{
						float *data_set = &Sns_Cfg_struct.Sensor[ChanOne].Prescaler_ConcCalibrGas;
						if(sub == 3){
							data_set = &Sns_Cfg_struct.Sensor[ChanOne].Limit[0];
						}
						else if(sub == 4){
							data_set = &Sns_Cfg_struct.Sensor[ChanOne].Limit[1];
						}
						else if(sub == 5){
							data_set = &Sns_Cfg_struct.Sensor[ChanOne].Limit[2];
						}
						if(InitRankDisplayParameterInput(data_set, ChanOne) == 1){
							TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET_P);
						}
					}
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					uint8_t sub = state_menu_str.state_submenu2;
					if(sub == 3){
						if(TEST_STATUS_BIT(ST_BIT_TYPE_LIMIT_1, ChanOne)){
							CLEAR_STATUS_BIT(ST_BIT_TYPE_LIMIT_1, ChanOne);
						}
						else{
							SET_STATUS_BIT(ST_BIT_TYPE_LIMIT_1, ChanOne);
						}
					}
					else if(sub == 4){
						if(TEST_STATUS_BIT(ST_BIT_TYPE_LIMIT_2, ChanOne)){
							CLEAR_STATUS_BIT(ST_BIT_TYPE_LIMIT_2, ChanOne);
						}
						else{
							SET_STATUS_BIT(ST_BIT_TYPE_LIMIT_2, ChanOne);
						}
					}
					else if(sub == 5){
						if(TEST_STATUS_BIT(ST_BIT_TYPE_LIMIT_3, ChanOne)){
							CLEAR_STATUS_BIT(ST_BIT_TYPE_LIMIT_3, ChanOne);
						}
						else{
							SET_STATUS_BIT(ST_BIT_TYPE_LIMIT_3, ChanOne);
						}
					}
				}
				break;
			case STATE_SCREEN_SETTING_CHAN_SET_M_D:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					uint8_t sub = state_menu_str.state_submenu3;
					uint8_t sub_pre = state_menu_str.state_submenu2;
					uint32_t val = (uint32_t)GetDisplayDeviceSettingInput();
					if(sub == 0){
						if(sub_pre == 1){
							val = (val + 1) & MEASURE_MASK;
						}
						else if(sub_pre == 2){
							val = ((((val << OFFSET_DISK) & MEASURE_DISK) >> OFFSET_DISK) + 1) & (MEASURE_DISK >> OFFSET_DISK);
						}
						InitDisplayDeviceSettingInput(val);
					}
					else if(sub == 1){
						val = (uint32_t)GetDisplayDeviceSettingInput();
						if(sub_pre == 1){
							Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & ~MEASURE_MASK) | val;
						}
						else if(sub_pre == 2){
							Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & ~MEASURE_DISK) | (val << OFFSET_DISK);
						}
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET);
						state_menu_str.dirty_data = 1;
					}
					else if(sub == 2){
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET);
					}
				}
				break;
			case STATE_SCREEN_SETTING_CHAN_SET_P:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					if(CheckRankDisplayParameterInput() == DISP_PARAM_INPUT_DEC_MAX){
						DisplayParameterInputSave(ChanOne);
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET);
						state_menu_str.dirty_data = 1;
					}
					else if(CheckRankDisplayParameterInput() == (DISP_PARAM_INPUT_DEC_MAX + 1)){
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET);
					}
					else{
						SetIncDisplayParameterInput();
					}
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET);
				}
				break;
		}
		{
			case STATE_SCREEN_SETTING_DATA_SET:
			case STATE_SCREEN_SETTING_TIME_SET:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					const uint8_t setting_param_input[] = {
						0,           // 0
						DISP_PARAM_INPUT_DATA_MAX,         // 1
						DISP_PARAM_INPUT_TIME_MAX,         // 2
					};
					uint8_t sub = state_menu_str.state_submenu1;
					if(CheckRankDisplayParameterInput() == (setting_param_input[sub] + 1)){
						Display_data_time_save();
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DATA);
						state_menu_str.dirty_data = 1;
					}
					else if(CheckRankDisplayParameterInput() == (setting_param_input[sub])){
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DATA);
					}
					else{
						struct tm my_tm;
						time_t tmp_setting_data = Get_data_time();
						my_tm = *localtime(&tmp_setting_data);
						Setting_data_time(&my_tm);
					}
				}
				break;
		}
//		{
//			case STATE_SCREEN_SETTING_MEAS_CHAN_SET:
//				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
//					uint8_t sub = state_menu_str.state_submenu2;
//					if(sub == 0){
//						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_MEAS_CHAN);
//					}
//					else if(sub == 1){
//						uint16_t val = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting + 1) & MEASURE_MASK;
//						Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & ~MEASURE_MASK) | val;
//						state_menu_str.dirty_data = 1;
//					}
//					else if(sub == 2){
//						uint16_t val = (((Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & MEASURE_DISK) >> OFFSET_DISK) + 1) & (MEASURE_DISK >> OFFSET_DISK);
//						Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & ~MEASURE_DISK) | (val << OFFSET_DISK);
//						state_menu_str.dirty_data = 1;
//					}
//				}
//				break;
//		}
		//===========================================================================================================================
		{// Меню STATE_SCREEN_SETTING_DEVICE state_submenu1
			//========================================================================
			{// Калибровка нуля одного канала
				case STATE_SCREEN_CALIB_ZERO:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           // 0
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN,   // 1
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub > 0){
							Init_Start_Calib(ST_BIT_AUTO_ZERO);
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_SELECT_CHAN:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           // 0
							STATE_SCREEN_CALIB_ZERO_START,         // 1
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub == 0){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						else if(sub <= 9){
							TO_GO_STATE_SCREEN(setting_screens[1]);
						}
						if(sub >= 1){
							ChanOne = active_channels_map[sub - 1];
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_START:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,         // 0
							STATE_SCREEN_CALIB_ZERO_END,         // 1
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							Set_Start_Calib(ChanOne, true);
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_END:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,         // 0
							STATE_SCREEN_CALIB_ZERO_END,         // 1
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							Set_Save_Calib();
						}
						else{
							Set_Exit_Calib();
						}
					}
					break;
			}
			//========================================================================
			{// Калибровка нуля всех каналов
				case STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           // 0
							STATE_SCREEN_CALIB_ZERO_START,         // 1
						};
						uint8_t sub = state_menu_str.state_submenu1;
						if(sub <= 1){
							state_menu_str.state_submenu1 = 0;
							TO_GO_STATE_SCREEN(setting_screens[sub]);
							if(sub == 1){
								if(state_menu_str.calib.calib_flag_disp == 0){
									TO_GO_STATE_SCREEN(setting_screens[0]);
								}
							}
						}
						if(sub >= 2){
							ChanOne = active_channels_map[sub - 2];
							uint8_t state = !TESTBIT(state_menu_str.calib.calib_flag_disp, ChanOne);
							Set_Start_Calib(ChanOne, state);
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_START_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           	// 0
							STATE_SCREEN_CALIB_ZERO_END_MULTI,      // 1
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 1){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							state_menu_str.calib.calib_time = GetTick();
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_END_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = 0;
						if(Get_Calib_Flag_State()){
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_ZERO);
						}
						else{
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_ZERO_END_MULTI_ACK);
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_END_MULTI_ACK:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           	// 0
							STATE_SCREEN_CALIB_ZERO_END_MULTI,      // 1
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 0){
							Set_Exit_Calib();
						}
						else if(sub == 1){
							Set_Save_Calib();
						}
					}
					break;
			}
	        //====================================================================
	        {// Калибровки диапазона одного канала
				case STATE_SCREEN_CALIB_SPAN:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           	// 0
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,    // 1
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub > 0){
							Init_Start_Calib(ST_BIT_AUTO_SPAN);
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_SELECT_CHAN:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           // 0
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,   // 1
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub == 0){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						else if(sub <= 9){
							TO_GO_STATE_SCREEN(setting_screens[1]);
						}
						if(sub >= 1){
							ChanOne = active_channels_map[sub - 1];
							float *data_set = &Sns_Cfg_struct.Sensor[ChanOne].Prescaler_ConcCalibrGas;
							if(InitRankDisplayParameterInput(data_set, ChanOne) == 1){
								TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_SPAN_CONC_P);
							}
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_CONC_P:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						if(CheckRankDisplayParameterInput() == DISP_PARAM_INPUT_DEC_MAX){
							DisplayParameterInputSave(ChanOne);
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_SPAN_START);
							state_menu_str.dirty_data = 1;
						}
						else if(CheckRankDisplayParameterInput() == (DISP_PARAM_INPUT_DEC_MAX + 1)){
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_SPAN_SELECT_CHAN);
						}
						else{
							SetIncDisplayParameterInput();
						}
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						TO_GO_STATE_SCREEN(STATE_SCREEN_SERVICE_DEVICE);
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_START:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,         // 0
							STATE_SCREEN_CALIB_SPAN_END,         // 1
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,         // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							Set_Start_Calib(ChanOne, true);
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_END:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,         // 0
							STATE_SCREEN_CALIB_SPAN_END,         // 1
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,         // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							Set_Save_Calib();
						}
						else{
							Set_Exit_Calib();
						}
					}
					break;
	        }
			//========================================================================
			{// Калибровка нуля всех каналов
				case STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           // 0
							STATE_SCREEN_CALIB_SPAN_START_MULTI,   // 1
						};
						uint8_t sub = state_menu_str.state_submenu1;
						if(sub <= 1){
							state_menu_str.state_submenu1 = 0;
							TO_GO_STATE_SCREEN(setting_screens[sub]);
							if(sub == 1){
								if(state_menu_str.calib.calib_flag_disp == 0){
									TO_GO_STATE_SCREEN(setting_screens[0]);
								}
							}
						}
						if(sub >= 2){
							ChanOne = active_channels_map[sub - 2];
							uint8_t state = !TESTBIT(state_menu_str.calib.calib_flag_disp, ChanOne);
							Set_Start_Calib(ChanOne, state);
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_START_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           	// 0
							STATE_SCREEN_CALIB_SPAN_END_MULTI,      // 1
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							state_menu_str.calib.calib_time = GetTick();
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_END_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = 0;
						if(Get_Calib_Flag_State()){
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_SPAN);
						}
						else{
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_SPAN_END_MULTI_ACK);
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_END_MULTI_ACK:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           	// 0
							STATE_SCREEN_CALIB_SPAN_END_MULTI,      // 1
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI,      // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 0){
							Set_Exit_Calib();
						}
						else if(sub == 1){
							Set_Save_Calib();
						}
					}
					break;
			}
			//========================================================================
	        case STATE_SCREEN_SERVICE_RESET_ERR:
	        	if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					const uint8_t setting_screens[] = {
						STATE_SCREEN_SERVICE_DEVICE,    // 0
						STATE_SCREEN_SERVICE_DEVICE_END,   	// 1
						STATE_SCREEN_SERVICE_DEVICE_END,    // 2
					};
					uint8_t sub = state_menu_str.state_submenu1;
					if(sub <= 2){
						TO_GO_STATE_SCREEN(setting_screens[sub]);
					}
					state_menu_str.calib.calib_time = GetTick();
					if(sub == 1){
						Sns_Cfg_struct.Config_common.StateErr = 0;
						for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
							ReinitSensor(&Sns_Cfg_struct.Sensor[chan_r], chan_r);
						}
					}
					else if(sub == 2){
						ReadConfigDefault = 1;
					}
	        	}
			break;
	        case STATE_SCREEN_SERVICE_DEVICE_END:
	        	if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){

	        	}
	        	break;
		}
			//====================================================================
			{// BUMP TEST
			case STATE_SCREEN_BUMP_TEST:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					TO_GO_STATE_SCREEN(STATE_SCREEN_BUMP_TEST);
					state_menu_str.state_submenu ++;
					if(state_menu_str.state_submenu >= 3){
						state_menu_str.state_submenu = 0;
					}
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					if(state_menu_str.state_submenu == 0){
						for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
							if(Check_status_before_action(chan)){
								SET_STATUS_COMMON_BIT(ST_COMMON_BIT_BUMP_TEST_RUN);
								START_TIMER_RTC(TIMER_RTC_BUMP_TEST_L, TIME_RTC_BUMP_TEST_L);
								TO_GO_STATE_SCREEN(STATE_SCREEN_BUMP_TEST_START);
							}
						}
					}
					else{
						TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
					}
				}
				break;
			  case STATE_SCREEN_BUMP_TEST_START:
				  if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){}                				//Переключение экрана
				  else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					  CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_BUMP_TEST_RUN);
					  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);                   		//Переключение экрана
				  }
				  break;
			  case STATE_SCREEN_BUMP_TEST_END:
				  if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);                   		//Переключение экрана
				  }
				  break;
			}
			//====================================================================

		//===========================================================================================================================
		{// Прогрев
		case STATE_SCREEN_WARM_AUTO_CALIB:
		case STATE_SCREEN_WARM_INFO_DEVICE:
		case STATE_SCREEN_WARM:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT))
			{}
			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
#if CONFIG_CHARGE
				if((!TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_TURN_OFF)) ||
						(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE) && TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_TURN_OFF)))
#endif
				{
					START_TIMER(TIMER_SHUTDOWN, TIME_SHUTDOWN);
					TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE_TO_COUNTDOWN);
				}
			}
		  break;
		}
		//===========================================================================================================================
		default:
		break;
		}
//	}
//	else if(!button.button_flag && !button.button_count && !TEST_TIMER_RUN(TIMER_MAIN_SCREEN)){
//	  //CLEAR_STATUS_BIT(ST_BIT_BUTTON);
//	  //START_TIMER(TIMER_MAIN_SCREEN, 1)
//	}
}

//===========================================================================================================================
void Button_2_Menu(void)
{
	//----------------------------------------------------------------------
	SET_STATE_BUTTON2(STATE_BUTTON_CLR);
	if(button2.button_count >= BUTTON_PRESS_LONG){
		SET_STATE_BUTTON2(STATE_BUTTON_LONG);
		button2.button_flag = 0;
	}
	else if(button2.button_count > BUTTON_PRESS_SHORT){
		if(!button2.button_flag){
			SET_STATE_BUTTON2(STATE_BUTTON_SHORT);
		}
	}
	else if((button2.button_count > 0) && !button2.button_flag){
		SET_STATE_BUTTON2(STATE_BUTTON_FAKE);
	}
	if(TEST_STATE_BUTTON2(STATE_BUTTON_LONG) ||
			TEST_STATE_BUTTON2(STATE_BUTTON_SHORT) ||
			TEST_STATE_BUTTON2(STATE_BUTTON_FAKE))
	{
		button2.button_count = 0;
		START_TIMER_RTC(TIMER_RTC_MAIN_RESET, TIME_RTC_MAIN_RESET);// Запуск таймера для возвращения на главный экран
	}

	//----------------------------------------------------------------------
	switch(IS_STATE_SCREEN())
	{
	//===========================================================================================================================
	{// Навигация по меню
	case STATE_SCREEN_MEASURE:

		break;
	case STATE_SCREEN_MEASURE_TO_MENU:

		break;
	}
	//===========================================================================================================================

	//===========================================================================================================================
	{// Меню по второму короткому нажатию
		case STATE_SCREEN_MENU:
			if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
				state_menu_str.state_menu = scroll_down(state_menu_str.state_menu, 4);
			}
			break;
		case STATE_SCREEN_PASSWORD:
			if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
				Inc_password(2);
			}
			else if(TEST_STATE_BUTTON2(STATE_BUTTON_LONG)){

			}
			break;
	}
	//===========================================================================================================================
	{// Меню инфо
		case STATE_SCREEN_INFO_DEVICE:
			if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu = scroll_down(state_menu_str.state_submenu, info_total_pages);
			}
			else if(TEST_STATE_BUTTON2(STATE_BUTTON_LONG)){
//				TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
			}
			break;
	}
	//===========================================================================================================================
	{// Меню настройки
	case STATE_SCREEN_SETTING_DEVICE:
		if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu = scroll_down(state_menu_str.state_submenu, 6);
		}
		else if(TEST_STATE_BUTTON2(STATE_BUTTON_LONG)){
//			TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
		}
		break;
	}
	//===========================================================================================================================
	{// Меню сервис
	case STATE_SCREEN_SERVICE_DEVICE:
		if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu = scroll_down(state_menu_str.state_submenu, 7);
		}
		else if(TEST_STATE_BUTTON2(STATE_BUTTON_LONG)){
//			TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
		}
		break;
	}
	//===========================================================================================================================
	{// Меню STATE_SCREEN_SETTING_DEVICE
	case STATE_SCREEN_SETTING_CHAN:
		if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, active_channels_count + 1);
		}
		break;
	case STATE_SCREEN_SETTING_DATA:
		if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
		}
		break;
	case STATE_SCREEN_SETTING_RESET_LIMIT:
		if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
		}
		break;
	case STATE_SCREEN_SETTING_LANGUAGE:
		if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
		}
		break;
	}
	{
		case STATE_SCREEN_SETTING_CHAN_SET:
			if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu2 = scroll_down(state_menu_str.state_submenu2, 6);
			}
			break;
		case STATE_SCREEN_SETTING_CHAN_SET_M_D:
			if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu3 = scroll_down(state_menu_str.state_submenu3, 3);
			}
			break;
		case STATE_SCREEN_SETTING_CHAN_SET_P:
			if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
				SwitchingRankDecDisplayParameterInput();
			}
			break;
	}
	{
		case STATE_SCREEN_SETTING_DATA_SET:
		case STATE_SCREEN_SETTING_TIME_SET:
			if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
				SwitchingRankIncDisplayParameterInput();
			}
			break;
	}
//	{
//		case STATE_SCREEN_SETTING_MEAS_CHAN_SET:
//			if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
//				state_menu_str.state_submenu2 = scroll_down(state_menu_str.state_submenu2, 3);
//			}
//			break;
//	}
	//===========================================================================================================================
	{// Меню STATE_SCREEN_SETTING_DEVICE state_submenu1
		//========================================================================
		{// Калибровка нуля одного канала
			case STATE_SCREEN_CALIB_ZERO:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_SELECT_CHAN:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, active_channels_count + 1);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_START:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_END:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
			}
		//========================================================================
		{// Калибровка нуля всех каналов
			case STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, active_channels_count + 2);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_START_MULTI:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_END_MULTI:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
//					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_END_MULTI_ACK:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
		}
        //====================================================================
        {// Калибровки диапазона одного канала
			case STATE_SCREEN_CALIB_SPAN:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_SELECT_CHAN:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, active_channels_count + 1);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_CONC_P:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					SwitchingRankDecDisplayParameterInput();
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_START:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_END:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
        }
		//========================================================================
		{// Калибровка нуля всех каналов
			case STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, active_channels_count + 2);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_START_MULTI:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_END_MULTI:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
//					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_END_MULTI_ACK:
				if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
				}
				break;
		}
		//========================================================================
        case STATE_SCREEN_SERVICE_RESET_ERR:
			if(TEST_STATE_BUTTON2(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu1 = scroll_down(state_menu_str.state_submenu1, 3);
			}
		break;
	}

		//====================================================================
		{// BUMP TEST
		case STATE_SCREEN_BUMP_TEST:

			break;
		  case STATE_SCREEN_BUMP_TEST_START:

			  break;
		  case STATE_SCREEN_BUMP_TEST_END:

			  break;
		}
		//====================================================================

	//===========================================================================================================================
	{// Прогрев
	case STATE_SCREEN_WARM:

	  break;
	}
	//===========================================================================================================================
	default:
	break;
	}
}

//===========================================================================================================================
void Button_3_Menu(void)
{
	//----------------------------------------------------------------------
	SET_STATE_BUTTON3(STATE_BUTTON_CLR);
	if(button3.button_count >= BUTTON_PRESS_LONG){
		SET_STATE_BUTTON3(STATE_BUTTON_LONG);
		button3.button_flag = 0;
	}
	else if(button3.button_count > BUTTON_PRESS_SHORT){
		if(!button3.button_flag){
			SET_STATE_BUTTON3(STATE_BUTTON_SHORT);
		}
	}
	else if((button3.button_count > 0) && !button3.button_flag){
		SET_STATE_BUTTON3(STATE_BUTTON_FAKE);
	}
	if(TEST_STATE_BUTTON3(STATE_BUTTON_LONG) ||
			TEST_STATE_BUTTON3(STATE_BUTTON_SHORT) ||
			TEST_STATE_BUTTON3(STATE_BUTTON_FAKE))
	{
		button3.button_count = 0;
		START_TIMER_RTC(TIMER_RTC_MAIN_RESET, TIME_RTC_MAIN_RESET);// Запуск таймера для возвращения на главный экран
	}

	//----------------------------------------------------------------------
	switch(IS_STATE_SCREEN())
	{
	//===========================================================================================================================
	{// Навигация по меню
	case STATE_SCREEN_MEASURE:

		break;
	case STATE_SCREEN_MEASURE_TO_MENU:

		break;
	}
	//===========================================================================================================================

	//===========================================================================================================================
	{// Меню по второму короткому нажатию
		case STATE_SCREEN_MENU:
			if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
				state_menu_str.state_menu = scroll_up(state_menu_str.state_menu, 4);
			}
			break;
		case STATE_SCREEN_PASSWORD:
			if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
				Inc_password(1);
			}
			else if(TEST_STATE_BUTTON3(STATE_BUTTON_LONG)){

			}
			break;
	}
	//===========================================================================================================================
	{// Меню инфо
		case STATE_SCREEN_INFO_DEVICE:
			if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu = scroll_up(state_menu_str.state_submenu, info_total_pages);
			}
			else if(TEST_STATE_BUTTON3(STATE_BUTTON_LONG)){
//				TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
			}
			break;
	}
	//===========================================================================================================================
	{// Меню настройки
	case STATE_SCREEN_SETTING_DEVICE:
		if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu = scroll_up(state_menu_str.state_submenu, 6);
		}
		else if(TEST_STATE_BUTTON3(STATE_BUTTON_LONG)){
//			TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
		}
		break;
	}
	//===========================================================================================================================
	{// Меню сервис
	case STATE_SCREEN_SERVICE_DEVICE:
		if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu = scroll_up(state_menu_str.state_submenu, 7);
		}
		else if(TEST_STATE_BUTTON3(STATE_BUTTON_LONG)){
//			TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
		}
		break;
	}
	//===========================================================================================================================
	{// Меню STATE_SCREEN_SETTING_DEVICE
	case STATE_SCREEN_SETTING_CHAN:
		if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, active_channels_count + 1);
		}
		break;
	case STATE_SCREEN_SETTING_DATA:
		if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
		}
		break;
	case STATE_SCREEN_SETTING_RESET_LIMIT:
		if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
		}
		break;
	case STATE_SCREEN_SETTING_LANGUAGE:
		if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
			state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
		}
		break;
	}
	{
		case STATE_SCREEN_SETTING_CHAN_SET:
			if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu2 = scroll_up(state_menu_str.state_submenu2, 6);
			}
			break;
		case STATE_SCREEN_SETTING_CHAN_SET_M_D:
			if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu3 = scroll_up(state_menu_str.state_submenu3, 3);
			}
			break;
		case STATE_SCREEN_SETTING_CHAN_SET_P:
			if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
				SwitchingRankIncDisplayParameterInput();
			}
			break;
	}
	{
		case STATE_SCREEN_SETTING_DATA_SET:
		case STATE_SCREEN_SETTING_TIME_SET:
			if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
				SwitchingRankDecDisplayParameterInput();
			}
			break;
	}
//	{
//		case STATE_SCREEN_SETTING_MEAS_CHAN_SET:
//			if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
//				state_menu_str.state_submenu2 = scroll_up(state_menu_str.state_submenu2, 3);
//			}
//			break;
//	}
	//===========================================================================================================================
	{// Меню STATE_SCREEN_SETTING_DEVICE state_submenu1
		//========================================================================
		{// Калибровка нуля одного канала
			case STATE_SCREEN_CALIB_ZERO:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_SELECT_CHAN:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, active_channels_count + 1);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_START:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_END:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
		}
		//========================================================================
		{// Калибровка нуля всех каналов
			case STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, active_channels_count + 2);
				}
			break;
			case STATE_SCREEN_CALIB_ZERO_START_MULTI:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_END_MULTI:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
//					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_ZERO_END_MULTI_ACK:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
		}
        //====================================================================
        {// Калибровки диапазона одного канала
			case STATE_SCREEN_CALIB_SPAN:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_SELECT_CHAN:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, active_channels_count + 1);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_CONC_P:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					SwitchingRankIncDisplayParameterInput();
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_START:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_END:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
        }
		//========================================================================
		{// Калибровка нуля всех каналов
			case STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, active_channels_count + 2);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_START_MULTI:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_END_MULTI:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
//					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
			case STATE_SCREEN_CALIB_SPAN_END_MULTI_ACK:
				if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				break;
		}
		//========================================================================
        case STATE_SCREEN_SERVICE_RESET_ERR:
			if(TEST_STATE_BUTTON3(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
			}
		break;
	}
		//====================================================================
		{// BUMP TEST
		case STATE_SCREEN_BUMP_TEST:

			break;
		  case STATE_SCREEN_BUMP_TEST_START:

			  break;
		  case STATE_SCREEN_BUMP_TEST_END:

			  break;
		}
		//====================================================================

	//===========================================================================================================================
	{// Прогрев
	case STATE_SCREEN_WARM:

	  break;
	}
	//===========================================================================================================================
	default:
	break;
	}
}

#elif (CONFIG_MODEL_LCD == 1)
void Button_1_Menu(void)
{
//	if(button.button_count){
		//----------------------------------------------------------------------
		SET_STATE_BUTTON1(STATE_BUTTON_CLR);
		if(button.button_count >= BUTTON_PRESS_LONG){
//#if CONFIG_FREERTOS
//	        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
//#else
	        uint32_t now = GetTick();
//#endif
	        if((now - button.last_short_click_time < TIME_DOUBLE_CLICK) && (button.last_short_click_time != 0)){
	            SET_STATE_BUTTON1(STATE_BUTTON_SHORT_LONG);
	        }else{
	            SET_STATE_BUTTON1(STATE_BUTTON_LONG);
	        }
	        button.button_flag = 0;
	        button.last_short_click_time = 0;
		}
		else if(button.button_count > BUTTON_PRESS_SHORT){
			if(!button.button_flag){
//#if CONFIG_FREERTOS
//				uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
//#else
				uint32_t now = GetTick();
//#endif
		        if((now - button.last_short_click_time) < TIME_DOUBLE_CLICK){
		            SET_STATE_BUTTON1(STATE_BUTTON_DOUBLE);
		            button.last_short_click_time = 0;
		        }else{
		            SET_STATE_BUTTON1(STATE_BUTTON_SHORT);
		            button.last_short_click_time = now;
		        }
			}
		}
		else if((button.button_count > 0) && !button.button_flag){
			SET_STATE_BUTTON1(STATE_BUTTON_FAKE);
		}
		if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG) ||
				TEST_STATE_BUTTON1(STATE_BUTTON_SHORT) ||
				TEST_STATE_BUTTON1(STATE_BUTTON_DOUBLE) ||
				TEST_STATE_BUTTON1(STATE_BUTTON_SHORT_LONG) ||
				TEST_STATE_BUTTON1(STATE_BUTTON_FAKE))
		{
			START_TIMER_RTC(TIMER_RTC_MAIN_RESET, TIME_RTC_MAIN_RESET);// Запуск таймера для возвращения на главный экран
			button.button_count = 0;
		}

		//----------------------------------------------------------------------
		switch(IS_STATE_SCREEN())
		{
		//===========================================================================================================================
		{// Навигация по меню
		case STATE_SCREEN_MEASURE:
			  if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
//				  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE_TO_MENU);                 // Переключение экрана
//				  START_TIMER(TIMER_MAIN_SCREEN, TIME_MAIN_SCREEN);            		// Запуск таймера для возвращения на главный экран
			  }
			  else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
	#if CONFIG_CHARGE
				  if((!TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_TURN_OFF)) ||
						  (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE) && TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_TURN_OFF)))
	#endif
				  {
					  START_TIMER(TIMER_SHUTDOWN, TIME_SHUTDOWN);                		// Запуск таймера на обратный отсчет
					  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE_TO_COUNTDOWN);            // Переключение экрана
				  }
			  }
			  else if(TEST_STATE_BUTTON1(STATE_BUTTON_DOUBLE)){
				  TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);                  	// Переключение экрана
				  state_menu_str.state_menu = 0;
				  state_menu_str.state_set_input = 0;
				  START_TIMER(TIMER_DISP_FLASHING, TIME_DISP_FLASH_CALIB);
			  }
			  else if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT_LONG)){

			  }
			break;

		}
		//===========================================================================================================================

		//===========================================================================================================================
		{// Меню по второму короткому нажатию state_menu
			case STATE_SCREEN_MENU:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					state_menu_str.state_menu = scroll_up(state_menu_str.state_menu, 4);
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					state_menu_str.state_submenu = 0;
					TO_GO_STATE_SCREEN(STATE_SCREEN_PASSWORD);
					if(state_menu_str.state_menu == 0){
						TO_GO_STATE_SCREEN(STATE_SCREEN_INFO_DEVICE);
					}
					else if(state_menu_str.state_menu == 1){
						Init_password(PASSWORD_SETTING_NUM);
						if(Check_password_num(PASSWORD_SETTING_NUM) == PASSWORD_SETTING){// Настройка
							TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DEVICE);
						}
					}
					else if(state_menu_str.state_menu == 2){
						Init_password(PASSWORD_CALIB_NUM);
						if(Check_password_num(PASSWORD_CALIB_NUM) == PASSWORD_CALIB){// Сервисное меню
							TO_GO_STATE_SCREEN(STATE_SCREEN_SERVICE_DEVICE);
						}
					}
					else if(state_menu_str.state_menu == 3){
						TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);                  	// Переключение экрана
						RESET_TIMER(TIMER_DISP_FLASHING);
					}
				}
				break;
			case STATE_SCREEN_PASSWORD:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					if(state_menu_str.state_set_input == 0){
						Inc_password(1);
					}
					else{
						Enter_password();
					}
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					state_menu_str.state_submenu = 0;
					if(Check_password_rank() == 5){
						TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
						if(state_menu_str.state_menu == 1){	// Настройка
							if(Check_password_num(PASSWORD_SETTING_NUM) == PASSWORD_SETTING){
								TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DEVICE);
							}
						}
						else if(state_menu_str.state_menu == 2){	// Сервисное меню
							if(Check_password_num(PASSWORD_CALIB_NUM) == PASSWORD_CALIB){
								TO_GO_STATE_SCREEN(STATE_SCREEN_SERVICE_DEVICE);
							}
						}
					}
					else if(Check_password_rank() == 6){
						TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
					}
					else{
						if(state_menu_str.state_set_input == 0){
							state_menu_str.state_set_input = 1;
						}
						else{
							state_menu_str.state_set_input = 0;
						}
					}
				}
				break;
		}
		//===========================================================================================================================
		{// Меню инфо state_submenu
			case STATE_SCREEN_INFO_DEVICE:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu = scroll_up(state_menu_str.state_submenu, info_total_pages);
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
				}
				break;
		}
		//===========================================================================================================================
		{// Меню настройки state_submenu
		case STATE_SCREEN_SETTING_DEVICE:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu = scroll_up(state_menu_str.state_submenu, 5);
			}
			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
			    const uint8_t setting_screens[] = {
			        STATE_SCREEN_MENU,                // 0
			        STATE_SCREEN_SETTING_CHAN,        // 1
			        STATE_SCREEN_SETTING_DATA,        // 2
			        STATE_SCREEN_SETTING_RESET_LIMIT, // 3
			        STATE_SCREEN_SETTING_LANGUAGE,    // 4
			    };
			    uint8_t sub = state_menu_str.state_submenu;
			    state_menu_str.state_submenu1 = 0;
				if(sub <= 4){
					TO_GO_STATE_SCREEN(setting_screens[sub]);
				}
//				else if(sub == 6){
//					if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_PUMP)){
//						CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_PUMP);
//					}
//					else{
//						SET_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_PUMP);
//					}
////					state_menu_str.dirty_data = 1;
//				}
			}
			break;
		}
		//===========================================================================================================================
		{// Меню сервис state_submenu
		case STATE_SCREEN_SERVICE_DEVICE:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu = scroll_up(state_menu_str.state_submenu, 4);
			}
			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
			    const uint8_t setting_screens[] = {
			        STATE_SCREEN_MENU,                // 0
					STATE_SCREEN_CALIB_ZERO,         // 1
					STATE_SCREEN_CALIB_SPAN,         // 2
					STATE_SCREEN_SERVICE_RESET_ERR,  // 3
			    };
//			    const uint32_t feature_bits[] = {
//			        ST_COMMON_BIT_TURN_ON_BLE,  // для sub 4 (индекс 0)
//			        ST_COMMON_BIT_TURN_ON_LORA, // для sub 5 (индекс 1)
//			        ST_COMMON_BIT_TURN_ON_GPS   // для sub 6 (индекс 2)
//			    };
			    uint8_t sub = state_menu_str.state_submenu;
			    state_menu_str.state_submenu1 = 0;
				if(sub <= 3){
					TO_GO_STATE_SCREEN(setting_screens[sub]);
					if((sub == 1) || (sub == 2)){
						if(Get_Max_Count_Chan_Calib() == 0){
							TO_GO_STATE_SCREEN(STATE_SCREEN_SERVICE_DEVICE);
						}
					}
				}
//				else if (sub >= 4 && sub <= 6) {
//				    // Универсальное переключение бита
//				    uint32_t bit = feature_bits[sub - 4];
//				    if (TEST_STATUS_COMMON_BIT(bit)){
//				        CLEAR_STATUS_COMMON_BIT(bit);
//				    }
//				    else{
//				        SET_STATUS_COMMON_BIT(bit);
//				    }
//				    state_menu_str.dirty_data = 1;
//				}


			}
			break;
		}
		//===========================================================================================================================
		{// Меню STATE_SCREEN_SETTING_DEVICE state_submenu1
		case STATE_SCREEN_SETTING_CHAN:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, active_channels_count + 1);
			}
			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
				const uint8_t setting_screens[] = {
					STATE_SCREEN_SETTING_DEVICE,           // 0
					STATE_SCREEN_SETTING_CHAN_SET,         // 1
				};
				uint8_t sub = state_menu_str.state_submenu1;
				state_menu_str.state_submenu2 = 0;
				if(sub == 0){
					TO_GO_STATE_SCREEN(setting_screens[sub]);
				}
				else if(sub <= 9){
					TO_GO_STATE_SCREEN(setting_screens[1]);
				}
				if(sub >= 1){
					ChanOne = active_channels_map[sub - 1];
				}
			}
			break;
		case STATE_SCREEN_SETTING_DATA:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
			}
			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
				const uint8_t setting_screens[] = {
					STATE_SCREEN_SETTING_DEVICE,           // 0
					STATE_SCREEN_SETTING_DATA_SET,         // 1
					STATE_SCREEN_SETTING_TIME_SET,         // 2
				};
				uint8_t sub = state_menu_str.state_submenu1;
				state_menu_str.state_submenu2 = 0;
				if(sub <= 2){
					TO_GO_STATE_SCREEN(setting_screens[sub]);
				}
				if(sub == 1){
					Init_Setting_data_time_rank(DISP_PARAM_INPUT_DATA_MAX - 1, DISP_PARAM_INPUT_DATA_MAX, 0);
				}
				else if(sub == 2){
					Init_Setting_data_time_rank(DISP_PARAM_INPUT_TIME_MAX - 1, DISP_PARAM_INPUT_TIME_MAX, DISP_PARAM_INPUT_DATA_MAX);
				}
			}
			break;
		case STATE_SCREEN_SETTING_RESET_LIMIT:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
			}
			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
				TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DEVICE);
				if(state_menu_str.state_submenu1 == 1){
					for(uint8_t chan = 0; chan < COUNT_CHAN; chan++){
						Sns_Cfg_struct.Sensor[chan].StateErr = 0;
						CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT_STEL, chan);
						RESET_TIMER_RTC_CH(TIMER_RTC_STEL, chan);
						clearCalculationTwa(chan);
						Sns_Cfg_struct.Sensor[chan].ConcentrationValHigh = 0;
						START_TIMER_RTC(TIMER_RTC_RESET_ERR, 2);
//						state_menu_str.dirty_data = 1;
					}
				}
			}
			break;
		case STATE_SCREEN_SETTING_LANGUAGE:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
				state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
			}
			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
				if(state_menu_str.state_submenu1 == 0){
					TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DEVICE);
				}
				else if(state_menu_str.state_submenu1 == 1){
					APPLANG = RUS;
					state_menu_str.dirty_data = 1;
				}
				else if(state_menu_str.state_submenu1 == 2){
					APPLANG = EN;
					state_menu_str.dirty_data = 1;
				}
			}
			break;
		}
		{
			case STATE_SCREEN_SETTING_CHAN_SET:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu2 = scroll_up(state_menu_str.state_submenu2, 6);
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					uint8_t sub = state_menu_str.state_submenu2;
					state_menu_str.state_submenu3 = 0;
					state_menu_str.state_set_input = 0;
					if(sub == 0){
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN);
					}
					else if(sub == 1){
						uint32_t val = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting) & MEASURE_MASK;
						InitDisplayDeviceSettingInput(val);
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET_M_D);
					}
					else if(sub == 2){
						uint32_t val = (((Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & MEASURE_DISK) >> OFFSET_DISK)) & (MEASURE_DISK >> OFFSET_DISK);
						InitDisplayDeviceSettingInput(val);
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET_M_D);
					}
					else{
						float *data_set = &Sns_Cfg_struct.Sensor[ChanOne].Prescaler_ConcCalibrGas;
						if(sub == 3){
							data_set = &Sns_Cfg_struct.Sensor[ChanOne].Limit[0];
						}
						else if(sub == 4){
							data_set = &Sns_Cfg_struct.Sensor[ChanOne].Limit[1];
						}
						else if(sub == 5){
							data_set = &Sns_Cfg_struct.Sensor[ChanOne].Limit[2];
						}
						if(InitRankDisplayParameterInput(data_set, ChanOne) == 1){
							InitRankDisplayParameterInputLimit();
							TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET_P);
						}
					}
				}
//				else if(TEST_STATE_BUTTON1(STATE_BUTTON_DOUBLE)){
//					uint8_t sub = state_menu_str.state_submenu2;
//					if(sub == 3){
//						if(TEST_STATUS_BIT(ST_BIT_TYPE_LIMIT_1, ChanOne)){
//							CLEAR_STATUS_BIT(ST_BIT_TYPE_LIMIT_1, ChanOne);
//						}
//						else{
//							SET_STATUS_BIT(ST_BIT_TYPE_LIMIT_1, ChanOne);
//						}
//					}
//					else if(sub == 4){
//						if(TEST_STATUS_BIT(ST_BIT_TYPE_LIMIT_2, ChanOne)){
//							CLEAR_STATUS_BIT(ST_BIT_TYPE_LIMIT_2, ChanOne);
//						}
//						else{
//							SET_STATUS_BIT(ST_BIT_TYPE_LIMIT_2, ChanOne);
//						}
//					}
//					else if(sub == 5){
//						if(TEST_STATUS_BIT(ST_BIT_TYPE_LIMIT_3, ChanOne)){
//							CLEAR_STATUS_BIT(ST_BIT_TYPE_LIMIT_3, ChanOne);
//						}
//						else{
//							SET_STATUS_BIT(ST_BIT_TYPE_LIMIT_3, ChanOne);
//						}
//					}
//				}
				break;
			case STATE_SCREEN_SETTING_CHAN_SET_M_D:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu3 = scroll_up(state_menu_str.state_submenu3, 3);
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					uint8_t sub = state_menu_str.state_submenu3;
					uint8_t sub_pre = state_menu_str.state_submenu2;
					uint32_t val = (uint32_t)GetDisplayDeviceSettingInput();
					if(sub == 0){
						if(sub_pre == 1){
							val = (val + 1) & MEASURE_MASK;
						}
						else if(sub_pre == 2){
							val = ((((val << OFFSET_DISK) & MEASURE_DISK) >> OFFSET_DISK) + 1) & (MEASURE_DISK >> OFFSET_DISK);
						}
						InitDisplayDeviceSettingInput(val);
					}
					else if(sub == 1){
						val = (uint32_t)GetDisplayDeviceSettingInput();
						if(sub_pre == 1){
							Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & ~MEASURE_MASK) | val;
						}
						else if(sub_pre == 2){
							Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & ~MEASURE_DISK) | (val << OFFSET_DISK);
						}
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET);
						state_menu_str.dirty_data = 1;
					}
					else if(sub == 2){
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET);
					}
				}
				break;
			case STATE_SCREEN_SETTING_CHAN_SET_P:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					if(state_menu_str.state_set_input == 0){
						SwitchingRankIncDisplayParameterInput();
					}
					else{
						if(CheckRankDisplayParameterInput() == (DISP_PARAM_INPUT_DEC_MAX + 2)){
							uint8_t sub = state_menu_str.state_submenu2;
							uint8_t setting_type_limit = 0;
							if(sub == 3){
								setting_type_limit = ST_BIT_TYPE_LIMIT_1;
							}
							else if(sub == 4){
								setting_type_limit = ST_BIT_TYPE_LIMIT_2;
							}
							else if(sub == 5){
								setting_type_limit = ST_BIT_TYPE_LIMIT_3;
							}
							if((sub >= 3) && (sub <= 5)){
								if(TEST_STATUS_BIT(setting_type_limit, ChanOne)){
									CLEAR_STATUS_BIT(setting_type_limit, ChanOne);
								}
								else{
									SET_STATUS_BIT(setting_type_limit, ChanOne);
								}
							}
						}
						else{
							SetIncDisplayParameterInput();
						}
					}
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					if(CheckRankDisplayParameterInput() == DISP_PARAM_INPUT_DEC_MAX){
						DisplayParameterInputSave(ChanOne);
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET);
						state_menu_str.dirty_data = 1;
					}
					else if(CheckRankDisplayParameterInput() == (DISP_PARAM_INPUT_DEC_MAX + 1)){
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_CHAN_SET);
					}
					else{
						if(state_menu_str.state_set_input == 0){
							state_menu_str.state_set_input = 1;
						}
						else{
							state_menu_str.state_set_input = 0;
						}
					}
				}
				break;
		}
		{
			case STATE_SCREEN_SETTING_DATA_SET:
			case STATE_SCREEN_SETTING_TIME_SET:
				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					if(state_menu_str.state_set_input == 0){
						SwitchingRankDecDisplayParameterInput();
					}
					else{
						struct tm my_tm;
						time_t tmp_setting_data = Get_data_time();
						my_tm = *localtime(&tmp_setting_data);
						Setting_data_time(&my_tm);
					}
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					const uint8_t setting_param_input[] = {
						0,           // 0
						DISP_PARAM_INPUT_DATA_MAX,         // 1
						DISP_PARAM_INPUT_TIME_MAX,         // 2
					};
					uint8_t sub = state_menu_str.state_submenu1;
					if(CheckRankDisplayParameterInput() == (setting_param_input[sub] + 1)){
						Display_data_time_save();
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DATA);
						state_menu_str.dirty_data = 1;
					}
					else if(CheckRankDisplayParameterInput() == (setting_param_input[sub])){
						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_DATA);
					}
					else{
						if(state_menu_str.state_set_input == 0){
							state_menu_str.state_set_input = 1;
						}
						else{
							state_menu_str.state_set_input = 0;
						}
					}
				}
				break;
		}
//		{
//			case STATE_SCREEN_SETTING_MEAS_CHAN_SET:
//				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
//					state_menu_str.state_submenu2 = scroll_up(state_menu_str.state_submenu2, 3);
//				}
//				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
//					uint8_t sub = state_menu_str.state_submenu2;
//					if(sub == 0){
//						TO_GO_STATE_SCREEN(STATE_SCREEN_SETTING_MEAS_CHAN);
//					}
//					else if(sub == 1){
//						uint16_t val = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting + 1) & MEASURE_MASK;
//						Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & ~MEASURE_MASK) | val;
//						state_menu_str.dirty_data = 1;
//					}
//					else if(sub == 2){
//						uint16_t val = (((Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & MEASURE_DISK) >> OFFSET_DISK) + 1) & (MEASURE_DISK >> OFFSET_DISK);
//						Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting = (Sns_Cfg_struct.Sensor[ChanOne].DeviceSetting & ~MEASURE_DISK) | (val << OFFSET_DISK);
//						state_menu_str.dirty_data = 1;
//					}
//				}
//				break;
//		}
		//===========================================================================================================================
		{// Меню STATE_SCREEN_SETTING_DEVICE state_submenu1
			//========================================================================
			{// Калибровка нуля одного канала
				case STATE_SCREEN_CALIB_ZERO:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           // 0
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN,   // 1
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI,   // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub > 0){
							Init_Start_Calib(ST_BIT_AUTO_ZERO);
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_SELECT_CHAN:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, active_channels_count + 1);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_ZERO,           // 0
							STATE_SCREEN_CALIB_ZERO_START,     // 1
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub == 0){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						else if(sub <= 9){
							TO_GO_STATE_SCREEN(setting_screens[1]);
						}
						if(sub >= 1){
							ChanOne = active_channels_map[sub - 1];
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_START:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_ZERO,         	 // 0
							STATE_SCREEN_CALIB_ZERO_END,         // 1
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							Set_Start_Calib(ChanOne, true);
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_END:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_ZERO,         	 // 0
							STATE_SCREEN_CALIB_ZERO_END,         // 1
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							Set_Save_Calib();
						}
						else{
							Set_Exit_Calib();
						}
					}
					break;
			}
			//========================================================================
			{// Калибровка нуля всех каналов
				case STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, active_channels_count + 2);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_ZERO,           // 0
							STATE_SCREEN_CALIB_ZERO_START_MULTI,  // 1
						};
						uint8_t sub = state_menu_str.state_submenu1;
						if(sub <= 1){
							state_menu_str.state_submenu1 = 0;
							TO_GO_STATE_SCREEN(setting_screens[sub]);
							if(sub == 1){
								if(state_menu_str.calib.calib_flag_disp == 0){
									TO_GO_STATE_SCREEN(setting_screens[0]);
								}
							}
						}
						if(sub >= 2){
							ChanOne = active_channels_map[sub - 2];
							uint8_t state = !TESTBIT(state_menu_str.calib.calib_flag_disp, ChanOne);
							Set_Start_Calib(ChanOne, state);
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_START_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_ZERO,           	// 0
							STATE_SCREEN_CALIB_ZERO_END_MULTI,  // 1
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							state_menu_str.calib.calib_time = GetTick();
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_END_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						state_menu_str.state_submenu1 = 0;
						if(Get_Calib_Flag_State()){
//							Set_Start_Calib(ST_BIT_AUTO_ZERO, NOCHAN);
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_ZERO);
						}
						else{
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_ZERO_END_MULTI_ACK);
						}
					}
					break;
				case STATE_SCREEN_CALIB_ZERO_END_MULTI_ACK:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_ZERO,           	// 0
							STATE_SCREEN_CALIB_ZERO_END_MULTI,  // 1
							STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 0){
							Set_Exit_Calib();
						}
						else if(sub == 1){
							Set_Save_Calib();
						}
					}
					break;
			}
	        //====================================================================
	        {// Калибровки диапазона одного канала
				case STATE_SCREEN_CALIB_SPAN:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_SERVICE_DEVICE,           	// 0
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,    // 1
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI,    // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub > 0){
							Init_Start_Calib(ST_BIT_AUTO_SPAN);
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_SELECT_CHAN:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, active_channels_count + 1);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_SPAN,           		// 0
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,   	// 1
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub == 0){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						else if(sub <= 9){
							TO_GO_STATE_SCREEN(setting_screens[1]);
						}
						if(sub >= 1){
							ChanOne = active_channels_map[sub - 1];
							float *data_set = &Sns_Cfg_struct.Sensor[ChanOne].Prescaler_ConcCalibrGas;
							if(InitRankDisplayParameterInput(data_set, ChanOne) == 1){
								TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_SPAN_CONC_P);
							}
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_CONC_P:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						if(state_menu_str.state_set_input == 0){
							SwitchingRankIncDisplayParameterInput();
						}
						else{
							SetIncDisplayParameterInput();
						}
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						if(CheckRankDisplayParameterInput() == DISP_PARAM_INPUT_DEC_MAX){
							DisplayParameterInputSave(ChanOne);
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_SPAN_START);
							state_menu_str.dirty_data = 1;
						}
						else if(CheckRankDisplayParameterInput() == (DISP_PARAM_INPUT_DEC_MAX + 1)){
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_SPAN_SELECT_CHAN);
						}
						else{
							if(state_menu_str.state_set_input == 0){
								state_menu_str.state_set_input = 1;
							}
							else{
								state_menu_str.state_set_input = 0;
							}
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_START:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_SPAN,         		// 0
							STATE_SCREEN_CALIB_SPAN_END,         	// 1
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,    // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							Set_Start_Calib(ChanOne, true);
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_END:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_SPAN,         		// 0
							STATE_SCREEN_CALIB_SPAN_END,         	// 1
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,    // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							Set_Save_Calib();
						}
						else{
							Set_Exit_Calib();
						}
					}
					break;
	        }
			//========================================================================
			{// Калибровка нуля всех каналов
				case STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, active_channels_count + 2);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_SPAN,           // 0
							STATE_SCREEN_CALIB_SPAN_START_MULTI, // 1
						};
						uint8_t sub = state_menu_str.state_submenu1;
						if(sub <= 1){
							state_menu_str.state_submenu1 = 0;
							TO_GO_STATE_SCREEN(setting_screens[sub]);
							if(sub == 1){
								if(state_menu_str.calib.calib_flag_disp == 0){
									TO_GO_STATE_SCREEN(setting_screens[0]);
								}
							}
						}
						if(sub >= 2){
							ChanOne = active_channels_map[sub - 2];
							uint8_t state = !TESTBIT(state_menu_str.calib.calib_flag_disp, ChanOne);
							Set_Start_Calib(ChanOne, state);
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_START_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_SPAN,           	// 0
							STATE_SCREEN_CALIB_SPAN_END_MULTI,  // 1
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 1){
							state_menu_str.calib.calib_time = GetTick();
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_END_MULTI:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
//						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						state_menu_str.state_submenu1 = 0;
						if(Get_Calib_Flag_State()){
//							Set_Start_Calib(ST_BIT_AUTO_SPAN, NOCHAN);
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_SPAN);
						}
						else{
							TO_GO_STATE_SCREEN(STATE_SCREEN_CALIB_SPAN_END_MULTI_ACK);
						}
					}
					break;
				case STATE_SCREEN_CALIB_SPAN_END_MULTI_ACK:
					if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
						state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
					}
					else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
						const uint8_t setting_screens[] = {
							STATE_SCREEN_CALIB_SPAN,           		// 0
							STATE_SCREEN_CALIB_SPAN_END_MULTI,      // 1
							STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI, // 2
						};
						uint8_t sub = state_menu_str.state_submenu1;
						state_menu_str.state_submenu1 = 0;
						if(sub <= 2){
							TO_GO_STATE_SCREEN(setting_screens[sub]);
						}
						if(sub == 0){
							Set_Exit_Calib();
						}
						else if(sub == 1){
							Set_Save_Calib();
						}
					}
					break;
			}
			//========================================================================
	        case STATE_SCREEN_SERVICE_RESET_ERR:
	        	if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
					state_menu_str.state_submenu1 = scroll_up(state_menu_str.state_submenu1, 3);
				}
				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
					const uint8_t setting_screens[] = {
						STATE_SCREEN_SERVICE_DEVICE,    // 0
						STATE_SCREEN_SERVICE_DEVICE_END,   	// 1
						STATE_SCREEN_SERVICE_DEVICE_END,    // 2
					};
					uint8_t sub = state_menu_str.state_submenu1;
					state_menu_str.calib.calib_time = GetTick();
					if(sub <= 2){
						TO_GO_STATE_SCREEN(setting_screens[sub]);
					}
					if(sub == 1){
						Sns_Cfg_struct.Config_common.StateErr = 0;
						for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
							ReinitSensor(&Sns_Cfg_struct.Sensor[chan_r], chan_r);
						}
					}
					else if(sub == 2){
						ReadConfigDefault = 1;
					}
	        	}
			break;
	        case STATE_SCREEN_SERVICE_DEVICE_END:
	        	if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){

	        	}
	        	else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){

	        	}
	        	break;
		}
			//====================================================================
			{// BUMP TEST
			case STATE_SCREEN_BUMP_TEST:
//				if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
//					TO_GO_STATE_SCREEN(STATE_SCREEN_BUMP_TEST);
//					state_menu_str.state_submenu ++;
//					if(state_menu_str.state_submenu >= 3){
//						state_menu_str.state_submenu = 0;
//					}
//				}
//				else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
//					if(state_menu_str.state_submenu == 0){
//						for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
//							if(Check_status_before_action(chan)){
//								SET_STATUS_COMMON_BIT(ST_COMMON_BIT_BUMP_TEST_RUN);
//								START_TIMER_RTC(TIMER_RTC_BUMP_TEST_L, TIME_RTC_BUMP_TEST_L);
//								TO_GO_STATE_SCREEN(STATE_SCREEN_BUMP_TEST_START);
//							}
//						}
//					}
//					else{
//						TO_GO_STATE_SCREEN(STATE_SCREEN_MENU);
//					}
//				}
				break;
			  case STATE_SCREEN_BUMP_TEST_START:
//				  if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){}                				//Переключение экрана
//				  else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
//					  CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_BUMP_TEST_RUN);
//					  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);                   		//Переключение экрана
//				  }
				  break;
			  case STATE_SCREEN_BUMP_TEST_END:
//				  if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT)){
//					  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);                   		//Переключение экрана
//				  }
				  break;
			}
			//====================================================================

		//===========================================================================================================================
		{// Прогрев
		case STATE_SCREEN_WARM_AUTO_CALIB:
		case STATE_SCREEN_WARM_INFO_DEVICE:
		case STATE_SCREEN_WARM:
			if(TEST_STATE_BUTTON1(STATE_BUTTON_SHORT))
			{}
			else if(TEST_STATE_BUTTON1(STATE_BUTTON_LONG)){
#if CONFIG_CHARGE
				if((!TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_TURN_OFF)) ||
						(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE) && TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_TURN_OFF)))
#endif
				{
					START_TIMER(TIMER_SHUTDOWN, TIME_SHUTDOWN);
					TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE_TO_COUNTDOWN);
				}
			}
		  break;
		}
		//===========================================================================================================================
		default:
		break;
		}
//	}
//	else if(!button.button_flag && !button.button_count && !TEST_TIMER_RUN(TIMER_MAIN_SCREEN)){
//	  //CLEAR_STATUS_BIT(ST_BIT_BUTTON);
//	  //START_TIMER(TIMER_MAIN_SCREEN, 1)
//	}
}

#endif
//===========================================================================================================================
void Init_Start_Calib(uint16_t type_calib)
{
	memset(&state_menu_str.calib, 0, sizeof(CALIB_CFG));
	state_menu_str.calib.type_calib = type_calib;
//	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
//		if((chan_one == NOCHAN) || (chan_one == chan)){
//			if(Check_status_before_action(chan)){
//				Block_Sound(1);
//				Turn_FID_Sensor_To_Menu(chan, 1);
//				SETBIT(state_menu_str.calib.calib_flag_disp, chan);
//			}
//		}
//	}
//	state_menu_str.calib.calib_time = GetTick();
}

void Set_Start_Calib(uint8_t chan_one, uint8_t state)
{
	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
		if((chan_one == NOCHAN) || (chan_one == chan)){
			if(Check_status_before_action(chan)){
				Block_Sound(1);
				Turn_FID_Sensor_To_Menu(chan, 1);
				if(state == true){
					SETBIT(state_menu_str.calib.calib_flag_disp, chan);
				}
				else{
					CLRBIT(state_menu_str.calib.calib_flag_disp, chan);
				}
			}
		}
	}
	state_menu_str.calib.calib_time = GetTick();
}

void Set_Save_Calib(void)
{
	if(state_menu_str.calib.calib_flag_start == 1){
		return;
	}

	// Выполнить
	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
//		if((chan_one == NOCHAN) || (chan_one == chan)){
			if(Check_status_before_action(chan)){
				if(TESTBIT(state_menu_str.calib.calib_flag_disp, chan)){
					state_menu_str.calib.calib_flag_start = 1;
					state_menu_str.calib.calib_flag_state = 0;
					SETBIT(state_menu_str.calib.calib_flag, chan);
					CLEAR_STATUS_BIT(state_menu_str.calib.type_calib, chan);
				}
			}
			Turn_FID_Sensor_To_Menu(chan, 0);
//		}
	}
}

void Set_Exit_Calib(void)
{
	if(state_menu_str.calib.calib_flag_start == 1){
		return;
	}
	Block_Sound(0);
	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
		state_menu_str.calib.calib_flag_state = 0;
		CLRBIT(state_menu_str.calib.calib_flag_disp, chan);
		Turn_FID_Sensor_To_Menu(chan, 0);
	}
}

uint16_t Get_Calib_Flag_State(void)
{
	return state_menu_str.calib.calib_flag_state;
}

#endif
