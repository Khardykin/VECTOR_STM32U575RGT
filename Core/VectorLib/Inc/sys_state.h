#ifndef SYS_STATE_H
#define SYS_STATE_H

/**
  ******************************************************************************
  * @file    sys_state.h
  * @brief   Единое состояние системы.
  *          Заменяет: sys_state.power, sys_state.soft_reset, StateProg.
  ******************************************************************************
  */

#include <stdint.h>
#include "shared_types.h"   /* DEVICE_TURNED enum */
#include "sys_state.h"

/* ==========================================================================
 * Процесс работы с памятью (для индикации на экране)
 * ========================================================================== */
typedef enum
{
  SYS_PROC_IDLE = 0,      /* ничего не происходит */
  SYS_PROC_FW,            /* работа с прошивкой (загрузка/применение) */
  SYS_PROC_ARCHIVE        /* работа с архивом (чтение/стирание логов) */
} sys_process_t;

/* ==========================================================================
 * Единая структура состояния
 * ========================================================================== */
typedef struct
{
  volatile DEVICE_TURNED  power;        /* было sys_state.power */
  volatile sys_process_t  process;      /* что делаем с памятью */
  volatile uint8_t        soft_reset;   /* было sys_state.soft_reset */
} sys_state_t;

extern sys_state_t sys_state;

void sys_state_init(void);

#endif /* SYS_STATE_H */
