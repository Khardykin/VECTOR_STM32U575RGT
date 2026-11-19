#include "bkp.h"
#include "rtc.h"
#include "stm32u5xx_hal.h"

void bkp_write_flag(uint32_t value)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, value);
}

uint32_t bkp_read_flag(void)
{
    uint32_t value;
    HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0, &value);
    return value;
}

void bkp_clear_flag(void)
{
    bkp_write_flag(0x00000000);
}
