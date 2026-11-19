#include "fw_update.h"
#include "bkp.h"
#include "stm32u5xx_hal.h"

void fw_apply_request(void)
{
    bkp_write_flag(BKP_FLAG_APPLY);
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
