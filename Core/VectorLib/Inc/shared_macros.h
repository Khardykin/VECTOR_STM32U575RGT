/**
  ******************************************************************************
  * @file    shared_macros.h
  * @brief   Общие макросы автора: маски ошибок/статусы и таймеры прибора
  *
  *          Блоки: маски ошибок в байтах состояния (ERROR_MASK_..., ERROR_BIT_...),
  *          битовые операции (SETBIT/CLRBIT/TESTBIT), общий статус прибора
  *          (SET/TEST/RESET_STATUS_COMMON_BIT - поле State SNS_CFG: блокировка
  *          звука, режим обмена, TURN_ON_BLE/LORA/GPS/GSM, ошибки модулей),
  *          таймеры прибора: миллисекундные START/TEST/RESET/END_TIMER
  *          (countdown_time[], обслуживание Timer_Tick_1ms() от TIM3 1 кГц)
  *          и секундные *_TIMER_RTC (countdown_time_rtc[], Timer_Tick_1s()
  *          от RTC wakeup 1 Гц). ID таймеров и экземпляры (volatile) -
  *          shared_types.h, обслуживание - Vector_main.c.
  ******************************************************************************
  */
#ifndef SHARED_MACROS_H
#define SHARED_MACROS_H
//--------------------------------------------------------------------------------------------------------------
#define TIME_RTC_LIGHT_RESET            (25)     		// Время сброса подсветки
//--------------------------------------------------------------------------------------------------------------
//Маски для ошибок в байте состояния----------------------------------------------------------------------------
#define ERROR_MASK_ST_1                 (0x03FF)
#define ERROR_MASK_ST_1_1       		(0x007F)
#define ERROR_COUNT_ST_1                (0x0A)
#define ERROR_BIT_ST_1                  (0x0001)

#define ERROR_MASK_ST_2               	(0x03FF)
#define ERROR_MASK_ST_2_1             	(0x03F8)
#define ERROR_COUNT_ST_2              	(0x0A)

#define ERROR_BIT_ST_2                  (0x0001)

//--------------------------------------------------------------------------------------------------------------
#define APPLANG		Sns_Cfg_struct.application_language
//===========================================================================================================================
#define SETBIT(var,bit)		        (var |= (1UL << (bit)))
#define CLRBIT(var,bit)		        (var &= ~(1UL << (bit)))
#define TESTBIT(var,bit)	        (var & (1UL << (bit)))
//--------------------------------------------------------------------------------------------------------------
#define SET_STATUS_COMMON_BIT(BIT)	{SETBIT(Sns_Cfg_struct.Config_common.State,BIT);}
#define CLEAR_STATUS_COMMON_BIT(BIT)	{CLRBIT(Sns_Cfg_struct.Config_common.State,BIT);}
#define TEST_STATUS_COMMON_BIT(BIT)	(TESTBIT(Sns_Cfg_struct.Config_common.State,BIT))
//--------------------------------------------------------------------------------------------------------------
#define SET_STATUS_COMMON_ERR_BIT(BIT)	{SETBIT(Sns_Cfg_struct.Config_common.StateErr,BIT);}
#define CLEAR_STATUS_COMMON_ERR_BIT(BIT){CLRBIT(Sns_Cfg_struct.Config_common.StateErr,BIT);}
#define TEST_STATUS_COMMON_ERR_BIT(BIT)	(TESTBIT(Sns_Cfg_struct.Config_common.StateErr,BIT))
//--------------------------------------------------------------------------------------------------------------
#define START_TIMER(TIMER, TIME)	{ countdown_time.Timers[TIMER] = TIME;	CLRBIT(countdown_time.TimerIsEnd, TIMER); }
#define RESET_TIMER(TIMER)	        { countdown_time.Timers[TIMER] = 0;	CLRBIT(countdown_time.TimerIsEnd, TIMER); }
#define TEST_TIMER(TIMER)			TESTBIT(countdown_time.TimerIsEnd, TIMER)
#define TEST_TIMER_RUN(TIMER)		(countdown_time.Timers[TIMER])
#define END_TIMER(TIMER)			{ SETBIT(countdown_time.TimerIsEnd, TIMER); }
//--------------------------------------------------------------------------------------------------------------
#define START_TIMER_RTC(TIMER, TIME)	{ countdown_time_rtc.Timers[TIMER] = TIME;	CLRBIT(countdown_time_rtc.TimerIsEnd, TIMER); }
#define RESET_TIMER_RTC(TIMER)	        { countdown_time_rtc.Timers[TIMER] = 0;	CLRBIT(countdown_time_rtc.TimerIsEnd, TIMER); }
#define TEST_TIMER_RTC(TIMER)			TESTBIT(countdown_time_rtc.TimerIsEnd, TIMER)
#define TEST_TIMER_RUN_RTC(TIMER)		(countdown_time_rtc.Timers[TIMER])
#define END_TIMER_RTC(TIMER)			{ SETBIT(countdown_time_rtc.TimerIsEnd, TIMER); }
//--------------------------------------------------------------------------------------------------------------
#define TO_GO_STATE_SCREEN(x)	(state_screen = x)
#define IS_STATE_SCREEN()		(state_screen)
#define SET_STATE_BUTTON(x)		(state_button = x)
#define TEST_STATE_BUTTON(x)	(state_button == x)

#define SET_STATE_BUTTON1(x)	(button.state_button = x)
#define TEST_STATE_BUTTON1(x)	(button.state_button == x)
#define SET_STATE_BUTTON2(x)	(button2.state_button = x)
#define TEST_STATE_BUTTON2(x)	(button2.state_button == x)
#define SET_STATE_BUTTON3(x)	(button3.state_button = x)
#define TEST_STATE_BUTTON3(x)	(button3.state_button == x)
//--------------------------------------------------------------------------------------------------------------
#define LOBYTE(w)			(((uint8_t*)(&w))[0])
#define HIBYTE(w)			(((uint8_t*)(&w))[1])

#define LOWORD(w)			(((uint16_t*)(&w))[0])
#define HIWORD(w)			(((uint16_t*)(&w))[1])

#define to_word(w)			((((uint8_t*)(&w))[1] << 8) + ((uint8_t*)(&w))[0])
//--------------------------------------------------------------------------------------------------------------

#endif /* SHARED_MACROS_H */
