#ifndef BKP_H
#define BKP_H

#include <stdint.h>

#define BKP_FLAG_APPLY  0xA5A5F0F0u

void bkp_write_flag(uint32_t value);
uint32_t bkp_read_flag(void);
void bkp_clear_flag(void);

#endif /* BKP_H */
