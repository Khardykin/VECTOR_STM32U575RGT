#include "Avis_main.h"
#if (CONFIG_TYPE_LCD_TFT)

uint16_t TFT_Initialized = 0;

static void TFT_WriteCommand(uint8_t cmd)
{
    LCD_writeCommand8Bit(cmd);
}

static void TFT_WriteByte(uint8_t data)
{
    LCD_writeData8Bit(&data, 1);
}

static void TFT_WriteData(uint8_t *data, uint32_t size)
{
    LCD_writeData8Bit(data, size);
}

static void TFT_WriteData16(uint8_t *data, uint32_t size)
{
    LCD_writeData16Bit(data, size);
}

static void TFT_WriteBulk(uint8_t *data, uint32_t size)
{
    LCD_writeBulk(data, size);
}

/**
 * @brief Initialize TFT controller
 * @param none
 * @return none
 */
void TFT_Init(void)
{
	SET_POWER_ON();
	if(TFT_Initialized == 0){
#if (CONFIG_MODEL_LCD == 0)
		TFT_DeInit();
		DelayInt(10);
		LCD_RST_Set();
		LCD_initPlatform();

		TFT_WriteCommand(0x01); // Software Reset
	//    osDelay(10);
		DelayInt(10);

		TFT_WriteCommand(TFT_COLMOD); // Set color mode
		TFT_WriteByte(0x55);             // RGB565
		TFT_WriteCommand(0xB2);		// Porch control
		TFT_WriteByte(0x0C);
		TFT_WriteByte(0x0C);
		TFT_WriteByte(0x00);
		TFT_WriteByte(0x33);
		TFT_WriteByte(0x33);


		TFT_Rotation(0);
//		TFT_WriteCommand(TFT_MADCTL); // MADCTL
//	//    TFT_WriteByte(TFT_MADCTL_MX | TFT_MADCTL_MV | TFT_MADCTL_RGB);
//		TFT_WriteByte(TFT_MADCTL_RGB);

		// Internal LCD Voltage generator settings
		TFT_WriteCommand(0XB7);		// Gate Control
		TFT_WriteByte(0x35);		// Default value
		TFT_WriteCommand(0xBB);		// VCOM setting
		TFT_WriteByte(0x19);		// 0.725v (default 0.75v for 0x20)
		TFT_WriteCommand(0xC0);		// LCMCTRL
		TFT_WriteByte(0x2C);		// Default value
		TFT_WriteCommand(0xC2);		// VDV and VRH command Enable
		TFT_WriteByte(0x01);		// Default value
		TFT_WriteCommand(0xC3);		// VRH set
		TFT_WriteByte(0x12);		// +-4.45v (defalut +-4.1v for 0x0B)
		TFT_WriteCommand(0xC4);		// VDV set
		TFT_WriteByte(0x20);		// Default value
		TFT_WriteCommand(0xC6);		// Frame rate control in normal mode
		TFT_WriteByte(0x0F);		// Default value (60HZ)
		TFT_WriteCommand(0xD0);		// Power control
		TFT_WriteByte(0xA4);		// Default value
		TFT_WriteByte(0xA1);		// Default value

		TFT_WriteCommand(0xE0);
		{
			uint8_t data[14] = { 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23 };
			for (size_t i = 0; i < sizeof(data); ++i)
				TFT_WriteByte(data[i]);
		}

		TFT_WriteCommand(0xE1);
		{
			uint8_t data[14] = { 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23 };
			for (size_t i = 0; i < sizeof(data); ++i)
				TFT_WriteByte(data[i]);
		}


	//    TFT_WriteCommand(0xB0);          // RAM Control
	//    TFT_WriteByte(0x00);             //
	//    TFT_WriteByte(0x08);             // Little endian - same as STM32s


		TFT_WriteCommand(0x35);          // TE on
		TFT_WriteByte(0x00);		// V-blank only

		TFT_WriteCommand(TFT_INVON);	// Inversion ON
		TFT_WriteCommand(TFT_SLPOUT);	// Out of sleep mode
		TFT_WriteCommand(TFT_NORON);	// Normal Display on
		TFT_WriteCommand(TFT_DISPON);	// Main screen turned on

		TFT_Fill(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1, 0x0000);

		TFT_Initialized = 1;
		DelayInt(50);
#elif (CONFIG_MODEL_LCD == 1)
		TFT_DeInit();
		DelayInt(50);
		LCD_RST_Set();
		DelayInt(150);
		TFT_WriteCommand(0x01);
		DelayInt(150);

	       TFT_WriteCommand(0xCF);
	       TFT_WriteByte(0x00);
	       TFT_WriteByte(0xC1);
	       TFT_WriteByte(0x30);

	       TFT_WriteCommand(0xED);
	       TFT_WriteByte(0x64);
	       TFT_WriteByte(0x03);
	       TFT_WriteByte(0x12);
	       TFT_WriteByte(0x81);

	       TFT_WriteCommand(0xE8);
	       TFT_WriteByte(0x85);
	       TFT_WriteByte(0x00);
	       TFT_WriteByte(0x7A);

	       TFT_WriteCommand(0xCB);
	       TFT_WriteByte(0x39);
	       TFT_WriteByte(0x2C);
	       TFT_WriteByte(0x00);
	       TFT_WriteByte(0x34);
	       TFT_WriteByte(0x02);

	       TFT_WriteCommand(0xF7);
	       TFT_WriteByte(0x20);

	       TFT_WriteCommand(0xEA);
	       TFT_WriteByte(0x00);
	       TFT_WriteByte(0x00);

	       TFT_WriteCommand(0xc0);
	       TFT_WriteByte(0x21);

	       TFT_WriteCommand(0xc1);
	       TFT_WriteByte(0x11);

	       TFT_WriteCommand(0xc5);
	       TFT_WriteByte(0x25);
	       TFT_WriteByte(0x32);

	       TFT_WriteCommand(0xc7);
	       TFT_WriteByte(0xaa);

	       //180 переворот
//	       TFT_WriteCommand(0x36);
//	       TFT_WriteByte(0x08);
//	       TFT_WriteCommand(0x36);
//	       TFT_WriteByte(0xC8);
	       TFT_Rotation(2);


	       TFT_WriteCommand(0xb6);
	       TFT_WriteByte(0x0a);
	       TFT_WriteByte(0xA2);

	       TFT_WriteCommand(0xb1);
	       TFT_WriteByte(0x00);
	       TFT_WriteByte(0x1B);

	       TFT_WriteCommand(0xf2);
	       TFT_WriteByte(0x00);

	       TFT_WriteCommand(0x26);
	       TFT_WriteByte(0x01);

	       TFT_WriteCommand(0x3a);
	       TFT_WriteByte(0x55);

	       TFT_WriteCommand(0xE0);
	       TFT_WriteByte(0x0f);	       TFT_WriteByte(0x2D);	       TFT_WriteByte(0x0e);	       TFT_WriteByte(0x08);
	       TFT_WriteByte(0x12);	       TFT_WriteByte(0x0a);	       TFT_WriteByte(0x3d);	       TFT_WriteByte(0x95);
	       TFT_WriteByte(0x31);	       TFT_WriteByte(0x04);	       TFT_WriteByte(0x10);	       TFT_WriteByte(0x09);
	       TFT_WriteByte(0x09);	       TFT_WriteByte(0x0d);	       TFT_WriteByte(0x00);

	       TFT_WriteCommand(0xE1);
	       TFT_WriteByte(0x00);	       TFT_WriteByte(0x12);	       TFT_WriteByte(0x17);	       TFT_WriteByte(0x03);
	       TFT_WriteByte(0x0d);	       TFT_WriteByte(0x05);	       TFT_WriteByte(0x2c);	       TFT_WriteByte(0x44);
	       TFT_WriteByte(0x41);	       TFT_WriteByte(0x05);	       TFT_WriteByte(0x0f);	       TFT_WriteByte(0x0a);
	       TFT_WriteByte(0x30);	       TFT_WriteByte(0x32);	       TFT_WriteByte(0x0F);
	       TFT_WriteCommand(0x21);

		TFT_WriteCommand(0x11); // Sleep Out
		DelayInt(120);              // ЖДАТЬ ОБЯЗАТЕЛЬНО!

		TFT_WriteCommand(0x29); // Display ON
		DelayInt(20);

	    DelayInt(1000);
//	    TFT_WriteCommand(0x20); // Inversion OFF
//		TFT_Fill(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1, 0x0000);
//	    while(1) {
//	        TFT_WriteCommand(0x21); // Inversion ON
//	        DelayInt(500);
//	        TFT_WriteCommand(0x20); // Inversion OFF
//	        DelayInt(500);
//	    }
	    TFT_Fill(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1, 0x0000);

		TFT_Initialized = 1;
		DelayInt(50);
#endif
	}
	backlight_set(40);
//	LCD_LED_Set();//
}

//===========================================================================================================================
void TFT_DeInit(void)
{
	backlight_set(0);
//	LCD_LED_Clr();
	LCD_RST_Clr();
	TFT_Initialized = 0;
}

void TFT_Rotation(uint8_t state)
{
    TFT_WriteCommand(TFT_MADCTL); // MADCTL
#if (CONFIG_MODEL_LCD == 0)
    uint8_t bgr = TFT_MADCTL_RGB;
	if(state == 0){
		TFT_WriteByte(bgr);
	}
	else if(state == 1){
		TFT_WriteByte(bgr|TFT_MADCTL_MX|TFT_MADCTL_MV);
	}
	else if(state == 2){
		TFT_WriteByte(bgr|TFT_MADCTL_MX|TFT_MADCTL_MY);
	}
	else if(state == 3){
		TFT_WriteByte(bgr|TFT_MADCTL_MY|TFT_MADCTL_MV);
	}
	else if(state == 4){
		TFT_WriteByte(bgr|TFT_MADCTL_MV);
	}
#elif (CONFIG_MODEL_LCD == 1)
    // Используем BGR (0x08), так как обычно на TFT без него цвета инвертированы
    uint8_t bgr = TFT_MADCTL_BGR;

    switch(state) {
        case 0: // Стандарт (Landscape)
            TFT_WriteByte(bgr);
            break;
        case 1: // Portrait
            TFT_WriteByte(bgr | TFT_MADCTL_MX | TFT_MADCTL_MV);
            break;
        case 2: // Landscape 180° (Перевернутый)
            TFT_WriteByte(bgr | TFT_MADCTL_MX | TFT_MADCTL_MY);
            break;
        case 3: // Portrait 180°
            TFT_WriteByte(bgr | TFT_MADCTL_MY | TFT_MADCTL_MV);
            break;
        default:
            TFT_WriteByte(bgr);
            break;
    }
#endif
    lv_obj_invalidate(lv_scr_act());
}

/**
 * @brief Set address of DisplayWindow
 * @param xi&yi -> coordinates of window
 * @return none
 */
static void TFT_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t x_start = x0, x_end = x1;
    uint16_t y_start = y0, y_end = y1;

    TFT_WriteCommand(TFT_CASET);
    {
        uint8_t data[] = { (uint8_t) (x_start >> 8), (uint8_t) (x_start & 0xFF), (uint8_t) (x_end >> 8), (uint8_t) (x_end & 0xFF) };
        for (size_t i = 0; i < sizeof(data); ++i)
            TFT_WriteByte(data[i]);
    }

    TFT_WriteCommand(TFT_RASET);
    {
        uint8_t data[] = { (uint8_t) (y_start >> 8), (uint8_t) (y_start & 0xFF), (uint8_t) (y_end >> 8), (uint8_t) (y_end & 0xFF) };
        for (size_t i = 0; i < sizeof(data); ++i)
            TFT_WriteByte(data[i]);
    }
    TFT_WriteCommand(TFT_RAMWR);
}

/**
 * @brief Fill an Area with single color
 * @param xSta&ySta -> coordinate of the start point
 * @param xEnd&yEnd -> coordinate of the end point
 * @param color -> color to Fill with
 * @return none
 */
void TFT_Fill(uint16_t xSta, uint16_t ySta, uint16_t xEnd, uint16_t yEnd, uint16_t color)
{
    if ((xEnd >= TFT_WIDTH) || (yEnd >= TFT_HEIGHT))
        return;

    uint16_t i, j;
    TFT_SetAddressWindow(xSta, ySta, xEnd, yEnd);
    for (i = ySta; i <= yEnd; i++)
        for (j = xSta; j <= xEnd; j++)
        {
            uint8_t data[] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
            TFT_WriteData(data, sizeof(data));
        }
}

void TFT_Fill_All(uint16_t color)
{
	uint16_t xSta = 0;
	uint16_t ySta = 0;
	uint16_t xEnd = TFT_WIDTH - 1;
	uint16_t yEnd = TFT_HEIGHT - 1;
    uint16_t i, j;
    TFT_SetAddressWindow(xSta, ySta, xEnd, yEnd);
    for (i = ySta; i <= yEnd; i++)
        for (j = xSta; j <= xEnd; j++)
        {
            uint8_t data[] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
            TFT_WriteData(data, sizeof(data));
        }
}

/**
 * @brief Flush Buffer on the screen (up to 0xFFFF pixels)
 * @param x&y -> start point
 * @param w&h -> width & height
 * @param data -> pointer of the array
 * @return none
 */
void TFT_FlushBuffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data)
{
    if (((x + w - 1) >= TFT_WIDTH) || ((y + h - 1) >= TFT_HEIGHT))
        return;

    TFT_SetAddressWindow(x, y, (uint16_t) (x + w - 1), (uint16_t) (y + h - 1));
//    TFT_Fill(x, y, (uint16_t) (x + w - 1), (uint16_t) (y + h - 1), data);
    TFT_WriteBulk(data, w * h);
}

void TFT_FlushBuffer_1(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data)
{
	if (((x + w - 1) >= TFT_WIDTH) || ((y + h - 1) >= TFT_HEIGHT))
	        return;

    TFT_SetAddressWindow(x, y, (uint16_t) (x + w - 1), (uint16_t) (y + h - 1));
    TFT_WriteData16(data, w * h);
}

/** 
 * @brief A Simple test function for TFT
 * @param  none
 * @return  none
 */
void TFT_Test(void)
{
    TFT_Fill(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1, 0x0000);

    const uint16_t red = 0xF800, green = 0x07E0, blue = 0x001F;
    uint16_t colors[3] = { red, green, blue };
    for (int i = 0; i < 3; ++i)
    {
        int y_start = i * TFT_HEIGHT / 3;
        int y_end = (i + 1) * TFT_HEIGHT / 3 - 1;
        TFT_Fill(0, (uint16_t) y_start, TFT_WIDTH - 1, (uint16_t) y_end, colors[i]);
    }

}
#endif
