/*
 * Ble_mk01.h
 *
 *  Created on: 7 июл. 2022 г.
 *      Author: Dmitriy
 */

#ifndef _BLE_MK01_H_
#define _BLE_MK01_H_

#if(DEVICE_NUMBER == Device2_1)
	#define DIVICE_ID_BLE	(1)
	#define USART_BLE		(USART2)
#elif ((DEVICE_NUMBER == Device2_Pro) || (DEVICE_NUMBER == Device2_Pro_1))
	#define DIVICE_ID_BLE	(2)
#if (CONFIG_HW == 3)
	#define USART_BLE		(USART1)
#else
	#define USART_BLE		(USART2)
#endif
#elif ((DEVICE_NUMBER == Device3_Pro))
	#define DIVICE_ID_BLE	(2)
	#define USART_BLE		(USART1)
#endif

#define DATA_STRUCT_VER (2)
#define COUNT_CHAN_BLE	(7)

typedef struct{
	uint8_t 	Type_gas;
	uint8_t 	Concentration[2];
	uint8_t 	State_channel;
	uint8_t 	Setting_channel;
}sensor_data_t;

typedef struct{
	uint8_t 	data_struct_ver;
	uint8_t 	dev_id;
	uint8_t 	dev_hw_ver;
	uint8_t 	dev_fw_ver;
	uint8_t 	charge;
	uint8_t 	sensors_enabled:5;
	uint8_t 	rfu:3;
	sensor_data_t 	sensors[COUNT_CHAN_BLE];
}str_ble_data_set_t;
//extern str_ble_data_set_t ble_data_set;

#define SIZE_STR_SENSOR_DATA 	(sizeof(sensor_data_t))
#define SIZE_STR_BLE_DATA_SET 	(sizeof(str_ble_data_set_t) - (SIZE_STR_SENSOR_DATA*COUNT_CHAN_BLE))

typedef enum{
	CMD_AT = 0,
	CMD_MODINFO,
	CMD_ADVMSDDATASET,
	CMD_ADVBLENAMESET,
	CMD_ADVSTART,
	CMD_ADVPERIODMS,
	CMD_ADVPOWER,
	CMD_SLEEP,
	CMD_SYSTEMOFF,
	CMD_DFU,
	CMD_BLEADDR,
}COMMAND_BLE;

#define	AT 					("AT")
#define	MODINFO 			("AT+MODINFO?")
#define ADVMSDDATASET 		("AT+ADVMSDDATASET=")
#define ADVBLENAMESET 		("AT+ADVBLENAMESET=")
#define ADVSTART 			("AT+ADVSTART=")
#define ADVPERIODMS 		("AT+ADVPERIODMS=")
#define ADVPOWER 			("AT+ADVPOWER=")
#define SLEEP 				("AT+SLEEP")
#define SYSTEMOFF 			("AT+SYSTEMOFF")
#define DFU 				("AT+DFU")
#define	BLEADDR 			("AT+BLEADDR?")

#define	SIZE_AT	 			(sizeof(AT))
#define	SIZE_MODINFO 		(sizeof(MODINFO))
#define	SIZE_ADVMSDDATASET 	(sizeof(ADVMSDDATASET))
#define	SIZE_ADVBLENAMESET 	(sizeof(ADVBLENAMESET))
#define	SIZE_ADVSTART 		(sizeof(ADVSTART))
#define	SIZE_ADVPERIODMS 	(sizeof(ADVPERIODMS))
#define	SIZE_ADVPOWER 		(sizeof(ADVPOWER))
#define	SIZE_SLEEP 			(sizeof(SLEEP))
#define	SIZE_SYSTEMOFF 		(sizeof(SYSTEMOFF))
#define	SIZE_DFU 			(sizeof(DFU))
#define	SIZE_BLEADDR		(sizeof(BLEADDR))


typedef struct{
  uint8_t       receive[256];
  uint8_t       transmit[256];
  uint8_t       count_byter_r;
  uint8_t       TimeRX;
}Message_buffer_uart_ble;
extern Message_buffer_uart_ble buffer_uart_ble;
extern uint8_t MACAddrBle[8];
//===========================================================================================================================
extern uint32_t get_state_err_ble(void);
extern uint32_t get_state_init_flag_ble(void);
extern uint8_t Ble_Init(uint32_t serial);
extern void Ble_DeInit(uint8_t init);
extern void BLE_InitName(uint32_t serial);
extern void Ble_MsdDataSet(SNS_CFG *Cfg_struct);
extern void Ble_MsdDataSleep(void);
extern void Ble_CompleteDataSet(SNS_CFG *Cfg_struct, str_ble_data_set_t *data_set);
extern void Ble_AdvStart(uint32_t data);
extern void Ble_SystemOff(void);
extern void SetBleFlagDFU(uint32_t flag);
extern void Ble_SystemDFU(void);
extern void Ble_AdvPeriodms(uint32_t data);
extern void Ble_AdvPower(uint32_t data);
//===========================================================================================================================

#endif /* _BLE_MK01_H_ */
