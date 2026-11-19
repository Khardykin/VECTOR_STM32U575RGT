#include "fw_update.h"
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
    return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);  /* STM32U5: возвращает значение напрямую */
}

void fw_bkp_clear(void)
{
    fw_bkp_write(0x00000000u);
}

void fw_apply_request(void)
{
//    fw_bkp_write(BKP_FLAG_APPLY);
    HAL_Delay(100);
    NVIC_SystemReset();
}

void fw_reboot_request(void)
{
    NVIC_SystemReset();
}

uint8_t fw_verify_image(void)
{
    return FW_STATUS_IDLE;
}

void fw_boot_complete(void)
{
//    if (fw_bkp_read() == BKP_FLAG_APPLY)
    {
//        fw_bkp_clear();
    }
}
