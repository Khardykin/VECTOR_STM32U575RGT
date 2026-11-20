/*
 * Avis_Menu_Functionality.c
 *
 *  Created on: 2025 Oct 22
 *      Author: Dmitriy
 */
#include "Avis_main.h"
//===========================================================================================================================
uint8_t  state_flip = 0;                // Переключение отображений экрана при вкл
//------------------------------------------------------------------------------
STATE_SCREEN_AVIS  state_screen_pre_sos = STATE_SCREEN_MEASURE;
//------------------------------------------------------------------------------
time_t  tmp_current = 0;                // Время текущее
uint8_t tmp_calib_flag = 0;             // Флаг время до калибровки
time_t  tmp_calib = 0;                  // Время до калибровки
time_t  tmp_calib_ch[COUNT_CHAN] = {0}; // Время до калибровки
time_t  tmp_calib_pre = 0;              // Вреям до калибровки предыдущее
time_t 	tmp_bt = 0;                     // Время до БТ
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
uint8_t state_auto_calib_zero[COUNT_CHAN];
//------------------------------------------------------------------------------
static uint16_t password_set[2] = {0, 0};          // Набранное число
//static uint16_t password_set[2] = {PASSWORD_CALIB, PASSWORD_SETTING};          // Набранное число
static uint8_t  password_num = 0;
static uint8_t  password_dec = 0;               // Инкремент разряда
static uint8_t  password_rank = 0;              // Разряд числа

static float 	ConcentrationOnDisp[COUNT_CHAN];
//------------------------------------------------------------------------------
uint8_t  ChanOne = CHAN0;                // Выбранный канал во время калибровки
static uint8_t  disp_param_input_dec = 0;             // Инкремент разряда
static uint8_t  disp_param_input_rank = 0;            // Разряд числа
static uint8_t  disp_param_input_rank_max = 0;        // Min Разряд числа
static uint8_t  disp_param_input_rank_min = 0;        // Min Разряд числа
static uint32_t displayParameterInput = 0;
static float * displaydata_input = 0;
static time_t 	tmp_disp_data;
//------------------------------------------------------------------------------
float  conversion_factor[COUNT_CHAN];  // Коэффициент перевода в отображаемую единицу измерения
float  conversion_factor_rf[COUNT_CHAN];  // Коэффициент перевода в отображаемую единицу измерения
//===========================================================================================================================
void DisplayIndicationConc(SNS_CFG_Type *pSnsCfg)
{
	ConcentrationOnDisp[pSnsCfg->channel] = pSnsCfg->ConcentrationVal;
}

float GetDisplayIndicationConc(uint8_t chan)
{
	return ConcentrationOnDisp[chan];
}

//===========================================================================================================================
// Перевод времени в другой формат и проверка на истечение интервалов времени
void DisplayTimeConversion(SNS_CFG *pSnsCfg)
{
	tmp_calib_flag = 0;
	tmp_calib_pre = pSnsCfg->Config_common.working_hours;
    for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
      CLEAR_STATUS_ERR_BIT(ST_BIT_CALIB_INTERVAL, chan);                            // Сброс ошибки интервала калибровки
      tmp_calib = 0;
      tmp_calib_ch[chan] = 0;
      if(pSnsCfg->Sensor[chan].channel == chan){                          // Проверка что канал включен
        if(pSnsCfg->Sensor[chan].Calibration_interval){
          tmp_calib_flag = 1;
          tmp_calib = pSnsCfg->Config_common.working_hours;
          tmp_calib = (time_t)((tmp_calib - pSnsCfg->Sensor[chan].DataLastCalibration)*TIME_DEL_2);
          struct tm tm_ptr_calib = *gmtime(&tmp_calib);
          if(pSnsCfg->Sensor[chan].Calibration_interval <= tm_ptr_calib.tm_yday){
            SET_STATUS_ERR_BIT(ST_BIT_CALIB_INTERVAL, chan);
            tmp_calib = 0;
          }
          else{
            tmp_calib = pSnsCfg->Sensor[chan].Calibration_interval - tm_ptr_calib.tm_yday;
            tmp_calib_ch[chan] = tmp_calib;
          }
          if(tmp_calib <= tmp_calib_pre){
            tmp_calib_pre = tmp_calib;
          }
        }
      }
    }
	if(tmp_calib_flag){
		tmp_calib = tmp_calib_pre;
	}

    tmp_bt = 0;
    if(pSnsCfg->Config_common.Bump_interval){
    	tmp_bt = pSnsCfg->Config_common.working_hours;
    	tmp_bt = (time_t)((tmp_bt - pSnsCfg->Config_common.DataLastBumpTest)*TIME_DEL_2);
    	struct tm tm_ptr_bt = *gmtime(&tmp_bt);
    	if((pSnsCfg->Config_common.Bump_interval <= tm_ptr_bt.tm_yday) || !TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BUMP_TEST)){
    		SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_BUMP_INTERVAL);
    		tmp_bt = 0;
    	}
    	else{
    		CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_BUMP_INTERVAL);               // Сброс ошибки истечения интервала теста
    		tmp_bt = pSnsCfg->Config_common.Bump_interval - tm_ptr_bt.tm_yday;
    	}
    }
    else{
    	CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_BUMP_INTERVAL);                 // Сброс ошибки истечения интервала теста
	}

    tmp_current = (time_t)(pSnsCfg->Config_common.working_hours + pSnsCfg->Config_common.working_hours_offset);
}

//===========================================================================================================================
// Перевод единицы измерения у сенсоров
void DisplayConversionUnitMasure(SNS_CFG_Type *pSnsCfg)
{
	if((IS_STATE_SCREEN() == STATE_SCREEN_MEASURE) || (IS_STATE_SCREEN() == STATE_SCREEN_MEASURE_TO_MENU)){
		if(TESTBIT(pSnsCfg->DeviceSetting, BIT_SET_UART_SENSOR)){
			if(TESTBIT(pSnsCfg->DeviceSetting1, BIT_SET_BELEAD_BLRM_SENSOR) || TESTBIT(pSnsCfg->DeviceSetting1, BIT_SET_BELEAD_BLDM_SENSOR)){
				if(TEST_STATUS_BIT(ST_BIT_TK_MAX_RANGE, pSnsCfg->channel)){
					if(GetDisplayIndicationConc(pSnsCfg->channel) <= pSnsCfg->Limit_Conc_MaxRange){
						if((pSnsCfg->DeviceSetting&MEASURE_MASK) == MEASURE_CONC_NKPR){
							pSnsCfg->DeviceSetting &=~ MEASURE_MASK;
							pSnsCfg->DeviceSetting |= MEASURE_CONC_MG_M3;
						}
					}
				}
			}
		}
	}
}

//===========================================================================================================================
// Функция вывода значений на экран
float Get_Conversion_factor(uint8_t chan)
{
	return conversion_factor[chan];
}

// Функция вывода значений на RF
float Get_Conversion_factor_rf(uint8_t chan)
{
	return conversion_factor_rf[chan];
}

//===========================================================================================================================
float calculating_conversion_factor(SNS_CFG_Type *pSnsCfg)
{
  float conversion_factor = 1.0;
  uint16_t DeviceSetting = pSnsCfg->DeviceSetting;
  float MGM3Coeff = pSnsCfg->MGM3Coeff;
  float LELCoeff = pSnsCfg->LELCoeff;
  float CORRCoeff = pSnsCfg->CORRCoeff/1000.0;
  if(((DeviceSetting&MEASURE_MASK_BASIC)>>MEASURE_OFFSET_BASIC) == MEASURE_CONC_PPM){
	  if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_MG_M3){
		  if(MGM3Coeff == 0.0){
			  conversion_factor = 1;
		  }
		  else{
			  conversion_factor = MGM3Coeff*DEL_MG_M3;
		  }
	  }
	  else if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_OB_D){
			  conversion_factor = (1/10000.0);
	  }
	  else if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_NKPR){
		  if(LELCoeff == 0.0){
			  conversion_factor = 1;
		  }
		  else{
			  conversion_factor = (1/10000.0)*(100.0/LELCoeff);
		  }
	  }
  }
  else if(((DeviceSetting&MEASURE_MASK_BASIC)>>MEASURE_OFFSET_BASIC) == MEASURE_CONC_MG_M3){
	  if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_PPM){
		  if(MGM3Coeff == 0.0){
			  conversion_factor = 1;
		  }
		  else{
			  conversion_factor = 1/(MGM3Coeff*DEL_MG_M3);
		  }
	  }
	  else if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_OB_D){
		  if(MGM3Coeff == 0.0){
			  conversion_factor = 1;
		  }
		  else{
			  conversion_factor = 1/(MGM3Coeff*DEL_MG_M3*DEL_MG_M3_VOL*10000.0);
		  }
	  }
	  else if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_NKPR){
		  if((MGM3Coeff == 0.0) || (LELCoeff == 0.0)){
			  conversion_factor = 1;
		  }
		  else{
			  conversion_factor = (1/(MGM3Coeff*DEL_MG_M3*DEL_MG_M3_VOL*10000.0))*(100.0/LELCoeff);
		  }
	  }
  }
  else if(((DeviceSetting&MEASURE_MASK_BASIC)>>MEASURE_OFFSET_BASIC) == MEASURE_CONC_OB_D){
	  if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_PPM){
		  conversion_factor = (10000.0);
	  }
	  else if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_MG_M3){
		  if(MGM3Coeff == 0.0){
			  conversion_factor = 1;
		  }
		  else{
			  conversion_factor = MGM3Coeff*DEL_MG_M3*DEL_MG_M3_VOL*10000.0;
		  }
	  }
	  else if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_NKPR){
		  if(LELCoeff == 0.0){
			  conversion_factor = 1;
		  }
		  else{
			  conversion_factor = (100.0/LELCoeff);
		  }
	  }
  }
  else if(((DeviceSetting&MEASURE_MASK_BASIC)>>MEASURE_OFFSET_BASIC) == MEASURE_CONC_NKPR){
	  if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_PPM){
		  if(LELCoeff == 0.0){
			  conversion_factor = 1;
		  }
		  else{
			  conversion_factor = (10000.0)*(LELCoeff/100.0);
		  }
	  }
	  else if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_MG_M3){
		  if((MGM3Coeff == 0.0) || (LELCoeff == 0.0)){
			  conversion_factor = 1;
		  }
		  else{
			  conversion_factor = (MGM3Coeff*DEL_MG_M3*DEL_MG_M3_VOL*10000.0) * (LELCoeff/100.0);
		  }
	  }
	  else if((DeviceSetting&MEASURE_MASK) == MEASURE_CONC_OB_D){
		  if(LELCoeff == 0.0){
			  conversion_factor = 1;
		  }
		  else{
			  conversion_factor = (LELCoeff/100.0);
		  }
	  }
  }
  if(CORRCoeff != 0){
	  conversion_factor = conversion_factor*CORRCoeff;
  }
  return conversion_factor;
}

//===========================================================================================================================
// Функция перевода вывода дискретности по настройкам
uint16_t calculating_the_discreteness(SNS_CFG_Type *pSnsCfg, uint16_t num1)
{
  uint8_t degree = 0;
  if(num1 == BASIC){
    degree = (pSnsCfg->DeviceSetting&MEASURE_DISK_BASIC)>>(MEASURE_OFFSET_BASIC+OFFSET_DISK);
  }
  else if(num1 == DISP){
    degree = (pSnsCfg->DeviceSetting&MEASURE_DISK)>>OFFSET_DISK;
  }

  if(degree == 0)
  {return 1;}
  else if(degree == 1)
  {return 10;}
  else if(degree == 2)
  {return 100;}
  else if(degree == 3)
  {return 1000;}
  return 0;
}

//===========================================================================================================================
uint8_t ParamStelState(void)
{
	uint8_t state = 0;
	for(uint8_t chan = 0; chan < COUNT_CHAN; chan++){
		if(!TEST_STATUS_BIT(ST_BIT_CHANNEL_TURN_OFF, chan) && (Sns_Cfg_struct.Sensor[chan].Limit_Stel)){
			state = 1;
		}
	}
	return state;
}

//===========================================================================================================================
uint8_t ParamTwaState(void)
{
	uint8_t state = 0;
	for(uint8_t chan = 0; chan < COUNT_CHAN; chan++){
		if(!TEST_STATUS_BIT(ST_BIT_CHANNEL_TURN_OFF, chan) && (Sns_Cfg_struct.Sensor[chan].Limit_Twa)){
			state = 1;
		}
	}
	return state;
}

//===========================================================================================================================
void Turn_FID_Sensor_To_Menu(uint8_t chan, uint8_t state)
{
	if(TESTBIT(Sns_Cfg_struct.Sensor[chan].DeviceSetting, BIT_SET_FID_SENSOR)){
		if(state){
			SET_STATUS_BIT(ST_BIT_FID_TURN_ON, chan);
		}
		else{
			CLEAR_STATUS_BIT(ST_BIT_FID_TURN_ON, chan);
		}
		TurnOnPowerSensor(&Sns_Cfg_struct.Sensor[chan], chan);
	}
}

//===========================================================================================================================
uint8_t Check_status_before_action(uint8_t chan)
{
	if(TEST_STATUS_BIT(ST_BIT_INIT, chan) && (!TEST_STATUS_BIT(ST_BIT_CHANNEL_TURN_OFF, chan)) && (!TEST_STATUS_ERR_BIT(ST_BIT_SENSOR_FAILED, chan)) && TEST_STATUS_BIT(ST_BIT_WARM_SENSOR, chan)){
		return 1;
	}
	else{
		return 0;
	}
}

//===========================================================================================================================
uint8_t Check_SerialNumberChannel(uint8_t chan)
{
	if(Sns_Cfg_struct.Sensor[chan].SerialNum[0] != 0){
		return 1;
	}
	else{
		return 0;
	}
}

//===========================================================================================================================
//Диагностика системы при включение
void SystemDiagnostics(void)
{
#if CONFIG_MEASURE
  #if CONFIG_SENSOR_TEST

  #endif
#endif
}

//===========================================================================================================================
static uint8_t get_days_in_month(int month, int year)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int real_year = year + 1900;
    if (month == 1) { // Февраль (индекс 1 в struct tm)
        // Проверка на високосный год (кратен 4, но не 100, либо кратен 400)
        if (((real_year % 4 == 0) && (real_year % 100 != 0)) || (real_year % 400 == 0))
            return 29;
    }
    return days[month];
}

//===========================================================================================================================
time_t Setting_data_time(struct tm*tm_ptr)
{
	uint8_t rank = disp_param_input_rank;

    int year = tm_ptr->tm_year;
    int mon  = tm_ptr->tm_mon;
    int day  = tm_ptr->tm_mday;
    int hour = tm_ptr->tm_hour;
    int min  = tm_ptr->tm_min;

	switch(rank){
    case 0: // Единицы года
    	year = (year / 10) * 10 + (year + 1) % 10;
        break;
    case 1: // Десятки года
    	year = (year / 100) * 100 + ((year / 10 + 1) % 10) * 10 + (year % 10);
        break;
    case 2: // Сотни года
    	year = (year / 1000) * 1000 + ((year / 100 + 1) % 10) * 100 + (year % 100);
        break;
    case 3: // Тысячи года
    	year = ((year / 1000 + 1) % 3) * 1000 + (year % 1000); // Ограничим до 3000 года
        break;
        // --- МЕСЯЦ (0-11) ---
    case 4: // Единицы месяца
        {
            int h_mon = mon + 1; // Человеческий вид 1-12
            h_mon = (h_mon / 10) * 10 + (h_mon % 10 + 1);
            if (h_mon % 10 == 0) h_mon -= 10;
            if (h_mon > 12) h_mon = 1;
            mon = h_mon - 1;
        }
        break;
    case 5: // Десятки месяца
        {
            int h_mon = mon + 1;
            h_mon = (h_mon > 10) ? (h_mon - 10) : (h_mon + 10);
            if (h_mon > 12) h_mon %= 10;
            if (h_mon == 0) h_mon = 1;
            mon = h_mon - 1;
        }
        break;
		// --- ДЕНЬ (1-31) ---
    case 6: // Единицы дня
        day = (day / 10) * 10 + (day % 10 + 1);
        if (day % 10 == 0) day -= 10;
        if (day > 31) day = 1;
        if (day == 0) day = 1;
        break;
    case 7: // Десятки дня
        day = (day + 10);
        if (day > 31) day %= 10;
        if (day == 0) day = 1;
        break;
		// --- МИНУТЫ (0-59) ---
	case 8: // Единицы минут
		min = (min / 10) * 10 + (min + 1) % 10;
		break;
	case 9: // Десятки минут
		min = (min + 10) % 60;
		break;

	// --- ЧАСЫ (0-23) ---
	case 10: // Единицы часов
        hour = (hour / 10) * 10 + (hour + 1) % 10;
        if (hour >= 24) hour %= 10;
		break;
	case 11: // Десятки часов
        hour = (hour + 10);
        if (hour >= 24) hour %= 10;
		break;

		break;
	default:
		break;
	}

    uint8_t max_d = get_days_in_month(mon, year);
    if (day > max_d) {
        day = max_d; // Если выбрали Февраль, а было 31 число -> станет 28 или 29
    }

    // Записываем результат обратно в структуру
    tm_ptr->tm_year = year;
    tm_ptr->tm_mon  = mon;
    tm_ptr->tm_mday = day;
    tm_ptr->tm_hour = hour;
    tm_ptr->tm_min  = min;
    tm_ptr->tm_sec  = 0;
    tm_ptr->tm_isdst = -1; // Автоматическое определение летнего времени

    // Нормализация структуры (исправляет ошибки типа 31 февраля)
    time_t t = mktime(tm_ptr);
    tmp_disp_data = t;
    // Рекомендуется считать нормализованные данные обратно в tm_ptr
    struct tm *normalized = localtime(&t);
    if(normalized) {
        memcpy(tm_ptr, normalized, sizeof(struct tm));
    }

	return t;
}

time_t Setting_data_time_dec(struct tm* tm_ptr)
{
    uint8_t rank = disp_param_input_rank;

    int year = tm_ptr->tm_year;
    int mon  = tm_ptr->tm_mon;
    int day  = tm_ptr->tm_mday;
    int hour = tm_ptr->tm_hour;
    int min  = tm_ptr->tm_min;

    switch(rank) {
        // --- ГОД ---
        case 0: // Единицы года
            year = (year / 10) * 10 + (year + 9) % 10;
            break;
        case 1: // Десятки года
            year = (year / 100) * 100 + ((year / 10 + 9) % 10) * 10 + (year % 10);
            break;
        case 2: // Сотни года
            year = (year / 1000) * 1000 + ((year / 100 + 9) % 10) * 100 + (year % 100);
            break;
        case 3: // Тысячи года
            year = ((year / 1000 + 2) % 3) * 1000 + (year % 1000); // 2000 -> 1000, 1000 -> 0, 0 -> 2000
            if (year < 0) year = 2000;
            break;

        // --- МЕСЯЦ (0-11) ---
        case 4: // Единицы месяца (1-12)
            {
                int h_mon = mon + 1;
                h_mon = (h_mon / 10) * 10 + (h_mon + 8) % 10 + 1; // Честный декремент единиц
                if (h_mon > 12) h_mon = 12;
                if (h_mon < 1) h_mon = 1;
                mon = h_mon - 1;
            }
            break;
        case 5: // Десятки месяца
            {
                int h_mon = mon + 1;
                h_mon = (h_mon > 10) ? (h_mon - 10) : (h_mon + 10);
                if (h_mon > 12) h_mon = 12;
                if (h_mon < 1) h_mon = 1;
                mon = h_mon - 1;
            }
            break;

        // --- ДЕНЬ (1-31) ---
        case 6: // Единицы дня
            day = (day / 10) * 10 + (day + 8) % 10 + 1;
            if (day > 31) day = 31;
            break;
        case 7: // Десятки дня
            day = (day < 10) ? (day + 30) : (day - 10);
            if (day > 31) day %= 10;
            if (day == 0) day = 1;
            break;

        // --- МИНУТЫ (0-59) ---
        case 8: // Единицы минут
            min = (min / 10) * 10 + (min + 9) % 10;
            break;
        case 9: // Десятки минут
            min = (min + 50) % 60;
            break;

        // --- ЧАСЫ (0-23) ---
        case 10: // Единицы часов
            {
                int next_h = (hour / 10) * 10 + (hour + 9) % 10;
                if (next_h >= 24) next_h = 23;
                hour = next_h;
            }
            break;
        case 11: // Десятки часов
            hour = (hour < 10) ? (hour + 20) : (hour - 10);
            if (hour >= 24) hour %= 10;
            break;

        default: break;
    }

    // Проверка корректности дня в месяце
    uint8_t max_d = get_days_in_month(mon, year);
    if (day > max_d) day = max_d;

    // Сборка и нормализация
    tm_ptr->tm_year = year;
    tm_ptr->tm_mon  = mon;
    tm_ptr->tm_mday = day;
    tm_ptr->tm_hour = hour;
    tm_ptr->tm_min  = min;
    tm_ptr->tm_sec  = 0;

    time_t t = mktime(tm_ptr);
    struct tm *normalized = localtime(&t);
    if(normalized) memcpy(tm_ptr, normalized, sizeof(struct tm));

    tmp_disp_data = t;
    return t;
}

time_t Get_data_time(void)
{
	return tmp_disp_data;
}

void Display_data_time_save(void)
{
	Sns_Cfg_struct.Config_common.working_hours = ((time_t)(Get_data_time() - Sns_Cfg_struct.Config_common.working_hours_offset)*TIME_DEL_2);
}

void Init_Setting_data_time_rank(uint8_t rank, uint8_t rank_max, uint8_t rank_min)
{
	disp_param_input_rank = rank;
	disp_param_input_rank_min = rank_min;
	disp_param_input_rank_max = rank_max;
	tmp_disp_data = tmp_current;
}

uint8_t Check_Setting_data_time_rank(void)
{
	disp_param_input_rank++;
	if(disp_param_input_rank >= (disp_param_input_rank_max + 1)){
		return 1;
	}
	return 0;
}

//===========================================================================================================================
// Ввод пароля
uint8_t Init_password(uint8_t num)
{
	password_num = num;
	password_dec = 0;
	password_rank = 2;
	return 0;
}

uint8_t Enter_password(void)
{
	uint16_t delpassword = 0;
	uint16_t count_dec;

	if(password_rank == 2){
		delpassword = 100;
		count_dec = password_set[password_num];
	}
	else if(password_rank == 3){
		delpassword = 10;
		count_dec = password_set[password_num]%100;
	}
	else if(password_rank == 4){
		delpassword = 1;
		count_dec = password_set[password_num]%10;
	}

	password_dec = count_dec/delpassword;

	password_dec++;
	if(password_dec == 10){
		password_dec = 0;
		password_set[password_num] -= 9*delpassword;
	}
	else{
		password_set[password_num] += 1*delpassword;
	}
	return 0;
}

uint8_t Inc_password(uint8_t inc_rank)
{
	if(inc_rank == 1){
		password_rank++;
		password_dec = 0;
		if(password_rank > 6){
			password_rank = 2;
		}
		if(password_rank >= 5){
			return 1;
		}
		return 0;
	}
	if(inc_rank == 2){
		password_rank--;
		password_dec = 0;
		if(password_rank < 2){
			password_rank = 6;
		}
		if(password_rank >= 5){
			return 1;
		}
		return 0;
	}
	return 0;
}

uint8_t Check_password_rank(void)
{
	return password_rank;
}

uint16_t Check_password_num(uint8_t num)
{
	return password_set[num];
}

//===========================================================================================================================
// Инициализация разрядности концентрации Sns_Cfg_struct.Sensor[chan].Prescaler_ConcCalibrGas
uint8_t InitRankDisplayParameterInput(float *data_input, uint8_t chan)
{
	displaydata_input = data_input;
	if(Sns_Cfg_struct.Sensor[chan].Limit[3]){
		uint32_t datalim = (uint32_t)lround (Sns_Cfg_struct.Sensor[chan].Limit[3]*
				Get_Conversion_factor(chan)*
				Sns_Cfg_struct.Sensor[chan].Discreteness);
		displayParameterInput = (uint32_t)lround (*data_input*
				Get_Conversion_factor(chan)*
				Sns_Cfg_struct.Sensor[chan].Discreteness);
//		disp_param_input_rank = DISP_PARAM_INPUT_DEC_MAX;

		uint8_t digits = 0;
		while(datalim > 0){
			datalim /= 10;
			digits++;
		}
		if(digits == 0){
			digits = 1;
		}

		if (digits >= DISP_PARAM_INPUT_DEC_MAX) {
		    // Если число занимает 6 или более знаков, встаем в самый левый край
		    disp_param_input_rank = 0;
		} else {
		    // Если меньше 6 знаков — вычисляем отступ
		    disp_param_input_rank = DISP_PARAM_INPUT_DEC_MAX - digits;
		}
		disp_param_input_rank_max = DISP_PARAM_INPUT_DEC_MAX;
		disp_param_input_rank_min = disp_param_input_rank;
		return 1;
	}
	else{
		return 0;
	}
}

void InitRankDisplayParameterInputLimit(void)
{
	disp_param_input_rank_max = disp_param_input_rank_max + 1;
	disp_param_input_rank = disp_param_input_rank_max + 1;
}

// Ввод концентрации
void SetIncDisplayParameterInput(void)
{
    uint32_t delcalib = 1;
    for(uint8_t i = 0; i < (DISP_PARAM_INPUT_DEC_MAX - 1 - disp_param_input_rank); i++) {
    	delcalib *= 10;
    }

	disp_param_input_dec = (displayParameterInput/delcalib)%10;

	if(disp_param_input_dec < 9){
		displayParameterInput += delcalib;
	}
	else{
		displayParameterInput -= 9*delcalib;
	}
}

void SetDecDisplayParameterInput(void)
{
	uint32_t delcalib = 1;
	for(uint8_t i = 0; i < (DISP_PARAM_INPUT_DEC_MAX - 1 - disp_param_input_rank); i++) {
		delcalib *= 10;
	}

	uint8_t current_digit = (displayParameterInput / delcalib) % 10;

	if (current_digit > 0) {
		displayParameterInput -= delcalib;
	} else {
		displayParameterInput += 9 * delcalib;
	}
}

// Переключение разряда концентрации
uint8_t SwitchingRankDisplayParameterInput(float *data_input, uint8_t chan)
{
	*data_input = ((float)displayParameterInput/(float)Sns_Cfg_struct.Sensor[chan].Discreteness)/Get_Conversion_factor(chan);
	disp_param_input_rank++;
	if(disp_param_input_rank > DISP_PARAM_INPUT_DEC_MAX){
		return 0;
	}
	return 1;
}

// Переключение разряда концентрации
void DisplayParameterInputSave(uint8_t chan)
{
	*displaydata_input = ((float)displayParameterInput/(float)Sns_Cfg_struct.Sensor[chan].Discreteness)/Get_Conversion_factor(chan);
}

float GetDisplayParameterInput(uint8_t chan)
{
	return ((float)displayParameterInput/(float)Sns_Cfg_struct.Sensor[chan].Discreteness);
}

// Переключение разряда концентрации
uint8_t SwitchingRankIncDisplayParameterInput(void)
{
	disp_param_input_rank++;
    if(disp_param_input_rank > (disp_param_input_rank_max + 1)) {
        disp_param_input_rank = disp_param_input_rank_min;
    }
	return 1;
}

// Переключение разряда концентрации
uint8_t SwitchingRankDecDisplayParameterInput(void)
{
    if (disp_param_input_rank == disp_param_input_rank_min) {
        disp_param_input_rank = (disp_param_input_rank_max + 1); // Прыгаем на Ok (7)
    } else {
        disp_param_input_rank--;
    }
	return 1;
}

uint8_t CheckRankDisplayParameterInput(void)
{
	return disp_param_input_rank;
}

//===========================================================================================================================
void InitDisplayDeviceSettingInput(uint32_t data_input)
{
	displayParameterInput = data_input;
}

float GetDisplayDeviceSettingInput(void)
{
	return ((float)displayParameterInput);
}
//===========================================================================================================================
#if 0
#define LED_SOUND_PLAY()										\
      {															\
	div_led_sound++;                                            \
        if((div_led_sound >= repeat) && repeat){				\
          div_led_sound = 0;                                    \
          flag_led_sound = (flag_led_sound) ? 0:1;              \
          flag_no_vibro = 2;                                    \
        }                                                       \
        if(flag_led_sound){                                     \
          if(flag_no_vibro){                                    \
            VIBRO_OFF();            \
            SOUND_ON();          \
            SET_ON(ALARM_LED_1);                       			\
            SET_ON(ALARM_LED_2);                       			\
            flag_no_vibro --;                                   \
          }                                                     \
          else{                                                 \
            SET_OFF(ALARM_LED_1);                       		\
            SET_OFF(ALARM_LED_2);                       		\
            SOUND_OFF();           \
            VIBRO_ON();           \
          }                                                     \
        }                                                       \
        else{                                                   \
        	SOUND_OFF();           \
        	VIBRO_OFF();            \
        	SET_OFF(ALARM_LED_1);                         		\
        	SET_OFF(ALARM_LED_2);}                        		\
      }
#define LED_SOUND_PLAY_CLEAR()                                  \
      {                                                         \
        flag_no_vibro = 0;                                      \
        flag_led_sound = 0;                                     \
        div_led_sound = 8;                                      \
        SOUND_OFF();               \
        VIBRO_OFF();            	\
        SET_OFF(ALARM_LED_1);                           		\
        SET_OFF(ALARM_LED_2);                           		\
      }
static uint8_t flag_led_sound = 0;
static uint8_t flag_no_vibro = 0;
static uint16_t div_led_sound = 8;
#else
void Countdown_Indication_Step(uint8_t is_final);
void Countdown_Clear(void);

static uint8_t flag_led_sound = 0;
static uint16_t div_led_sound = 0;
static uint8_t countdown_phase = 0; // 0 - LED1, 1 - LED2, 2 - Звук/Вибро

void Countdown_Indication_Step(uint8_t is_final)
{
    if (is_final) {
        // ФИНАЛ: Включаем всё сразу и выходим, игнорируя сбросы и таймеры
        SOUND_ON();
        VIBRO_ON();
        SET_OFF(ALARM_LED_1); // Светодиоды гасим, как ты и хотел
        SET_OFF(ALARM_LED_2);
        return;
    }

    uint16_t current_repeat = (flag_led_sound) ? 4 : 6;

    // --- ОБЫЧНЫЙ РЕЖИМ (до 0.5 сек) ---
    div_led_sound++;
    if (div_led_sound >= current_repeat) {
        div_led_sound = 0;
        flag_led_sound = !flag_led_sound;
        countdown_phase = 0;
    }

    // ГАСИМ ВСЁ ТОЛЬКО В ПАУЗЕ (когда flag_led_sound == 0)
    if (flag_led_sound == 0) {
        SOUND_OFF(); VIBRO_OFF();
        SET_OFF(ALARM_LED_1); SET_OFF(ALARM_LED_2);
        return;
    }

    if (countdown_phase == 0) {
        // 1-й тик: Звук + LED1 (LED2 выключен)
        SOUND_ON();
        VIBRO_OFF();
        SET_ON(ALARM_LED_1);
        SET_OFF(ALARM_LED_2);
    }
    else if (countdown_phase == 1) {
        // 2-й тик: Звук + LED2 (LED1 выключен)
        // Звук продолжает работать БЕЗ вызова SOUND_OFF в начале функции
        SOUND_ON();
        VIBRO_OFF();
        SET_OFF(ALARM_LED_1);
        SET_ON(ALARM_LED_2);
    }
    else {
        // Все последующие тики: Только Вибро
        SOUND_OFF();
        SET_OFF(ALARM_LED_1);
        SET_OFF(ALARM_LED_2);
        VIBRO_ON();
    }

    countdown_phase++;
}

void Countdown_Clear(void)
{
    flag_led_sound = 0;
    div_led_sound = 0;
    countdown_phase = 0;
    SOUND_OFF(); VIBRO_OFF();
    SET_OFF(ALARM_LED_1); SET_OFF(ALARM_LED_2);
}
#endif

void CountdownToShutdown(void)
{
  if(button.button_pressed_action == 1)
  {
    LCD_CountdownToShutdown((uint32_t)(TEST_TIMER_RUN(TIMER_SHUTDOWN)*TIME_DEL_1));

    if(TEST_TIMER_RUN(TIMER_SHUTDOWN) <= ((uint32_t)(0.5/TIME_DEL_1 + 0.5)))
    {
      //repeat = (flag_led_sound) ? 30:4;
//      flag_no_vibro = 1;
//    	flag_led_sound = 1;
    	Countdown_Indication_Step(1); // 1 - это флаг is_final
    }
    else
    {
    	Countdown_Indication_Step(0);
    }
//    LED_SOUND_PLAY();
    if(TEST_TIMER(TIMER_SHUTDOWN))
    {
      TurnOffSystem();
      //------------------------------------------------------------------------
      RESET_TIMER(TIMER_SHUTDOWN);
      button.button_pressed = 0;
      //------------------------------------------------------------------------
    }
  }
  else
  {
    RESET_TIMER(TIMER_SHUTDOWN);
    //--------------------------------------------------------------------------
    // Выключение индикации "выключение прибора"
//    LED_SOUND_PLAY_CLEAR();
    Countdown_Clear(); // Очистка
    //--------------------------------------------------------------------------
    if(TEST_TIMER_RTC(TIMER_RTC_WARM) || TEST_TIMER_RUN_RTC(TIMER_RTC_WARM)){
    	TO_GO_STATE_SCREEN(STATE_SCREEN_WARM);
    }
    else{
    	TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);
    }
  }
}

void CountdownToShutdownImit(void)
{
	//------------------------------------------------------------------------
	LCD_CountdownToShutdown((uint32_t)(TEST_TIMER_RUN(TIMER_SHUTDOWN)*TIME_DEL_1));
	//------------------------------------------------------------------------
	if(TEST_TIMER_RUN(TIMER_SHUTDOWN) <= ((uint32_t)(0.5/TIME_DEL_1 + 0.5)))
	{
	  //repeat = (flag_led_sound) ? 30:4;
//	  flag_no_vibro = 1;
//	  flag_led_sound = 1;
		Countdown_Indication_Step(1); // 1 - это флаг is_final
	}
	else
	{
		Countdown_Indication_Step(0);
	}
//	LED_SOUND_PLAY();

	if(TEST_TIMER(TIMER_SHUTDOWN))
	{
	  TurnOffSystem();
	  //------------------------------------------------------------------------
	  RESET_TIMER(TIMER_SHUTDOWN);
	  button.button_pressed = 0;
	  button.button_count = 0;
	  button.button_flag = 0;
	  //------------------------------------------------------------------------
	}
}

void CountdownToSOS(void)
{
#if CONFIG_LORA

  if(button.button_pressed_action == 1)
  {
    LCD_CountdownToSOS((uint32_t)(TEST_TIMER_RUN(TIMER_PRESS_SOS)*TIME_DEL_1), TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_SOS));

    if(TEST_TIMER_RUN(TIMER_PRESS_SOS) <= ((uint32_t)(0.5/TIME_DEL_1 + 0.5)))
    {
      //repeat = (flag_led_sound) ? 30:4;
//      flag_no_vibro = 1;
//      flag_led_sound = 1;
    	Countdown_Indication_Step(1); // 1 - это флаг is_final
    }
    else
    {
    	Countdown_Indication_Step(0);
    }
//	LED_SOUND_PLAY();
    if(TEST_TIMER(TIMER_PRESS_SOS))
    {
      RESET_TIMER(TIMER_PRESS_SOS);
      Countdown_Clear(); // Очистка
      //------------------------------------------------------------------------
      Button_Sos_Set();
      if(TEST_TIMER_RTC(TIMER_RTC_WARM) || TEST_TIMER_RUN_RTC(TIMER_RTC_WARM)){
    	  TO_GO_STATE_SCREEN(STATE_SCREEN_WARM);
      }
      else{
    	  TO_GO_STATE_SCREEN(state_screen_pre_sos);
      }
      //------------------------------------------------------------------------
    }
  }
  else
  {
    RESET_TIMER(TIMER_PRESS_SOS);
    //--------------------------------------------------------------------------
    // Выключение индикации "выключение прибора"
//    LED_SOUND_PLAY_CLEAR();
    Countdown_Clear(); // Очистка
    //--------------------------------------------------------------------------
    if(TEST_TIMER_RTC(TIMER_RTC_WARM) || TEST_TIMER_RUN_RTC(TIMER_RTC_WARM)){
    	TO_GO_STATE_SCREEN(STATE_SCREEN_WARM);
    }
    else{
    	TO_GO_STATE_SCREEN(state_screen_pre_sos);
    }
  }
#endif
}

void TurnOffSystem(void)
{
	if(device_turn == DEVICE_TURNED_OFF){
		return;
	}
	//------------------------------------------------------------------------
#if (CONFIG_TYPE_LCD == 2)
	ui_reset_visibility_flags();
#endif
    //------------------------------------------------------------------------
    // Выключение индикации "выключение прибора"
//    LED_SOUND_PLAY_CLEAR();
    Countdown_Clear(); // Очистка
#if(CONFIG_TYPE_LCD == 0)
    // Выключение индикации "выключение прибора"
    SET_OFF(BACKLIGHT_GREEN);                     // Выключается подсветка зеленая
    SET_OFF(BACKLIGHT_RED);						// Выключается подсветка красная
#endif
    //------------------------------------------------------------------------
    CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND);                  	// Выкл ф-ии блокировки звуки
    CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_DATA_EXCHANGE);               	// Выкл режим обмена данными
    for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
    	Sns_Cfg_struct.Sensor[chan_r].StateErr &=~ ERROR_MASK_ST_1;       	// Очистка ошибок сенсора
    	TurnOffPowerSensor(&Sns_Cfg_struct.Sensor[chan_r], chan_r);
    }
    Sns_Cfg_struct.Config_common.StateErr = 0;                				// Очиста общих ошибок
    SET_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_OFF)                    		// Установка статустного бита "Прибор выключен"
#if CONFIG_SAVE_PARAM_LOG
    TypeLogEventType_2 = TYPE_TURN_OFF;
    SaveEventType_2 = 1;
#endif
#if CONFIG_BLE
    // Деинициализация BLE
    Ble_DeInit(0);
#endif
#if CONFIG_LORA
    Lora_turnoff_system_set();
#endif
#if CONFIG_LORA_G
    Lora_g_turnoff_system_set();
#endif
    //------------------------------------------------------------------------
	device_turn = DEVICE_TURNED_OFF;	// Выключение
	TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);
	START_TIMER_RTC(TIMER_RTC_SOFT_RESET_PROGRAM, TIME_RTC_SOFT_RESET_PROGRAM*2);
	//------------------------------------------------------------------------
}
#undef LED_SOUND_PLAY_CLEAR
#undef LED_SOUND_PLAY

//===========================================================================================================================
// Включение питания на плате
void TurnOnPower(void)
{
	if(!READ_PIN_OUT(POWER_ON)){
		SET_ON(POWER_ON);
		Delay(10);
		SET_OFF(POWER_ON);
		Delay(10);
		SET_ON(POWER_ON);
		Delay(200);
	}
}

//===========================================================================================================================
// Выключение питания на плате
void TurnOffPower(void)
{
	if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA)){
#if CONFIG_LORA
		if(Lora_system_msg_read() == 1){
			return;
		}
#endif
#if CONFIG_LORA_G
		if(Lora_g_turnoff_system_read() == 1){
			return;
		}
#endif
	}

	if((SaveEventType_2 == 1) && !TEST_TIMER_RTC(TIMER_RTC_SOFT_RESET_PROGRAM)){
		return;
	}
	RESET_TIMER_RTC(TIMER_RTC_SOFT_RESET_PROGRAM);
	SET_OFF(POWER_ON);
	SET_OFF(POWER_ON_A);
	SET_OFF(POWER_ON_MCU);
#if (DEVICE_NUMBER == Device1)

#elif ((DEVICE_NUMBER == Device2_1))
	SET_OFF(WRLS_ON);
#elif ((DEVICE_NUMBER == Device2_2))

#elif ((DEVICE_NUMBER == Device2_Pro) || (DEVICE_NUMBER == Device2_Pro_1) || (DEVICE_NUMBER == Device3_Pro))
	SET_OFF(WRLS_ON);
#endif
}

//===========================================================================================================================
void Button_Run_Backlight(void)
{
#if((CONFIG_TYPE_LCD == 0))
	  if(TEST_TIMER_RTC(TIMER_RTC_LIGHT_RESET)){
		RESET_TIMER_RTC(TIMER_RTC_LIGHT_RESET);
	    button.button_pressed = 0;
	  }
#else
//    backlight_set(Sns_Cfg_struct.Config_common.SoundPWM);
    if((button.button_pressed) || (button2.button_pressed) || (button3.button_pressed)){
    	button.button_pressed = 0;
    	button2.button_pressed = 0;
    	button3.button_pressed = 0;
    	backlight_set(40);
    	START_TIMER_RTC(TIMER_RTC_LIGHT_RESET, TIME_RTC_LIGHT_RESET*12);
    }
    if(TEST_TIMER_RTC(TIMER_RTC_LIGHT_RESET)){
    	RESET_TIMER_RTC(TIMER_RTC_LIGHT_RESET);
    	backlight_set(5);
    }
#endif
}

//===========================================================================================================================
//Обработка нажатия кнопки, при включение
void Button_Run_Start(void)
{
	Button_Run_Backlight();
  if(button.button_count >= BUTTON_PRESS_LONG)
  {
	if(TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_CRITICAL_BATTERY)){
		START_TIMER_RTC(TIMER_RTC_OFF_BAT_CRITICAL_LOW, TIME_RTC_OFF_BAT_CRITICAL_LOW);
		button.button_count = 0;
		return;
	}
	//------------------------------------------------------------------------
	SET_ON(POWER_ON_A);
	SET_ON(POWER_ON_MCU);                                	// Включение(питание) платы
	//------------------------------------------------------------------------
	Clear_Timer_Rtc();
    //------------------------------------------------------------------------
    // Включение(питание) платы
	TurnOnPower();
	//------------------------------------------------------------------------
	Delay(200);
	//----------------------------------------------------------------------------
	// Инициализация LCD
    LCD_Init();
    //------------------------------------------------------------------------
#if CONFIG_LORA
    Lora_DeInit(0);
#endif
#if CONFIG_LORA_G
    Lora_g_DeInit(0);
#endif
#if CONFIG_GPS
    Gps_DeInit(0);
#endif
    //------------------------------------------------------------------------
#if 0
    // Вибро
    VIBRO_ON();
    Delay(500);
    VIBRO_OFF();
    // Короткий сигнал
    SOUND_ON();
    SET_ON(ALARM_LED_1);     SET_ON(ALARM_LED_2);
    Delay(200);
    SOUND_OFF();
    SET_OFF(ALARM_LED_1);    SET_OFF(ALARM_LED_2);
    Delay(200);
    // Длинный сигнал
    SOUND_ON();
    SET_ON(ALARM_LED_1);     SET_ON(ALARM_LED_2);
    Delay(800);
    SOUND_OFF();
    SET_OFF(ALARM_LED_1);    SET_OFF(ALARM_LED_2);
#else
    VIBRO_ON();
    Delay(500);
    VIBRO_OFF();

    Delay(200); // Пауза перед звуком

    // 2. Короткий сигнал (200 мс)
    SOUND_ON();
    SET_ON(ALARM_LED_1);    // Зажигаем только первый светодиод
    Delay(200);
    SOUND_OFF();
    SET_OFF(ALARM_LED_1);

    Delay(200); // Пауза между пиками

    // 3. Длинный сигнал (800 мс)
    SOUND_ON();
    SET_ON(ALARM_LED_2);    // Зажигаем только второй светодиод
    Delay(800);
    SOUND_OFF();
    SET_OFF(ALARM_LED_2);
#endif
    //------------------------------------------------------------------------
    button.button_count = 1;
    button.button_flag = 0;
    {
//    	Avis_Search_Temp();
    	Avis_Search_Temp_Start(0);
		state_flip = 15;
		TO_GO_STATE_SCREEN(STATE_SCREEN_WARM);
		START_TIMER_RTC(TIMER_RTC_WARM, 2);
		for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
			ReinitSensor(&Sns_Cfg_struct.Sensor[chan_r], chan_r);
		}
		device_turn = DEVICE_TURNED_ON_1;
    }
    START_TIMER_RTC(TIMER_RTC_LIGHT_RESET, TIME_RTC_LIGHT_RESET*12)                     // Таймер на выключение подсветки
    //вход в обычный режим работы!!!!!!!!!!!
    CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_OFF);
#if CONFIG_SAVE_PARAM_LOG
#if CONFIG_SAVE_PARAM_LOG_AT45
    exit_ultra_deep_power_down_memory();              // Выход из режима низкого потребления микросхемы памяти
    power_of_2_at64();								// Включить режим 256 байт
#endif
    TypeLogEventType_2 = TYPE_TURN_ON;
    SaveEventType_2 = 1;
#endif
  }
  else if(button.button_flag == 0){
	  button.button_count = 0;
  }
}

//===========================================================================================================================
//Обработка нажатия кнопки
void Button_Run(void)
{
#if((CONFIG_TYPE_LCD == 0))
	uint8_t state_flag_err = 0;
	for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
		if((Sns_Cfg_struct.Sensor[chan_r].StateErr & ERROR_MASK_ST_1_1)||
				(Sns_Cfg_struct.Config_common.StateErr & ERROR_MASK_ST_2)){
			state_flag_err = 1;
			chan_r = COUNT_CHAN;
		}
	}
    if(state_flag_err){
    	SET_OFF(BACKLIGHT_GREEN);       	// Включается подсветка красная и выключается зеленая подсветка
    	SET_ON(BACKLIGHT_RED);
    }
    else{
    	SET_OFF(BACKLIGHT_RED);
    	if(button.button_pressed == 1){
    		SET_ON(BACKLIGHT_GREEN);       	// Включается подсветка зеленая и выключается красная подсветка
    	}
    	else{
    		SET_OFF(BACKLIGHT_GREEN);
    	}
    }
	//------------------------------------------------------------------------
    Button_Sos();
	Button_Sos_Timer_Clr();
	//------------------------------------------------------------------------
  if(TEST_TIMER(TIMER_MAIN_SCREEN)){
    RESET_TIMER(TIMER_MAIN_SCREEN);
      if(button.button_count <= BUTTON_PRESS_SHORT){
    	  if(IS_STATE_SCREEN() == STATE_SCREEN_MEASURE_TO_MENU){
    		  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);
    	  }
    	  else if(IS_STATE_SCREEN() == STATE_SCREEN_WARM_TO_MENU){
    		  TO_GO_STATE_SCREEN(STATE_SCREEN_WARM);
    	  }
      }
  }
  if(TEST_TIMER_RTC(TIMER_RTC_MAIN_RESET) && (device_turn == DEVICE_TURNED_ON)){
	  if((IS_STATE_SCREEN() != STATE_SCREEN_CALIB_SPAN_START) &&
			  (IS_STATE_SCREEN() != STATE_SCREEN_CALIB_ZERO_START) &&
			  (IS_STATE_SCREEN() != STATE_SCREEN_BUMP_TEST_START)){
		  RESET_TIMER_RTC(TIMER_RTC_MAIN_RESET);
		  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);
		  RESET_TIMER_RTC(TIMER_RTC_DISP_FLASHING_ERR);               		// Сброс таймера мигание сегмента
		  RESET_TIMER(TIMER_DISP_FLASHING);                          		// Сброс таймера мигание сегмента
	  }
	  else{
		  START_TIMER_RTC(TIMER_RTC_MAIN_RESET, TIME_RTC_MAIN_RESET*2);               // Запуск таймера для возвращения на главный экран
	  }
  }
  //----------------------------------------------------------------------------
  Button_Run_Backlight();
  //----------------------------------------------------------------------------
  if(button.button_pressed == 1){
	  if(button.button_count){
		  START_TIMER_RTC(TIMER_RTC_LIGHT_RESET, TIME_RTC_LIGHT_RESET);                // Таймер на выключеине подсветки
	  }
    //----------------------------------------------------------------------
    Button_Menu();
    //----------------------------------------------------------------------
  }
#else
  //------------------------------------------------------------------------
  Button_Sos();
  Button_Sos_Timer_Clr();
  //------------------------------------------------------------------------
//    if(TEST_TIMER(TIMER_MAIN_SCREEN)){
//      RESET_TIMER(TIMER_MAIN_SCREEN);
//        if(button.button_count <= BUTTON_PRESS_SHORT){
//      	  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);
//        }
//    }
    if(TEST_TIMER_RTC(TIMER_RTC_MAIN_RESET) && (device_turn == DEVICE_TURNED_ON)){
  	  if((IS_STATE_SCREEN() != STATE_SCREEN_CALIB_SPAN_END) &&
  			  (IS_STATE_SCREEN() != STATE_SCREEN_CALIB_ZERO_END) &&
			  (IS_STATE_SCREEN() != STATE_SCREEN_CALIB_SPAN_END_MULTI) &&
			  (IS_STATE_SCREEN() != STATE_SCREEN_CALIB_ZERO_END_MULTI) &&
  			  (IS_STATE_SCREEN() != STATE_SCREEN_BUMP_TEST_END)){
  		  if((IS_STATE_SCREEN() == STATE_SCREEN_CALIB_SPAN_END_MULTI_ACK) ||
  				  (IS_STATE_SCREEN() == STATE_SCREEN_CALIB_ZERO_END_MULTI_ACK)){
  			Set_Exit_Calib();
  		  }
  		  RESET_TIMER_RTC(TIMER_RTC_MAIN_RESET);
  		  TO_GO_STATE_SCREEN(STATE_SCREEN_MEASURE);
  		  RESET_TIMER_RTC(TIMER_RTC_DISP_FLASHING_ERR);               		// Сброс таймера мигание сегмента
  		  RESET_TIMER(TIMER_DISP_FLASHING);                          		// Сброс таймера мигание сегмента
  	  }
    }

    //----------------------------------------------------------------------
    Button_Run_Backlight();
	//----------------------------------------------------------------------
	Button_Menu();
	//----------------------------------------------------------------------
#endif
	//----------------------------------------------------------------------
	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
		//Рассчет коэффициента перевода в отображаемую единицу измерения
		conversion_factor_rf[chan] = calculating_conversion_factor(&Sns_Cfg_struct.Sensor[chan]);
	}
	//----------------------------------------------------------------------
}

//===========================================================================================================================
void Button_Sos(void)
{
#if CONFIG_LORA
	uint16_t button_press_count = 0;
	if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA) && (get_state_start_flag_lora() == 1)){
		if(button_sos.button_flag == 1){
			if(IS_STATE_SCREEN() == STATE_SCREEN_MEASURE_TO_COUNTDOWN){
				return;
			}
			else if(IS_STATE_SCREEN() == STATE_SCREEN_MEASURE){
				button_press_count = BUTTON_PRESS_LONG_SOS;
			}
			else{
				button_press_count = BUTTON_PRESS_LONG_SOS;
			}
			if(button_sos.button_count >= button_press_count){
				button_sos.button_flag = 0;
				START_TIMER(TIMER_PRESS_SOS, TIME_PRESS_SOS);
				state_screen_pre_sos = IS_STATE_SCREEN();
				TO_GO_STATE_SCREEN(STATE_SCREEN_COUNTDOWN_SOS);
			}
		}
		else{
			button_sos.button_count = 0;
		}
	}
	else{
		if(TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_SOS)){
			Lora_SetControlLoraBit(ST_LORA_BIT_SOS, 0);
		}
	}
#endif
}

void Button_Sos_Set(void)
{
#if CONFIG_LORA
	if(TESTBIT(Lora_GetControlLora(), ST_LORA_BIT_SOS)){
		Lora_SetControlLoraBit(ST_LORA_BIT_SOS, 0);
	}
	else{
		Lora_SetControlLoraBit(ST_LORA_BIT_SOS, 1);
		START_TIMER_RTC(TIMER_RTC_COUNT_PRESS_SOS_CLR, TIME_RTC_COUNT_PRESS_SOS_CLR);
	}
#endif
}

void Button_Sos_Timer_Clr(void)
{
#if CONFIG_LORA
	if(TEST_TIMER_RTC(TIMER_RTC_COUNT_PRESS_SOS_CLR)){
		if((get_state_repeat_flag_lora() == 0)){
			RESET_TIMER_RTC(TIMER_RTC_COUNT_PRESS_SOS_CLR);
			Lora_SetControlLoraBit(ST_LORA_BIT_SOS, 0);
		}
	}
#endif
}


