#ifndef COMMAND_VECTOR_H
#define COMMAND_VECTOR_H

#include <stdint.h>
#include <stddef.h>

#include "shared_types.h"

/* ==========================================================================
 * Command_vector - Modbus RTU register server.
 *
 * com_map[] : holding registers. 0x03 read, 0x06 / 0x10 write.
 * in_map[]  : input registers.   0x04 read.
 *
 * Register address is a plain sequential number from 0x0001.
 * A 32-bit value occupies two consecutive registers (Lo then Hi).
 * Access rights are defined by the command, not by a map field.
 *
 * Memory control registers are flags in cmd_state. Erase / program is
 * performed later by Measure Task in the quiet window.
 * ========================================================================== */

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
#define CMD_ERR_ILLEGAL_FUNCTION 0x01u
#define CMD_ERR_ADDR             0x02u
#define CMD_ERR_VALUE            0x03u
#define CMD_ERR_IO               0x04u
#define CMD_ERR_BUSY             0x06u

/* Register element types. */
#define T_U8    0u   /* uint8_t,  1 register                          */
#define T_U16   1u   /* uint16_t, 1 register                          */
#define T_U32   2u   /* uint32_t, 2 registers (Lo, Hi)                */
#define T_ENUM  3u   /* enum (4 bytes in RAM), 1 register             */
#define T_STR   4u   /* byte array, len bytes, big-endian byte pairs   */
#define T_FLT4  5u   /* float array, len floats, 2 registers each     */

/* Backing object selector: lets ONE map cover several structures. */
#define OBJ_COMMON   0u   /* Sns_Cfg_struct.Config_common              */
#define OBJ_ARCHIVE  1u   /* Sns_Cfg_struct.Config_archive             */
#define OBJ_WORKING  2u   /* Sns_Cfg_struct.Working_data               */
#define OBJ_CFG      3u   /* Sns_Cfg_struct (application_language)     */
#define OBJ_TEMPCAL  4u   /* tempsensor_calib                          */
#define OBJ_APPKEY   5u   /* appkey[16]                                */
#define OBJ_DEVEUI   6u   /* deveui_com[8]                             */
#define OBJ_DEVICE   7u   /* Connected_device                          */
#define OBJ_SENSORS  8u   /* sensors_status                            */
#define OBJ_CMD      9u   /* cmd_state                                 */
#define OBJ_SYS     10u   /* sys_state                                 */
#define OBJ_SNDTEST 11u

typedef struct
{
  uint16_t addr;   /* register number                          */
  uint16_t off;    /* byte offset inside the object            */
  uint8_t  type;   /* T_xx                                     */
  uint8_t  n;      /* arrays only: T_STR bytes, T_FLT4 floats; */
                   /* scalars: 0 (unused)                      */
  uint8_t  obj;    /* OBJ_xx                                   */
} reg_map_t;

/* flash_target values. */
#define CV_TGT_NONE   0u
#define CV_TGT_SOUND  1u
#define CV_TGT_FW     2u
#define CV_TGT_BOOT   3u
#define CV_TGT_LOG    4u

/* flash_status values. */
#define CV_ST_OK      0u
#define CV_ST_BUSY    1u
#define CV_ST_PROT    2u
#define CV_ST_ADDR    3u
#define CV_ST_VERIFY  4u
#define CV_ST_IO      5u

#ifndef CV_WRITE_BUF_SIZE
#define CV_WRITE_BUF_SIZE 256u
#endif

typedef struct
{
  /* Memory control registers (mapped in com_map). */
  uint16_t flash_target;
  uint32_t flash_addr;
  uint32_t flash_len;
  uint32_t flash_key;
  uint16_t flash_status;

  /* Request flags for Measure Task. */
  volatile uint8_t  erase_req;
  volatile uint16_t erase_mode;          /* 1 erase, 2 erase + restore */
  volatile uint8_t  save_config;
  volatile uint8_t  save_config_default;
  volatile uint8_t  load_config_default;
  volatile uint8_t  fw_apply_req;

  /* Staging buffer for 0x0E / 0x0F. */
  volatile uint8_t int_write_pending;
  volatile uint8_t ext_write_pending;
  volatile uint8_t audio_reload_pending;
  uint32_t write_addr;
  uint16_t write_len;
  uint8_t  write_buf[CV_WRITE_BUF_SIZE];
} cmd_state_t;

extern cmd_state_t cmd_state;

void Command_vector_Init(void);
void Command_vector_RxFeed(uint8_t byte);
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

/* Measure Task service. main_vector.c already calls this name. */
void Command_vector_ExecutePendingActions(void);

/* COM consumer, called by Receiver Task (strong override of the
   weak stub in main_vector.c). Drains InputBuffer[TYPE_USART]. */
void command_message(void);

/* Key field sizes are fixed by the protocol, not by CONFIG_LORA.
   appkey[16] -> 8 registers, deveui_com[8] -> 4 registers.        */
#define CV_APPKEY_SIZE  16u
#define CV_DEVEUI_SIZE   8u

#endif /* COMMAND_VECTOR_H */
