#include "Vector_main.h"
#include "bme280_com.h"
#include "BME280/bme280.h"

#if (CONFIG_BME)

#include <string.h>   /* memcpy */


static BME280_INTF_RET_TYPE bme280_i2c_read(uint8_t reg_addr, uint8_t *data, uint32_t len, void *intf_ptr);
static BME280_INTF_RET_TYPE bme280_i2c_write(uint8_t reg_addr, const uint8_t *data, uint32_t len, void *intf_ptr);
static void bme280_delay_us(uint32_t period, void *intf_ptr);
static void bme280_interface_selection(struct bme280_dev *dev);
/*! Variable that holds the I2C device address or SPI chip selection */
static uint8_t dev_addr;
struct bme280_dev dev;
struct bme280_data comp_data;
struct bme280_settings settings;
int8_t rslt = 0;
static uint8_t count_err = 0;
uint32_t period = 0;

BME280_INTF_RET_TYPE bme280_i2c_read(uint8_t reg_addr, uint8_t *data, uint32_t len, void *intf_ptr)
{
	dev_addr = *(uint8_t*)intf_ptr;

	if(I2C_Master_Transmit(PERIPH_I2C_BME280, (dev_addr << 1), &reg_addr, 1, BME280_TIME_ERR_I2C) != I2C_OK)
		return BME280_E_NULL_PTR;
	if(I2C_Master_Receive(PERIPH_I2C_BME280, (dev_addr << 1), data, len, BME280_TIME_ERR_I2C) != I2C_OK)
		return BME280_E_NULL_PTR;

	return BME280_OK;
}

void bme280_delay_us(uint32_t period, void *intf_ptr)
{
	Delay(period/1000);
}

BME280_INTF_RET_TYPE bme280_i2c_write(uint8_t reg_addr, const uint8_t *data, uint32_t len, void *intf_ptr)
{
	/* Статический буфер вместо malloc(): в оригинале память не освобождалась
	   (утечка на каждой записи), а команда BME280 короче 8 байт.            */
	uint8_t buf[8];

	if((len + 1u) > sizeof(buf))
		return BME280_E_INVALID_LEN;

	buf[0] = reg_addr;
	memcpy(buf +1, data, len);
	dev_addr = *(uint8_t*)intf_ptr;

	if(I2C_Master_Transmit(PERIPH_I2C_BME280, (dev_addr << 1), (uint8_t*)buf, len + 1, BME280_TIME_ERR_I2C) != I2C_OK)
		return BME280_E_NULL_PTR;

	return BME280_OK;
}

int8_t bme280_init_com(void)
{
	rslt = 0;
	count_err ++;
	if(count_err >= 5){
		count_err = 5;
		Vector_Search_Temp_Start(2);
	}

	bme280_interface_selection(&dev);

	rslt |= bme280_init(&dev);
//	if(rslt == BME280_E_DEV_NOT_FOUND){
//		SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_STS4);
//	}

	rslt |= bme280_get_sensor_settings(&settings, &dev);
	/* Configuring the over-sampling rate, filter coefficient and standby time */
	/* Overwrite the desired settings */
	settings.filter = BME280_FILTER_COEFF_2;

	/* Over-sampling rate for humidity, temperature and pressure */
	settings.osr_h = BME280_OVERSAMPLING_1X;
	settings.osr_p = BME280_OVERSAMPLING_1X;
	settings.osr_t = BME280_OVERSAMPLING_8X;

	/* Setting the standby time */
	settings.standby_time = BME280_STANDBY_TIME_0_5_MS;

	rslt |= bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings, &dev);

	/* Always set the power mode after setting the configuration */
	rslt |= bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &dev);
//	rslt = bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &dev);

	/* Calculate measurement time in microseconds */
	rslt |= bme280_cal_meas_delay(&period, &settings);

	return rslt;
}

static void bme280_interface_selection(struct bme280_dev *dev)
{
	/* BME280*/
	dev_addr = BME280_I2C_ADDR_PRIM;
	dev->read = bme280_i2c_read;
	dev->write = bme280_i2c_write;
	dev->intf = BME280_I2C_INTF;
	/* Holds the I2C device addr or SPI chip selection */
	dev->intf_ptr = &dev_addr;
	/* Configure delay in microseconds */
	dev->delay_us = bme280_delay_us;

	if(I2C_IsBusy(PERIPH_I2C_BME280))
	{
		I2C_ReConfig(PERIPH_I2C_BME280);
	}

	Delay(100);
}


int8_t bme280_measure(SNS_CFG *pSnsCfg)	/* 0 = данные обновлены, 1 = не готово, <0 = ошибка */
{
	uint8_t status_reg = 0;

	rslt = bme280_get_regs(BME280_REG_STATUS, &status_reg, 1, &dev);
	if(rslt != BME280_OK){
		count_err ++;
		return -1;
	}
	if((status_reg & BME280_STATUS_MEAS_DONE)){
		count_err = 0;
		/* Measurement time delay given to read sample */
		dev.delay_us(period, dev.intf_ptr);

		/* Read compensated data */
		rslt = bme280_get_sensor_data(BME280_ALL, &comp_data, &dev);

#if VECTOR_BME_CALIBRATION
		Calib_bme280_Temp(comp_data.temperature);
		comp_data.temperature = comp_data.temperature + TEMPSENSOR_CALIB_TEMP_T;
#endif
		pSnsCfg->Config_common.Temperature = comp_data.temperature;      /* ��C  */
		pSnsCfg->Config_common.Humidity = comp_data.humidity;           /* %   */
		pSnsCfg->Config_common.Pressure = comp_data.pressure/133.3;          /* hPa: 1 мм рт. ст. = 133,3 Па*/
		return 0;
	}
	else{
		count_err ++;
		if(count_err >= 10){
			count_err = 0;
			Vector_Search_Temp_Start(0);
			SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_STS4);
		}
		return 1;
	}

}

//===========================================================================================================================
#if VECTOR_BME_CALIBRATION
void Calib_bme280_Temp(float temperature)
{
	uint16_t calib_v = TEMPSENSOR_CALIB_TEMP_V;
	float calib_t = tempsensor_calib.temperature - temperature;
	if(calib_v == 0xFFFF){
		calib_v = 0;
		tempsensor_calib.flag = 1;
	}
	if(tempsensor_calib.flag == 0){
		return;
	}
	tempsensor_calib.flag = 0;
	if(tempsensor_calib.temperature == 0.0){
		calib_t = 0.0;
	}

	uint32_t calibration_payload[2];
	calibration_payload[0] = *(uint32_t*)&calib_t; // Приведение вашей переменной calib_t
	calibration_payload[1] = calib_v;              // Ваша переменная calib_v

	// Вызов функции (передаем базовый адрес и наш массив)
	flash_write_calibration_safe(calibration_payload);
}
#endif /* VECTOR_BME_CALIBRATION */


#endif /* CONFIG_BME */
