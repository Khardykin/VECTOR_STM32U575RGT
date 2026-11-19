#ifndef FW_UPDATE_H
#define FW_UPDATE_H

/**
  ******************************************************************************
  * @file    fw_update.h
  * @brief   Обновление прошивки через Modbus + работа с BKP-регистрами RTC.
  ******************************************************************************
  */

#include <stdint.h>
#include "vector_macros.h"   /* BKP_FLAG_APPLY */


#define FW_STATUS_IDLE       0
#define FW_STATUS_VERIFYING  1
#define FW_STATUS_APPLYING   2
#define FW_STATUS_ERROR      3

void     fw_update_init(void);
void     fw_bkp_write(uint32_t value);
uint32_t fw_bkp_read(void);
void     fw_bkp_clear(void);
void     fw_apply_request(void);
void     fw_reboot_request(void);
uint8_t  fw_verify_image(void);
void     fw_boot_complete(void);

#endif /* FW_UPDATE_H */
