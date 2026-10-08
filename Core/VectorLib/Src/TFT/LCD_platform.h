/*
 * lcd_platform.h
 *
 *  Created on: Nov 5, 2024
 *      Author: me
 */

#ifndef LCD_PLATFORM_H_
#define LCD_PLATFORM_H_

#include "gpio.h"
#include "spi.h"
#include "dma.h"

// #define CFG_NO_CS
#if (CONFIG_MODEL_LCD == 0)
	#define LCD_SPI_PORT 	(&hspi2)
	#define	DMA_CHANNEL  	DMA1_CHANNEL2

	#define LCD_LED_PORT LCD_LED_GPIO_Port
	#define LCD_LED_PIN  LCD_LED_Pin
	#define LCD_LED_Clr() 	gpio_bits_reset(LCD_LED_PORT, LCD_LED_PIN)
	#define LCD_LED_Set() 	gpio_bits_set(LCD_LED_PORT, LCD_LED_PIN)
#endif

#define LCD_RST_PORT LCD_RST_GPIO_Port
#define LCD_RST_PIN  LCD_RST_Pin
#define LCD_DC_PORT  LCD_DC_GPIO_Port
#define LCD_DC_PIN   LCD_DC_Pin

#ifndef CFG_NO_CS
#define LCD_CS_PORT  LCD_CS_GPIO_Port
#define LCD_CS_PIN   LCD_CS_Pin
#endif

//#define READ_PIN_IN(x)  	(gpio_input_data_read(x##_GPIO_Port) & (x##_Pin))
//#define READ_PIN_OUT(x)  	(gpio_output_data_read(x##_GPIO_Port) & (x##_Pin))
#define SET_POWER_OFF()  		gpio_bits_reset(POWER_ON_GPIO_Port, POWER_ON_Pin);
#define SET_POWER_ON()   		gpio_bits_set(POWER_ON_GPIO_Port, POWER_ON_Pin);
//#define SET_TGL(x)			gpio_bits_toggle(x##_GPIO_Port, x##_Pin);


#define LCD_RST_Clr() 	gpio_bits_reset(LCD_RST_PORT, LCD_RST_PIN)
#define LCD_RST_Set() 	gpio_bits_set(LCD_RST_PORT, LCD_RST_PIN)
#define LCD_DC_Clr() 	gpio_bits_reset(LCD_DC_PORT, LCD_DC_PIN)
#define LCD_DC_Set() 	gpio_bits_set(LCD_DC_PORT, LCD_DC_PIN)
#ifndef CFG_NO_CS
#define LCD_Select() 	gpio_bits_reset(LCD_CS_PORT, LCD_CS_PIN)
#define LCD_UnSelect() 	gpio_bits_set(LCD_CS_PORT, LCD_CS_PIN)

//#define LCD_RST_Clr() HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET)
//#define LCD_RST_Set() HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET)
//#define LCD_DC_Clr() HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_RESET)
//#define LCD_DC_Set() HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET)
//#ifndef CFG_NO_CS
//#define LCD_Select() HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET)
//#define LCD_UnSelect() HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET)
#else
#define LCD_Select() asm("nop")
#define LCD_UnSelect() asm("nop")
#endif

uint16_t GET_LCD_SPI_PORT_State(void);
void LCD_initPlatform(void);
void LCD_writeCommand8Bit(uint8_t cmd);
void LCD_writeData8Bit(uint8_t *data, uint32_t size);
void LCD_writeData16Bit(uint8_t *data, uint32_t size);
void LCD_writeBulk(const uint8_t *data, uint32_t size);
void LCD_transferCpltCallback(void);
void backlight_set(uint8_t percent);

#endif /* LCD_PLATFORM_H_ */
