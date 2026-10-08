#ifndef __TFT_H
#define __TFT_H

//#include "main.h"
#include "TFT_indicator.h"
#include "ui.h"
//#include "LCD_platform.h"
#if (CONFIG_MODEL_LCD == 0)
	#define TFT_WIDTH    (240)
	#define TFT_HEIGHT   (320)
#elif (CONFIG_MODEL_LCD == 1)
	#define TFT_WIDTH    (320)
	#define TFT_HEIGHT   (240)
#endif
/* Control Registers and constant codes */
#define TFT_NOP     0x00
#define TFT_SWRESET 0x01
#define TFT_RDDID   0x04
#define TFT_RDDST   0x09

#define TFT_SLPIN   0x10
#define TFT_SLPOUT  0x11
#define TFT_PTLON   0x12
#define TFT_NORON   0x13

#define TFT_INVOFF  0x20
#define TFT_INVON   0x21
#define TFT_DISPOFF 0x28
#define TFT_DISPON  0x29
#define TFT_CASET   0x2A
#define TFT_RASET   0x2B
#define TFT_RAMWR   0x2C
#define TFT_RAMRD   0x2E

#define TFT_PTLAR   0x30
#define TFT_COLMOD  0x3A
#define TFT_MADCTL  0x36

/**
 * Memory Data Access Control Register (0x36H)
 * MAP:     D7  D6  D5  D4  D3  D2  D1  D0
 * param:   MY  MX  MV  ML  RGB MH  -   -
 *
 */

/* Page Address Order ('0': Top to Bottom, '1': the opposite) */
#define TFT_MADCTL_MY  0x80
/* Column Address Order ('0': Left to Right, '1': the opposite) */
#define TFT_MADCTL_MX  0x40
/* Page/Column Order ('0' = Normal Mode, '1' = Reverse Mode) */
#define TFT_MADCTL_MV  0x20
/* Line Address Order ('0' = LCD Refresh Top to Bottom, '1' = the opposite) */
#define TFT_MADCTL_ML  0x10
/* RGB/BGR Order ('0' = RGB, '1' = BGR) */
#define TFT_MADCTL_RGB 0x00
#define TFT_MADCTL_BGR 0x08 // Порядок цветов BGR (если 0, то RGB)

#define TFT_RDID1   0xDA
#define TFT_RDID2   0xDB
#define TFT_RDID3   0xDC
#define TFT_RDID4   0xDD

#define LCD_Init() 			TFT_Init()
#define LCD_DeInit()		TFT_DeInit()
#define LCD_Get_flag_init() (TFT_Initialized)
#define LCD_UpdateScreen()	(1)//TFT_UpdateScreen()
#define LCD_SetAllRam(x)	TFT_Fill_All(x)

void TFT_Init(void);
void TFT_DeInit(void);
void TFT_Rotation(uint8_t state);
void TFT_Fill(uint16_t xSta, uint16_t ySta, uint16_t xEnd, uint16_t yEnd, uint16_t color);
void TFT_Fill_All(uint16_t color);

void TFT_FlushBuffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data);
void TFT_FlushBuffer_1(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data);
void TFT_TransferCpltCallback(void);

void TFT_Test(void);

extern uint16_t TFT_Initialized;

#endif
