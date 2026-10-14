//#include "rtc.h"
//#include "adc.h"
#include "Avis_main.h"
//===========================================================================================================================
static uint16_t receipt_limit = 0;			// Квитирование первого порога
static uint8_t 	button_block_sound_lim = 0;     // Блокировка звука при нажатие кнопки и если сенсор не кислород, сброс при сбросе порога в фун-ии LimitProcessing();
uint8_t 		button_block_sound_lora = 0;    // Блокировка звука при нажатие кнопки
//===========================================================================================================================
void ReinitSensor(SNS_CFG_Type *pSnsCfg, uint8_t chan)
{
	TurnOnPowerSensor(pSnsCfg, chan);
	SET_STATUS_BIT(ST_BIT_AUTO_SPAN, chan);
	SET_STATUS_BIT(ST_BIT_AUTO_ZERO, chan);
	ErrorClear(pSnsCfg);
}

void ErrorClear(SNS_CFG_Type *pSnsCfg)
{
	pSnsCfg->StateErr = 0;                 // Очистка статусного состояния канала
}

//===========================================================================================================================
void LimitClear(SNS_CFG_Type *pSnsCfg)
{
    RESET_TIMER_RTC_CH(TIMER_RTC_STEL, pSnsCfg->channel);
    RESET_TIMER_CH(TIMER_LIMIT1_SET, pSnsCfg->channel);
    RESET_TIMER_CH(TIMER_LIMIT2_SET, pSnsCfg->channel);
    RESET_TIMER_CH(TIMER_LIMIT3_SET, pSnsCfg->channel);
    RESET_TIMER_CH(TIMER_LIMIT1_CLR, pSnsCfg->channel);
    RESET_TIMER_CH(TIMER_LIMIT2_CLR, pSnsCfg->channel);
    RESET_TIMER_CH(TIMER_LIMIT3_CLR, pSnsCfg->channel);

    CLEAR_STATUS_ERR_BIT(ST_BIT_EXCEEDED_THE_RANGE, pSnsCfg->channel);
    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT_TWA, pSnsCfg->channel);
    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT_STEL, pSnsCfg->channel);
    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT1, pSnsCfg->channel);
    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT2, pSnsCfg->channel);
    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT3, pSnsCfg->channel);
}

//===========================================================================================================================
uint8_t LimitReceiptFlag(SNS_CFG_Type *pSnsCfg)
{
	uint8_t state_r = 0;
	if(TEST_STATUS_BIT(ST_BIT_RECEIPT_LIMIT, pSnsCfg->channel)){
		if(TESTBIT(receipt_limit, pSnsCfg->channel)){
			state_r = 1;
		}
	}
	else{
		state_r = 1;
	}
	CLRBIT(receipt_limit, pSnsCfg->channel);
	return state_r;
}

//===========================================================================================================================
void LimitReceiptStart(void)
{
	for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
		if((Sns_Cfg_struct.Sensor[chan_r].channel == chan_r)
				&& (TEST_STATUS_BIT(ST_BIT_RECEIPT_LIMIT, chan_r)) &&
				TEST_STATUS_ERR_BIT(ST_BIT_LIMIT1, chan_r)){
			SETBIT(receipt_limit, chan_r);
		}
		else{
			CLRBIT(receipt_limit, chan_r);
		}
	}
}

void BlockSoundLimitReceipt(void)
{
	static uint8_t ButtonCount = 0;
	if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT1) ||
			!TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT2) ||
			!TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT3) ||
			!TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_EXCEEDED_THE_RANGE)){
		if(ButtonCount == 0){
//			ButtonCount = 1;
//		}
//		else if(ButtonCount == 1){
//			ButtonCount = 0;
			LimitReceiptStart();
		}
	}
	else{
		ButtonCount = 0;
	}

	if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT1) && receipt_limit){
		receipt_limit = 0;
		button_block_sound_lim = 1;  //Блокировка звука при нажатие кнопки
	}

#if CONFIG_LORA
	if(TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_SOS) || TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_SOS_GROUP) ||
			TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_BACK_GROUP) || TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_GROUP))
	{
		button_block_sound_lora = 1;
	}
#endif
}

//===========================================================================================================================
void Block_Sound(uint8_t state)
{
	if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND_LIMIT_CALIB)){
		if(state){
			SET_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND);
		}
		else{
			CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND);
		}
	}
}

//===========================================================================================================================
//Проверка порогов, флагов состояние
void LimitProcessing(SNS_CFG_Type *pSnsCfg)
{
  float Concentration = pSnsCfg->ConcentrationVal;
  float ConcentrationTwa = pSnsCfg->ConcentrationValTwa;

  float Histerezis 	= pSnsCfg->HisterezisLimit;
  float Limit1       = pSnsCfg->Limit[0];
  float Limit2       = pSnsCfg->Limit[1];
  float Limit3       = pSnsCfg->Limit[2];
  float LimitRange   = pSnsCfg->Limit[3];
  float LimitStel    = pSnsCfg->Limit_Stel;
  float LimitTwa     = pSnsCfg->Limit_Twa;
  uint8_t TypeLimit1    = TEST_STATUS_BIT(ST_BIT_TYPE_LIMIT_1, pSnsCfg->channel)?TRUE:FALSE;
  uint8_t TypeLimit2    = TEST_STATUS_BIT(ST_BIT_TYPE_LIMIT_2, pSnsCfg->channel)?TRUE:FALSE;
  uint8_t TypeLimit3    = TEST_STATUS_BIT(ST_BIT_TYPE_LIMIT_3, pSnsCfg->channel)?TRUE:FALSE;
  uint8_t TypeLimitStel = 0;
  uint8_t FlagLimitClear = 1;//LimitReceiptFlag(pSnsCfg);
  //-------------------------------------------------------------------------------
  if(!TEST_STATUS_BIT(ST_BIT_WARM_SENSOR, pSnsCfg->channel)){
	  return;
  }
  //------------------------------------------------------------------------------- 
  // Обработка верхнего предела
  if(LimitRange){
    if((Concentration > LimitRange) && !TEST_STATUS_ERR_BIT(ST_BIT_EXCEEDED_THE_RANGE, pSnsCfg->channel))
    {
      SET_STATUS_ERR_BIT(ST_BIT_EXCEEDED_THE_RANGE, pSnsCfg->channel);
      END_TIMER_RTC(TIMER_RTC_LOG);                                             // Флаг старта записи лога
    }
    else if((Concentration <= LimitRange) && TEST_STATUS_ERR_BIT(ST_BIT_EXCEEDED_THE_RANGE, pSnsCfg->channel))
    {
    	if(FlagLimitClear){
    		CLEAR_STATUS_ERR_BIT(ST_BIT_EXCEEDED_THE_RANGE, pSnsCfg->channel);
    	}
    }
  }
  else{
    CLEAR_STATUS_ERR_BIT(ST_BIT_EXCEEDED_THE_RANGE, pSnsCfg->channel);
  }
  //-------------------------------------------------------------------------------   
//  if(TEST_STATUS_BIT(ST_BIT_BLOCK_LIMIT, pSnsCfg->channel)){    
//    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT1, pSnsCfg->channel);
//    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT2, pSnsCfg->channel);
//    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT3, pSnsCfg->channel);
//    return;
//  }
  //------------------------------------------------------------------------------- 
  // Обработка порога 1
  if(Limit1){
      if(TestLimit1_1() &&	                                                // Превышение порога 1
              !TEST_STATUS_ERR_BIT(ST_BIT_LIMIT1, pSnsCfg->channel) &&		// Порог 1 - выключен
              !TEST_TIMER_RUN_CH(TIMER_LIMIT1_SET, pSnsCfg->channel) && 		// Таймер на установку не запущен
              !TEST_TIMER_CH(TIMER_LIMIT1_SET, pSnsCfg->channel) )			// Нет события от таймера
      {
        if(Sns_Cfg_struct.Config_common.TimeLimit == 0){
          END_TIMER_CH(TIMER_LIMIT1_SET, pSnsCfg->channel)
        }
        else{
          START_TIMER_CH(TIMER_LIMIT1_SET, Sns_Cfg_struct.Config_common.TimeLimit, pSnsCfg->channel);
        }
        RESET_TIMER_CH(TIMER_LIMIT1_CLR, pSnsCfg->channel);
      } 
      else if(TestLimit1_2() &&	                                                // Ниже порога 1
              !TEST_STATUS_ERR_BIT(ST_BIT_LIMIT1, pSnsCfg->channel) &&		// Порог 1 не установлен
              TEST_TIMER_RUN_CH(TIMER_LIMIT1_SET, pSnsCfg->channel))		// Таймер на установку запущен
      {
        RESET_TIMER_CH(TIMER_LIMIT1_SET, pSnsCfg->channel);
      }
      else if(TestLimit1_2() &&	                                                // Ниже порога 1
              TEST_STATUS_ERR_BIT(ST_BIT_LIMIT1, pSnsCfg->channel) &&		// Установлен порог 1
              !TEST_TIMER_RUN_CH(TIMER_LIMIT1_CLR, pSnsCfg->channel) &&		// Таймер на сброс порога 1 не запущен
              !TEST_TIMER_CH(TIMER_LIMIT1_CLR, pSnsCfg->channel) )			// Нет события от таймера
      { // Ниже порога, порог 1 - включен
        // Если значения = 0, то устанавливаем флаг
        if(Sns_Cfg_struct.Config_common.TimeLimitRST == 0){
          END_TIMER_CH(TIMER_LIMIT1_CLR, pSnsCfg->channel)
        }
        else{
          START_TIMER_CH(TIMER_LIMIT1_CLR, Sns_Cfg_struct.Config_common.TimeLimitRST, pSnsCfg->channel);
        }
      }
  }
  else{
    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT1, pSnsCfg->channel);
  }
  //------------------------------------------------------------------------------- 
  // Обработка порога 2
  if(Limit2){
      if(TestLimit2_1() &&	                                                // Превышение порога 2
              !TEST_STATUS_ERR_BIT(ST_BIT_LIMIT2, pSnsCfg->channel) &&		// Порог 2 - выключен
              !TEST_TIMER_RUN_CH(TIMER_LIMIT2_SET, pSnsCfg->channel) && 		// Таймер на установку не запущен
              !TEST_TIMER_CH(TIMER_LIMIT2_SET, pSnsCfg->channel) )			// Нет события от таймера
      {
        if(Sns_Cfg_struct.Config_common.TimeLimit == 0){
          END_TIMER_CH(TIMER_LIMIT2_SET, pSnsCfg->channel)
        }
        else{
          START_TIMER_CH(TIMER_LIMIT2_SET, Sns_Cfg_struct.Config_common.TimeLimit, pSnsCfg->channel);
        }
        RESET_TIMER_CH(TIMER_LIMIT2_CLR, pSnsCfg->channel);
      } 
      else if(TestLimit2_2()&&	                                                // Ниже порога 2
              !TEST_STATUS_ERR_BIT(ST_BIT_LIMIT2, pSnsCfg->channel) &&		// Порог 2 не установлен
              TEST_TIMER_RUN_CH(TIMER_LIMIT2_SET, pSnsCfg->channel))		// Таймер на установку запущен
      {
        RESET_TIMER_CH(TIMER_LIMIT2_SET, pSnsCfg->channel);
      } 
      else if(TestLimit2_2() &&	                                                // Ниже порога 2
              TEST_STATUS_ERR_BIT(ST_BIT_LIMIT2, pSnsCfg->channel) &&		// Установлен порог 2
              !TEST_TIMER_RUN_CH(TIMER_LIMIT2_CLR, pSnsCfg->channel) &&		// Таймер на сброс порога 2 не запущен
              !TEST_TIMER_CH(TIMER_LIMIT2_CLR, pSnsCfg->channel) )			// Нет события от таймера
      { // Ниже порога, порог 2 - включен
        // Если значения = 0, то устанавливаем флаг
        if(Sns_Cfg_struct.Config_common.TimeLimitRST == 0){
          END_TIMER_CH(TIMER_LIMIT2_CLR, pSnsCfg->channel)
        }
        else{
          START_TIMER_CH(TIMER_LIMIT2_CLR, Sns_Cfg_struct.Config_common.TimeLimitRST, pSnsCfg->channel);
        }
      }
  }
  else{
    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT2, pSnsCfg->channel);
  }
  //------------------------------------------------------------------------------- 
  // Обработка порога 3
  if(Limit3){
      if(TestLimit3_1() &&	                                                // Превышение порога 3
              !TEST_STATUS_ERR_BIT(ST_BIT_LIMIT3, pSnsCfg->channel) &&		// Порог 3 - выключен
              !TEST_TIMER_RUN_CH(TIMER_LIMIT3_SET, pSnsCfg->channel) && 		// Таймер на установку не запущен
              !TEST_TIMER_CH(TIMER_LIMIT3_SET, pSnsCfg->channel) )			// Нет события от таймера
      {
        if(Sns_Cfg_struct.Config_common.TimeLimit == 0){
          END_TIMER_CH(TIMER_LIMIT3_SET, pSnsCfg->channel)
        }
        else{
          START_TIMER_CH(TIMER_LIMIT3_SET, Sns_Cfg_struct.Config_common.TimeLimit, pSnsCfg->channel);
        }
        RESET_TIMER_CH(TIMER_LIMIT3_CLR, pSnsCfg->channel);
      } 
      else if(TestLimit3_2()&&	                                                // Ниже порога 3
              !TEST_STATUS_ERR_BIT(ST_BIT_LIMIT3, pSnsCfg->channel) &&		// Порог 3 не установлен
              TEST_TIMER_RUN_CH(TIMER_LIMIT3_SET, pSnsCfg->channel))		// Таймер на установку запущен
      {
        RESET_TIMER_CH(TIMER_LIMIT3_SET, pSnsCfg->channel);
      } 
      else if(TestLimit3_2() &&	                                                // Ниже порога 3
              TEST_STATUS_ERR_BIT(ST_BIT_LIMIT3, pSnsCfg->channel) &&		// Установлен порог 3
              !TEST_TIMER_RUN_CH(TIMER_LIMIT3_CLR, pSnsCfg->channel) &&		// Таймер на сброс порога 3 не запущен
              !TEST_TIMER_CH(TIMER_LIMIT3_CLR, pSnsCfg->channel) )			// Нет события от таймера
      { // Ниже порога, порог 3 - включен
        // Если значения = 0, то устанавливаем флаг
        if(Sns_Cfg_struct.Config_common.TimeLimitRST == 0){
          END_TIMER_CH(TIMER_LIMIT3_CLR, pSnsCfg->channel)
        }
        else{
          START_TIMER_CH(TIMER_LIMIT3_CLR, Sns_Cfg_struct.Config_common.TimeLimitRST, pSnsCfg->channel);
        }
      }
  }
  else{
    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT3, pSnsCfg->channel);
  }
  //------------------------------------------------------------------------------- 
  // Обработка порога STEL
  if(LimitStel){
      if(TestLimitStel_1() &&	                                                // Превышение порога STEL
              !TEST_STATUS_ERR_BIT(ST_BIT_LIMIT_STEL, pSnsCfg->channel) &&		// Порог STEL - выключен
              !TEST_TIMER_RUN_RTC_CH(TIMER_RTC_STEL, pSnsCfg->channel) && 	// Таймер на установку не запущен
              !TEST_TIMER_RTC_CH(TIMER_RTC_STEL, pSnsCfg->channel) )		// Нет события от таймера
      {
        START_TIMER_RTC_CH(TIMER_RTC_STEL, (uint16_t)(pSnsCfg->Stel_interval*60/TIME_DEL_2), pSnsCfg->channel);
      } 
      else if(TestLimitStel_2()){	                                        // Ниже порога STEL
        RESET_TIMER_RTC_CH(TIMER_RTC_STEL, pSnsCfg->channel);
        CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT_STEL, pSnsCfg->channel);
      }
  }
  else{
    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT_STEL, pSnsCfg->channel);
  }
  //-------------------------------------------------------------------------------
  // Обработка TWA
  if(LimitTwa){
    if((ConcentrationTwa > LimitTwa) && !TEST_STATUS_ERR_BIT(ST_BIT_LIMIT_TWA, pSnsCfg->channel)){
      SET_STATUS_ERR_BIT(ST_BIT_LIMIT_TWA, pSnsCfg->channel);
      END_TIMER_RTC(TIMER_RTC_LOG);                                             // Флаг старта записи лога
    }
    else if((ConcentrationTwa <= LimitTwa) && TEST_STATUS_ERR_BIT(ST_BIT_LIMIT_TWA, pSnsCfg->channel)){
      CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT_TWA, pSnsCfg->channel);
    }
  }
  else{
    CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT_TWA, pSnsCfg->channel);
  }
  //------------------------------------------------------------------------------- 
  // Сработал таймер на превышение порога STEL
  if(TEST_TIMER_RTC_CH(TIMER_RTC_STEL, pSnsCfg->channel)){
    RESET_TIMER_RTC_CH(TIMER_RTC_STEL, pSnsCfg->channel);
    SET_STATUS_ERR_BIT(ST_BIT_LIMIT_STEL, pSnsCfg->channel);
    END_TIMER_RTC(TIMER_RTC_LOG);                                               // Флаг старта записи лога
  }
  // Сработал таймер на превышение порога 1
  if(TEST_TIMER_CH(TIMER_LIMIT1_SET, pSnsCfg->channel)){
    RESET_TIMER_CH(TIMER_LIMIT1_SET, pSnsCfg->channel);
    SET_STATUS_ERR_BIT(ST_BIT_LIMIT1, pSnsCfg->channel);
    END_TIMER_RTC(TIMER_RTC_LOG);                                               // Флаг старта записи лога
  }
  // Сработал таймер на превышение порога 2
  if(TEST_TIMER_CH(TIMER_LIMIT2_SET, pSnsCfg->channel)){
    RESET_TIMER_CH(TIMER_LIMIT2_SET, pSnsCfg->channel);
    SET_STATUS_ERR_BIT(ST_BIT_LIMIT2, pSnsCfg->channel);
    END_TIMER_RTC(TIMER_RTC_LOG);                                               // Флаг старта записи лога
  }
  // Сработал таймер на превышение порога 3
  if(TEST_TIMER_CH(TIMER_LIMIT3_SET, pSnsCfg->channel)){
    RESET_TIMER_CH(TIMER_LIMIT3_SET, pSnsCfg->channel);
    SET_STATUS_ERR_BIT(ST_BIT_LIMIT3, pSnsCfg->channel);
    END_TIMER_RTC(TIMER_RTC_LOG);                                               // Флаг старта записи лога
  }
  // Сработал таймер на сброс порога 1
  if(TEST_TIMER_CH(TIMER_LIMIT1_CLR, pSnsCfg->channel)){
	  RESET_TIMER_CH(TIMER_LIMIT1_CLR, pSnsCfg->channel);
    if(FlagLimitClear){
    	CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT1, pSnsCfg->channel);
    	button_block_sound_lim = 0;//сброс блокировки звука от нажатия кнопки
    }
  }
  // Сработал таймер на сброс порога 2
  if(TEST_TIMER_CH(TIMER_LIMIT2_CLR, pSnsCfg->channel)){
    RESET_TIMER_CH(TIMER_LIMIT2_CLR, pSnsCfg->channel);
    if(FlagLimitClear){
    	CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT2, pSnsCfg->channel);
    	button_block_sound_lim = 0;//сброс блокировки звука от нажатия кнопки
    }
  }
  // Сработал таймер на сброс порога 3
  if(TEST_TIMER_CH(TIMER_LIMIT3_CLR, pSnsCfg->channel)){
    RESET_TIMER_CH(TIMER_LIMIT3_CLR, pSnsCfg->channel);
    if(FlagLimitClear){
    	CLEAR_STATUS_ERR_BIT(ST_BIT_LIMIT3, pSnsCfg->channel);
    }
  }
}

//===========================================================================================================================
// Проверка таймеров DOWN_TIMER и статусных байтов
// Функция используется для определения в каком режиме работаем( Sleep = 1 else = 0)
uint8_t TestSleep()
{
  if(button.button_flag){
	  return 0;
  }
  for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
	  if(device_turn == DEVICE_TURNED_ON){
		  //Если пороги и превышение диапазона и если вкл калибровка
		  if(Sns_Cfg_struct.Sensor[chan].StateErr & ERROR_MASK_ST_1){
			  return 0;
		  }
	  }
    
	  for(uint8_t i = 0; i < COUNT_TIMERS_CHAN; i++){
		  if(TEST_TIMER_RUN_CH(i, chan) || TEST_TIMER_CH(i, chan)){
			  return 0;
		  }
	  }
  }

  if((device_turn == DEVICE_TURNED_ON) || (device_turn == DEVICE_TURNED_ON_1)){
	  if(TEST_TIMER_RUN_RTC(TIMER_RTC_UART_RX_WAKE_UP) ||
			  TEST_TIMER_RTC(TIMER_RTC_UART_RX_WAKE_UP)){
		  return 0;
	  }
  }

  if(device_turn == DEVICE_TURNED_ON){
	  //Если пороги и превышение диапазона и если вкл калибровка
	  if((Sns_Cfg_struct.Config_common.StateErr&ERROR_MASK_ST_2_1) ||
			  TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_TEST_SOUND) ||
			  TEST_TIMER_RTC(TIMER_RTC_LOW_BAT)){
		  return 0;
	  }
  }
  for(uint8_t i = 0; i < COUNT_TIMERS; i++){
	  if(TEST_TIMER_RUN(i) || TEST_TIMER(i)){
		  return 0;
	  }
  }

  return 1;
}

//==============================================================================
uint8_t Test_all_chan_status_bit(uint32_t bit)
{
	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
		if(TESTBIT(Sns_Cfg_struct.Sensor[chan].State,bit)){
			return 1;
		}
	}
	return 0;
}

//==============================================================================
uint8_t Test_all_chan_status_err_bit(uint32_t bit)
{
	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
		if(TESTBIT(Sns_Cfg_struct.Sensor[chan].StateErr,bit)){
			return 1;
		}
	}
	return 0;
}

//==============================================================================
// Переход к bootloader программе
#define FLASH_ADDRESS_START_BOOT (0x08000000)
void Go_To_User_App(void)
{
    uint32_t app_jump_address;
#if CONFIG_SAVE_PARAM_LOG
    ultra_deep_power_down_memory();                                             //вход в энергосберегающий режим микросхемы памяти
#endif
    usart_interrupt_enable(USART2, USART_RDBF_INT, FALSE);
    usart_interrupt_enable(USART2, USART_ERR_INT, FALSE);
	usart_interrupt_enable(USART3, USART_RDBF_INT, FALSE);
	usart_interrupt_enable(USART3, USART_ERR_INT, FALSE);
    usart_interrupt_enable(SENSOR_UART, USART_RDBF_INT, FALSE);
    usart_interrupt_enable(SENSOR_UART, USART_ERR_INT, FALSE);
	usart_interrupt_enable(USART6, USART_RDBF_INT, FALSE);
	usart_interrupt_enable(USART6, USART_ERR_INT, FALSE);
	rtc_interrupt_enable(RTC_TS_INT, FALSE);
	dma_interrupt_enable(DMA1_CHANNEL1, DMA_FDT_INT, FALSE);
    tmr_counter_enable(TMR1, FALSE);
    tmr_counter_enable(TMR5, FALSE);

    void(*GoToApp)(void);                                                       //объявляем пользовательский тип

    __disable_irq();//запрещаем прерывания

    app_jump_address = *( uint32_t*) (FLASH_ADDRESS_START_BOOT+4);                   // извлекаем адрес перехода из вектора Reset
    GoToApp = (void (*)(void))app_jump_address;                                 // приводим его к пользовательскому типу
    SCB->VTOR = FLASH_ADDRESS_START_BOOT;                                            // Изменяем адресс векторов прерывания
    __set_CONTROL(0);
      __set_MSP(*(__IO uint32_t*) FLASH_ADDRESS_START_BOOT);                         // устанавливаем SP приложения
    GoToApp();		                                                            // запускаем приложение
}

//===========================================================================================================================
uint8_t SearchOxygenLimit1(void)
{
	for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
		if(TESTBIT(Sns_Cfg_struct.Sensor[chan_r].DeviceSetting, BIT_SET_OXYGEN_SENSOR) && (Sns_Cfg_struct.Sensor[chan_r].channel == chan_r)){
			if(TEST_STATUS_ERR_BIT(ST_BIT_LIMIT1, chan_r)){
				return 1;
			}
		}
	}
	return 0;
}

//===========================================================================================================================
//===========================================================================================================================
#define BLINK_LED_OFF		(0)
#define BLINK_LED_ON		(1)
#define BLOCK_SOUND_OFF		(0)
#define BLOCK_SOUND_RECEIPT	(1)
#define BLOCK_SOUND_ON		(2)
#define BLOCK_LED_OFF		(0)
#define BLOCK_LED_ON		(1)
#define TEST_SOUND_OFF		(0)
#define TEST_SOUND_ON		(1)
//===========================================================================================================================
#if 0
//===========================================================================================================================
static void LedSoundVibroPlay(uint8_t switch_flag_led, uint8_t time_pulse, uint8_t time_pause, uint8_t mode_blink_led, uint8_t block_sound, uint8_t block_led, uint8_t test_sound);
static void LedSoundVibroPlay(uint8_t switch_flag_led, uint8_t time_pulse, uint8_t time_pause, uint8_t mode_blink_led, uint8_t block_sound, uint8_t block_led, uint8_t test_sound)
{
	static uint8_t switch_flag_led_pre = 0x00;
	static uint8_t state_step = 0;
	static uint32_t count_repeat = 0;
	uint32_t repeat;
	//----------------------------------------------------------------------------
	// Была нажата кнопка, увеличение интервалов срабтывания в 5 раз
	// Отключение звука и вибро
	if(block_sound == BLOCK_SOUND_RECEIPT){
		time_pause = (time_pulse + time_pause)*5;
	}
	//----------------------------------------------------------------------------
	// Отключение звука и вибро
	if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND) || (block_sound == BLOCK_SOUND_RECEIPT)){
		block_sound = BLOCK_SOUND_ON;
	}
	//----------------------------------------------------------------------------
	if(switch_flag_led_pre != switch_flag_led){
		switch_flag_led_pre = switch_flag_led;
		count_repeat = 0;
		if(block_led == BLOCK_LED_ON){
			state_step = 1;
		}
		else{
			state_step = 0;
		}
	}
	repeat = (state_step) ? time_pulse:time_pause;
	//----------------------------------------------------------------------------
	if(switch_flag_led_pre){
	  count_repeat++;
	}
	//----------------------------------------------------------------------------
	if(count_repeat >= repeat){
		count_repeat = 0;
		state_step = (state_step) ? 0:1;
	}
	//----------------------------------------------------------------------------
	if(state_step){
		if(test_sound == TEST_SOUND_OFF){
			if(block_sound == BLOCK_SOUND_ON){
				SOUND_OFF();
				VIBRO_OFF();
			}
			else{
				SOUND_ON();
				VIBRO_ON();
			}

			if(block_led == BLOCK_LED_ON){
				SET_OFF(ALARM_LED_1);
				SET_OFF(ALARM_LED_2);
			}
			else{
				if(mode_blink_led == BLINK_LED_ON){
					SET_TGL(ALARM_LED_1);
					SET_TGL(ALARM_LED_2);
				}
				else{
					SET_ON(ALARM_LED_1);
					SET_ON(ALARM_LED_2);
				}
			}
		}
		else{
			if(block_sound == BLOCK_SOUND_ON){
				SOUND_OFF();
			}
			else{
				SOUND_ON();
			}
			VIBRO_OFF();

			SET_OFF(ALARM_LED_1);
			SET_OFF(ALARM_LED_2);
		}
	}
	else{
		SOUND_OFF();
		VIBRO_OFF();

		SET_OFF(ALARM_LED_1);
		SET_OFF(ALARM_LED_2);
	}

}
//===========================================================================================================================
void Indication(void)
{
	static uint16_t repeat_flag_sound = 0;
	static uint8_t flag_sound = 0;
	static uint16_t div_sound = 0;
	uint16_t repeat;

	//----------------------------------------------------------------------------
	LedStateIndication();
	//----------------------------------------------------------------------------
	if(StateProg == 0){
		LedSoundVibroPlay(0x00, 6, 2, BLINK_LED_OFF, BLOCK_SOUND_ON, BLOCK_LED_ON, TEST_SOUND_OFF);
		return;
	}
	if(TEST_TIMER(TIMER_SHUTDOWN) ||
			TEST_TIMER_RUN(TIMER_SHUTDOWN) ||
			TEST_TIMER_RUN(TIMER_PRESS_SOS) ||
			device_turn != DEVICE_TURNED_ON){
		return;
	}

	CHANGING_SOUND_PARAMETERS();
	//----------------------------------------------------------------------------
	if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_TEST_SOUND)){
		LedSoundVibroPlay(0x0F, 5, 10, BLINK_LED_OFF, BLOCK_SOUND_OFF, BLOCK_LED_ON, TEST_SOUND_ON);
	}
	else if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_EXCEEDED_THE_RANGE))
	{
		LedSoundVibroPlay(0x02, 10, 2, BLINK_LED_OFF, BLOCK_SOUND_OFF, BLOCK_LED_OFF, TEST_SOUND_OFF);
	}
	else if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT3) || TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT_TWA) || TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT_STEL))
	{
		LedSoundVibroPlay(0x03, 5, 3, BLINK_LED_OFF, BLOCK_SOUND_OFF, BLOCK_LED_OFF, TEST_SOUND_OFF);
	}
	else if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT2))
	{
		LedSoundVibroPlay(0x04, 5, 5, BLINK_LED_OFF, BLOCK_SOUND_OFF, BLOCK_LED_OFF, TEST_SOUND_OFF);
	}
	else if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT1))
	{
		if(SearchOxygenLimit1() || !button_block_sound_lim){
			LedSoundVibroPlay(0x05, 6, 9, BLINK_LED_ON, BLOCK_SOUND_OFF, BLOCK_LED_OFF, TEST_SOUND_OFF);
		}
		else{
			LedSoundVibroPlay(0x05, 6, 9, BLINK_LED_ON, BLOCK_SOUND_RECEIPT, BLOCK_LED_OFF, TEST_SOUND_OFF);
		}
	}
	else if(Sns_Cfg_struct.Config_common.StateErr&ERROR_MASK_ST_2_1)
	{
		LedSoundVibroPlay(0x01, 10, 5, BLINK_LED_OFF, BLOCK_SOUND_ON, BLOCK_LED_OFF, TEST_SOUND_OFF);
	}
	else if(TEST_TIMER_RTC(TIMER_RTC_LOW_BAT)){
		repeat_critical_battery++;
		if(repeat_critical_battery >= 20){
			RESET_TIMER_RTC(TIMER_RTC_LOW_BAT);
			LedSoundVibroPlay(0x00, 2, 3, BLINK_LED_OFF, BLOCK_SOUND_ON, BLOCK_LED_ON, TEST_SOUND_OFF);
		}
		else{
			LedSoundVibroPlay(0x06, 2, 3, BLINK_LED_OFF, BLOCK_SOUND_OFF, BLOCK_LED_OFF, TEST_SOUND_OFF);
		}
	}
	else if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_SENSOR_FAILED))
	{
		LedSoundVibroPlay(0x07, 10, 5, BLINK_LED_OFF, BLOCK_SOUND_ON, BLOCK_LED_OFF, TEST_SOUND_OFF);
	}
#if CONFIG_LORA
	else if(TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_BACK_GROUP))
	{
		if(!button_block_sound_lora){
			LedSoundVibroPlay(0x0A, 10, 2, BLINK_LED_ON, BLOCK_SOUND_OFF, BLOCK_LED_OFF, TEST_SOUND_OFF);
		}
		else{
			LedSoundVibroPlay(0x0A, 10, 2, BLINK_LED_ON, BLOCK_SOUND_ON, BLOCK_LED_OFF, TEST_SOUND_OFF);
		}
	}
	else if(TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_SOS) || TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_SOS_GROUP))
	{
		if(!button_block_sound_lora){
			LedSoundVibroPlay(0x08, 6, 4, BLINK_LED_ON, BLOCK_SOUND_OFF, BLOCK_LED_OFF, TEST_SOUND_OFF);
		}
		else{
			LedSoundVibroPlay(0x08, 6, 4, BLINK_LED_ON, BLOCK_SOUND_ON, BLOCK_LED_OFF, TEST_SOUND_OFF);
		}
	}
	else if(TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_GROUP))
	{
		if(!button_block_sound_lora){
			LedSoundVibroPlay(0x09, 10, 5, BLINK_LED_ON, BLOCK_SOUND_OFF, BLOCK_LED_OFF, TEST_SOUND_OFF);
		}
		else{
			LedSoundVibroPlay(0x09, 10, 5, BLINK_LED_ON, BLOCK_SOUND_ON, BLOCK_LED_OFF, TEST_SOUND_OFF);
		}
	}
#endif
	else
	{
		if(TEST_TIMER_RUN(TIMER_CHARGE_SOUND_VIBRO)){
			LedSoundVibroPlay(0x0B, 6, 2, BLINK_LED_OFF, BLOCK_SOUND_OFF, BLOCK_LED_ON, TEST_SOUND_OFF);
		}
		else{
			LedSoundVibroPlay(0x00, 6, 2, BLINK_LED_OFF, BLOCK_SOUND_ON, BLOCK_LED_ON, TEST_SOUND_OFF);
		}
	}
	//----------------------------------------------------------------------------
}

#else
typedef struct {
    uint16_t t_on;    // Длительность активной фазы (в тиках по 10мс)
    uint16_t t_off;   // Пауза между пачками (в тиках по 10мс)
    uint8_t  pulses;  // Сколько импульсов в пачке
} AlarmPattern_t;

//const AlarmPattern_t alarm_table[12] = {
//    // ID  {t_on, t_off, pulses}  -- Описание характера сигнала
//    {0,    0,    0}, // 0: ВЫКЛЮЧЕНО
//
//    // --- ПОРОГИ ГАЗА (Иерархия 1-2-3) ---
//    {30,   60,   1}, // 1: ПОРОГ 1 (ID 1) - Одиночный редкий маяк (раз в 2 сек). Спокойное бдительное оповещение.
//    {30,   60,   2}, // 2: ПОРОГ 2 (ID 2) - Двойной "удар" (сердцебиение) раз в 0.6 сек. Ощутимая тревога.
//    {30,   30,   3}, // 3: ПОРОГ 3 (ID 3) - Плотные тройные вспышки. Почти нет тишины, очень агрессивно.
//
//    // --- КРИТИЧЕСКИЕ СОСТОЯНИЯ ---
//    {5,    5,    1}, // 4: ПРЕВЫШЕНИЕ ДИАПАЗОНА - Самый быстрый треск (пулемет), паника.
//    {100,  50,   1}, // 5: ОШИБКА СЕНСОРА - Тяжелый длинный гудок (1 сек), заставляющий обратить внимание.
//    {30,   30,   1}, // 6: КРИТИЧЕСКАЯ АВАРИЯ (ОБЩАЯ) - Равномерный пульс (300мс вкл/выкл).
//
//    // --- СЕРВИСНЫЕ И ДРУГИЕ ---
//    {60,   900,  4}, // 7: БАТАРЕЯ - Короткий "клик" раз в 5 секунд. Не мешает работать, но напоминает.
//    {20,   50,   3}, // 8: SOS / LORA - Специфический ритм (три средних вспышки).
//    {60,   40,   2}, // 9: ГРУППОВОЙ ВЫЗОВ - Два затяжных гудка.
//    {180,  20,   1}, // 10: ЗАРЯДКА - Почти постоянное свечение (мягкое мигание).
//    {40,   30,   2}  // 11: ВНИМАНИЕ / ПОДТВЕРЖДЕНИЕ - Короткий двойной "бип-бип".
//};

AlarmPattern_t alarm_table[13] = {
    // ID  {t_on, t_off, pulses}  -- Описание характера сигнала
    {0,    0,    0}, // 0: ВЫКЛЮЧЕНО

    // --- ПОРОГИ ГАЗА ---
    {60,   90,   1}, // 1: ПОРОГ 1 -
    {90,   60,   2}, // 2: ПОРОГ 2 -
    {90,   30,   3}, // 3: ПОРОГ 3 -

    // --- КРИТИЧЕСКИЕ СОСТОЯНИЯ ---
    {45,   15,   1}, // 4: ПРЕВЫШЕНИЕ ДИАПАЗОНА - быстрый треск
    {100,  50,   1}, // 5: ОШИБКА СЕНСОРА - Тяжелый длинный гудок (1 сек)
    {45,   30,   1}, // 6: КРИТИЧЕСКАЯ АВАРИЯ (ОБЩАЯ)

    // --- СЕРВИСНЫЕ И ДРУГИЕ ---
    {45,   900,  4}, // 7: БАТАРЕЯ -
    {60,   30,   3}, // 8: SOS / LORA -
    {60,   60,   2}, // 9: ГРУППОВОЙ ВЫЗОВ -
	{60,   20,   2}, // 10: ВНИМАНИЕ / ГРУППА НАЗАД - .
    {90,   10,   2}, // 11: ЗАРЯДКА -
	{90,   60,   2}, // 12: TEST
};

static void All_Hardware_Off(void);

void Indication_Manager (uint8_t id, uint8_t block_snd, uint8_t block_led, uint8_t test_snd)
{
    static uint8_t  prev_id = 0xFF;
    static uint16_t tick_cnt = 0;
    static uint8_t  pulse_cnt = 0;
    static uint8_t  is_pause = 0;
    static uint8_t  prev_slot = 0xFF; // Для отслеживания смены зоны

    // 1. Сброс при смене режима
    if (id != prev_id) {
        prev_id = id;
        tick_cnt = 0; pulse_cnt = 0; is_pause = 0;
        All_Hardware_Off();
    }

    if (id == 0 || id >= 13) { All_Hardware_Off(); return; }

    const AlarmPattern_t *p = &alarm_table[id];

    // 2. Основной таймер
    tick_cnt++;
    uint16_t current_limit = is_pause ? p->t_off : p->t_on;

    if (tick_cnt >= current_limit) {
        tick_cnt = 0;
        if (!is_pause) {
            if (++pulse_cnt >= p->pulses) {
                is_pause = 1;
                pulse_cnt = 0;
            }
        } else {
            is_pause = 0;
        }
        All_Hardware_Off();
        prev_slot = 0xFF; // Сбрасываем зону при выходе в паузу
        return;
    }

    if (!is_pause && p->t_on > 0) {
        // --- РАСЧЕТ ТЕКУЩЕЙ ЗОНЫ ---
        uint8_t current_slot = (uint8_t)((uint32_t)tick_cnt * 3 / p->t_on);
        if (current_slot != prev_slot) {
            prev_slot = current_slot;
            All_Hardware_Off();
        }

        if (test_snd) {
            // В режиме теста: только звук в свою фазу (фаза 2)
            if (current_slot == 2 && block_snd != BLOCK_SOUND_ON) SOUND_ON();
        }
        else {
            // Обычный режим: Разделение приборов во времени
            switch (current_slot) {
                case 0: // Первая треть времени - только LED1
                    if (!block_led) SET_ON(ALARM_LED_1);
                    break;
                case 1: // Вторая треть времени - только LED2
                    if (!block_led) SET_ON(ALARM_LED_2);
                    break;
                case 2: // Последняя треть времени - Звук и Вибро
                    if (!block_snd && !TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND)) {
                        SOUND_ON();
                        VIBRO_ON();
                    }
                    break;
            }
        }
    }
}

static void All_Hardware_Off(void)
{
    SOUND_OFF(); VIBRO_OFF();
    SET_OFF(ALARM_LED_1); SET_OFF(ALARM_LED_2);
}

void Indication(void)
{
    // 1. Предварительные проверки (Exit early)
    LedStateIndication();

    if(StateProg == 0){
        Indication_Manager(0, BLOCK_SOUND_ON, BLOCK_LED_ON, TEST_SOUND_OFF);
        return;
    }

    if(TEST_TIMER(TIMER_SHUTDOWN) ||
       TEST_TIMER_RUN(TIMER_SHUTDOWN) ||
       TEST_TIMER_RUN(TIMER_PRESS_SOS) ||
       device_turn != DEVICE_TURNED_ON){
        return;
    }

    CHANGING_SOUND_PARAMETERS();

    // 2. Определение активного режима (ID) и флагов
    uint8_t current_id = 0;
    uint8_t snd_block = BLOCK_SOUND_OFF;
    uint8_t led_block = BLOCK_LED_OFF;
    uint8_t is_test = TEST_SOUND_OFF;

    if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_TEST_SOUND)) {
        current_id = Sns_Cfg_struct.Config_common.PeriodTimeLora&0xFF;//1; // Берем ID из нашей таблицы (например, 1 - Критично/Тест)
//        alarm_table[12].t_on = Sns_Cfg_struct.Config_common.TimeLedStatePeriod;
//        alarm_table[12].t_off = Sns_Cfg_struct.Config_common.TimeLedStatePulse;
//        alarm_table[12].pulses = (Sns_Cfg_struct.Config_common.PeriodTimeLora>>8)&0xFF;
        is_test = TEST_SOUND_ON;
        led_block = BLOCK_LED_ON;
    }
    else if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_EXCEEDED_THE_RANGE)) {
        current_id = 4; // Очень быстрый треск
    }
    else if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT3) ||
            TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT_TWA) ||
            TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT_STEL)) {
        current_id = 3; // Порог 2 (двойной удар)
    }
    else if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT2)) {
        current_id = 2; // Спец-сигнал (тройной)
    }
    else if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_LIMIT1)) {
        current_id = 1; // Порог 1 (редкий маяк)
        // Логика блокировки для Порога 1
        if(!SearchOxygenLimit1() && button_block_sound_lim) {
            snd_block = BLOCK_SOUND_RECEIPT;
        }
    }
    else if(Sns_Cfg_struct.Config_common.StateErr & ERROR_MASK_ST_2_1) {
        current_id = 5; // Ошибка (длинный гудок)
        snd_block = BLOCK_SOUND_ON;
    }
    else if(TEST_TIMER_RTC(TIMER_RTC_LOW_BAT)) {
        current_id = 7; // Батарея
        repeat_critical_battery++;
        if(repeat_critical_battery >= (alarm_table[current_id].t_on * alarm_table[current_id].pulses)) {
            RESET_TIMER_RTC(TIMER_RTC_LOW_BAT);
            current_id = 0; // Пауза
        }
    }
    else if(TEST_ALL_CHAN_STATUS_ERR_BIT(ST_BIT_SENSOR_FAILED)) {
        current_id = 5;
        snd_block = BLOCK_SOUND_ON;
    }
#if CONFIG_LORA
    else if(TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_BACK_GROUP)) {
        current_id = 10; // Внимание
        if(button_block_sound_lora) snd_block = BLOCK_SOUND_ON;
    }
    else if(TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_SOS) || TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_SOS_GROUP) || TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_ZONE)) {
        current_id = 8; // SOS
        if(button_block_sound_lora) snd_block = BLOCK_SOUND_ON;
    }
    else if(TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_GROUP)) {
        current_id = 9; // Групповой
        if(button_block_sound_lora) snd_block = BLOCK_SOUND_ON;
    }
#endif
    else {
        // Режим по умолчанию
        if(TEST_TIMER_RUN(TIMER_CHARGE_SOUND_VIBRO)) {
            current_id = 11; // Зарядка
            led_block = BLOCK_LED_ON;
        } else {
            current_id = 0; // Всё выключено
        }
    }

    // 3. Единый вызов менеджера индикации
    Indication_Manager(current_id, snd_block, led_block, is_test);
}

#endif
//===========================================================================================================================
void LedStateIndication(void)
{
//	return;
	//------------------------------------------------------------------------
	uint8_t state_flag_err = 0;
	for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
		if((Sns_Cfg_struct.Sensor[chan_r].StateErr & ERROR_MASK_ST_1_1)||
				(Sns_Cfg_struct.Config_common.StateErr & ERROR_MASK_ST_2)){
			state_flag_err = 1;
			chan_r = COUNT_CHAN;
		}
	}
  //------------------------------------------------------------------------
	if(device_turn == DEVICE_TURNED_ON){
		if(state_flag_err)
		{
#if CONFIG_CHARGE
			if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE)){
				SET_ON(STATE_LED);
			}
			else
#endif
			{
				SET_OFF(STATE_LED);
			}
			return;
		}
	}
#if CONFIG_CHARGE
	if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE)){
		SET_ON(STATE_LED);
	}
	else
#endif
	if(TEST_TIMER_RTC(TIMER_RTC_RESET_ERR) || TEST_TIMER_RUN_RTC(TIMER_RTC_RESET_ERR)){
		if(TEST_TIMER_RUN_RTC(TIMER_RTC_RESET_ERR)){
			SET_ON(STATE_LED);
		}
		if(TEST_TIMER_RTC(TIMER_RTC_RESET_ERR)){
			SET_OFF(STATE_LED);
		}
	}
	else if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_STATE_RUN_LED) && (device_turn == DEVICE_TURNED_ON)){
		if(TEST_TIMER_RTC(TIMER_RTC_LED_STATE_RUN_PERIOD)){
			RESET_TIMER_RTC(TIMER_RTC_LED_STATE_RUN_PERIOD);
			SET_ON(STATE_LED);
			START_TIMER_RTC(TIMER_RTC_LED_STATE_RUN_PULSE, Sns_Cfg_struct.Config_common.TimeLedStatePulse);
		}
		if(TEST_TIMER_RTC(TIMER_RTC_LED_STATE_RUN_PULSE)){
			RESET_TIMER_RTC(TIMER_RTC_LED_STATE_RUN_PULSE);
			SET_OFF(STATE_LED);
		}
		if(!TEST_TIMER_RUN_RTC(TIMER_RTC_LED_STATE_RUN_PERIOD) && !TEST_TIMER_RTC(TIMER_RTC_LED_STATE_RUN_PERIOD)){
			START_TIMER_RTC(TIMER_RTC_LED_STATE_RUN_PERIOD, Sns_Cfg_struct.Config_common.TimeLedStatePeriod);
		}
	}
	else{
		SET_OFF(STATE_LED);
	}
}

//===========================================================================================================================
// Bump Test, проверка на срабатывание первого порога, запускается установкой бита ST_COMMON_BIT_BUMP_TEST_RUN
// chan - номер канала
// state_chan - состяние канала(вкл\выкл)
void BumpTest(uint8_t chan, uint8_t state_chan)
{
  static uint32_t Bump_test_limit_1 = 0;
  static uint8_t Start_timer_bump_test = 0;
#if (DEVICE_NUMBER == Device1)
  uint32_t mask_test_limit = 0x01;
#elif ((DEVICE_NUMBER == Device2) || (DEVICE_NUMBER == Device2_1) || (DEVICE_NUMBER == Device2_Pro_1) || (DEVICE_NUMBER == Device2_2))
  uint32_t mask_test_limit = 0x0F;
#elif (DEVICE_NUMBER == Device2_Pro)
  uint32_t mask_test_limit = 0x3F;
#elif (DEVICE_NUMBER == Device3_Pro)
  uint32_t mask_test_limit = 0xFF;
#endif
  //---------------------------------------------------------------------------- 
  if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BUMP_TEST_RUN)){
	if(Start_timer_bump_test == 0){
		Start_timer_bump_test = 1;
		START_TIMER_RTC(TIMER_RTC_BUMP_TEST_L, TIME_RTC_BUMP_TEST_L);
	}
    //--------------------------------------------------------------------------
    if(!(Bump_test_limit_1 & (0x01<<chan))){
      if(TEST_STATUS_ERR_BIT(ST_BIT_LIMIT1, chan) || (state_chan == FALSE)){
        Bump_test_limit_1 |= 0x01<<chan;
      }
      else if(TEST_TIMER_RTC(TIMER_RTC_BUMP_TEST_L)){
        Bump_test_limit_1 |= 0x10000<<chan;
        Bump_test_limit_1 |= 0x01<<chan;
      }
    }
    //--------------------------------------------------------------------------
    if((Bump_test_limit_1&mask_test_limit) == mask_test_limit){
      RESET_TIMER_RTC(TIMER_RTC_BUMP_TEST_L);
      if(!(Bump_test_limit_1&0xFFFF0000)){
        SET_STATUS_COMMON_BIT(ST_COMMON_BIT_BUMP_TEST);
      }
      else{
        CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_BUMP_TEST);
      } 
      Sns_Cfg_struct.Config_common.DataLastBumpTest = Sns_Cfg_struct.Config_common.working_hours;     //Дата последнего теста
      CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_BUMP_TEST_RUN);
      Bump_test_limit_1 = 0;
      SaveConfig = 1;
    }
    //--------------------------------------------------------------------------
  }
  else{
	  Start_timer_bump_test = 0;
	  Bump_test_limit_1 = 0;
  }
  //----------------------------------------------------------------------------
}

//===========================================================================================================================
uint16_t Search_text(uint8_t *data1, uint8_t *data2)
{
	uint8_t * index;
	uint16_t pos = 0;
	index = strstr(data1, data2) + 1;
	pos = (uint16_t)(index - data1);
	return pos;
}

//===========================================================================================================================
uint32_t RTC_ReadTime(void)
{
  return rtc_counter_get();
}

//===========================================================================================================================
error_status RTC_WriteTime(uint32_t TimeCounter)
{
	error_status status = SUCCESS;

	rtc_counter_set(TimeCounter);

	rtc_wait_config_finish();

	return status;
}

