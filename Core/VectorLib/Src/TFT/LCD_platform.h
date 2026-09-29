/*
 * LCD_platform.h
 *
 *  Created on: Nov 5, 2024
 *      Author: me
 *
 *  v17: перенос с AT32 на STM32 HAL. Названия функций и макросов СОХРАНЕНЫ,
 *  чтобы TFT.c / TFT_indicator.c / flush-callback LVGL не править.
 *
 *  АППАРАТУРА (задаёт CubeMX, этот файл её не конфигурирует):
 *    SPI2  PB13 SCK / PB15 MOSI, Simplex Bidirectional Master (только передача),
 *          8 бит, MSB, prescaler 2 -> 80 МГц. ВНИМАНИЕ: у ST7789P3 запись
 *          рассчитана максимум на ~62.5 МГц, на практике стабильно 20-40 МГц -
 *          поставьте в кубе Baud Rate Prescaler /4 (40 МГц) или /8 (20 МГц).
 *    DMA   GPDMA1 Channel8 = SPI2_TX, Memory->Periph, BYTE, Normal mode
 *          (SPI2_IRQn и GPDMA1_Channel8_IRQn в кубе включены).
 *    Пиновое управление: LCD_CS PB14, LCD_DC PB10, LCD_RST PB12, LCD_LED PC6
 *          (все - обычный GPIO output, ШИМ подсветки не настроен).
 *
 *  SPI всегда в 8-битном режиме: переключения frame bit num (AT32
 *  spi_frame_bit_num_set) в HAL не нужно - 16-битные пиксели уходят двумя
 *  байтами, старший байт пикселя первым (порядок байт правит
 *  VECTOR_LCD_SWAP_RGB565, см. vector_config.h).
 *
 *  ВАЖНО ПРО КОЛБЭКИ HAL: HAL_SPI_ErrorCallback в проекте уже занят
 *  spiflash.c (внешняя flash на SPI1), поэтому ошибка SPI2 обрабатывается без
 *  него - ожидание порта ограничено по времени (lcd_wait_idle), счётчики в
 *  lcd_status. HAL_SPI_TxCpltCallback свободен и принадлежит экрану.
 */

#ifndef LCD_PLATFORM_H_
#define LCD_PLATFORM_H_

#include <stdint.h>
#include "main.h"          /* LCD_*_Pin/_GPIO_Port, GPIO_WritePin, SPI_* */
#include "spi.h"           /* hspi2 */
#include "tim.h"           /* htim8: ШИМ подсветки (PC6 = TIM8_CH1, AF3) */
#include "vector_config.h"

#if VECTOR_LCD_USE_LVGL
#include "lvgl.h"
#endif

/* Порт экрана и его DMA-канал. В HAL работаем через handle, поэтому
   LCD_SPI_PORT - указатель на hspi2 (как и был в AT32-версии), а канал берём
   из handle: CubeMX привязал его сам (__HAL_LINKDMA(spiHandle, hdmatx,
   handle_GPDMA1_Channel8)), отдельного extern на него в проекте нет.         */
#define LCD_SPI_PORT        (&hspi2)
#define LCD_SPI_INSTANCE    (SPI2)
#define DMA_CHANNEL         (LCD_SPI_PORT->hdmatx)

/* --- пины экрана: макрос GPIO_WritePin из main.h (вместо AT32 gpio_bits_*) --- */
#define LCD_RST_PORT LCD_RST_GPIO_Port
#define LCD_RST_PIN  LCD_RST_Pin
#define LCD_DC_PORT  LCD_DC_GPIO_Port
#define LCD_DC_PIN   LCD_DC_Pin
#define LCD_LED_PORT LCD_LED_GPIO_Port
#define LCD_LED_PIN  LCD_LED_Pin

#ifndef CFG_NO_CS
#define LCD_CS_PORT  LCD_CS_GPIO_Port
#define LCD_CS_PIN   LCD_CS_Pin
#endif

#define LCD_RST_Clr()   GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, PIN_RESET)
#define LCD_RST_Set()   GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, PIN_SET)
#define LCD_DC_Clr()    GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  PIN_RESET)
#define LCD_DC_Set()    GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  PIN_SET)

#ifndef CFG_NO_CS
#define LCD_Select()    GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  PIN_RESET)
#define LCD_UnSelect()  GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  PIN_SET)
#else
#define LCD_Select()    ((void)0)
#define LCD_UnSelect()  ((void)0)
#endif

/* Подсветка: CONFIG_MODEL_LCD 0 -> вкл/выкл пином LCD_LED (PC6). */
#define LCD_LED_Clr()   GPIO_WritePin(LCD_LED_PORT, LCD_LED_PIN, PIN_RESET)
#define LCD_LED_Set()   GPIO_WritePin(LCD_LED_PORT, LCD_LED_PIN, PIN_SET)

/* Пина POWER_ON на этой плате нет (в main.h не определён): макросы оставлены
   пустыми, чтобы TFT_Init() из Avis переносился без правок.                  */
#define SET_POWER_OFF() do { } while (0)
#define SET_POWER_ON()  do { } while (0)

/* --- состояние экрана: ОДНА структура (как audio_status/tasks_status) ------
 * Смотреть в Expressions при "белом экране":
 *   cnt_bulk > 0, cnt_flush_ready == 0 -> DMA уходит, колбэк не приходит
 *                                         (GPDMA1_Channel8_IRQn / SPI2_IRQn);
 *   cnt_dma_fail > 0                   -> HAL_SPI_Transmit_DMA отказала;
 *   cnt_wait_timeout > 0               -> предыдущая передача не завершилась,
 *                                         порт освобождён принудительно;
 *   last_hal_error != 0                -> код ошибки HAL последнего обмена.  */
typedef struct
{
  volatile uint8_t  busy;            /* зеркало внутреннего флага занятости  */
  volatile uint8_t  dma_active;      /* идёт асинхронная выдача кадра        */
  volatile uint32_t cnt_cmd;         /* команд контроллеру                   */
  volatile uint32_t cnt_data8;       /* блоки данных (8 бит)                 */
  volatile uint32_t cnt_data16;      /* блоки пикселей (16 бит, блокирующе)  */
  volatile uint32_t cnt_bulk;        /* кадров, отданных в DMA               */
  volatile uint32_t cnt_flush_ready; /* из них завершилось (колбэк DMA)      */
  volatile uint32_t cnt_dma_fail;    /* DMA не стартовала                    */
  volatile uint32_t cnt_wait_timeout;/* принудительное освобождение порта    */
  volatile uint32_t last_hal_error;  /* HAL ErrorCode последней передачи     */
} lcd_status_t;

extern volatile lcd_status_t lcd_status;

/* --- API (имена сохранены от AT32-версии) ----------------------------------
 * size у LCD_writeData16Bit()/LCD_writeBulk() - в 16-БИТНЫХ СЛОВАХ (пикселях),
 * а не в байтах: ровно так же, как в оригинале и как зовёт TFT_FlushBuffer()
 * (w * h).                                                                    */
uint16_t GET_LCD_SPI_PORT_State(void);
void LCD_initPlatform(void);
void LCD_writeCommand8Bit(uint8_t cmd);
void LCD_writeData8Bit(uint8_t *data, uint32_t size);
void LCD_writeData16Bit(uint8_t *data, uint32_t size);
void LCD_writeBulk(const uint8_t *data, uint32_t size);
void LCD_transferCpltCallback(void);
void backlight_set(uint8_t percent);

/* Поменять местами байты пикселей RGB565 (len - в БАЙТАХ, чётное). Нужно
   потому, что LVGL рисует в little-endian, а ST7789P3 ждёт старший байт
   пикселя первым. При VECTOR_LCD_SWAP_RGB565 1 вызывается сама из
   writeData16Bit/writeBulk; наружу вынесена, чтобы можно было поменять байты
   один раз в своём flush-callback и выключить макрос.                        */
void LCD_swap_rgb565(uint8_t *data, uint32_t len);

#endif /* LCD_PLATFORM_H_ */
