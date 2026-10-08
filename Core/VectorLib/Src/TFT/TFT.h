#ifndef __TFT_H
#define __TFT_H

//#include "main.h"
#include "vector_config.h"   /* CONFIG_MODEL_LCD, VECTOR_LCD_USE_LVGL */
/* TFT_indicator.h сюда НЕ включаем намеренно: это слой приборной индикации,
   ему нужны SNS_CFG_Type/CALIB_CFG/COUNT_CHAN (модель каналов газоанализатора),
   которых в этом проекте пока нет. Драйвер панели от индикации зависеть не
   должен - включайте TFT_indicator.h в тех файлах, где она реально нужна
   (TFT_indicator.c), вместе с TFT.h.                                     */
#if VECTOR_LCD_USE_LVGL
#include "lvgl.h"           /* lv_obj_invalidate/lv_scr_act */
#include "ui.h"              /* SquareLine Studio: экспорт в Drivers (см. docs/LVGL.md) */
#endif
//#include "LCD_platform.h"
/* Модель 0 - ST7789P3 172x320 (портрет). RAM контроллера 240x320, поэтому у
   узкой панели есть смещение окна по столбцам: TFT_COL_OFFSET. Если картинка
   сдвинута/обрезана по горизонтали - поправьте смещение (типовые значения для
   172x320: 34, реже 0 или 68 - зависит от ревизии панели).
   Модель 1 - 320x240 (ILI9341, ландшафт), смещений нет.                     */
#if (CONFIG_MODEL_LCD == 0)
	#define TFT_WIDTH       (172)
	#define TFT_HEIGHT      (320)
	#define TFT_COL_OFFSET  (34)
	#define TFT_ROW_OFFSET  (0)
#elif (CONFIG_MODEL_LCD == 1)
	#define TFT_WIDTH       (320)
	#define TFT_HEIGHT      (240)
	#define TFT_COL_OFFSET  (0)
	#define TFT_ROW_OFFSET  (0)
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
