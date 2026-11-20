#ifndef COMMAND_VECTOR_H
#define COMMAND_VECTOR_H

#include <stdint.h>
#include "shared_types.h"

/* ==========================================================================
 * Command vector protocol.
 *
 * Architecture:
 * - ordinary config registers: com_map / sn_map / in_map
 * - special flash control block: 0x0F00..0x0F09 as contiguous registers
 * - scattered system registers: compact table
 * - heavy flash work: flags in cmd_state, executed by Measure Task
 * ========================================================================== */

#ifndef COUNT_CHAN
#define COUNT_CHAN 1u
#endif

#ifndef NOCHAN
#define NOCHAN 0xF0u
#endif

/* Command codes. */
enum
{
  CMD_OP_READ_HOLD    = 0x03u,
  CMD_OP_READ_IN      = 0x04u,
  CMD_OP_WRITE_SINGLE = 0x06u,
  CMD_OP_READ_LOG     = 0x0Cu,
  CMD_OP_WRITE_MCU    = 0x0Eu,
  CMD_OP_WRITE_EXT    = 0x0Fu,
  CMD_OP_WRITE_MULTI  = 0x10u,
  CMD_OP_FIND_LOG     = 0x14u
};

/* Modbus exception codes. */
#ifndef CMD_ERR_ILLEGAL_FUNCTION
#define CMD_ERR_ILLEGAL_FUNCTION 0x01u
#endif

#ifndef CMD_ERR_ADDR
#define CMD_ERR_ADDR             0x02u
#endif

#ifndef CMD_ERR_VALUE
#define CMD_ERR_VALUE            0x03u
#endif

#ifndef CMD_ERR_IO
#define CMD_ERR_IO               0x04u
#endif

#ifndef CMD_ERR_BUSY
#define CMD_ERR_BUSY             0x06u
#endif

#ifndef CMD_ERR_VERIFY
#define CMD_ERR_VERIFY           0x08u
#endif

#ifndef CMD_ERR_PROTECTED
#define CMD_ERR_PROTECTED        0x09u
#endif

#ifndef CMD_ERR_LENGTH
#define CMD_ERR_LENGTH           0x0Au
#endif

#ifndef CMD_ERR_UNKNOWN
#define CMD_ERR_UNKNOWN          0x0Bu
#endif

/* Special contiguous flash control block. */
#define CV_FLASH_BLOCK_BASE      0x0F00u
#define CV_FLASH_BLOCK_REGS      0x000Au
#define CV_FLASH_BLOCK_END       (CV_FLASH_BLOCK_BASE + CV_FLASH_BLOCK_REGS - 1u)

/* Flash status codes. */
#define CV_FLASH_ST_OK           0u
#define CV_FLASH_ST_BUSY         1u
#define CV_FLASH_ST_PROTECTED    2u
#define CV_FLASH_ST_ERR_ADDR     3u
#define CV_FLASH_ST_ERR_VERIFY   4u
#define CV_FLASH_ST_ERR_IO       5u

/* Flash targets. */
#define CV_TARGET_NONE           0u
#define CV_TARGET_EXT_SOUND      1u
#define CV_TARGET_EXT_FW         2u
#define CV_TARGET_INT_BOOT       3u
#define CV_TARGET_LOGS           4u

/* Flash commands. */
#define CV_CMD_NONE              0u
#define CV_CMD_ERASE             1u
#define CV_CMD_FW_APPLY          2u

/* Staging buffer size for 0x0E / 0x0F. */
#ifndef CV_WRITE_BUF_SIZE
#define CV_WRITE_BUF_SIZE        256u
#endif

typedef struct
{
  /* Contiguous block 0x0F00..0x0F09. */
  uint16_t build_type;
  uint16_t flash_target;
  uint32_t flash_addr;
  uint32_t flash_len;
  uint32_t flash_key;
  uint16_t flash_status;

  /* Requests for Measure Task. */
  volatile uint8_t  erase_req;
  volatile uint16_t erase_mode;
  volatile uint8_t  save_config;
  volatile uint8_t  save_config_default;
  volatile uint8_t  load_config_default;
  volatile uint8_t  fw_apply_req;

  /* Staging for flash write commands. */
  volatile uint8_t int_write_pending;
  volatile uint8_t ext_write_pending;
  uint32_t write_addr;
  uint16_t write_len;
  uint8_t  write_buf[CV_WRITE_BUF_SIZE];
} cmd_state_t;

extern cmd_state_t cmd_state;

void Command_vector_Init(void);
void Command_vector_RxFeed(uint8_t byte);

/* Transport hook. Implement for UART / NFC / BLE bridge. */
void Command_vector_Transmit(const uint8_t *data, uint16_t len);

uint16_t parsing_command(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap);

uint16_t CmdFunc3 (const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap);
uint16_t CmdFunc4 (const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap);
uint16_t CmdFunc6 (const uint8_t *mas, uint16_t len, uint8_t *out);
uint16_t CmdFunc16(const uint8_t *mas, uint16_t len, uint8_t *out);

uint16_t CmdFunc12(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap);
uint16_t CmdFunc20(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap);
uint16_t CmdFunc14(const uint8_t *mas, uint16_t len, uint8_t *out);
uint16_t CmdFunc15(const uint8_t *mas, uint16_t len, uint8_t *out);

/* Call from Measure Task periodically. */
void Command_vector_ServiceFlashRequests(void);

/* Optional strong hooks to connect with existing project logic. */
void Command_vector_HookSaveConfig(void);
void Command_vector_HookSaveConfigDefault(void);
void Command_vector_HookLoadConfigDefault(void);
void Command_vector_HookSoftReset(void);
void Command_vector_HookPowerOff(void);
void Command_vector_HookClearDataExchange(void);
void Command_vector_HookTempCalib(void);

#endif /* COMMAND_VECTOR_H */
