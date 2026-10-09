/*
 * LCD_platform.c
 *
 *  Created on: Nov 5, 2024
 *      Author: me
 *
 *  v17: STM32 HAL вместо AT32. НАЗВАНИЯ ФУНКЦИЙ СОХРАНЕНЫ - TFT.c,
 *  TFT_indicator.c и flush-callback LVGL править не нужно.
 *
 *  Что изменилось по сравнению с AT32-версией:
 *    spi_enable()/spi_frame_bit_num_set()    -> не нужны: SPI2 всегда 8 бит,
 *        16-битные пиксели уходят двумя байтами (старший первым, см.
 *        LCD_swap_rgb565 / VECTOR_LCD_SWAP_RGB565);
 *    SPI_Transmit()/SPI_Transmit_DMA()/SPI_Abort() - макросы main.h (HAL там);
 *    dma_channel_config()/dma_channel_enable()-> SPI_Transmit_DMA() (канал
 *        GPDMA1_Channel8 привязан к SPI2 в кубе, Normal mode);
 *        GPDMA1_Channel8 привязан к SPI2 в HAL_SPI_MspInit, Normal mode);
 *    gpio_bits_set()/gpio_bits_reset()       -> GPIO_WritePin (макрос main.h);
 *    tmr_channel_value_set(TMR8, CH3)        -> HAL: TIM8_CH1 (PC6, AF3),
 *        куб: PSC 159 / ARR 39 -> ШИМ 25 кГц, backlight_set() масштабирует
 *        скважность от фактического ARR (__HAL_TIM_GET_AUTORELOAD);
 *    завершение DMA: HAL_SPI_TxCpltCallback(SPI2) -> LCD_transferCpltCallback().
 *
 *  ЧЕГО ЗДЕСЬ НЕТ И ПОЧЕМУ: HAL_SPI_ErrorCallback. В проекте он уже определён
 *  в spiflash.c (внешняя flash на SPI1) - второй такой же символ линкер не
 *  переживёт. Поэтому ошибка/потеря завершения обрабатывается локально:
 *  ожидание свободного порта ограничено временем (lcd_wait_idle), всё видно в
 *  lcd_status.
 *
 *  КОНТЕКСТ: поток (LVGL/индикация). Из ISR звать нельзя - внутри блокирующие
 *  HAL_SPI_Transmit и ожидание готовности.
 */

#include "LCD_platform.h"

#if (CONFIG_TYPE_LCD_TFT)

#include "vector_config.h"
#include "vector_log.h"
#include "vector_tick.h"

#if VECTOR_LCD_USE_LVGL
/* Указатель на дисплей LVGL, которому сообщаем lv_display_flush_ready().
   Объявлен WEAK: если ваш порт LVGL (ui_init/lv_port из SquareLine Studio)
   определяет disp сам - возьмётся ваше определение, а проект при этом
   линкуется даже до появления UI-кода (тогда disp == NULL и flush_ready
   просто не вызывается - см. проверку в LCD_transferCpltCallback).          */
__attribute__((weak)) lv_display_t *disp = (lv_display_t *)0;
#endif

/* 0 = порт свободен, 1 = идёт передача. Нужен, чтобы вызовы из разных мест
   (TFT_Fill из потока индикации и flush из LVGL) не наложились друг на друга:
   DMA-выдача кадра асинхронная, флаг снимает LCD_transferCpltCallback().     */
static volatile uint16_t LCD_SPI_PORT_State = 0;

/* Сколько итераций ждать освобождения порта ДО планировщика (тика RTOS нет,
   спать нельзя). ~160 МГц / ~5 тактов на итерацию = порядка единиц мс на
   тысячу итераций; передачу 64 КБ на 40 МГц ждём не больше ~13 мс.           */
#define LCD_BUSY_GUARD_INIT   4000000u

volatile lcd_status_t lcd_status =
{
  .busy             = 0u,
  .dma_active       = 0u,
  .last_hal_error   = 0u
};

uint16_t GET_LCD_SPI_PORT_State(void)
{
    return LCD_SPI_PORT_State;
}

/* Принудительно освободить порт: предыдущая передача не завершилась (потеряно
   прерывание DMA/SPI, ошибка канала). Иначе следующий вызов ждал бы вечно, а
   экран остался бы с зажатым CS. КОНТЕКСТ: поток.                            */
static void lcd_force_idle(const char *reason)
{
    lcd_status.cnt_wait_timeout++;
    lcd_status.last_hal_error = (uint32_t)SPI_GetErrorCode(LCD_SPI_PORT);
    (void)SPI_Abort(LCD_SPI_PORT);
    LCD_UnSelect();
    LCD_SPI_PORT_State = 0;
    lcd_status.busy      = 0u;
    lcd_status.dma_active = 0u;
    LOG_E(VLOG_M_SYS, "LCD: port stuck (%s), forced idle, hal_err=%x",
          reason, (uint32_t)lcd_status.last_hal_error);
}

/* Дождаться свободного порта. В потоке - сном по тику RTOS (CPU свободен),
   до планировщика - счётчиком итераций. Таймаут VECTOR_LCD_SPI_TIMEOUT_MS.   */
static void lcd_wait_idle(void)
{
    if (VTICK_IN_THREAD())
    {
        uint32_t t0 = VTICK_MS();
        while (LCD_SPI_PORT_State != 0u)
        {
            if (VTICK_ELAPSED_MS(t0) > (uint32_t)LCD_SPI_TIMEOUT_MS)
            {
                lcd_force_idle("wait timeout");
                return;
            }
            VTICK_SLEEP_MS(1);
        }
    }
    else
    {
        uint32_t guard = LCD_BUSY_GUARD_INIT;
        while ((LCD_SPI_PORT_State != 0u) && (guard != 0u))
        {
            guard--;
        }
        if (LCD_SPI_PORT_State != 0u)
        {
            lcd_force_idle("guard");
        }
    }
}

/* Занять порт под передачу. */
static void lcd_begin(void)
{
    lcd_wait_idle();
    LCD_SPI_PORT_State = 1;
    lcd_status.busy    = 1u;
}

/* Отпустить порт (блокирующие пути; DMA-путь отпускает колбэк). */
static void lcd_end(void)
{
    LCD_SPI_PORT_State = 0;
    lcd_status.busy    = 0u;
}

/* SPI2, DMA и пины настраивает CubeMX (MX_SPI2_Init + HAL_SPI_MspInit), поэтому
   здесь только контроль конфигурации: если в кубе что-то сбросилось, причина
   "белого экрана" видна в логе сразу, а не по косвенным признакам.           */
void LCD_initPlatform(void)
{
    if (SPI_GetDataSize(LCD_SPI_PORT) != SPI_DATA_SIZE_8BIT)
    {
        LOG_W(VLOG_M_SYS, "LCD: SPI2 DataSize != 8bit (CubeMX -> SPI2)");
    }
    if (SPI_GetDMA(LCD_SPI_PORT) == (void *)0)
    {
        LOG_W(VLOG_M_SYS, "LCD: SPI2 TX DMA not linked -> writeBulk will fail");
    }
    lcd_status.busy       = 0u;
    lcd_status.dma_active = 0u;
    LCD_SPI_PORT_State    = 0;
    LCD_UnSelect();
}

/* Команда контроллеру (DC = 0). 8 бит, блокирующая передача. КОНТЕКСТ: поток. */
void LCD_writeCommand8Bit(uint8_t cmd)
{
    lcd_begin();
    lcd_status.cnt_cmd++;

    LCD_Select();
    LCD_DC_Clr();
    if (SPI_Transmit(LCD_SPI_PORT, &cmd, 1, LCD_SPI_TIMEOUT_MS) != SPI_OK)
    {
        lcd_status.last_hal_error = (uint32_t)SPI_GetErrorCode(LCD_SPI_PORT);
        LOG_E(VLOG_M_SYS, "LCD: cmd 0x%x fail, hal_err=%x", (uint32_t)cmd,
              lcd_status.last_hal_error);
    }
    LCD_UnSelect();

    lcd_end();
}

/**
 * @brief Данные в контроллер (DC = 1), 8 бит, блокирующе, кусками до 64 КБ
 * @param data -> указатель на буфер
 * @param size -> размер в БАЙТАХ
 */
void LCD_writeData8Bit(uint8_t *data, uint32_t size)
{
    lcd_begin();
    lcd_status.cnt_data8++;

    LCD_Select();
    LCD_DC_Set();

    /* HAL принимает uint16_t Size - режем на куски, как и в оригинале */
    while (size > 0)
    {
        uint16_t chunk_size = (size > 65535u) ? 65535u : (uint16_t)size;

        if (SPI_Transmit(LCD_SPI_PORT, data, chunk_size, LCD_SPI_TIMEOUT_MS) != SPI_OK)
        {
            lcd_status.last_hal_error = (uint32_t)SPI_GetErrorCode(LCD_SPI_PORT);
            break;
        }

        data += chunk_size;
        size -= chunk_size;
    }

    LCD_UnSelect();
    lcd_end();
}

/**
 * @brief 16-битные данные (пиксели RGB565), блокирующе
 * @param data -> буфер пикселей
 * @param size -> число 16-битных СЛОВ (пикселей), не байт
 */
void LCD_writeData16Bit(uint8_t *data, uint32_t size)
{
    uint32_t bytes = size * 2u;

    lcd_begin();
    lcd_status.cnt_data16++;

#if VECTOR_LCD_SWAP_RGB565
    LCD_swap_rgb565(data, bytes);
#endif

    LCD_Select();
    LCD_DC_Set();

    while (bytes > 0)
    {
        uint16_t chunk_size = (bytes > 65535u) ? 65535u : (uint16_t)bytes;

        if (SPI_Transmit(LCD_SPI_PORT, data, chunk_size, LCD_SPI_TIMEOUT_MS) != SPI_OK)
        {
            lcd_status.last_hal_error = (uint32_t)SPI_GetErrorCode(LCD_SPI_PORT);
            break;
        }

        data  += chunk_size;
        bytes -= chunk_size;
    }

    LCD_UnSelect();
    lcd_end();
}

/**
 * @brief Асинхронная выда кадра по DMA (GPDMA1 Channel8 -> SPI2_TX)
 * @param data -> буфер пикселей RGB565
 * @param size -> число 16-битных СЛОВ (пикселей)
 *
 * CS и флаг занятости снимаются в LCD_transferCpltCallback(), который зовётся
 * из HAL_SPI_TxCpltCallback() - то есть когда DMA реально выдала последний
 * байт. До этого момента буфер менять нельзя (LVGL так и работает: ждёт
 * lv_display_flush_ready()).
 */
void LCD_writeBulk(const uint8_t *data, uint32_t size)
{
    uint32_t bytes = size * 2u;

    /* HAL_SPI_Transmit_DMA берёт uint16_t Size: максимум 65535 байт за раз
       (32767 пикселей). Ограничиваем, как в оригинале.                       */
    if (bytes > 65535u)
    {
        bytes = 65534u;               /* чётное: пиксель пополам не режем */
    }
    if ((data == (const uint8_t *)0) || (bytes == 0u))
    {
        return;
    }

    lcd_begin();

#if VECTOR_LCD_SWAP_RGB565
    /* меняем байты ДО старта DMA: после старта буфер принадлежит контроллеру */
    LCD_swap_rgb565((uint8_t *)data, bytes);
#endif

    LCD_Select();
    LCD_DC_Set();

    lcd_status.dma_active = 1u;       /* колбэк TxCplt поймёт, что это DMA-кадр */

    if (SPI_Transmit_DMA(LCD_SPI_PORT, (uint8_t *)data, (uint16_t)bytes) != SPI_OK)
    {
        /* DMA не стартовала (канал занят/не привязан): снимаем состояние сами,
           иначе экран завис бы с зажатым CS. Кадр потерян - LVGL перерисует по
           следующей инвалидации.                                             */
        lcd_status.dma_active = 0u;
        lcd_status.cnt_dma_fail++;
        lcd_status.last_hal_error = (uint32_t)SPI_GetErrorCode(LCD_SPI_PORT);
        LCD_UnSelect();
        lcd_end();
        LOG_E(VLOG_M_SYS, "LCD: SPI2 DMA start fail (bytes=%u hal_err=%x)",
              bytes, lcd_status.last_hal_error);
        return;
    }

    lcd_status.cnt_bulk++;
}

/**
 * @brief Конец DMA-передачи кадра: снять CS и сказать LVGL, что буфер свободен
 *
 * Вызывается из HAL_SPI_TxCpltCallback() (ниже), то есть из прерывания
 * GPDMA1_Channel8/SPI2 - поэтому здесь только пин, счётчики и
 * lv_display_flush_ready() (он неблокирующий).
 */
void LCD_transferCpltCallback(void)
{
    LCD_UnSelect();
    lcd_end();

    lcd_status.cnt_flush_ready++;

#if VECTOR_LCD_USE_LVGL
    /* IMPORTANT!!! Inform the graphics library that you are ready with the flushing */
    if (disp != (lv_display_t *)0)
    {
        lv_display_flush_ready(disp);
    }
#endif
}

/* Колбэк HAL: передача по SPI2 завершилась.
   КОНТЕКСТ: прерывание.
   ДВА ВАЖНЫХ МОМЕНТА:
     1) чужие SPI не трогаем - на SPI1 внешняя flash (её колбэки в spiflash.c);
     2) HAL зовёт этот колбэк и после БЛОКИРУЮЩЕЙ HAL_SPI_Transmit, а там CS и
        флаг уже сняты самим вызовом. Поэтому реагируем только если шла
        асинхронная выдача кадра (lcd_status.dma_active) - иначе LVGL получил бы
        лишний lv_display_flush_ready() и ушёл бы рисовать в занятый буфер.    */
/* Единственное место файла, где HAL виден напрямую: это ЕГО точка входа
   (переопределяем weak-колбэк). Остальное - через макросы main.h.          */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if ((SPI_INSTANCE(hspi) == LCD_SPI_INSTANCE) && (lcd_status.dma_active != 0u))
    {
        lcd_status.dma_active = 0u;
        LCD_transferCpltCallback();
    }
}

/* Поменять байты местами в каждом 16-битном пикселе RGB565.
   LVGL рисует в порядке байт хоста (little-endian: младший первым), а
   ST7789P3 принимает пиксель старшим байтом вперёд - без обмена цвета уходят
   в сине-зелёную гамму. len - в байтах; нечётный хвост не трогаем.
   КОНТЕКСТ: поток, ДО старта DMA.                                            */
void LCD_swap_rgb565(uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (data == (uint8_t *)0)
    {
        return;
    }
    len &= ~(uint32_t)1u;

    for (i = 0; i < len; i += 2u)
    {
        uint8_t t    = data[i];
        data[i]      = data[i + 1u];
        data[i + 1u] = t;
    }
}

/* Подсветка: ШИМ. PC6 = TIM8_CH1 (AF3, куб): PSC 159 / ARR 39 -> 25 кГц,
   CCR 0..39. Скважность считается от ФАКТИЧЕСКОГО ARR таймера
   (__HAL_TIM_GET_AUTORELOAD) - если настройки в кубе изменятся, код не
   разъедется (старый вариант с VECTOR_LCD_PWM_PERIOD 249 давал постоянные
   100% уже при percent >= 16, т.к. CCR не влезал в ARR 39).
   CCR = ARR+1 -> постоянная 1 (100%). HAL_TIM_PWM_Start идемпотентен.
   КОНТЕКСТ: поток (LVGL/индикация).                                          */
void backlight_set(uint8_t percent)
{
#if (CONFIG_MODEL_LCD == 0)
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim8);
    uint32_t duty;

    if (percent > 100u)
    {
        percent = 100u;
    }
    duty = ((arr + 1u) * (uint32_t)percent + 50u) / 100u;   /* с округлением */
    if (duty > (arr + 1u))
    {
        duty = arr + 1u;
    }

    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, duty);
    (void)HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1);
#endif
}

#endif /* CONFIG_TYPE_LCD_TFT */
