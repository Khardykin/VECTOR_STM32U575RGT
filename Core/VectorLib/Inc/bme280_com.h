/*
 * bme280_com.h
 *
 *  Created on: 2024 Feb 26
 *      Author: Dmitriy
 */

#ifndef USER_AVIS_LIB_INC_BME280_COM_H_
#define USER_AVIS_LIB_INC_BME280_COM_H_

#include <stdint.h>
#include "config_device.h"   /* CONFIG_BME */
#include "vector_config.h"   /* VECTOR_BME_CALIBRATION */
#include "shared_types.h"    /* SNS_CFG */


#define PERIPH_I2C_BME280		(&hi2c1)


#define BME280_TIME_ERR_I2C		(10)
#define BME280_I2C_ADDR    		UINT8_C(0x76)

extern int8_t bme280_init_com(void);
extern int8_t bme280_measure(SNS_CFG *pSnsCfg);  /* 0 = данные обновлены, 1 = не готово, <0 = ошибка */
#if VECTOR_BME_CALIBRATION
extern void Calib_bme280_Temp(float temperature);
#endif

#endif /* USER_AVIS_LIB_INC_BME280_COM_H_ */
