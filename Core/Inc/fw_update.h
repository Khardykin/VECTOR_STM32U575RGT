#ifndef FW_UPDATE_H
#define FW_UPDATE_H

#include <stdint.h>

#define FW_STATUS_IDLE       0
#define FW_STATUS_VERIFYING  1
#define FW_STATUS_APPLYING   2
#define FW_STATUS_ERROR      3

void fw_apply_request(void);
void fw_reboot_request(void);
uint8_t fw_verify_image(void);

#endif /* FW_UPDATE_H */
