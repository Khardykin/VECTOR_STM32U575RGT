/*
 * bme280_com.h
 *
 *  Created on: 2024 Feb 26
 *      Author: Dmitriy
 */

#ifndef USER_AVIS_LIB_INC_BME280_COM_H_
#define USER_AVIS_LIB_INC_BME280_COM_H_

#if(DEVICE_NUMBER == Device2_1)
	#define PERIPH_I2C_BME280		(&hi2c1)
#endif

#define BME280_TIME_ERR_I2C		(10)
#define BME280_I2C_ADDR    		UINT8_C(0x76)

extern int8_t bme280_init_com(void);
extern void bme280_measure(SNS_CFG *pSnsCfg);
extern void Calib_bme280_Temp(float temperature);

#endif /* USER_AVIS_LIB_INC_BME280_COM_H_ */
