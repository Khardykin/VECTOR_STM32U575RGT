#include "Avis_main.h"
#if CONFIG_NFS
  Message_buffer_nfc buffer_nfc = {0};
#endif
#if CONFIG_UART
  Message_buffer_uart buffer_uart[1] = {0};
#endif
#define TO_M_WORD(tw_data)		(((uint16_t)(*(tw_data)) << 8) + *((tw_data)+1))
#define DIV_4	1000
#define DIV_3	100
#define DIV_2	10
#define DIV_1	1
//======================================================================================================================================
uint8_t deveui_com[8] = {0};
//======================================================================================================================================
typedef enum {
    T_U16,   // Одиночный 16-бит
    T_U32,   // 32-бит (автоматическая запись LOWORD + HIWORD)
    T_STR,   // Массив char или uint16
    T_FLT4   // Float с делением на DIV_4
} reg_type_t;

typedef struct {
    uint16_t reg;   // Адрес регистра Modbus
    uint16_t off;   // Смещение в структуре
    uint8_t  type;  // Тип reg_type_t
    uint8_t  len;   // Длина в байтах (для массивов) или элементах
} reg_map_t;

// Карта регистров сенсора
static const reg_map_t sn_map[] = {
    {0x0102, offsetof(SNS_CFG_Type, channel), T_U16, 2},
    {0x0103, offsetof(SNS_CFG_Type, DeviceSetting), T_U16, 2},
    {0x0104, offsetof(SNS_CFG_Type, DeviceSetting1), T_U16, 2},
    {0x0105, offsetof(SNS_CFG_Type, DeviceSetting2), T_U16, 2},
    {0x0106, offsetof(SNS_CFG_Type, Prescaler_ConcCalibrGas), T_U32, 4},
    {0x0108, offsetof(SNS_CFG_Type, Limit), T_U32, 8}, // Limit[0..1]
    {0x010C, offsetof(SNS_CFG_Type, Limit[2]), T_U32, 8}, // Limit[2..3]
    {0x0110, offsetof(SNS_CFG_Type, Limit_Stel), T_U32, 4},
    {0x0112, offsetof(SNS_CFG_Type, Limit_Twa), T_U32, 4},
    {0x0114, offsetof(SNS_CFG_Type, Stel_interval), T_U16, 2},
    {0x0115, offsetof(SNS_CFG_Type, HisterezisLimit), T_U32, 4},
    {0x0117, offsetof(SNS_CFG_Type, LELCoeff), T_U32, 4},
    {0x0119, offsetof(SNS_CFG_Type, MGM3Coeff), T_U32, 4},
    {0x011B, offsetof(SNS_CFG_Type, Calibration_interval), T_U16, 2},
    {0x011D, offsetof(SNS_CFG_Type, Reserve1), T_STR, 6},
    {0x0120, offsetof(SNS_CFG_Type, MolecularFormula), T_STR, 10},
    {0x0125, offsetof(SNS_CFG_Type, Vzero), T_U16, 2},
    {0x0126, offsetof(SNS_CFG_Type, Vbias), T_U16, 2},
    {0x0127, offsetof(SNS_CFG_Type, Pga), T_U16, 2},
    {0x0128, offsetof(SNS_CFG_Type, Rfilter), T_U16, 2},
    {0x0129, offsetof(SNS_CFG_Type, Rload), T_U16, 2},
    {0x012A, offsetof(SNS_CFG_Type, Rtia), T_U16, 2},
    {0x0130, offsetof(SNS_CFG_Type, Limit_Conc_MaxRange), T_U32, 4},
    {0x0132, offsetof(SNS_CFG_Type, dead_zone), T_U32, 8},
    {0x0136, offsetof(SNS_CFG_Type, range_calibration_limitation), T_U16, 2},
    {0x0137, offsetof(SNS_CFG_Type, num_formula_gas), T_U16, 2},
    {0x0138, offsetof(SNS_CFG_Type, TimeWarm), T_U16, 2},
    {0x0150, offsetof(SNS_CFG_Type, gas_calibration_temp.PrescalerTemp_T), T_STR, 20},
    {0x015A, offsetof(SNS_CFG_Type, gas_calibration_temp.PrescalerTemp), T_FLT4, 10},
    {0x0164, offsetof(SNS_CFG_Type, gas_calibration.Concentration), T_STR, 20},
    {0x016E, offsetof(SNS_CFG_Type, gas_calibration.PrescalerConc), T_FLT4, 10},
    {0x0178, offsetof(SNS_CFG_Type, gas_calibration_temp_z.PrescalerTemp_T), T_STR, 20},
    {0x0182, offsetof(SNS_CFG_Type, gas_calibration_temp_z.PrescalerTemp), T_STR, 20},
    {0x0190, offsetof(SNS_CFG_Type, SerialNum), T_STR, 14},
    {0x0224, offsetof(SNS_CFG_Type, State), T_U32, 4},
    {0x0226, offsetof(SNS_CFG_Type, StateErr), T_U32, 4},
    {0x0228, offsetof(SNS_CFG_Type, StateSensorUART), T_U16, 2}
};

// Карта общих регистров (Config_common)
static const reg_map_t com_map[] = {
    {0x00FE, offsetof(SNS_CFG_Type_common, working_hours_offset), T_U32, 4},
    {0x0100, offsetof(SNS_CFG_Type_common, working_hours), T_U32, 4},
    {0x011C, offsetof(SNS_CFG_Type_common, Bump_interval), T_U16, 2},
    {0x012B, offsetof(SNS_CFG_Type_common, SerialLo), T_U16, 2},
    {0x012C, offsetof(SNS_CFG_Type_common, SerialHi), T_U16, 2},
    {0x012D, offsetof(SNS_CFG_Type_common, HardwareVersion), T_U16, 2},
    {0x012E, offsetof(SNS_CFG_Type_common, ProductionDate), T_U32, 4},
    {0x0200, offsetof(SNS_CFG_Type_common, SoundPWM), T_U16, 2},
    {0x0201, offsetof(SNS_CFG_Type_common, TimeLedStatePulse), T_U16, 2},
    {0x0202, offsetof(SNS_CFG_Type_common, TimeLedStatePeriod), T_U16, 2},
    {0x0203, offsetof(SNS_CFG_Type_common, battery_charge_volt_max), T_U16, 2},
    {0x0204, offsetof(SNS_CFG_Type_common, battery_charge_volt_min), T_U16, 2},
	{0x0205, offsetof(SNS_CFG_Type_common, battery_charge_volt_lim_1), T_U16, 2},
	{0x0206, offsetof(SNS_CFG_Type_common, battery_charge_volt_lim_2), T_U16, 2},
	{0x0207, offsetof(SNS_CFG_Type_common, Type_lcd), T_U16, 2},
    {0x0210, offsetof(SNS_CFG_Type_common, Archiveinterval), T_U16, 2},
    {0x0211, offsetof(SNS_CFG_Type_common, BegginAddrFile), T_U32, 4},
    {0x0213, offsetof(SNS_CFG_Type_common, CurrentAddrFile), T_U32, 4},
	{0x0215, offsetof(SNS_CFG_Type_common, ArchiveRecording), T_U16, 2},
    {0x0220, offsetof(SNS_CFG_Type_common, State), T_U32, 4},
    {0x0222, offsetof(SNS_CFG_Type_common, StateErr), T_U32, 4},
    {0x0310, offsetof(SNS_CFG_Type_common, Gps_type_nav), T_U32, 4},
    {0x0312, offsetof(SNS_CFG_Type_common, PeriodTimeLora), T_U16, 2},
    {0x0313, offsetof(SNS_CFG_Type_common, Lora_Config_Flags), T_U16, 2},
    {0x0314, offsetof(SNS_CFG_Type_common, Lora_freq_rx2), T_U32, 4},
    {0x0316, offsetof(SNS_CFG_Type_common, Lora_dr_rx2), T_U16, 2},
};

static const reg_map_t in_map[] = {
    {0x0012, offsetof(SNS_CFG_Type_common, DataLastBumpTest), T_U32, 4},
    {0x0014, offsetof(SNS_CFG_Type_common, Temperature), T_U32, 4},
    {0x0016, offsetof(SNS_CFG_Type_common, battery_charge_percent), T_U16, 2},
    {0x0017, offsetof(SNS_CFG_Type_common, battery_charge_volt), T_U16, 2},
    {0x0018, offsetof(SNS_CFG_Type_common, Pressure), T_U32, 4},
    {0x0020, offsetof(SNS_CFG_Type, ConcentrationCalibrGasCurrent), T_U32, 4},
    {0x0022, offsetof(SNS_CFG_Type, DataLastCalibration), T_U32, 4},
    {0x0024, offsetof(SNS_CFG_Type, Prescaler_coef), T_U32, 4},
    {0x0026, offsetof(SNS_CFG_Type, State), T_U32, 4},
    {0x0028, offsetof(SNS_CFG_Type, StateErr), T_U32, 4},
    {0x002A, offsetof(SNS_CFG_Type, StateSensorUART), T_U16, 2},
    {0x002B, offsetof(SNS_CFG_Type, adc_volt), T_U16, 2},
    {0x002C, offsetof(SNS_CFG_Type, adc_zero), T_U16, 2},
    {0x002D, offsetof(SNS_CFG_Type, ConcentrationVal), T_U32, 4},
    {0x002F, offsetof(SNS_CFG_Type, ConcentrationValTwa), T_U32, 4},
    {0x0031, offsetof(SNS_CFG_Type, ConcentrationValHigh), T_U32, 4},
    {0x0033, offsetof(SNS_CFG_Type, ConcentrationValNoDeadZ), T_U32, 4},
    {0x0035, offsetof(SNS_CFG_Type, ConcentrationValNoCorrect), T_U32, 4},
    {0x0037, offsetof(SNS_CFG_Type, temperature_calib), T_U16, 2},
    {0x0038, offsetof(SNS_CFG_Type, Temperature), T_U32, 4},
    {0x003A, offsetof(SNS_CFG_Type, Humidity), T_U32, 4}
};

//======================================================================================================================================
// Подсчет контрольной суммы
#define defCalcCRC( _DATA_, _CRC_)						\
        {									\
          _CRC_ = _CRC_ ^ _DATA_;						\
          for(uint8_t i=0; i<8; i++)						\
          {									\
            if((_CRC_ & 0x0001)==1) {_CRC_ >>= 1; _CRC_ ^= 0xA001;}		\
            else {_CRC_ >>= 1;}							\
          }									\
        }

uint16_t CreateCRC(uint8_t *DataCRC, uint16_t len)
  {
  	uint16_t crc_value = 0;
  	uint16_t CRCrtu = 0xFFFF;
  	for(uint16_t q=0; q<len; q++)
  	{
            defCalcCRC(DataCRC[q], CRCrtu);
  	}
  	DataCRC[len]   = LOBYTE(CRCrtu);
  	DataCRC[len+1] = HIBYTE(CRCrtu);
  	crc_value = (LOBYTE(CRCrtu) << 8) + HIBYTE(CRCrtu);
  	return crc_value;
  }

uint16_t CreateCRC_out(uint8_t *DataCRC, uint16_t len)
  {
  	uint16_t crc_value = 0;
  	uint16_t CRCrtu = 0xFFFF;
  	for(uint16_t q=0; q<len; q++)
  	{
            defCalcCRC(DataCRC[q], CRCrtu);
  	}
  	crc_value = (LOBYTE(CRCrtu) << 8) + HIBYTE(CRCrtu);
  	return crc_value;
  }
//======================================================================================================================================
void Uart_Command_Receive_Timer_Inc(void)
{
	for(uint8_t type = 0; type <= TYPE_USART; type++){
		if(buffer_uart[type].TimeRX){
			buffer_uart[type].TimeRX --;
			if(buffer_uart[type].TimeRX == 0){
				buffer_uart[type].TimeFlagRX  = 1;
			}
		}
	}
}
//======================================================================================================================================
//Функция заполнения буфера, для разбора команд по nfc
#if CONFIG_NFS
NFCTAG_StatusTypeDef statustypedef = NFCTAG_ERROR;
ST25DV_MB_CTRL_DYN_STATUS st25_mb_ctrl_dyn_status; 
void command_message_nfs(void)
{
  uint8_t err_num = 0;   
  uint16_t CRCcalc,CRCrec;
  uint8_t RXLen, TXlen = 0;
  st25_mb_ctrl_dyn_status.RfPutMsg = 0;  
  
  statustypedef = St25Dv_i2c_ExtDrv.ReadMBctrl_Dyn(&st25_mb_ctrl_dyn_status);
  if((statustypedef == NFCTAG_OK))
  {
    CRCcalc = 0;
  }
  if(((st25_mb_ctrl_dyn_status.CurrentMsg == ST25DV_RF_MSG) || st25_mb_ctrl_dyn_status.RfPutMsg) &&
     (!st25_mb_ctrl_dyn_status.HostPutMsg) &&
       (statustypedef == NFCTAG_OK))//  st25_mb_ctrl_dyn_status.HostMissMsg
  {
    while(St25Dv_i2c_ExtDrv.ReadMBLength_Dyn(&buffer_nfc.count_byter_r) != NFCTAG_OK)
    {
      err_num++;
      if(err_num > 3)
        return;
    }
    err_num = 0; 
    while(St25Dv_i2c_ExtDrv.ReadMailboxData(buffer_nfc.receive, 0, buffer_nfc.count_byter_r+1) != NFCTAG_OK)
    {
      err_num++;
      if(err_num > 3)
        return;
    }
    if((buffer_nfc.count_byter_r + 1) < 2){
      return;
    }
    if((buffer_nfc.receive[0] == 1)		||
    (buffer_nfc.receive[0] == 0x00)		||
    (buffer_nfc.receive[0] == 0xFF))
    {
      RXLen = buffer_nfc.count_byter_r+1;
      CRCcalc = CreateCRC_out(buffer_nfc.receive, (RXLen - 2));
      CRCrec = (buffer_nfc.receive[buffer_nfc.count_byter_r - 1] << 8) + (buffer_nfc.receive[buffer_nfc.count_byter_r]);        
      if(CRCcalc == CRCrec)
      {        
        TXlen = parsing_command(buffer_nfc.receive, RXLen, buffer_nfc.transmit);
        err_num = 0; 
        while(St25Dv_i2c_ExtDrv.WriteMailboxData(buffer_nfc.transmit, TXlen) != NFCTAG_OK)
        {
          err_num++;
          if(err_num > 3)
            return;
        }
      }
      //memset((char*)buffer_nfc.receive, 0, 256);
    }
  }
}
#endif
//======================================================================================================================================
#if CONFIG_UART
static uint8_t parsing_command_activ(uint8_t *mas, uint16_t len, uint8_t type_transmit)
{
	if((mas[0]=='A')&&(mas[1]=='C')&&(mas[2]=='T')&&(mas[3]=='I')&&(mas[4]=='V')){
		transmit_buffer(mas, len, type_transmit);
	}
	else if((mas[0]=='D')&&(mas[1]=='E')&&(mas[2]=='A')&&(mas[3]=='C')&&(mas[4]=='T')){
		transmit_buffer(mas, len, type_transmit);
	}
	else{
		return 1;
	}
	return 0;
}
//Функция заполнения буфера, для разбора команд
//uint8_t mirax_mas[5] = {"MIRAX"};
uint8_t CRC_ERRRR = 0;
//uint16_t RXLen, TXlen;
void command_message(void)
{
	for(uint8_t type = 0; type <= TYPE_USART; type++){
		if(buffer_uart[type].TimeFlagRX){
			buffer_uart[type].TimeFlagRX = 0;
			//--------------------------------------------------------------------
			while(check_buffer(&InputBuffer[type]) && (buffer_uart[type].count_byter_r < 256)){
				receive_buffer(&InputBuffer[type], &buffer_uart[type].receive[buffer_uart[type].count_byter_r]);
				buffer_uart[type].count_byter_r++;
			}
			//--------------------------------------------------------------------
#if 0
			RXLen = buffer_uart[type].count_byter_r;
			for(uint16_t i = 0; i < RXLen; i++){
				if(buffer_uart[type].receive[i] != 0x55){
					SET_ON(STATE_LED);
					Delay(10);
					SET_OFF(STATE_LED);
				}
			}
			transmit_buffer(buffer_uart[type].receive, RXLen, type);
			buffer_uart[type].count_byter_r = 0;
#else
#if TEST_GPS
//			transmit_buffer(buffer_uart[type].receive, buffer_uart[type].count_byter_r, type);
			if(buffer_uart[type].receive[0] == 0x99){
				start_gps_read = ~start_gps_read;
				buffer_uart[type].count_byter_r = 0;
				return;
			}
			if(buffer_uart[type].receive[0] == 0x98){
				Sns_Cfg_struct.Config_common.Gps_type_nav = (buffer_uart[type].receive[4])+
						(buffer_uart[type].receive[3]<<8)+
						(buffer_uart[type].receive[2]<<16)+
						(buffer_uart[type].receive[1]<<24);
				Gps_Init_Nav_Sys(&Sns_Cfg_struct);
				SaveConfig = 1;
				buffer_uart[type].count_byter_r = 0;
				return;
			}
			if(buffer_uart[type].receive[0] == 0xF1){
				transmit_buffer(buffer_uart[type].receive, buffer_uart[type].count_byter_r, TYPE_GPS);
				buffer_uart[type].count_byter_r = 0;
				return;
			}
//			if(start_gps_read){
//				buffer_uart[type].count_byter_r = 0;
//				return;
//			}
#endif
#if LORA_DEBUG
			if(buffer_uart[type].receive[0] == 0x99){
				start_lora_read = ~start_lora_read;
				transmit_buffer(buffer_uart[type].receive, buffer_uart[type].count_byter_r, TYPE_USART);
				buffer_uart[type].count_byter_r = 0;
				return;
			}
			if(buffer_uart[type].receive[0] == 0xF1){
				transmit_buffer(&buffer_uart[type].receive[1], buffer_uart[type].count_byter_r-1, TYPE_LORA);
				transmit_buffer(&buffer_uart[type].receive[1], buffer_uart[type].count_byter_r-1, TYPE_USART);
				buffer_uart[type].count_byter_r = 0;
				return;
			}
			if(buffer_uart[type].receive[0] == 0xF2){
				transmit_buffer(&buffer_uart[type].receive[1], buffer_uart[type].count_byter_r-1, TYPE_USART);
				Sns_Cfg_struct.Config_common.PeriodTimeLora = (buffer_uart[type].receive[1]<<8) + buffer_uart[type].receive[2];
				SaveConfig = 1;
				buffer_uart[type].count_byter_r = 0;
				return;
			}
			if(buffer_uart[type].receive[0] == 0xF3){
				Lora_DebugPrintStatus();
				return;
			}
#endif
			uint32_t address_modbus = ((Sns_Cfg_struct.Config_common.SerialHi<<16) | Sns_Cfg_struct.Config_common.SerialLo)%100;
			if(address_modbus == 0){
				address_modbus = 100;
			}
			if(buffer_uart[type].count_byter_r > 2){
				if(parsing_command_activ(buffer_uart[type].receive, buffer_uart[type].count_byter_r, type)){
					if((buffer_uart[type].receive[0] == address_modbus) ||
							(buffer_uart[type].receive[0] == 0x00) ||
							(buffer_uart[type].receive[0] == 0xFF))
					{
						uint16_t CRCcalc,CRCrec;
						uint16_t RXLen, TXlen;
						RXLen = buffer_uart[type].count_byter_r;
						CRCcalc = CreateCRC_out(buffer_uart[type].receive, (RXLen - 2));
						CRCrec = (buffer_uart[type].receive[RXLen - 2] << 8) + (buffer_uart[type].receive[RXLen - 1]);
						buffer_uart[type].count_byter_r = 0;
						if(CRCcalc == CRCrec){
							TXlen = parsing_command(buffer_uart[type].receive, RXLen, buffer_uart[type].transmit);
							transmit_buffer(buffer_uart[type].transmit, TXlen, type);
						}
						else{
							CRC_ERRRR = 1;
						}
					}
				}
				buffer_uart[type].count_byter_r = 0;
			}
#endif
			//--------------------------------------------------------------------
		}
	}
}
#endif
//======================================================================================================================================
//Основной обработчик команд
uint16_t parsing_command(uint8_t *mas, uint16_t len, uint8_t *out)
{
  //uint16_t tmp;
  uint16_t AnswerLen = 0;	// Длина ответа (в байтах)
  uint8_t cmd = mas[1];
  bool  is_state_prog = false;

#if CONFIG_FREERTOS
  bool  is_mutex_taken = false;
  if (cmd == 0x03 || cmd == 0x04 || cmd == 0x10) {
    if (xSemaphoreTake(xSemaphore_measure, (TickType_t)TIME_MUTEX_DELAY_MEASURE) != pdTRUE) {
      // Опционально: сформировать Modbus ошибку 0x06 (Device Busy) вместо тишины
      return 0;
    }
    is_mutex_taken = true; // Запоминаем, что мьютекс успешно взят
  }
#endif

  memset((char*)out, 0, 256);
  { // Если контрольная сумма правильная то обработка
    switch (cmd){
    //--------------------------------------------------------------------
    case 0x03: // Чтение группы выходных регистров
    	AnswerLen = CmdFunc3(mas, len, out);      break;
    //--------------------------------------------------------------------
    case 0x04: // Чтение группы входных регистров
    	AnswerLen = CmdFunc4(mas, len, out);	break;
    //--------------------------------------------------------------------
    //case 0x06: // Запись одного выходного регистра
    	//AnswerLen = CmdFunc6(mas, len, out);	break;
    //--------------------------------------------------------------------
    case 0x10: // Запись группы выходных  регистров
    	AnswerLen = CmdFunc16(mas, len, out);	break;
    //-------------------------------------------------------------------- 
    case 0x0C: // Чтение журнала событий
    	AnswerLen = CmdFunc12(mas, len, out);
    	is_state_prog = true;
        break;
      //--------------------------------------------------------------------
    case 0x0E: // Запись прошивки в флеш контроллера
    	AnswerLen = CmdFunc14(mas, len, out);
    	is_state_prog = true;
    	break;
    //--------------------------------------------------------------------
    case 0x0F: // Запись прошивки в микросхему памяти
//    	SET_ON(STATE_LED);
    	AnswerLen = CmdFunc15(mas, len, out);
//    	SET_OFF(STATE_LED);
    	is_state_prog = true;
        break;
    case 0x14: // Чтение номеов журнала по фильтрам
    	AnswerLen = CmdFunc20(mas, len, out);	break;
    default: AnswerLen = 0;
    } // switch (mas[1])

    if(is_state_prog){
		if(sys_state.process == 1){
#if((CONFIG_TYPE_LCD == 0))
		  sys_state.process = 2;
#else
		  sys_state.process = 0;
		  if((cmd == 0x0E) || (cmd == 0x0F)){
			  sys_state.processType = 1;
		  }
		  else{
			  sys_state.processType = 0;
		  }
#endif
		}
    }
#if CONFIG_FREERTOS
    if (is_mutex_taken) {
      xSemaphoreGive(xSemaphore_measure);
    }
#endif
    if(AnswerLen){
    	CreateCRC(out, AnswerLen);
    	return(AnswerLen+2);
    }
  }
  return(AnswerLen);
}

//======================================================================================================================================
uint16_t Count;			// Количество регистров
uint8_t *Value;			// Выходной поток
uint8_t *Countuint8_t;	// Счётчик
uint16_t AnswerLen;		// Длина ответа (в байтах)
static inline uint8_t CMD_PUT(uint16_t DATA) {
    Value[0] = (uint8_t)(DATA >> 8);
    Value[1] = (uint8_t)DATA;
    (*Countuint8_t) += 2;
    Value += 2;
    AnswerLen += 2;
    return (--Count == 0); // Возвращает 1, если данных больше не требуется
}

static inline uint8_t CMD_SAVE(uint16_t *DATA) {
    HIBYTE(*DATA) = Value[0];
    LOBYTE(*DATA) = Value[1];
    Value += 2;
    Count--;
    return (Count == 0); // Возвращает 1, если данных больше нет
}

// Расчет количества регистров, занимаемых элементом карты
static inline uint8_t get_reg_span(const reg_map_t *m) {
    if (m->type == T_FLT4) {
        return m->len;
    }
    // Для T_U32, T_STR, T_U16
    return (m->len + 1) / 2;
}
//------------------------------------------------------------------------------
// Чтение группы выходных регистров
__attribute__((noinline)) uint16_t CmdFunc3(uint8_t *mas, uint16_t len, uint8_t *out) {
    uint16_t tmp = 0, tmp1 = 0;
    uint16_t Address = TO_M_WORD(mas + 2);
    uint16_t sub_addr = Address & 0x0FFF;
    uint8_t chan = (Address >> 12) & 0x0F;
    uint16_t total_count = TO_M_WORD(mas + 4);

    Count = total_count;
    out[0] = mas[0];
    out[1] = mas[1];
    Value = &out[3];
    Countuint8_t = &out[2];
    AnswerLen = 3;
    *Countuint8_t = 0;

    if (chan >= COUNT_CHAN && ((chan != (NOCHAN>>4)))) {
        out[1] |= 0x80; out[2] = 2; return 3;
    }

    SNS_CFG_Type *pSn = &Sns_Cfg_struct.Sensor[chan];
    SNS_CFG_Type_common *pCom = &Sns_Cfg_struct.Config_common;

    // --- ФАЗА 1: ВАЛИДАЦИЯ ---
    for (uint16_t i = 0; i < total_count; i++) {
        uint16_t cur_check = sub_addr + i;
        bool found = false;
        const reg_map_t *check_maps[] = {sn_map, com_map};
        uint8_t check_sizes[] = {sizeof(sn_map)/sizeof(reg_map_t), sizeof(com_map)/sizeof(reg_map_t)};

        for (uint8_t m_idx = 0; m_idx < 2; m_idx++) {
            for (uint8_t j = 0; j < check_sizes[m_idx]; j++) {
                const reg_map_t *m = &check_maps[m_idx][j];
                uint8_t span = get_reg_span(m);
                if (cur_check >= m->reg && cur_check < (m->reg + span)) { found = true; break; }
            }
            if (found) break;
        }
        if (!found) {
            switch (cur_check) {
                case 0x0001: case 0x0002: case 0x0003: case 0x0004: case 0x0005: case 0x0006:
                case 0x0317: case 0x0318: case 0x0319: case 0x031A: case 0x031B: case 0x031C: case 0x031D: case 0x031E:
                case 0x0300: case 0x0301:
                case 0x0700:
                case 0x0F00:
                	found = true; break;
            }
        }
        if (!found) { out[1] |= 0x80; out[2] = 2; return 3; }
    }

    // --- ФАЗА 2: СБОР ДАННЫХ ---
    while (Count > 0) {
        uint16_t current_addr = sub_addr + (total_count - Count);
        const reg_map_t *target_map = NULL;
        uint8_t map_size = 0;
        void *base_ptr = NULL;

        // ПРОВЕРКА НА ПРИНАДЛЕЖНОСТЬ К com_map (ОБЩИЕ РЕГИСТРЫ)
        bool is_common = false;

        if ((current_addr >= 0x00FE && current_addr <= 0x0101) ||
            (current_addr == 0x011C) ||
            (current_addr >= 0x012B && current_addr <= 0x012F) ||
            (current_addr >= 0x0200 && current_addr <= 0x0223) ||
            (current_addr >= 0x0310 && current_addr <= 0x0316)) {
            is_common = true;
        }

        if (is_common) {
            target_map = com_map; map_size = sizeof(com_map)/sizeof(reg_map_t); base_ptr = pCom;
        } else {
            target_map = sn_map; map_size = sizeof(sn_map)/sizeof(reg_map_t); base_ptr = pSn;
        }

        bool handled = false;
        for (uint8_t i = 0; i < map_size; i++) {
            const reg_map_t *m = &target_map[i];
            uint8_t reg_span = get_reg_span(m);

            if (current_addr >= m->reg && current_addr < (m->reg + reg_span)) {
                uint8_t *pVarStart = (uint8_t*)base_ptr + m->off;
                // Смещение в регистрах ОТНОСИТЕЛЬНО начала записи в карте
                uint16_t reg_offset = current_addr - m->reg;

                if (m->type == T_U32) {
                    // Используем указатель uint16_t и смещение.
                    // На Little-Endian (Artery): reg_offset 0 -> LOWORD, 1 -> HIWORD.
                    // Это совпадает с вашим QT (Lo по первому адресу, Hi по второму).
                    uint16_t *pW = (uint16_t*)pVarStart + reg_offset;
                    CMD_PUT(*pW);
                } else if (m->type == T_FLT4) {
                    // Обратное преобразование float (len в карте - это регистры)
                    float *pF = (float*)pVarStart;
                    CMD_PUT((uint16_t)(pF[reg_offset] * DIV_4));
                } else {
                    // T_U16 и T_STR: reg_offset * 2 дает смещение в байтах
                    uint16_t *pW = (uint16_t*)(pVarStart + (reg_offset * 2));
                    CMD_PUT(*pW);
                }
                handled = true; break;
            }
        }

        if (!handled) {
            switch (current_addr) {
                case 0x0001: case 0x0002: CMD_PUT(0); break;
                case 0x0003:
#if CONFIG_SAVE_PARAM_LOG
#if CONFIG_FREERTOS
                	if(xSemaphoreTake(xSemaphore_flash, (TickType_t)TIME_MUTEX_DELAY_FLASH) == pdTRUE){
#endif
						ReadCurrentParamFirmware(&tmp, &tmp1);
#if CONFIG_FREERTOS
						xSemaphoreGive(xSemaphore_flash);
                	}
#endif
#endif
                    CMD_PUT(tmp); break;
                case 0x0004: CMD_PUT(tmp1); break;
                case 0x0005: CMD_PUT(sys_state.soft_reset); break;
                case 0x0006: CMD_PUT(TamperReset); break;
                case 0x0300: CMD_PUT(pSn->adc_zero); break;
                case 0x0301: CMD_PUT(pSn->adc_calib_value); break;
#if CONFIG_LORA
                case 0x0317:
                    CMD_PUT(to_word(appkey[0]));
                    break;
                case 0x0318:
                    CMD_PUT(to_word(appkey[2]));
                    break;
                case 0x0319:
                    CMD_PUT(to_word(appkey[4]));
                    break;
                case 0x031A:
                    CMD_PUT(to_word(appkey[6]));
                    break;
                case 0x031B:
                    CMD_PUT(to_word(appkey[8]));
                    break;
                case 0x031C:
                    CMD_PUT(to_word(appkey[10]));
                    break;
                case 0x031D:
                    CMD_PUT(to_word(appkey[12]));
                    break;
                case 0x031E:
                    CMD_PUT(to_word(appkey[14]));
                    break;
#else
                case 0x0317:
                case 0x0318:
                case 0x0319:
                case 0x031A:
                case 0x031B:
                case 0x031C:
                case 0x031D:
                case 0x031E:
                    CMD_PUT(tmp);
                    break;
#endif
                case 0x0700: CMD_PUT((uint16_t)Sns_Cfg_struct.application_language); break;
                case 0x0F00: CMD_PUT(MIRAX_BACS_BUILD); break;
                default:
                	out[1] |= 0x80; out[2] = 2; return 3; // Ошибка 02: Illegal Data Address
                break;
            }
        }
    }
    return AnswerLen;
}

//======================================================================================================================================
//------------------------------------------------------------------------------
// Чтение группы входных регистров
__attribute__((noinline)) uint16_t CmdFunc4(uint8_t *mas, uint16_t len, uint8_t *out) {
    uint16_t tmp = 0;
    uint16_t Address = TO_M_WORD(mas + 2);
    uint16_t sub_addr = Address & 0x0FFF;
    uint8_t chan = (Address >> 12) & 0x0F;
    uint16_t total_count = TO_M_WORD(mas + 4);

    // Инициализация указателей для CMD_PUT
    Count = total_count;
    out[0] = mas[0];
    out[1] = mas[1];
    Value = &out[3];
    Countuint8_t = &out[2];
    AnswerLen = 3;
    *Countuint8_t = 0;

    // 1. Проверка корректности канала
    if (chan >= COUNT_CHAN && ((chan != (NOCHAN>>4)))) {
        out[1] |= 0x80; out[2] = 2; return 3;
    }

    SNS_CFG_Type *pSn = &Sns_Cfg_struct.Sensor[chan];
    SNS_CFG_Type_common *pCom = &Sns_Cfg_struct.Config_common;

    // --- ФАЗА 1: ВАЛИДАЦИЯ (ПРОВЕРКА НА ПРОВАЛЫ)---
    for (uint16_t i = 0; i < total_count; i++) {
        uint16_t cur = sub_addr + i;
        bool found = false;

        // Проверка в карте регистров
        for (uint8_t j = 0; j < (sizeof(in_map) / sizeof(reg_map_t)); j++) {
            const reg_map_t *m = &in_map[j];

            // Универсальный расчет занимаемых регистров
            uint8_t span = get_reg_span(m);

            if (cur >= m->reg && cur < (m->reg + span)) {
                found = true;
                break;
            }
        }

        // Проверка специальных/системных адресов вне карты
        if (!found) {
            switch(cur) {
                case 0x0010: case 0x0011: case 0x0999:
                case 0x0060: case 0x0061: case 0x0062:
                case 0x0070: case 0x0071: case 0x0072: case 0x0073:
                    found = true; break;
            }
        }

        if (!found) {
            out[1] |= 0x80; out[2] = 2; // Illegal Data Address
            return 3;
        }
    }

    // --- ФАЗА 2: СБОР ДАННЫХ ---
    while (Count > 0) {
        uint16_t cur = sub_addr + (total_count - Count);
        bool handled = false;

        for (uint8_t j = 0; j < sizeof(in_map)/sizeof(reg_map_t); j++) {
            const reg_map_t *m = &in_map[j];

            // Считаем span так же, как в валидации
            uint8_t span = get_reg_span(m);

            if (cur >= m->reg && cur < (m->reg + span)) {
                void *base = (cur >= 0x0020) ? (void*)pSn : (void*)pCom;

                // ПРАВИЛЬНЫЙ РАСЧЕТ:
                // Вычисляем смещение в регистрах ОТНОСИТЕЛЬНО начала записи в карте
                uint16_t reg_offset = cur - m->reg;

                // Приводим указатель к uint16_t* и просто прибавляем смещение.
                // На Little-Endian (Artery/ARM) это даст:
                // reg_offset 0 -> LOWORD (Lo)
                // reg_offset 1 -> HIWORD (Hi)
                uint16_t *pW = (uint16_t*)((uint8_t*)base + m->off) + reg_offset;

                CMD_PUT(*pW);
                handled = true;
                break;
            }
        }

        // 2. Обработка спец-регистров (константы, массивы MAC/EUI)
        if (!handled) {
            switch(cur) {
                case 0x0010: CMD_PUT(DEVICE_NUMBER_COM); break;
                case 0x0011: case 0x0999: CMD_PUT(FIRMWARE_VERSION); break;
                // Обработка MAC-адреса BLE
                case 0x0060:
#if CONFIG_BLE
                    CMD_PUT(to_word(MACAddrBle[0]));
#else
                    CMD_PUT(0);
#endif
                    break;
                case 0x0061:
#if CONFIG_BLE
                    CMD_PUT(to_word(MACAddrBle[1]));
#else
                    CMD_PUT(0);
#endif
                    break;
                case 0x0062:
#if CONFIG_BLE
                    CMD_PUT(to_word(MACAddrBle[2]));
#else
                    CMD_PUT(0);
#endif
                    break;
                // Обработка LORA EUI
                case 0x0070: case 0x0071: case 0x0072: case 0x0073:
#if (CONFIG_LORA || CONFIG_LORA_G)
                    CMD_PUT(to_word(deveui_com[cur - 0x0070]));
#else
                    CMD_PUT(0);
#endif
                    break;
                default:
                	out[1] |= 0x80; out[2] = 2; return 3; // Ошибка 02: Illegal Data Address
                break;
            }
        }
    }

    return AnswerLen;
}

//======================================================================================================================================
__attribute__((noinline)) uint16_t CmdFunc16(uint8_t *mas, uint16_t len, uint8_t *out) {
    uint16_t tmp, state_f = 0, size_f = 0, flag_update = 0;
    uint16_t Address = TO_M_WORD(mas + 2);
    uint16_t sub_addr = Address & 0x0FFF;
    uint8_t chan = (Address >> 12) & 0x0F;

    uint16_t total_count = TO_M_WORD(mas + 4);
    Count = total_count;
    Value = &(mas[7]);

    if (chan >= COUNT_CHAN && ((chan != (NOCHAN>>4)))) {
        out[1] |= 0x80; out[2] = 2; return 3;
    }

    // --- ФАЗА 1: ПРОВЕРКА НА ПРОВАЛЫ ---
    // Проверяем существование каждого регистра в запрошенном диапазоне перед записью
    for (uint16_t i = 0; i < total_count; i++) {
        uint16_t current_check_addr = sub_addr + i;
        bool found = false;

        const reg_map_t *check_maps[] = {sn_map, com_map};
        uint8_t check_sizes[] = {sizeof(sn_map)/sizeof(reg_map_t), sizeof(com_map)/sizeof(reg_map_t)};

        for (uint8_t m_idx = 0; m_idx < 2; m_idx++) {
            for (uint8_t j = 0; j < check_sizes[m_idx]; j++) {
                const reg_map_t *m = &check_maps[m_idx][j];

                // УНИВЕРСАЛЬНЫЙ РАСЧЕТ SPAN (совместим с массивами U32 и FLT4)
                uint8_t span = get_reg_span(m);

                if (current_check_addr >= m->reg && current_check_addr < (m->reg + span)) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        if (!found) {
            switch (current_check_addr) {
            	case 0x0001: case 0x0003: case 0x0004: case 0x0005: case 0x0006:
                case 0x0300: case 0x0301:
                case 0x0240: case 0x0250: case 0x0252: case 0x0254: case 0x0256:
                case 0x0317: case 0x0318: case 0x0319: case 0x031A: case 0x031B: case 0x031C: case 0x031D: case 0x031E:
                case 0x0700: case 0x0800:
                case 0x0F00:
                    found = true; break;
            }
        }

        if (!found) {
            out[1] |= 0x80; out[2] = 2; return 3; // Ошибка 02: Illegal Data Address
        }
    }

    // --- ФАЗА 2: ЗАПИСЬ ДАННЫХ ---
    SNS_CFG_Type *pSn = &Sns_Cfg_struct.Sensor[chan];
    SNS_CFG_Type_common *pCom = &Sns_Cfg_struct.Config_common;
    memcpy(out, mas, 6);

    while (Count > 0) {
        uint16_t current_addr = sub_addr + (total_count - Count);
        const reg_map_t *target_map = NULL;
        uint8_t map_size = 0;
        void *base_ptr = NULL;

        // ПРОВЕРКА НА ПРИНАДЛЕЖНОСТЬ К com_map (ОБЩИЕ РЕГИСТРЫ)
        bool is_common = false;

        if ((current_addr >= 0x00FE && current_addr <= 0x0101) ||
            (current_addr == 0x011C) ||
            (current_addr >= 0x012B && current_addr <= 0x012F) ||
            (current_addr >= 0x0200 && current_addr <= 0x0223) ||
            (current_addr >= 0x0310 && current_addr <= 0x0316)) {
            is_common = true;
        }

        if (is_common) {
            target_map = com_map; map_size = sizeof(com_map)/sizeof(reg_map_t); base_ptr = pCom;
        } else {
            target_map = sn_map; map_size = sizeof(sn_map)/sizeof(reg_map_t); base_ptr = pSn;
        }

        bool handled = false;
		for (uint8_t i = 0; i < map_size; i++) {
			const reg_map_t *m = &target_map[i];

			// 1. Уточняем расчет span (для FLT4 len — это уже регистры, для остальных — байты)
			uint8_t reg_span = get_reg_span(m);

			if (current_addr >= m->reg && current_addr < (m->reg + reg_span)) {
				if (current_addr == 0x0103) flag_update = 1;

				// Указатель на начало данных переменной в структуре
				uint8_t *pVarStart = (uint8_t*)base_ptr + m->off;

				// КЛЮЧЕВОЙ МОМЕНТ: Смещение в регистрах от начала переменной
				uint16_t reg_offset = current_addr - m->reg;

				if (m->type == T_U32) {
					// Правильный расчет: приводим к uint16_t* и прибавляем смещение в регистрах
					uint16_t *pW = (uint16_t*)pVarStart + reg_offset;
					CMD_SAVE(pW);
				}
				else if (m->type == T_FLT4) {
					// 1. Сначала считываем 16-битное число из пакета в tmp
					CMD_SAVE(&tmp);
					// 2. Делим значение на 4 и кладем в float массив
					((float*)pVarStart)[reg_offset] = (float)tmp / DIV_4;
				}
				else {
					// T_U16 и T_STR: reg_offset * 2 дает точное смещение в байтах
					uint16_t *pW = (uint16_t*)(pVarStart + (reg_offset * 2));
					CMD_SAVE(pW);
				}
				handled = true;
				break;
			}
		}

        if (!handled) {
            switch (current_addr) {
                case 0x0001: CMD_SAVE(&FlashErase); break;
                case 0x0002: CMD_SAVE(&tmp); break;
                case 0x0003: CMD_SAVE(&state_f); break;
                case 0x0004: CMD_SAVE(&size_f); break;
                case 0x0005: START_TIMER_RTC(TIMER_RTC_SOFT_RESET_PROGRAM, TIME_RTC_SOFT_RESET_PROGRAM); CMD_SAVE(&sys_state.soft_reset); break;
                case 0x0006: CMD_SAVE(&TamperReset); break;
                case 0x0240: CMD_SAVE(&tmp); SaveConfig = (uint8_t)tmp; break;
                case 0x0250: CMD_SAVE(&tmp); SaveConfigDefault = (uint8_t)tmp; break;
                case 0x0252: CMD_SAVE(&tmp); ReadConfigDefault = (uint8_t)tmp; break;
                case 0x0254: CMD_SAVE(&tmp); CLRBIT(pCom->State, ST_COMMON_BIT_DATA_EXCHANGE);break;
                case 0x0256: CMD_SAVE(&tmp); tempsensor_calib.temperature = (float)tmp / DIV_3; tempsensor_calib.flag = 1; break;
                case 0x0300: CMD_SAVE(&pSn->adc_volt); adc_zero_calibration(pSn); break;
                case 0x0301: CMD_SAVE(&pSn->adc_calib_value); pSn->adc_volt_result = (pSn->adc_calib_value - pSn->adc_zero); adc_span_calibration(pSn, CALIB_SENS_NO_CHECK); break;
#if CONFIG_LORA
                case 0x0317:
                    CMD_SAVE((uint16_t*)&(appkey[0]));
                    break;
                case 0x0318:
                    CMD_SAVE((uint16_t*)&(appkey[2]));
                    break;
                case 0x0319:
                    CMD_SAVE((uint16_t*)&(appkey[4]));
                    break;
                case 0x031A:
                    CMD_SAVE((uint16_t*)&(appkey[6]));
                    break;
                case 0x031B:
                    CMD_SAVE((uint16_t*)&(appkey[8]));
                    break;
                case 0x031C:
                    CMD_SAVE((uint16_t*)&(appkey[10]));
                    break;
                case 0x031D:
                    CMD_SAVE((uint16_t*)&(appkey[12]));
                    break;
                case 0x031E:
                    CMD_SAVE((uint16_t*)&(appkey[14]));
                    break;
#else
                case 0x0317:
                case 0x0318:
                case 0x0319:
                case 0x031A:
                case 0x031B:
                case 0x031C:
                case 0x031D:
                case 0x031E:
                	CMD_SAVE(&tmp);
                    break;
#endif
                case 0x0700: CMD_SAVE(&tmp); Sns_Cfg_struct.application_language = (APPLICATIONLANGUAGE)tmp; break;
                case 0x0800: CMD_SAVE(&tmp); END_TIMER(TIMER_SHUTDOWN);TO_GO_STATE_SCREEN(STATE_SCREEN_COUNTDOWN_IMIT);/*TurnOffSystem();*/ break;
                case 0x0F00: CMD_SAVE(&tmp); Save_Build_Type_Dynamic(tmp); break;
                default:
                	out[1] |= 0x80; out[2] = 2; return 3; // Ошибка 02: Illegal Data Address
                break;
            }
        }
    }

    // --- ФАЗА 3: ПОСТ-ОБРАБОТКА ---
    if (state_f) {
#if CONFIG_SAVE_PARAM_LOG
#if CONFIG_FREERTOS
        if(xSemaphoreTake(xSemaphore_flash, (TickType_t)TIME_MUTEX_DELAY_FLASH) == pdTRUE){
#endif
			SaveCurrentParamFirmware(&state_f, &size_f);
#if CONFIG_FREERTOS
			xSemaphoreGive(xSemaphore_flash);
        }
#endif
#endif
    }
    if (flag_update) {
        pSn->Discreteness = calculating_the_discreteness(pSn, DISP);
        pSn->Discreteness_basic = calculating_the_discreteness(pSn, BASIC);
    }

    return 6;
}
//======================================================================================================================================
#define CMD_PUT_EVENT_LOG                                                       \
        {	                                                                	\
            read_event(Address, Value, Count);                                  \
            (*Countuint8_t) += (uint16_t)Count;                        			\
            AnswerLen += (uint16_t)Count*SIZE_LOG;                              \
        }
// Чтение журнала событий
uint16_t CmdFunc12(uint8_t *mas, uint16_t len, uint8_t *out)
{
	uint16_t BegginAddrFile = (uint16_t)Sns_Cfg_struct.Config_common.BegginAddrFile&MASK_COUNT_LOG;
	uint16_t CurrentAddrFile = (uint16_t)Sns_Cfg_struct.Config_common.CurrentAddrFile&MASK_COUNT_LOG;
	out[0] = mas[0];	// Адрес устройства
	out[1] = mas[1];	// Функция

	uint16_t Address 	= TO_M_WORD(mas+2)&MASK_COUNT_LOG;	// Адрес начального регистра
	uint16_t Count   	= TO_M_WORD(mas+4);					// Количество регистров
	uint8_t *Value		= &(out[3]);						// Выходной поток
	uint8_t *Countuint8_t	= &(out[2]);					// Счётчик
	uint16_t AnswerLen	= 3;								// Длина ответа (в байтах)
	*Countuint8_t = 0;

    if((BegginAddrFile > CurrentAddrFile) && ((Address >= BegginAddrFile) || (Address < CurrentAddrFile)))
    {
    	if((Address >= BegginAddrFile) && ((CurrentAddrFile + (MASK_COUNT_LOG - Address)) < Count))
    	{
    		out[1] |= 0x80;
    		out[2] = 2;
    		return(3);
    	}
    	else if((Address < CurrentAddrFile) && ((uint16_t)(CurrentAddrFile - Address) < Count))
    	{
    		out[1] |= 0x80;
    		out[2] = 2;
    		return(3);
    	}
    }
    else if(BegginAddrFile <= CurrentAddrFile && ((CurrentAddrFile - BegginAddrFile) >= Count))
    {
    	if((uint16_t)(CurrentAddrFile - Address) < Count)
    	{
    		out[1] |= 0x80;
    		out[2] = 2;
    		return(3);
    	}
    }
    else
    {
    	out[1] |= 0x80;
    	out[2] = 2;
    	return(3);
    }
#if CONFIG_SAVE_PARAM_LOG
        if(Count*SIZE_LOG > 3000 || Count == 0)
        {
          out[1] |= 0x80;
          out[2] = 3;
          return(3);
        }
#if CONFIG_FREERTOS
        if(xSemaphoreTake(xSemaphore_flash, (TickType_t)TIME_MUTEX_DELAY_FLASH) == pdTRUE){
			exit_ultra_deep_power_down_memory();
			CMD_PUT_EVENT_LOG
			ultra_deep_power_down_memory();
			xSemaphoreGive(xSemaphore_flash);
			if(TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45)){
			  out[1] |= 0x80;
			  out[2] = 3;
			  return(3);
			}
        }
        else{
        	out[1] |= 0x80;
        	out[2] = 3;
        	return(3);
        }
#else
        exit_ultra_deep_power_down_memory();
        CMD_PUT_EVENT_LOG
		ultra_deep_power_down_memory();
        if(TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45)){
          out[1] |= 0x80;
          out[2] = 3;
          return(3);
        }
#endif
#else
        *Value = *Value;
        out[1] |= 0x80;
        out[2] = 3;
#endif
        
        return(AnswerLen);
}
#undef CMD_PUT_EVENT_LOG

//======================================================================================================================================
#define CMD_PUT_EVENT_LOG_SEARCH(DATA)                      \
		{	                                                \
			Value[0] = LOBYTE(DATA);						\
			Value[1] = HIBYTE(DATA);						\
			Value += 2;                                     \
			AnswerLen += 2;                                 \
		}
uint32_t timestart = 0;
uint32_t timestop = 0;
uint32_t countevent = 0;
// Чтение журнала событий
uint16_t CmdFunc20(uint8_t *mas, uint16_t len, uint8_t *out)
{
    uint16_t BegginAddrFile = Sns_Cfg_struct.Config_common.BegginAddrFile&MASK_COUNT_LOG;
    uint16_t CurrentAddrFile = Sns_Cfg_struct.Config_common.CurrentAddrFile&MASK_COUNT_LOG;
	out[0] = mas[0];	// Адрес устройства
	out[1] = mas[1];	// Функция

#if CONFIG_SAVE_PARAM_LOG
	uint16_t state_search = 0;
	uint32_t timestartsearch = TO_M_WORD(mas+4) + (TO_M_WORD(mas+2)<< 16);
	uint32_t timestopsearch = TO_M_WORD(mas+8) + (TO_M_WORD(mas+6)<< 16);
#endif
	uint8_t *Value		= &(out[2]);				// Выходной поток
	uint16_t AnswerLen	= 10;					// Длина ответа (в байтах)


#if CONFIG_SAVE_PARAM_LOG
#if CONFIG_FREERTOS
	if(xSemaphoreTake(xSemaphore_flash, (TickType_t)TIME_MUTEX_DELAY_FLASH) == pdTRUE){
        exit_ultra_deep_power_down_memory();
        timestart = Sns_Cfg_struct.Config_common.working_hours;
        for(countevent = BegginAddrFile; countevent != CurrentAddrFile; countevent++){
        	countevent = countevent&MASK_COUNT_LOG;
        	read_event_for_search(countevent, (uint8_t*)&Event_log_read);
        	if((Event_log_read.working_hours_log >= timestartsearch) && (state_search == 0)){
        		state_search = 1;
        		CMD_PUT_EVENT_LOG_SEARCH(Event_log_read.CurrentAddrFile);
        		CMD_PUT_EVENT_LOG_SEARCH(HIWORD(Event_log_read.CurrentAddrFile));
//        		CMD_PUT_EVENT_LOG_SEARCH;
        	}
        	if(((Event_log_read.working_hours_log >= timestopsearch) && (state_search == 1)) || (countevent == (CurrentAddrFile - 1))){
        		countevent = CurrentAddrFile - 1;
        		state_search = 2;
        		CMD_PUT_EVENT_LOG_SEARCH(Event_log_read.CurrentAddrFile);
        		CMD_PUT_EVENT_LOG_SEARCH(HIWORD(Event_log_read.CurrentAddrFile));
        	}
        }

        timestop = Sns_Cfg_struct.Config_common.working_hours;
        ultra_deep_power_down_memory();
        xSemaphoreGive(xSemaphore_flash);
        if(TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45) || (state_search != 2)){
          out[1] |= 0x80;
          out[2] = 3;
          return(3);
        }
	}
	else{
		out[1] |= 0x80;
		out[2] = 3;
		return(3);
	}
#else
        exit_ultra_deep_power_down_memory();
        timestart = Sns_Cfg_struct.Config_common.working_hours;
        for(countevent = BegginAddrFile; countevent != CurrentAddrFile; countevent++){
        	countevent = countevent&MASK_COUNT_LOG;
        	read_event_for_search(countevent, (uint8_t*)&Event_log_read);
        	if((Event_log_read.working_hours_log >= timestartsearch) && (state_search == 0)){
        		state_search = 1;
        		CMD_PUT_EVENT_LOG_SEARCH(Event_log_read.CurrentAddrFile);
        		CMD_PUT_EVENT_LOG_SEARCH(HIWORD(Event_log_read.CurrentAddrFile));
//        		CMD_PUT_EVENT_LOG_SEARCH;
        	}
        	if(((Event_log_read.working_hours_log >= timestopsearch) && (state_search == 1)) || (countevent == (CurrentAddrFile - 1))){
        		countevent = CurrentAddrFile - 1;
        		state_search = 2;
        		CMD_PUT_EVENT_LOG_SEARCH(Event_log_read.CurrentAddrFile);
        		CMD_PUT_EVENT_LOG_SEARCH(HIWORD(Event_log_read.CurrentAddrFile));
        	}
        }

        timestop = Sns_Cfg_struct.Config_common.working_hours;
        ultra_deep_power_down_memory();

        if(TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45) || (state_search != 2)){
          out[1] |= 0x80;
          out[2] = 3;
          return(3);
        }
#endif
#else
        *Value = *Value;
        out[1] |= 0x80;
        out[2] = 3;
#endif

        return(AnswerLen);
}
#undef CMD_PUT_EVENT_LOG_SEARCH

//======================================================================================================================================
// Запись прошивки в микросхему памяти
uint8_t data_read_firmware[256];
uint16_t CmdFunc15(uint8_t *mas, uint16_t len, uint8_t *out)
{
  memcpy((uint8_t*)out, (uint8_t*)mas, 6);
  
#if CONFIG_SAVE_PARAM_LOG        
  uint32_t Address        = (TO_M_WORD(mas+2)<<16) + TO_M_WORD(mas+4);          // Адрес первого байта HI
  //uint16_t Address1       = TO_M_WORD(mas+4);			                // Адрес первого байта LO
  uint16_t size           = mas[6];			                        // Количество байт
  uint8_t *Value          = &(mas[7]);			                        // Значение регистра
    
  if(((Address + size)/SIZE_PAGE) > reg_mem->Size_fw){
    out[1] |= 0x80;
    out[2] = 3;
    return(3); 
  }
#if CONFIG_FREERTOS
  if(xSemaphoreTake(xSemaphore_flash, (TickType_t)TIME_MUTEX_DELAY_FLASH) == pdTRUE){
	  exit_ultra_deep_power_down_memory();
	  SaveFirmwareMemory(Address, Value, size);
	  ReadFirmwareMemory(Address, data_read_firmware, size);
	  ultra_deep_power_down_memory();
	  xSemaphoreGive(xSemaphore_flash);
	  for(uint16_t i = 0; i < size; i++)
	  {
		  if(Value[i] != data_read_firmware[i])
		  {
			  out[1] |= 0x80;
			  out[2] = 3;
			  return(3);
		  }
	  }

	  if(TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45)){
		  out[1] |= 0x80;
		  out[2] = 3;
		  return(3);
	  }
  }
  else{
	  out[1] |= 0x80;
	  out[2] = 3;
	  return(3);
  }
#else
  exit_ultra_deep_power_down_memory();
  SaveFirmwareMemory(Address, Value, size);
  ReadFirmwareMemory(Address, data_read_firmware, size);
  for(uint16_t i = 0; i < size; i++)
  {
	  if(Value[i] != data_read_firmware[i])
	  {
		  out[1] |= 0x80;
		  out[2] = 3;
		  return(3);
	  }
  }

  ultra_deep_power_down_memory();
  if(TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_AT45)){
	  out[1] |= 0x80;
	  out[2] = 3;
	  return(3);
  }
#endif
  return(6);
#else
  out[1] |= 0x80;
  out[2] = 3;
  return(3); 
#endif   
}

//======================================================================================================================================
// Запись прошивки в флеш контроллера
uint16_t CmdFunc14(uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint32_t Address        = (TO_M_WORD(mas+2)<<16) + TO_M_WORD(mas+4);  // Адрес первого байта HI
  uint16_t size           = mas[6];			        					// Количество байт

  uint32_t data_save      = 0;
  uint8_t *Value          = &(mas[7]);			        // Значение регистра
  memcpy((uint8_t*)out, (uint8_t*)mas, 6);

  if(Address < FLASH_ADDRESS_START_BOOTLOADER){
	out[1] |= 0x80;
	out[2] = 3;
	return(3);
  }
  if((Address + size) > FLASH_ADDRESS_STOP_BOOTLOADER){
	out[1] |= 0x80;
	out[2] = 3;
	return(3);
  }
  // разблокируем флэш,
  		flash_unlock();
  for(uint8_t i = 0; i < (size/4); i++){
	data_save = (uint64_t)Value[0] +
	((uint64_t)Value[1]<<8) +
	  ((uint64_t)Value[2]<<16) +
		((uint64_t)Value[3]<<24);
	flash_word_program(Address + i*4, data_save);
	Value += 4;
  }

  if(size%4){
	data_save = 0;
	for(uint8_t i = 0; i < (size%4); i++){
	  data_save |= ((uint64_t)Value[i]<<4*i);
	}
	flash_word_program(Address + (size - (size%4)), data_save);
  }
  flash_lock();

  return(6);
}


---

## Форматы команд 0x0C / 0x0E / 0x0F (Patch 1)

### 0x0C — Чтение журнала (Read Log)

| Поле | Байт | Описание |
|------|------|----------|
| Slave | 0 | Адрес устройства (или 0 = broadcast) |
| Func | 1 | `0x0C` |
| Reserved | 2–5 | `0x00 0x00 0x00 0x00` (зарезервировано) |
| Start ptr | 6 | Начальный указатель (0 = с начала) |
| Count | 7 | Количество записей `cnt` |
| CRC | 8–9 | CRC-16 Modbus (Lo, Hi) |

**Итого запрос: 10 байт.**

**Ответ:** `[Slave, 0x0C, Byte_Count, Data(cnt×40), CRC_Lo, CRC_Hi]`  
Парсер ждёт `3 + cnt*40 + 2` байт.

### 0x0E — Запись во внутреннюю Flash (MCU)

Staging-режим: данные копируются в `cmd_state.write_buf`, выставляется
`int_write_pending = 1`. Фактическая запись — в Measure Task.

**Ответ: 6-байтное эхо** `[Slave, 0x0E, addr_hi, addr_lo, len_hi, len_lo]` —
мастер подтверждает приём пакета.

### 0x0F — Запись во внешнюю Flash (SPI NOR)

Аналогично 0x0E, но `ext_write_pending = 1`.

**Ответ: 6-байтное эхо.**

### Broadcast (адрес 0x00)

Контроллер **отвечает** на broadcast-запросы. Используется для опроса
прибора «1-на-1» без прописки адреса на линии.

### Указатель кольцевого буфера логов

* Указатель — монотонный `uint32_t`.
* Размер кольца задаётся `LOG_RING_SLOTS` (по умолчанию 32768 записей).
* Физический индекс = `monotonic_ptr & (LOG_RING_SLOTS - 1)`.

### Неизвестные команды

Неизвестный код функции **игнорируется** — ответ не формируется.
Это упрощает диагностику: мастер просто получает таймаут.
