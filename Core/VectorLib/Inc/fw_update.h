#ifndef FW_UPDATE_H
#define FW_UPDATE_H

#include <stdint.h>
#include "vector_macros.h"

void     fw_update_init(void);
void     fw_bkp_write(uint32_t value);
uint32_t fw_bkp_read(void);
void     fw_bkp_clear(void);
void     fw_apply_request(void);
void     fw_boot_complete(void);

#endif /* FW_UPDATE_H */
