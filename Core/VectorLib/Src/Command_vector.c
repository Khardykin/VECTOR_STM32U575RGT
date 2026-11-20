#include <string.h>
#include <stdint.h>

#include "tx_api.h"

#include "Command_vector.h"
#include "shared_types.h"

/* ==========================================================================
 * Optional dependencies.
 * ========================================================================== */

#if defined(__has_include)
# if __has_include("Config_save_read.h")
#  include "Config_save_read.h"
#  define CV_HAVE_CONFIG_SAVE_READ 1
# endif

# if __has_include("boot_flash.h")
#  include "boot_flash.h"
#  define CV_HAVE_BOOT_FLASH 1
# endif

# if __has_include("fw_update.h")
#  include "fw_update.h"
#  define CV_HAVE_FW_UPDATE 1
# endif

# if __has_include("extstore.h")
#  include "extstore.h"
#  define CV_HAVE_EXTSTORE 1
# endif

# if __has_include("audio_player.h")
#  include "audio_player.h"
#  define CV_HAVE_AUDIO 1
# endif
#endif

/* ==========================================================================
 * Local register map type.
 * If shared_types.h already has reg_map_t, this block can be removed.
 * ========================================================================== */

#ifndef T_U16
#define T_U16  0u
#endif

#ifndef T_U32
#define T_U32  1u
#endif

#ifndef T_STR
#define T_STR  2u
#endif

#ifndef T_FLT4
#define T_FLT4 3u
#endif

typedef struct
{
  uint16_t addr;
  uint16_t type;
  uint16_t off;
} reg_map_t;

/* ==========================================================================
 * External project objects.
 * ========================================================================== */

extern SNS_CFG Sns_Cfg_struct;

extern uint16_t state_f;
extern uint32_t size_f;

extern uint8_t TamperReset;
extern uint8_t application_language;

extern uint16_t adc_zero_calibration;
extern uint16_t adc_span_calibration;

extern TX_MUTEX xSemaphore_measure;
extern TX_MUTEX xSemaphore_flash;

#ifndef TIME_MUTEX_DELAY_MEASURE
#define TIME_MUTEX_DELAY_MEASURE 100u
#endif

#ifndef TIME_MUTEX_DELAY_FLASH
#define TIME_MUTEX_DELAY_FLASH   100u
#endif

/* ==========================================================================
 * Log API fallback prototypes.
 * If Config_save_read.h already declares these, compatible declarations are fine.
 * ========================================================================== */

extern int log_read(SNS_CFG *cfg, uint32_t ptr, EVENT_LOG *rec);
extern int log_find_by_time(SNS_CFG *cfg,
                            uint32_t t_from,
                            uint32_t t_to,
                            uint32_t *ptr_from,
                            uint32_t *ptr_to);

#ifndef SIZE_LOG
#define SIZE_LOG ((uint16_t)sizeof(EVENT_LOG))
#endif

/* ==========================================================================
 * Bootloader guard.
 * Only enabled if required macros are really present.
 * ========================================================================== */

#if defined(CV_HAVE_BOOT_FLASH) && \
    defined(FLASH_ADDRESS_START_BOOTLOADER) && \
    defined(FLASH_ADDRESS_STOP_BOOTLOADER)
# define CV_BOOT_READY 1
#endif

/* ==========================================================================
 * External flash sector size.
 * Adjust if your MX25R6435F driver uses another erase granularity.
 * ========================================================================== */

#ifndef CV_EXT_SECTOR_SIZE
#define CV_EXT_SECTOR_SIZE 4096u
#endif

/* ==========================================================================
 * Register maps.
 *
 * If your project has real maps, create Command_vector_maps.h and define:
 *   sn_map[], sn_map_n
 *   com_map[], com_map_n
 *   in_map[], in_map_n
 * and define CV_HAS_MAPS.
 *
 * Otherwise empty maps are used and only special/system registers work.
 * ========================================================================== */

#if defined(__has_include)
# if __has_include("Command_vector_maps.h")
#  include "Command_vector_maps.h"
#  define CV_HAS_MAPS 1
# endif
#endif

#ifndef CV_HAS_MAPS

static const reg_map_t sn_map[] =
{
  { 0u, 0u, 0u }
};

static const reg_map_t com_map[] =
{
  { 0u, 0u, 0u }
};

static const reg_map_t in_map[] =
{
  { 0u, 0u, 0u }
};

#define sn_map_n  ((uint16_t)(sizeof(sn_map)  / sizeof(sn_map[0])))
#define com_map_n ((uint16_t)(sizeof(com_map) / sizeof(com_map[0])))
#define in_map_n  ((uint16_t)(sizeof(in_map)  / sizeof(in_map[0])))

#endif

/* ==========================================================================
 * Global command state.
 * ========================================================================== */

cmd_state_t cmd_state;

/* ==========================================================================
 * Small helpers.
 * ========================================================================== */

static uint16_t rd16be(const uint8_t *p)
{
  return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t rd32be(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8)  |
         ((uint32_t)p[3]);
}

static void wr16be(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xFFu);
}

static void wr32le(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v);
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static uint16_t cv_crc16(const uint8_t *buf, uint16_t len)
{
  uint16_t crc = 0xFFFFu;
  uint16_t i;

  while (len--)
  {
    crc ^= (uint16_t)(*buf++);

    for (i = 0u; i < 8u; i++)
    {
      if ((crc & 0x0001u) != 0u)
      {
        crc = (uint16_t)((crc >> 1) ^ 0xA001u);
      }
      else
      {
        crc = (uint16_t)(crc >> 1);
      }
    }
  }

  return crc;
}

static void cv_append_crc(uint8_t *buf, uint16_t len)
{
  uint16_t crc = cv_crc16(buf, len);
  buf[len]     = (uint8_t)(crc & 0xFFu);
  buf[len + 1] = (uint8_t)(crc >> 8);
}

static uint8_t cv_crc_ok(const uint8_t *buf, uint16_t len)
{
  uint16_t crc;

  if (len < 4u)
  {
    return 0u;
  }

  crc = cv_crc16(buf, (uint16_t)(len - 2u));

  if (buf[len - 2u] != (uint8_t)(crc & 0xFFu))
  {
    return 0u;
  }

  if (buf[len - 1u] != (uint8_t)(crc >> 8))
  {
    return 0u;
  }

  return 1u;
}

static uint16_t CmdError(uint8_t *out, const uint8_t *mas, uint8_t code)
{
  out[0] = mas[0];
  out[1] = (uint8_t)(mas[1] | 0x80u);
  out[2] = code;
  return 3u;
}

/* ==========================================================================
 * Mutex helpers.
 * ========================================================================== */

static uint8_t cv_lock_measure(void)
{
  return (tx_mutex_get(&xSemaphore_measure, TIME_MUTEX_DELAY_MEASURE) == TX_SUCCESS) ? 1u : 0u;
}

static void cv_unlock_measure(void)
{
  (void)tx_mutex_put(&xSemaphore_measure);
}

static uint8_t cv_lock_flash(void)
{
  return (tx_mutex_get(&xSemaphore_flash, TIME_MUTEX_DELAY_FLASH) == TX_SUCCESS) ? 1u : 0u;
}

static void cv_unlock_flash(void)
{
  (void)tx_mutex_put(&xSemaphore_flash);
}

/* ==========================================================================
 * Audio quiet window.
 * ========================================================================== */

static uint8_t cv_audio_busy(void)
{
#if defined(CV_HAVE_AUDIO)
  if (audio_status.sound_index != 0u)
  {
    return 1u;
  }
#endif

  return 0u;
}

/* ==========================================================================
 * Special contiguous block 0x0F00..0x0F09.
 *
 * No per-address switch in read/write.
 * Host can read/write the whole block as normal 16-bit registers.
 * ========================================================================== */

static uint16_t cv_block[CV_FLASH_BLOCK_REGS];
static uint8_t  cv_block_dirty = 1u;
static uint8_t  cv_block_pending = 0u;

static void cv_block_pack(void)
{
  cv_block[0] = cmd_state.build_type;
  cv_block[1] = cmd_state.flash_target;

  cv_block[2] = (uint16_t)(cmd_state.flash_addr & 0xFFFFu);
  cv_block[3] = (uint16_t)(cmd_state.flash_addr >> 16);

  cv_block[4] = (uint16_t)(cmd_state.flash_len & 0xFFFFu);
  cv_block[5] = (uint16_t)(cmd_state.flash_len >> 16);

  cv_block[6] = (uint16_t)(cmd_state.flash_key & 0xFFFFu);
  cv_block[7] = (uint16_t)(cmd_state.flash_key >> 16);

  /* Command register is self-clearing. */
  cv_block[8] = 0u;

  cv_block[9] = cmd_state.flash_status;

  cv_block_dirty = 0u;
}

static uint8_t cv_key_ok(void)
{
  return (cmd_state.flash_key != 0u) ? 1u : 0u;
}

static void cv_block_apply(void)
{
  uint16_t cmd;

  if (cv_block_pending == 0u)
  {
    return;
  }

  cv_block_pending = 0u;

  cmd_state.build_type   = cv_block[0];
  cmd_state.flash_target = cv_block[1];

  cmd_state.flash_addr = ((uint32_t)cv_block[3] << 16) | (uint32_t)cv_block[2];
  cmd_state.flash_len  = ((uint32_t)cv_block[5] << 16) | (uint32_t)cv_block[4];
  cmd_state.flash_key  = ((uint32_t)cv_block[7] << 16) | (uint32_t)cv_block[6];

  cmd = cv_block[8];

  /* Command pulse is consumed immediately. */
  cv_block[8] = 0u;

  if (cmd == CV_CMD_NONE)
  {
    cv_block_pack();
    return;
  }

  if (cv_key_ok() == 0u)
  {
    cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
    cv_block_pack();
    return;
  }

  if (cmd == CV_CMD_ERASE)
  {
    if (cmd_state.flash_target == CV_TARGET_INT_BOOT)
    {
#if defined(CV_BOOT_READY)
      if (boot_flash_allowed() == 0)
      {
        cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
      }
      else
      {
        cmd_state.erase_mode = 1u;
        cmd_state.erase_req  = 1u;
        cmd_state.flash_status = CV_FLASH_ST_BUSY;
      }
#else
      cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
#endif
    }
    else if (cmd_state.flash_target == CV_TARGET_EXT_SOUND ||
             cmd_state.flash_target == CV_TARGET_EXT_FW ||
             cmd_state.flash_target == CV_TARGET_LOGS)
    {
#if defined(CV_HAVE_EXTSTORE)
      cmd_state.erase_mode = 1u;
      cmd_state.erase_req  = 1u;
      cmd_state.flash_status = CV_FLASH_ST_BUSY;
#else
      cmd_state.flash_status = CV_FLASH_ST_ERR_IO;
#endif
    }
    else
    {
      cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
    }
  }
  else if (cmd == CV_CMD_FW_APPLY)
  {
    if (cmd_state.flash_target == CV_TARGET_EXT_FW)
    {
      cmd_state.fw_apply_req = 1u;
      cmd_state.flash_status = CV_FLASH_ST_BUSY;
    }
    else
    {
      cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
    }
  }
  else
  {
    cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
  }

  cv_block_dirty = 1u;
  cv_block_pack();
}

static uint8_t cv_block_read(uint16_t addr, uint16_t *val)
{
  uint16_t idx;

  if (addr < CV_FLASH_BLOCK_BASE || addr > CV_FLASH_BLOCK_END)
  {
    return 0u;
  }

  cv_block_apply();

  if (cv_block_dirty != 0u)
  {
    cv_block_pack();
  }

  idx  = (uint16_t)(addr - CV_FLASH_BLOCK_BASE);
  *val = cv_block[idx];

  return 1u;
}

static uint8_t cv_block_write(uint16_t addr, uint16_t val)
{
  uint16_t idx;

  if (addr < CV_FLASH_BLOCK_BASE || addr > CV_FLASH_BLOCK_END)
  {
    return 0u;
  }

  idx = (uint16_t)(addr - CV_FLASH_BLOCK_BASE);

  /* Status register is read-only. */
  if (idx == 9u)
  {
    return 0u;
  }

  cv_block[idx] = val;

  /*
   * Do not apply immediately inside Modbus write.
   * The whole block is applied before next read or in service/dispatch.
   */
  cv_block_pending = 1u;
  cv_block_dirty   = 1u;

  return 1u;
}

/* ==========================================================================
 * Scattered system registers.
 * ========================================================================== */

typedef enum
{
  CV_SYS_RO  = 0,
  CV_SYS_RW  = 1,
  CV_SYS_WO  = 2
} cv_sys_acc_t;

typedef enum
{
  CV_SYS_KIND_NONE = 0,
  CV_SYS_KIND_U16,
  CV_SYS_KIND_U8,
  CV_SYS_KIND_U32_LO,
  CV_SYS_KIND_U32_HI,
  CV_SYS_KIND_ERASE_MODE,
  CV_SYS_KIND_SOFT_RESET,
  CV_SYS_KIND_SAVE_CONFIG,
  CV_SYS_KIND_SAVE_CONFIG_DEFAULT,
  CV_SYS_KIND_LOAD_CONFIG_DEFAULT,
  CV_SYS_KIND_CLEAR_DATA_EXCHANGE,
  CV_SYS_KIND_TEMP_CALIB,
  CV_SYS_KIND_POWER_OFF
} cv_sys_kind_t;

typedef struct
{
  uint16_t        addr;
  uint8_t         acc;
  uint8_t         kind;
  volatile void  *ptr;
} cv_sys_item_t;

static const cv_sys_item_t cv_sys_items[] =
{
  { 0x0001u, CV_SYS_RW, CV_SYS_KIND_ERASE_MODE, &cmd_state.erase_mode },
  { 0x0003u, CV_SYS_RW, CV_SYS_KIND_U16, &state_f },
  { 0x0004u, CV_SYS_RO, CV_SYS_KIND_U32_LO, &size_f },
  { 0x0005u, CV_SYS_WO, CV_SYS_KIND_SOFT_RESET, 0 },
  { 0x0006u, CV_SYS_RW, CV_SYS_KIND_U8, &TamperReset },

  { 0x0240u, CV_SYS_WO, CV_SYS_KIND_SAVE_CONFIG, &cmd_state.save_config },
  { 0x0250u, CV_SYS_WO, CV_SYS_KIND_SAVE_CONFIG_DEFAULT, &cmd_state.save_config_default },
  { 0x0252u, CV_SYS_WO, CV_SYS_KIND_LOAD_CONFIG_DEFAULT, &cmd_state.load_config_default },

  { 0x0254u, CV_SYS_WO, CV_SYS_KIND_CLEAR_DATA_EXCHANGE, 0 },
  { 0x0256u, CV_SYS_WO, CV_SYS_KIND_TEMP_CALIB, 0 },

  { 0x0300u, CV_SYS_RW, CV_SYS_KIND_U16, &adc_zero_calibration },
  { 0x0301u, CV_SYS_RW, CV_SYS_KIND_U16, &adc_span_calibration },

  { 0x0700u, CV_SYS_RW, CV_SYS_KIND_U8, &application_language },
  { 0x0800u, CV_SYS_WO, CV_SYS_KIND_POWER_OFF, 0 },

  { 0u, 0u, 0u, 0 }
};

static const cv_sys_item_t *cv_sys_find(uint16_t addr)
{
  uint16_t i;

  for (i = 0u; cv_sys_items[i].addr != 0u; i++)
  {
    if (cv_sys_items[i].addr == addr)
    {
      return &cv_sys_items[i];
    }
  }

  return 0;
}

static uint8_t cv_sys_read(uint16_t addr, uint16_t *val)
{
  const cv_sys_item_t *it = cv_sys_find(addr);

  if (it == 0)
  {
    return 0u;
  }

  switch (it->kind)
  {
    case CV_SYS_KIND_U16:
      if (it->ptr != 0)
      {
        *val = *(volatile uint16_t *)it->ptr;
        return 1u;
      }
      break;

    case CV_SYS_KIND_U8:
      if (it->ptr != 0)
      {
        *val = (uint16_t)(*(volatile uint8_t *)it->ptr);
        return 1u;
      }
      break;

    case CV_SYS_KIND_U32_LO:
      if (it->ptr != 0)
      {
        *val = (uint16_t)(*(volatile uint32_t *)it->ptr & 0xFFFFu);
        return 1u;
      }
      break;

    case CV_SYS_KIND_U32_HI:
      if (it->ptr != 0)
      {
        *val = (uint16_t)(*(volatile uint32_t *)it->ptr >> 16);
        return 1u;
      }
      break;

    case CV_SYS_KIND_ERASE_MODE:
      *val = cmd_state.erase_mode;
      return 1u;

    default:
      break;
  }

  return 0u;
}

static uint8_t cv_sys_write(uint16_t addr, uint16_t val)
{
  const cv_sys_item_t *it = cv_sys_find(addr);

  if (it == 0)
  {
    return 0u;
  }

  if (it->acc == CV_SYS_RO)
  {
    return 0u;
  }

  switch (it->kind)
  {
    case CV_SYS_KIND_U16:
      if (it->ptr != 0)
      {
        *(volatile uint16_t *)it->ptr = val;
        return 1u;
      }
      break;

    case CV_SYS_KIND_U8:
      if (it->ptr != 0)
      {
        *(volatile uint8_t *)it->ptr = (uint8_t)val;
        return 1u;
      }
      break;

    case CV_SYS_KIND_U32_LO:
      if (it->ptr != 0)
      {
        volatile uint32_t *p = (volatile uint32_t *)it->ptr;
        *p = (*p & 0xFFFF0000u) | (uint32_t)val;
        return 1u;
      }
      break;

    case CV_SYS_KIND_U32_HI:
      if (it->ptr != 0)
      {
        volatile uint32_t *p = (volatile uint32_t *)it->ptr;
        *p = (*p & 0x0000FFFFu) | ((uint32_t)val << 16);
        return 1u;
      }
      break;

    case CV_SYS_KIND_ERASE_MODE:
      if (val == 1u || val == 2u)
      {
        cmd_state.erase_mode = val;
        cmd_state.erase_req  = 1u;
        cmd_state.flash_status = CV_FLASH_ST_BUSY;
        return 1u;
      }

      if (val == 0u)
      {
        cmd_state.erase_mode = 0u;
        cmd_state.erase_req  = 0u;
        return 1u;
      }

      return 0u;

    case CV_SYS_KIND_SOFT_RESET:
      if (val != 0u)
      {
        Command_vector_HookSoftReset();
      }
      return 1u;

    case CV_SYS_KIND_SAVE_CONFIG:
      if (val != 0u)
      {
        cmd_state.save_config = 1u;
      }
      return 1u;

    case CV_SYS_KIND_SAVE_CONFIG_DEFAULT:
      if (val != 0u)
      {
        cmd_state.save_config_default = 1u;
      }
      return 1u;

    case CV_SYS_KIND_LOAD_CONFIG_DEFAULT:
      if (val != 0u)
      {
        cmd_state.load_config_default = 1u;
      }
      return 1u;

    case CV_SYS_KIND_CLEAR_DATA_EXCHANGE:
      Command_vector_HookClearDataExchange();
      return 1u;

    case CV_SYS_KIND_TEMP_CALIB:
      Command_vector_HookTempCalib();
      return 1u;

    case CV_SYS_KIND_POWER_OFF:
      if (val != 0u)
      {
        Command_vector_HookPowerOff();
      }
      return 1u;

    default:
      break;
  }

  return 0u;
}

/* ==========================================================================
 * Unified special/control register access.
 * ========================================================================== */

static uint8_t ctrl_reg_read(uint16_t addr, uint16_t *val)
{
  if (val == 0)
  {
    return 0u;
  }

  if (cv_block_read(addr, val))
  {
    return 1u;
  }

  return cv_sys_read(addr, val);
}

static uint8_t ctrl_reg_write(uint16_t addr, uint16_t val)
{
  if (cv_block_write(addr, val))
  {
    return 1u;
  }

  return cv_sys_write(addr, val);
}

/* ==========================================================================
 * Ordinary mapped register access.
 * ========================================================================== */

static int reg_find(const reg_map_t *map, uint16_t n, uint16_t addr, uint16_t *idx)
{
  uint16_t i;

  for (i = 0u; i < n; i++)
  {
    if (map[i].addr == 0u && map[i].type == 0u && map[i].off == 0u)
    {
      break;
    }

    if (map[i].addr == addr)
    {
      *idx = i;
      return (int)i;
    }

    /* Allow high half of a 32-bit register. */
    if (map[i].type == T_U32 && (uint16_t)(map[i].addr + 1u) == addr)
    {
      *idx = i;
      return (int)i;
    }
  }

  return -1;
}

static uint8_t reg_read_mapped(const reg_map_t *map, uint16_t n,
                               uint16_t addr, void *base, uint16_t *val)
{
  uint16_t idx;
  const reg_map_t *m;
  uint8_t *p;

  if (reg_find(map, n, addr, &idx) < 0)
  {
    return 0u;
  }

  m = &map[idx];
  p = (uint8_t *)base + m->off;

  switch (m->type)
  {
    case T_U16:
      *val = *(uint16_t *)p;
      return 1u;

    case T_U32:
    {
      uint32_t v;
      memcpy(&v, p, sizeof(v));

      if (addr == m->addr)
      {
        *val = (uint16_t)(v & 0xFFFFu);
      }
      else
      {
        *val = (uint16_t)(v >> 16);
      }

      return 1u;
    }

    default:
      break;
  }

  return 0u;
}

static uint8_t reg_write_mapped(const reg_map_t *map, uint16_t n,
                                uint16_t addr, void *base, uint16_t val)
{
  uint16_t idx;
  const reg_map_t *m;
  uint8_t *p;

  if (reg_find(map, n, addr, &idx) < 0)
  {
    return 0u;
  }

  m = &map[idx];

  if (m->type == T_U32 && addr != m->addr)
  {
    /* High half write requires read-modify-write. */
    uint32_t v;
    p = (uint8_t *)base + m->off;
    memcpy(&v, p, sizeof(v));
    v = (v & 0x0000FFFFu) | ((uint32_t)val << 16);
    memcpy(p, &v, sizeof(v));
    return 1u;
  }

  p = (uint8_t *)base + m->off;

  switch (m->type)
  {
    case T_U16:
      *(uint16_t *)p = val;
      return 1u;

    case T_U32:
    {
      uint32_t v;
      memcpy(&v, p, sizeof(v));
      v = (v & 0xFFFF0000u) | (uint32_t)val;
      memcpy(p, &v, sizeof(v));
      return 1u;
    }

    default:
      break;
  }

  return 0u;
}

static uint8_t cv_chan_ok(uint16_t chan)
{
  if (chan != 0u && chan != (uint16_t)(NOCHAN >> 4))
  {
    return 0u;
  }

  return 1u;
}

static uint8_t cv_read_register(uint16_t full_addr, uint16_t *val)
{
  uint16_t sub  = (uint16_t)(full_addr & 0x0FFFu);
  uint16_t chan = (uint16_t)((full_addr >> 12) & 0x0Fu);

  if (cv_chan_ok(chan) == 0u)
  {
    return 0u;
  }

  /* Common config registers. */
  if (reg_read_mapped(com_map, com_map_n, sub,
                      (void *)&Sns_Cfg_struct.Config_common, val))
  {
    return 1u;
  }

  /*
   * Per-channel sensor registers are disabled in this build because
   * current SNS_CFG does not expose a Sensor member.
   * Restore this block when real sn_map and sensor base are available.
   */

  /* Input telemetry registers. */
  if (reg_read_mapped(in_map, in_map_n, sub,
                      (void *)&Sns_Cfg_struct.Config_common, val))
  {
    return 1u;
  }

  /* Special/control block and scattered system registers. */
  if (ctrl_reg_read(full_addr, val))
  {
    return 1u;
  }

  return 0u;
}

static uint8_t cv_write_register(uint16_t full_addr, uint16_t val)
{
  uint16_t sub  = (uint16_t)(full_addr & 0x0FFFu);
  uint16_t chan = (uint16_t)((full_addr >> 12) & 0x0Fu);

  if (cv_chan_ok(chan) == 0u)
  {
    return 0u;
  }

  /* Common config registers. */
  if (reg_write_mapped(com_map, com_map_n, sub,
                       (void *)&Sns_Cfg_struct.Config_common, val))
  {
    return 1u;
  }

  /*
   * Per-channel sensor registers are disabled in this build because
   * current SNS_CFG does not expose a Sensor member.
   */

  /* Special/control block and scattered system registers. */
  if (ctrl_reg_write(full_addr, val))
  {
    return 1u;
  }

  return 0u;
}

/* ==========================================================================
 * CmdFunc3 / CmdFunc4 / CmdFunc6 / CmdFunc16
 * ========================================================================== */

uint16_t CmdFunc3(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint16_t addr;
  uint16_t qty;
  uint16_t i;
  uint16_t need;

  (void)len;

  addr = rd16be(&mas[2]);
  qty  = rd16be(&mas[4]);

  if (qty == 0u || qty > 125u)
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  need = (uint16_t)(3u + qty * 2u);

  if (cap < need)
  {
    return CmdError(out, mas, CMD_ERR_LENGTH);
  }

  out[0] = mas[0];
  out[1] = mas[1];
  out[2] = (uint8_t)(qty * 2u);

  for (i = 0u; i < qty; i++)
  {
    uint16_t v;
    uint16_t a = (uint16_t)(addr + i);

    if (cv_read_register(a, &v) == 0u)
    {
      return CmdError(out, mas, CMD_ERR_ADDR);
    }

    wr16be(&out[3u + i * 2u], v);
  }

  return need;
}

uint16_t CmdFunc4(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  /* Input registers use the same read path in this implementation. */
  return CmdFunc3(mas, len, out, cap);
}

uint16_t CmdFunc6(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint16_t addr;
  uint16_t val;

  (void)len;

  addr = rd16be(&mas[2]);
  val  = rd16be(&mas[4]);

  if (cv_write_register(addr, val) == 0u)
  {
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  memcpy(out, mas, 6u);
  return 6u;
}

uint16_t CmdFunc16(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint16_t addr;
  uint16_t qty;
  uint16_t bytes;
  uint16_t i;

  addr  = rd16be(&mas[2]);
  qty   = rd16be(&mas[4]);
  bytes = (uint16_t)mas[6];

  if (qty == 0u || qty > 123u)
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if (bytes != (uint16_t)(qty * 2u))
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if (len != (uint16_t)(7u + bytes + 2u))
  {
    return CmdError(out, mas, CMD_ERR_LENGTH);
  }

  for (i = 0u; i < qty; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t v = rd16be(&mas[7u + i * 2u]);

    if (cv_write_register(a, v) == 0u)
    {
      return CmdError(out, mas, CMD_ERR_ADDR);
    }
  }

  memcpy(out, mas, 6u);
  return 6u;
}

/* ==========================================================================
 * Log commands.
 * ========================================================================== */

uint16_t CmdFunc12(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint16_t ptr16;
  uint16_t cnt;
  uint16_t i;
  uint32_t ptr;
  uint32_t beg;
  uint32_t cur;
  uint32_t payload;
  EVENT_LOG rec;

  (void)len;

  ptr16 = rd16be(&mas[2]);
  cnt   = rd16be(&mas[4]);

  if (cnt == 0u || cnt > 255u)
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  payload = (uint32_t)cnt * (uint32_t)SIZE_LOG;

  if (payload > 3000u)
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if (cap < (uint16_t)(3u + payload))
  {
    return CmdError(out, mas, CMD_ERR_LENGTH);
  }

  if (!cv_lock_flash())
  {
    return CmdError(out, mas, CMD_ERR_BUSY);
  }

  beg = Sns_Cfg_struct.Config_archive.BegginAddrFile;
  cur = Sns_Cfg_struct.Config_archive.CurrentAddrFile;

  if (cur == beg)
  {
    cv_unlock_flash();
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  ptr = (uint32_t)ptr16;

  if (ptr < beg)
  {
    cv_unlock_flash();
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  if ((ptr + (uint32_t)cnt) > cur)
  {
    cv_unlock_flash();
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  out[0] = mas[0];
  out[1] = mas[1];
  out[2] = (uint8_t)cnt;

  for (i = 0u; i < cnt; i++)
  {
    int rc = log_read(&Sns_Cfg_struct, (uint32_t)(ptr + i), &rec);

    if (rc != 0)
    {
      cv_unlock_flash();

      if (rc == 1)
      {
        return CmdError(out, mas, CMD_ERR_ADDR);
      }

      if (rc == 2)
      {
        return CmdError(out, mas, CMD_ERR_VERIFY);
      }

      return CmdError(out, mas, CMD_ERR_IO);
    }

    memcpy(&out[3u + (uint32_t)i * (uint32_t)SIZE_LOG], &rec, SIZE_LOG);
  }

  cv_unlock_flash();
  return (uint16_t)(3u + payload);
}

uint16_t CmdFunc20(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint32_t t_from;
  uint32_t t_to;
  uint32_t ptr_from = 0u;
  uint32_t ptr_to   = 0u;
  int rc;

  (void)len;
  (void)cap;

  t_from = rd32be(&mas[2]);
  t_to   = rd32be(&mas[6]);

  if (t_to < t_from)
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if (!cv_lock_flash())
  {
    return CmdError(out, mas, CMD_ERR_BUSY);
  }

  rc = log_find_by_time(&Sns_Cfg_struct, t_from, t_to, &ptr_from, &ptr_to);

  cv_unlock_flash();

  if (rc < 0)
  {
    return CmdError(out, mas, CMD_ERR_IO);
  }

  if (rc == 2)
  {
    return CmdError(out, mas, CMD_ERR_VERIFY);
  }

  out[0] = mas[0];
  out[1] = mas[1];
  wr32le(&out[2], ptr_from);
  wr32le(&out[6], ptr_to);

  return 10u;
}

/* ==========================================================================
 * Flash staging commands.
 * ========================================================================== */

uint16_t CmdFunc14(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint32_t addr;
  uint16_t size;

  (void)len;

  /* Make sure previous block writes are applied. */
  cv_block_apply();

  memcpy(out, mas, 6u);

  addr = rd32be(&mas[2]);
  size = (uint16_t)mas[6];

  if (size == 0u || size > CV_WRITE_BUF_SIZE)
  {
    cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if ((addr & 0x03u) != 0u || (size & 0x03u) != 0u)
  {
    cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if (cv_key_ok() == 0u)
  {
    cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
    return CmdError(out, mas, CMD_ERR_PROTECTED);
  }

#if defined(CV_BOOT_READY)
  if (boot_flash_allowed() == 0)
  {
    cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
    return CmdError(out, mas, CMD_ERR_PROTECTED);
  }

  if (addr < FLASH_ADDRESS_START_BOOTLOADER ||
      (addr + size) > FLASH_ADDRESS_STOP_BOOTLOADER)
  {
    cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
    return CmdError(out, mas, CMD_ERR_ADDR);
  }
#else
  cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
  return CmdError(out, mas, CMD_ERR_PROTECTED);
#endif

  if (cmd_state.int_write_pending != 0u)
  {
    cmd_state.flash_status = CV_FLASH_ST_BUSY;
    return CmdError(out, mas, CMD_ERR_BUSY);
  }

  cmd_state.write_addr = addr;
  cmd_state.write_len  = size;
  memcpy(cmd_state.write_buf, &mas[7], size);

  cmd_state.int_write_pending = 1u;
  cmd_state.flash_status      = CV_FLASH_ST_BUSY;
  cv_block_dirty              = 1u;

  return 6u;
}

uint16_t CmdFunc15(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint32_t addr;
  uint16_t size;

  (void)len;

  cv_block_apply();

  memcpy(out, mas, 6u);

  addr = rd32be(&mas[2]);
  size = (uint16_t)mas[6];

  if (size == 0u || size > CV_WRITE_BUF_SIZE)
  {
    cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if (cmd_state.flash_target != CV_TARGET_EXT_SOUND &&
      cmd_state.flash_target != CV_TARGET_EXT_FW)
  {
    cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  if (cv_key_ok() == 0u)
  {
    cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
    return CmdError(out, mas, CMD_ERR_PROTECTED);
  }

#if defined(SF_TOTAL_SIZE)
  if (addr >= SF_TOTAL_SIZE || (addr + size) > SF_TOTAL_SIZE)
  {
    cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
    return CmdError(out, mas, CMD_ERR_ADDR);
  }
#endif

  if (cmd_state.ext_write_pending != 0u)
  {
    cmd_state.flash_status = CV_FLASH_ST_BUSY;
    return CmdError(out, mas, CMD_ERR_BUSY);
  }

  cmd_state.write_addr = addr;
  cmd_state.write_len  = size;
  memcpy(cmd_state.write_buf, &mas[7], size);

  cmd_state.ext_write_pending = 1u;
  cmd_state.flash_status      = CV_FLASH_ST_BUSY;
  cv_block_dirty              = 1u;

  return 6u;
}

/* ==========================================================================
 * Dispatcher.
 * ========================================================================== */

uint16_t parsing_command(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint8_t op;
  uint16_t answer = 0u;
  uint8_t locked = 0u;

  if (len < 5u)
  {
    return CmdError(out, mas, CMD_ERR_LENGTH);
  }

  op = mas[1];

  /* Apply pending special block before processing any command. */
  cv_block_apply();

  if (op == CMD_OP_READ_HOLD || op == CMD_OP_READ_IN || op == CMD_OP_WRITE_MULTI)
  {
    if (!cv_lock_measure())
    {
      return CmdError(out, mas, CMD_ERR_BUSY);
    }

    locked = 1u;
  }

  switch (op)
  {
    case CMD_OP_READ_HOLD:
      if (len != 8u)
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }

      answer = CmdFunc3(mas, len, out, cap);
      break;

    case CMD_OP_READ_IN:
      if (len != 8u)
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }

      answer = CmdFunc4(mas, len, out, cap);
      break;

    case CMD_OP_WRITE_SINGLE:
      if (len != 8u)
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }

      answer = CmdFunc6(mas, len, out);
      break;

    case CMD_OP_WRITE_MULTI:
      if (len < 9u)
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }

      answer = CmdFunc16(mas, len, out);
      break;

    case CMD_OP_READ_LOG:
      if (len != 8u)
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }

      answer = CmdFunc12(mas, len, out, cap);
      break;

    case CMD_OP_FIND_LOG:
      if (len != 12u)
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }

      answer = CmdFunc20(mas, len, out, cap);
      break;

    case CMD_OP_WRITE_MCU:
      if (len < 9u || (uint32_t)len != 7u + (uint32_t)mas[6] + 2u)
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }

      answer = CmdFunc14(mas, len, out);
      break;

    case CMD_OP_WRITE_EXT:
      if (len < 9u || (uint32_t)len != 7u + (uint32_t)mas[6] + 2u)
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }

      answer = CmdFunc15(mas, len, out);
      break;

    default:
      answer = CmdError(out, mas, CMD_ERR_UNKNOWN);
      break;
  }

  if (locked != 0u)
  {
    cv_unlock_measure();
  }

  /* Apply block writes made by this command. */
  cv_block_apply();

  if (answer == 0u)
  {
    answer = CmdError(out, mas, CMD_ERR_IO);
  }

  cv_append_crc(out, answer);
  return (uint16_t)(answer + 2u);
}

/* ==========================================================================
 * Receiver.
 * ========================================================================== */

#define CV_RX_BUF_SIZE 280u
#define CV_TX_BUF_SIZE 4096u

typedef struct
{
  uint8_t  buf[CV_RX_BUF_SIZE];
  uint16_t len;
  uint16_t expect;
} cv_rx_t;

static cv_rx_t rx;
static uint8_t tx_buf[CV_TX_BUF_SIZE];

static uint16_t cv_expected_len(const uint8_t *buf, uint16_t len)
{
  uint8_t op;

  if (len < 2u)
  {
    return 0u;
  }

  op = buf[1];

  switch (op)
  {
    case CMD_OP_READ_HOLD:
    case CMD_OP_READ_IN:
    case CMD_OP_WRITE_SINGLE:
    case CMD_OP_READ_LOG:
      return 8u;

    case CMD_OP_FIND_LOG:
      return 12u;

    case CMD_OP_WRITE_MULTI:
    case CMD_OP_WRITE_MCU:
    case CMD_OP_WRITE_EXT:
      if (len >= 7u)
      {
        return (uint16_t)(7u + (uint16_t)buf[6] + 2u);
      }

      return 0u;

    default:
      return 8u;
  }
}

void Command_vector_RxFeed(uint8_t byte)
{
  if (rx.len >= CV_RX_BUF_SIZE)
  {
    rx.len    = 0u;
    rx.expect = 0u;
    return;
  }

  rx.buf[rx.len++] = byte;

  if (rx.len == 2u)
  {
    rx.expect = cv_expected_len(rx.buf, rx.len);
  }

  if (rx.len >= 7u &&
      (rx.buf[1] == CMD_OP_WRITE_MULTI ||
       rx.buf[1] == CMD_OP_WRITE_MCU ||
       rx.buf[1] == CMD_OP_WRITE_EXT))
  {
    rx.expect = cv_expected_len(rx.buf, rx.len);
  }

  if (rx.expect != 0u && rx.len == rx.expect)
  {
    if (cv_crc_ok(rx.buf, rx.len) != 0u)
    {
      uint16_t ans = parsing_command(rx.buf, rx.len, tx_buf, CV_TX_BUF_SIZE);

      if (ans != 0u)
      {
        Command_vector_Transmit(tx_buf, ans);
      }
    }

    rx.len    = 0u;
    rx.expect = 0u;
    return;
  }

  if (rx.expect != 0u && rx.len > rx.expect)
  {
    rx.len    = 0u;
    rx.expect = 0u;
  }
}

/* ==========================================================================
 * Init.
 * ========================================================================== */

void Command_vector_Init(void)
{
  memset(&rx, 0, sizeof(rx));
  memset(cv_block, 0, sizeof(cv_block));

  cv_block_dirty   = 1u;
  cv_block_pending = 0u;

  cv_block_pack();
}

/* ==========================================================================
 * Weak hooks.
 * Implement strong versions in main_vector.c or elsewhere if needed.
 * ========================================================================== */

__attribute__((weak)) void Command_vector_Transmit(const uint8_t *data, uint16_t len)
{
  (void)data;
  (void)len;
}

__attribute__((weak)) void Command_vector_HookSaveConfig(void)
{
}

__attribute__((weak)) void Command_vector_HookSaveConfigDefault(void)
{
}

__attribute__((weak)) void Command_vector_HookLoadConfigDefault(void)
{
}

__attribute__((weak)) void Command_vector_HookSoftReset(void)
{
}

__attribute__((weak)) void Command_vector_HookPowerOff(void)
{
}

__attribute__((weak)) void Command_vector_HookClearDataExchange(void)
{
}

__attribute__((weak)) void Command_vector_HookTempCalib(void)
{
}

/* Weak log fallbacks. Strong versions from Config_save_read.c override these. */

__attribute__((weak)) int log_read(SNS_CFG *cfg, uint32_t ptr, EVENT_LOG *rec)
{
  (void)cfg;
  (void)ptr;
  (void)rec;
  return -1;
}

__attribute__((weak)) int log_find_by_time(SNS_CFG *cfg,
                                           uint32_t t_from,
                                           uint32_t t_to,
                                           uint32_t *ptr_from,
                                           uint32_t *ptr_to)
{
  (void)cfg;
  (void)t_from;
  (void)t_to;

  if (ptr_from != 0)
  {
    *ptr_from = 0u;
  }

  if (ptr_to != 0)
  {
    *ptr_to = 0u;
  }

  return -1;
}

/* ==========================================================================
 * Measure Task service.
 *
 * This function must be called periodically from Measure Task.
 * Heavy flash operations are executed only in quiet window.
 * ========================================================================== */

void Command_vector_ServiceFlashRequests(void)
{
  /* Light config requests can be done immediately. */
  if (cmd_state.save_config != 0u)
  {
    cmd_state.save_config = 0u;
    Command_vector_HookSaveConfig();
  }

  if (cmd_state.save_config_default != 0u)
  {
    cmd_state.save_config_default = 0u;
    Command_vector_HookSaveConfigDefault();
  }

  if (cmd_state.load_config_default != 0u)
  {
    cmd_state.load_config_default = 0u;
    Command_vector_HookLoadConfigDefault();
  }

  /* Heavy flash work only in quiet window. */
  if (cv_audio_busy() != 0u)
  {
    return;
  }

  if (cmd_state.erase_req != 0u)
  {
    uint16_t mode   = cmd_state.erase_mode;
    uint16_t target = cmd_state.flash_target;

    cmd_state.erase_req  = 0u;
    cmd_state.erase_mode = 0u;
    cmd_state.flash_status = CV_FLASH_ST_BUSY;

    if (cv_key_ok() == 0u)
    {
      cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
      cv_block_dirty = 1u;
      return;
    }

    if (target == CV_TARGET_INT_BOOT)
    {
#if defined(CV_BOOT_READY)
      if (boot_flash_allowed() == 0)
      {
        cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
      }
      else if (mode == 2u)
      {
        if (boot_flash_restore() == 0)
        {
          cmd_state.flash_status = CV_FLASH_ST_OK;
        }
        else
        {
          cmd_state.flash_status = CV_FLASH_ST_ERR_IO;
        }
      }
      else
      {
        if (boot_flash_erase() == 0)
        {
          cmd_state.flash_status = CV_FLASH_ST_OK;
        }
        else
        {
          cmd_state.flash_status = CV_FLASH_ST_ERR_IO;
        }
      }
#else
      cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
#endif
    }
    else if (target == CV_TARGET_EXT_SOUND ||
             target == CV_TARGET_EXT_FW ||
             target == CV_TARGET_LOGS)
    {
#if defined(CV_HAVE_EXTSTORE)
      uint32_t a;
      uint32_t end = cmd_state.flash_addr + cmd_state.flash_len;

      if (cmd_state.flash_len == 0u)
      {
        cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
      }
      else
      {
        for (a = cmd_state.flash_addr; a < end; a += CV_EXT_SECTOR_SIZE)
        {
          if (ext_erase_sector(a) != 0)
          {
            cmd_state.flash_status = CV_FLASH_ST_ERR_IO;
            break;
          }

          cmd_state.flash_status = CV_FLASH_ST_OK;
        }
      }
#else
      cmd_state.flash_status = CV_FLASH_ST_ERR_IO;
#endif
    }
    else
    {
      cmd_state.flash_status = CV_FLASH_ST_ERR_ADDR;
    }

    cv_block_dirty = 1u;
    return;
  }

  if (cmd_state.fw_apply_req != 0u)
  {
    cmd_state.fw_apply_req = 0u;
    cmd_state.flash_status = CV_FLASH_ST_BUSY;

#if defined(CV_HAVE_FW_UPDATE)
    fw_apply_request();
    cmd_state.flash_status = CV_FLASH_ST_OK;
#else
    Command_vector_HookSoftReset();
    cmd_state.flash_status = CV_FLASH_ST_OK;
#endif

    cv_block_dirty = 1u;
    return;
  }

  if (cmd_state.int_write_pending != 0u)
  {
    cmd_state.int_write_pending = 0u;
    cmd_state.flash_status = CV_FLASH_ST_BUSY;

#if defined(CV_BOOT_READY)
    if (boot_flash_allowed() == 0)
    {
      cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
    }
    else
    {
      int rc = boot_flash_write(cmd_state.write_addr,
                                cmd_state.write_buf,
                                cmd_state.write_len);

      if (rc == 0)
      {
        cmd_state.flash_status = CV_FLASH_ST_OK;
      }
      else if (rc == -2)
      {
        cmd_state.flash_status = CV_FLASH_ST_ERR_VERIFY;
      }
      else
      {
        cmd_state.flash_status = CV_FLASH_ST_ERR_IO;
      }
    }
#else
    cmd_state.flash_status = CV_FLASH_ST_PROTECTED;
#endif

    cv_block_dirty = 1u;
    return;
  }

  if (cmd_state.ext_write_pending != 0u)
  {
    uint8_t verify[CV_WRITE_BUF_SIZE];
    uint16_t i;

    cmd_state.ext_write_pending = 0u;
    cmd_state.flash_status = CV_FLASH_ST_BUSY;

#if defined(CV_HAVE_EXTSTORE)
    if (ext_write(cmd_state.write_addr, cmd_state.write_buf, (uint32_t)cmd_state.write_len) != 0)
    {
      cmd_state.flash_status = CV_FLASH_ST_ERR_IO;
      cv_block_dirty = 1u;
      return;
    }

    if (ext_read(cmd_state.write_addr, verify, (uint32_t)cmd_state.write_len) != 0)
    {
      cmd_state.flash_status = CV_FLASH_ST_ERR_IO;
      cv_block_dirty = 1u;
      return;
    }

    for (i = 0u; i < cmd_state.write_len; i++)
    {
      if (verify[i] != cmd_state.write_buf[i])
      {
        cmd_state.flash_status = CV_FLASH_ST_ERR_VERIFY;
        cv_block_dirty = 1u;
        return;
      }
    }

    cmd_state.flash_status = CV_FLASH_ST_OK;
#else
    cmd_state.flash_status = CV_FLASH_ST_ERR_IO;
#endif

    cv_block_dirty = 1u;
    return;
  }
}
