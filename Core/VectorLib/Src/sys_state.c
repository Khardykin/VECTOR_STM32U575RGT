#include "sys_state.h"

sys_state_t sys_state = {
  .power      = DEVICE_TURNED_OFF_PRE_INIT,
  .process    = SYS_PROC_IDLE,
  .soft_reset = 0
};

void sys_state_init(void)
{
  sys_state.power      = DEVICE_TURNED_OFF_PRE_INIT;
  sys_state.process    = SYS_PROC_IDLE;
  sys_state.soft_reset = 0;
}
