/*
 * LCD_platform.c
 *
 *  Created on: Nov 5, 2024
 *      Author: me
 */

#include "LCD_platform.h"
#if (CONFIG_TYPE_LCD == 2)
#include "lvgl.h"
extern lv_display_t * disp;
static uint16_t LCD_SPI_PORT_State = 0;

uint16_t GET_LCD_SPI_PORT_State(void)
{
	return LCD_SPI_PORT_State;
}

void LCD_initPlatform(void)
{

}

void LCD_writeCommand8Bit(uint8_t cmd)
{
    while (LCD_SPI_PORT_State != 0)
    {
    }
    LCD_SPI_PORT_State = 1;

    spi_enable(LCD_SPI_PORT, FALSE);
    spi_frame_bit_num_set(LCD_SPI_PORT, SPI_FRAME_8BIT);
    spi_enable(LCD_SPI_PORT, TRUE);

    LCD_Select();
    LCD_DC_Clr();
    SPI_Transmit(LCD_SPI_PORT, &cmd, sizeof(cmd), MAX_DELAY);
    LCD_UnSelect();

    LCD_SPI_PORT_State = 0;
}

/**
 * @brief Write data to LCD controller
 * @param data -> pointer of data buffer
 * @param size -> size of the data buffer
 * @return none
 */
void LCD_writeData8Bit(uint8_t *data, uint32_t size)
{
    while (LCD_SPI_PORT_State != 0)
    {
    }
    LCD_SPI_PORT_State = 1;

    LCD_Select();
    LCD_DC_Set();

    // split data in small chunks because HAL can't send more than 64K at once
    while (size > 0)
    {
        uint16_t chunk_size = size > 65535 ? 65535 : size;

        SPI_Transmit(LCD_SPI_PORT, data, chunk_size, MAX_DELAY);

        data += chunk_size;
        size -= chunk_size;
    }

    LCD_UnSelect();

    LCD_SPI_PORT_State = 0;
}

void LCD_writeData16Bit(uint8_t *data, uint32_t size)
{
    while (LCD_SPI_PORT_State != 0)
    {
    }
    LCD_SPI_PORT_State = 1;

    spi_enable(LCD_SPI_PORT, FALSE);
    spi_frame_bit_num_set(LCD_SPI_PORT, SPI_FRAME_16BIT);
    spi_enable(LCD_SPI_PORT, TRUE);
//    HAL_SPI_Init(&hspi1);

    LCD_Select();
    LCD_DC_Set();

    while (size > 0)
    {
        uint16_t chunk_size = size > 65535 ? 65535 : size;

    SPI_Transmit(LCD_SPI_PORT, data, size, MAX_DELAY);
        data += chunk_size;
        size -= chunk_size;
    }

    LCD_UnSelect();

    LCD_SPI_PORT_State = 0;
}

/**
 * @brief DMA write data to LCD controller
 * @param data -> pointer of data buffer
 * @param size -> size of the data buffer
 * @return none
 */
void LCD_writeBulk(const uint8_t *data, uint32_t size)
{
    if (size > 0xFFFF) // Maximum chunk size that HAL_SPI_Transmit_DMA can handle (uint16_t max value)
        size = 0xFFFF;

    while (LCD_SPI_PORT_State != 0)
    {
    }
    LCD_SPI_PORT_State = 1;

    spi_enable(LCD_SPI_PORT, FALSE);
    spi_frame_bit_num_set(LCD_SPI_PORT, SPI_FRAME_16BIT);
    spi_enable(LCD_SPI_PORT, TRUE);

    LCD_Select();
    LCD_DC_Set();

    dma_channel_enable(DMA_CHANNEL, FALSE);
    dma_channel_config(DMA_CHANNEL,
                            (uint32_t)&LCD_SPI_PORT->dt,
							(uint32_t)data,
							size);
	dma_channel_enable(DMA_CHANNEL, TRUE);
//    if (HAL_OK != SPI_Transmit_DMA(LCD_SPI_PORT, (const uint8_t*) data, size))
//        Error_Handler();
}

/**
 * @brief DMA complete callback
 * @return none
 */
void LCD_transferCpltCallback(void)
{
    LCD_UnSelect();
    /* IMPORTANT!!!
     * Inform the graphics library that you are ready with the flushing */
    lv_display_flush_ready(disp);

    spi_enable(LCD_SPI_PORT, FALSE);
    spi_frame_bit_num_set(LCD_SPI_PORT, SPI_FRAME_8BIT);
    spi_enable(LCD_SPI_PORT, TRUE);

    LCD_SPI_PORT_State = 0;
}

#define PWM_PERIOD  249
void backlight_set(uint8_t percent)
{
    uint32_t duty;
#if (CONFIG_MODEL_LCD == 0)
    if (percent > 30){
    	LCD_LED_Set();
    }
    else{
    	LCD_LED_Clr();
    }
#elif (CONFIG_MODEL_LCD == 1)
    if (percent > 100)
        percent = 100;

    duty = ((PWM_PERIOD + 1) * percent) / 100;

    if (duty > (PWM_PERIOD + 1))
        duty = PWM_PERIOD + 1;

    if(tmr_channel_value_get(TMR8, TMR_SELECT_CHANNEL_3) != duty){
    	tmr_channel_value_set(TMR8, TMR_SELECT_CHANNEL_3, duty);
    }
#endif
}
#endif
