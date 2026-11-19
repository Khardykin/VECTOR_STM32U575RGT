#include "fw_update.h"
#include "main_vector.h"
#include "shared_macros.h"
#include "rtc.h"
#include "stm32u5xx_hal.h"

static uint8_t s_bkp_inited = 0u;

void fw_update_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    s_bkp_inited = 1u;
}

void fw_bkp_write(uint32_t value)
{
    if (s_bkp_inited == 0u) fw_update_init();
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, value);
}

uint32_t fw_bkp_read(void)
{
    if (s_bkp_inited == 0u) fw_update_init();
    return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);
}

void fw_bkp_clear(void)
{
    fw_bkp_write(0x00000000u);
}

void fw_apply_request(void)
{
    /* Устанавливаем флаг и запускаем таймер.
       Vector_Options_System при срабатывании таймера:
       1. Запишет событие TYPE_FW_UPDATE в лог
       2. Остановит TIM3
       3. Запишет BKP_FLAG_APPLY
       4. Сделает NVIC_SystemReset() */
    SoftResetProgram = 1;
    START_TIMER_RTC(TIMER_RTC_SOFT_RESET_PROGRAM, TIME_RTC_SOFT_RESET_PROGRAM);
}

void fw_boot_complete(void)
{
    if (fw_bkp_read() == BKP_FLAG_APPLY)
    {
        fw_bkp_clear();
    }
}
