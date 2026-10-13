#include "main.h"
#include "Avis_main.h"
#if CONFIG_BLE
static str_ble_data_set_t 	ble_data_set;
static uint16_t 	len_ble_data_set;
static uint32_t 	Ble_Init_flag = 0;
static uint32_t 	Ble_flag_err = 0;
static uint32_t 	Ble_counter_err = 0;
#if ((DEVICE_NUMBER == Device2) || (DEVICE_NUMBER == Device2_1))
    const char BLE_Label[2][5] = {
        [MIRAX] = "AVX",
        [BACS]  = "22."
    };
#elif (DEVICE_NUMBER == Device2_2)
    #if (DEVICE_NUMBER_MODIF == 0)
        const char BLE_Label[2][5] = {
            [MIRAX] = "SX",
            [BACS]  = "22."
        };
    #elif (DEVICE_NUMBER_MODIF == 1)
        const char BLE_Label[2][5] = {
            [MIRAX] = "SXR",
            [BACS]  = "22."
        };
    #endif
#elif ((DEVICE_NUMBER == Device2_Pro) || (DEVICE_NUMBER == Device2_Pro_1))
    const char BLE_Label[2][5] = {
        [MIRAX] = "AX4P",
        [BACS]  = "22."
    };
#elif (DEVICE_NUMBER == Device3_Pro)
    const char BLE_Label[2][5] = {
        [MIRAX] = "AX5P",
        [BACS]  = "22."
    };
#endif
static uint8_t 		ModInfo[20];
static uint8_t 		AdvOK[2] = "OK";
static uint8_t 		AdvERROR[5] = "ERROR";
static uint8_t 		EmptyBuffer[2];

static uint32_t 	ble_flag_DFU = 0;
//===========================================================================================================================
Message_buffer_uart_ble buffer_uart_ble = {0};
uint8_t MACAddrBle[8];
//===========================================================================================================================
static uint8_t Ble_TransmitReceive(COMMAND_BLE command, uint8_t *data);
static uint8_t Ble_Receive(COMMAND_BLE command);

uint32_t get_state_err_ble(void)
{
	return Ble_flag_err;
}

//===========================================================================================================================
uint32_t get_state_init_flag_ble(void)
{
	return Ble_Init_flag;
}

//===========================================================================================================================
uint8_t Ble_Init(uint32_t serial)
{
	if(get_state_init_flag_ble() == 0){
		Ble_Init_flag = 1;
		Ble_TransmitReceive(CMD_AT, EmptyBuffer);
		Ble_TransmitReceive(CMD_AT, EmptyBuffer);
		BLE_InitName(serial);
		Ble_TransmitReceive(CMD_BLEADDR, EmptyBuffer);
		Ble_AdvPeriodms(300);
		Ble_AdvPower(8);
		Ble_AdvStart(1);
		return 1;
	}
	else{
		return 0;
	}
}

//===========================================================================================================================
void Ble_DeInit(uint8_t init)
{
	Ble_SystemDFU();
	if(init){
		Ble_Init_flag = 1;
	}
	Ble_SystemOff();
	Ble_counter_err = 0;
	Ble_flag_err = 0;
}

//===========================================================================================================================
void Ble_CompleteDataSet(SNS_CFG *Cfg_struct, str_ble_data_set_t *data_set)
{
	uint16_t conc = 0;
	uint8_t chan_data_set = 0;
	data_set->data_struct_ver = DATA_STRUCT_VER;
	data_set->dev_id = DIVICE_ID_BLE;
	data_set->dev_hw_ver = Cfg_struct->Config_common.HardwareVersion;
	data_set->dev_fw_ver = FIRMWARE_VERSION;
	data_set->charge = Cfg_struct->Config_common.battery_charge_percent;
	data_set->sensors_enabled = 0;
	for(uint8_t chan = 0; chan < COUNT_CHAN; chan++){
		if(Cfg_struct->Sensor[chan].channel == chan){
			data_set->sensors[chan_data_set].Type_gas = Cfg_struct->Sensor[chan].num_formula_gas;
			data_set->sensors_enabled += 1;
			data_set->sensors[chan_data_set].Setting_channel = Cfg_struct->Sensor[chan].DeviceSetting&(MEASURE_MASK|MEASURE_DISK);
			conc = (uint16_t)lround (Cfg_struct->Sensor[chan].ConcentrationVal*Cfg_struct->Sensor[chan].Discreteness*Get_Conversion_factor_rf(chan));
			data_set->sensors[chan_data_set].Concentration[0] = conc;
			data_set->sensors[chan_data_set].Concentration[1] = conc>>8;
			data_set->sensors[chan_data_set].State_channel = Cfg_struct->Sensor[chan].StateErr & 0x7F;
			chan_data_set ++;
		}
//		else{
//			data_set->sensors[chan].Type_gas = 0;
//			data_set->sensors[chan].Concentration[0] = 0;
//			data_set->sensors[chan].Concentration[1] = 0;
//			data_set->sensors[chan].State_channel = 0;
//			chan_data_set ++;
//		}
	}
	len_ble_data_set = SIZE_STR_BLE_DATA_SET + (data_set->sensors_enabled*SIZE_STR_SENSOR_DATA);
}

void Ble_MsdDataSet(SNS_CFG *Cfg_struct)
{
	if((get_state_init_flag_ble() == 1) && (get_state_err_ble() == 0)){
		Ble_CompleteDataSet(Cfg_struct, &ble_data_set);
		Ble_TransmitReceive(CMD_AT, EmptyBuffer);
		Ble_TransmitReceive(CMD_ADVMSDDATASET, (uint8_t *)&ble_data_set);
		Ble_AdvStart(1);
//		Ble_TransmitReceive(CMD_SLEEP, EmptyBuffer);
	}
}

void Ble_MsdDataSleep(void)
{
	if((get_state_init_flag_ble() == 1) && (get_state_err_ble() == 0)){
		Ble_TransmitReceive(CMD_SLEEP, EmptyBuffer);
	}
}

//===========================================================================================================================
void BLE_InitName(uint32_t serial)
{
    char BLE_About_buffer[26];

    uint8_t format_type = 0; // 0: %04d%04d, 1: %03d%04d

    // Инициализация настроек смещения на этапе компиляции
    if(MIRAX_BACS_BUILD == MIRAX){
    #if ((DEVICE_NUMBER == Device2_Pro) || (DEVICE_NUMBER == Device2_Pro_1) || (DEVICE_NUMBER == Device3_Pro))
        format_type = 1;
    #endif
    }

    // Копируем базовое имя устройства из массива (приведение к char* снимает warning)
    strcpy(BLE_About_buffer, (char*)BLE_Label[MIRAX_BACS_BUILD]);

    uint8_t offset = strlen(BLE_About_buffer);

    // Вычисляем общую математику один раз
    uint16_t serial_high = (uint16_t)(serial / 10000);
    uint16_t serial_low  = (uint16_t)(serial % 10000);

    // Форматируем строку по выбранному типу маски
    if (format_type == 1) {
        sprintf(BLE_About_buffer + offset, "%03d%04d", serial_high, serial_low);
    } else {
        sprintf(BLE_About_buffer + offset, "%04d%04d", serial_high, serial_low);
    }

    // Передаем сформированный буфер с именем устройства
    Ble_TransmitReceive(CMD_ADVBLENAMESET, (uint8_t*)BLE_About_buffer);
}

//===========================================================================================================================
static uint32_t advstart_flag = 0;
void Ble_AdvStart(uint32_t data)
{
	advstart_flag = data;
	Ble_TransmitReceive(CMD_ADVSTART, (uint8_t *)&advstart_flag);
}

//===========================================================================================================================
void Ble_SystemOff(void)
{
	if(get_state_init_flag_ble() == 1){
		Ble_Init_flag = 0;

		SET_OFF(RESET_BLE);
		Delay(50);
		SET_ON(RESET_BLE);
		BLE_UART_Init(USART_BLE);
	}
}

//===========================================================================================================================
void SetBleFlagDFU(uint32_t flag)
{
	ble_flag_DFU = flag;
}

void Ble_SystemDFU(void)
{
	if(ble_flag_DFU){
		Ble_Init_flag = 0;
		ble_flag_DFU = 0;
		if(Ble_TransmitReceive(CMD_AT, EmptyBuffer) == 0){
			Delay(500);
			Ble_TransmitReceive(CMD_AT, EmptyBuffer);
		}
		Ble_TransmitReceive(CMD_DFU, EmptyBuffer);
	}
}

//===========================================================================================================================
static uint32_t data_ind = 0;
void Ble_AdvPeriodms(uint32_t data)
{
	data_ind = 0;
	data_ind = data;
	Ble_TransmitReceive(CMD_ADVPERIODMS, (uint8_t*)&data_ind);
}

//===========================================================================================================================
void Ble_AdvPower(uint32_t data)
{
	data_ind = 0;
	data_ind = data;
	Ble_TransmitReceive(CMD_ADVPOWER, (uint8_t*)&data_ind);
}

//===========================================================================================================================
static uint8_t Ble_TransmitReceive(COMMAND_BLE command, uint8_t *data)
{
	uint8_t read_uart = 0;
	uint16_t len = 0;
	uint16_t count_transmit = 0;
	uint8_t *data_command = buffer_uart_ble.transmit;// максимальная длина пакета
	do{
		switch(command){
		case CMD_AT:
			len = SIZE_AT;
			memcpy(&data_command[0], AT, SIZE_AT);
			break;
		case CMD_MODINFO:
			len = SIZE_MODINFO;
			memcpy(&data_command[0], MODINFO, SIZE_MODINFO);
			break;
		case CMD_ADVMSDDATASET:
			len = SIZE_ADVMSDDATASET;
			memcpy(&data_command[0], ADVMSDDATASET, SIZE_ADVMSDDATASET);
			for(uint8_t i = 0; i < len_ble_data_set; i++){
				sprintf((char*)&data_command[(SIZE_ADVMSDDATASET-1)+i*2], "%02x", data[i]);
			}
			len += len_ble_data_set*2;
//			memcpy(&data_command[SIZE_ADVMSDDATASET-1], (char*)data, len_ble_data_set);
			break;
		case CMD_ADVBLENAMESET:
			len = SIZE_ADVBLENAMESET;
			memcpy(&data_command[0], ADVBLENAMESET, SIZE_ADVBLENAMESET);
			len += strlen((char*)data);
			memcpy(&data_command[SIZE_ADVBLENAMESET-1], (char*)data, strlen((char*)data));
			break;
		case CMD_ADVSTART:
			len = SIZE_ADVSTART;
			memcpy(&data_command[0], ADVSTART, SIZE_ADVSTART);
			len += sprintf((char*)&data_command[SIZE_ADVSTART-1], "%d", *data);
			break;
		case CMD_ADVPERIODMS:
			len = SIZE_ADVPERIODMS;
			memcpy(&data_command[0], ADVPERIODMS, SIZE_ADVPERIODMS);
			len += sprintf((char*)&data_command[SIZE_ADVPERIODMS-1], "%d", *(uint16_t*)data);
			break;
		case CMD_ADVPOWER:
			len = SIZE_ADVPOWER;
			memcpy(&data_command[0], ADVPOWER, SIZE_ADVPOWER);
			len += sprintf((char*)&data_command[SIZE_ADVPOWER-1], "%d", *data);
			break;
		case CMD_SLEEP:
			len = SIZE_SLEEP;
			memcpy(&data_command[0], SLEEP, SIZE_SLEEP);
			break;
		case CMD_SYSTEMOFF:
			len = SIZE_SYSTEMOFF;
			memcpy(&data_command[0], SYSTEMOFF, SIZE_SYSTEMOFF);
			break;
		case CMD_DFU:
			len = SIZE_DFU;
			memcpy(&data_command[0], DFU, SIZE_DFU);
			break;
		case CMD_BLEADDR:
			len = SIZE_BLEADDR;
			memcpy(&data_command[0], BLEADDR, SIZE_BLEADDR);
			break;
		default:
			break;
		}

		data_command[len-1] = 0x0D;
		while(check_buffer(&InputBuffer[TYPE_BLE])){
			receive_buffer(&InputBuffer[TYPE_BLE], &read_uart);
		}
		transmit_buffer(data_command, len, TYPE_BLE);
		count_transmit++;
	}while(Ble_Receive(command) && (count_transmit < 4));
	if(count_transmit == 4){
		if(Ble_counter_err < 3){
			Ble_counter_err ++;
			if(Ble_counter_err == 3){
				Ble_flag_err = 1;
			}
		}
		return 0;
	}
	Ble_counter_err = 0;
	Ble_flag_err = 0;
	return 1;
}

//===========================================================================================================================
static uint8_t Ble_Receive(COMMAND_BLE command)
{
	uint16_t len = 0;
	uint32_t tickstart = GetTick();
	while(!check_buffer(&InputBuffer[TYPE_BLE]) && ((GetTick() - tickstart) <=  35));

	Delay(5);

	if(!check_buffer(&InputBuffer[TYPE_BLE])){
		Delay(10);
		return 1;
	}

	while(check_buffer(&InputBuffer[TYPE_BLE])){
		receive_buffer(&InputBuffer[TYPE_BLE], &buffer_uart_ble.receive[buffer_uart_ble.count_byter_r]);
		buffer_uart_ble.count_byter_r++;
	}

	len = buffer_uart_ble.count_byter_r;
	buffer_uart_ble.count_byter_r = 0;
	if(len < 2){
		memset(&buffer_uart_ble.receive[0], 0, len);
		return 1;
	}

	if(command == CMD_MODINFO){
		memset(ModInfo, 0, sizeof(ModInfo));
		memcpy(ModInfo, &buffer_uart_ble.receive[2], len-2);
	}
	else if(command == CMD_BLEADDR){
		memset(MACAddrBle, 0, sizeof(MACAddrBle));
		for(uint8_t i = 0; i < 6; i++){
			sscanf((char*)&buffer_uart_ble.receive[i*2 + 2], "%02x", &MACAddrBle[i]);
		}
	}
	else if(strstr ((char*)&buffer_uart_ble.receive[0], (char*)AdvOK) != NULL){

	}
	else if(strstr ((char*)&buffer_uart_ble.receive[0], (char*)AdvERROR) != NULL){
		memset(&buffer_uart_ble.receive[0], 0, len);
		return 1;
	}
	else{
		memset(&buffer_uart_ble.receive[0], 0, len);
		return 1;
	}
	memset(&buffer_uart_ble.receive[0], 0, len);
	return 0;
//	transmit_buffer(buffer_uart[type].transmit, len, TYPE_USART);
}

#endif
