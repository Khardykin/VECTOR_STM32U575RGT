/**
  ******************************************************************************
  * @file    Command_vector.c
  * @brief   Обмен по COM (USART3) в стиле Avis Command.c.
  *
  *          parsing_command() - диспетчер, добавляет CRC. CmdFuncN - обработчики,
  *          где N = десятичный код команды (как в Avis).
  *
  *          Протокол:
  *            0x03 CmdFunc3  - чтение holding-регистров  (Config_common)
  *            0x04 CmdFunc4  - чтение input-регистров    (sensors_status)
  *            0x10 CmdFunc16 - запись группы holding
  *            0x0F CmdFunc15 - блочная запись во внешнюю flash (адрес 32 бита)
  *
  *          Управляющие регистры 0x0F00+ (COMMIT, сведения об образе, стирание
  *          областей, окно данных) - карта ниже, работают через 0x03/0x10.
  *
  *          КОНТЕКСТ: receiver_task (command_message), не ISR.
  ******************************************************************************
  */
#include <string.h>
#include <stddef.h>
#include "Command_vector.h"
#include "spiflash.h"
#include "audio_image.h"
#include "shared_types.h"
#include "main_vector.h"
#include "Config_save_read.h"
#include "sfmap.h"
#include "vector_config.h"
#include "vector_log.h"
#include "fw_update.h"

#define CMD_SOUNDS_LIMIT    0x00400000u   /* sfmap.h: выше - CONFIG/LOG        */
#define CMD_ERASE_KEY       0xA5u

#define MB_WIN_REGS         125u
#define MB_WIN_BYTES        (MB_WIN_REGS * 2u)

/* ==========================================================================
 * Хуки: ставятся ОДИН раз при первом проходе command_message().
 *   hooks.pause  - стоп/пуск плеера на время записи/стирания;
 *   hooks.commit - вызывается по регистру 0x0F00 (COMMIT);
 *   hooks.tx     - отправка ответа в COM.
 * ========================================================================== */
typedef struct
{
  cmd_flash_pause_t pause;
  cmd_commit_t      commit;
  cmd_tx_t          tx;
  uint8_t           ready;        /* хуки привязаны (одноразово) */
} cmd_hooks_t;

/* Накопитель кадра + состояние сессии. */
typedef struct
{
  uint8_t  buf[CMD_MAX_FRAME];
  uint16_t len;                   /* принято байт                    */
  uint16_t expect;                /* ожидаемая длина кадра           */
  uint8_t  ready;                 /* кадр собран, ждёт разбора       */
  uint32_t ms;                    /* время последнего пополнения     */
  uint8_t  session;               /* 1 = идёт обмен (эхо/лог молчат) */
  uint32_t sess_ms;               /* время последнего кадра          */
} cmd_rx_t;

/* Фоновое стирание областей. */
typedef struct
{
  uint8_t  req;                   /* маска запрошенных областей      */
  uint8_t  busy;                  /* маска стирающейся области       */
  uint8_t  err;                   /* маска областей с ошибкой        */
  uint8_t  bit;                   /* бит текущей области             */
  uint8_t  pct;                   /* прогресс текущей области, %     */
  uint32_t cur;
  uint32_t end;
  uint32_t from;
} cmd_erase_t;

/* Окно данных во внешнюю flash (адрес + буфер 250 Б). */
typedef struct
{
  uint32_t addr;
  uint16_t len;                   /* чётная, <= 250                  */
  uint16_t status;                /* код последней операции          */
  uint8_t  autoinc;               /* адрес += len после записи       */
  uint8_t  dirty;                 /* буфер не соответствует flash    */
  uint8_t  buf[MB_WIN_BYTES];
} cmd_win_t;

/* Кэш заголовка образа звуков. */
typedef struct
{
  audio_img_header_t hdr;
  uint16_t rate;
  uint8_t  ok;
  uint8_t  valid;
} cmd_img_t;

static cmd_hooks_t hooks = { 0, 0, 0, 0u };
static cmd_rx_t    rx;
static cmd_erase_t era;
static cmd_win_t   win = { 0u, 0u, 0u, 0u, 1u, { 0 } };
static cmd_img_t   img;

void Command_vector_SetPauseHook(cmd_flash_pause_t hook)
{
  hooks.pause = hook;
}

void Command_vector_SetCommitHook(cmd_commit_t hook)
{
  hooks.commit = hook;
}

void Command_vector_SetTx(cmd_tx_t hook)
{
  hooks.tx = hook;
}

/* ==========================================================================
 * CRC и мелкие помощники (имена как в Avis)
 * ========================================================================== */
uint16_t CreateCRC_out(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFu;
  uint16_t i;
  uint8_t  bit;

  for (i = 0u; i < len; i++)
  {
    crc ^= (uint16_t)data[i];
    for (bit = 0u; bit < 8u; bit++)
    {
      if ((crc & 1u) != 0u)
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

uint16_t CreateCRC(uint8_t *data, uint16_t len)
{
  uint16_t crc = CreateCRC_out(data, len);

  data[len]      = (uint8_t)(crc & 0xFFu);
  data[len + 1u] = (uint8_t)(crc >> 8);
  return crc;
}

static uint32_t rd32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}
static void wr32le(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}


static void wr16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static uint16_t CmdError(uint8_t *out, const uint8_t *mas, uint8_t code)
{
  out[0] = mas[0];
  out[1] = (uint8_t)(mas[1] | 0x80u);
  out[2] = code;
  return 3u;
}

/* Программируем постранично: sf_program может не делить буфер сам. */
static uint8_t program_pages(uint32_t addr, const uint8_t *src, uint32_t len)
{
  while (len != 0u)
  {
    uint32_t room = SF_PAGE_SIZE - (addr % SF_PAGE_SIZE);
    uint32_t n    = (len < room) ? len : room;

    if (sf_program(addr, src, n) != HAL_OK)
    {
      return CMD_ERR_IO;
    }
    addr += n;
    src  += n;
    len  -= n;
  }
  return 0u;
}

/* ==========================================================================
 * Карта регистров (как sn_map/in_map в Avis): адрес задан ЯВНО, между
 * группами зазоры - чтение сквозь дыру даёт ошибку 02.
 * ========================================================================== */
#define MB_MAX_REGS   125u
#define MB_DIV        1000.0f

enum
{
  T_U16 = 0,
  T_U32,
  T_U8,
  T_FLT,
  T_STR
};

typedef struct
{
  uint16_t reg;
  uint16_t off;
  uint8_t  type;
  uint8_t  len;
} reg_map_t;

#define CM(r, f)        { (r), (uint16_t)offsetof(SNS_CFG_Type_common, f), T_U16, 2u }
#define CM32(r, f)      { (r), (uint16_t)offsetof(SNS_CFG_Type_common, f), T_U32, 4u }
#define CMSTR(r, f, n)  { (r), (uint16_t)offsetof(SNS_CFG_Type_common, f), T_STR, (n) }

static const reg_map_t com_map[] =
{
  CM   (0x0000, Build_type),
  CM32 (0x0001, State),
  CM32 (0x0003, StateErr),
  CM   (0x0010, TimeLedStatePulse),
  CM   (0x0011, TimeLedStatePeriod),
  CM   (0x0020, battery_charge_volt_max),
  CM   (0x0021, battery_charge_volt_min),
  CM   (0x0022, battery_charge_volt_lim_1),
  CM   (0x0023, battery_charge_volt_lim_2),
  CM   (0x0030, SerialLo),
  CM   (0x0031, SerialHi),
  CM   (0x0032, HardwareVersion),
  CM32 (0x0033, ProductionDate),
  CM32 (0x0040, Gps_type_nav),
  CM   (0x0042, PeriodTimeLora),
  CM   (0x0043, Lora_Config_Flags),
  CM32 (0x0044, Lora_freq_rx2),
  CM   (0x0046, Lora_dr_rx2),
  CM   (0x0047, current_build_type),
  CM   (0x0050, Immobility_Enable),
  CM   (0x0051, Immobility_Window_s),
  CM   (0x0052, Immobility_PreAlarm_s),
  CM   (0x0053, Immobility_Ths_mg),
  CM   (0x0054, Devices_Expected),
  CM   (0x0055, Alert_Flags),
  CMSTR(0x0060, Reserve, 8u),
};

#define IM(r, f)     { (r), (uint16_t)offsetof(sensors_status_t, f), T_U16, 2u }
#define IM32(r, f)   { (r), (uint16_t)offsetof(sensors_status_t, f), T_U32, 4u }
#define IM8(r, f)    { (r), (uint16_t)offsetof(sensors_status_t, f), T_U8,  1u }
#define IMF(r, f)    { (r), (uint16_t)offsetof(sensors_status_t, f), T_FLT, 4u }

static const reg_map_t in_map[] =
{
  IM32 (0x0000, cnt_init_ok),
  IM32 (0x0002, cnt_bme_reads),
  IM32 (0x0004, cnt_accel_reads),
  IM32 (0x0006, cnt_battery_reads),
  IM32 (0x0008, cnt_rotation_events),
  IM32 (0x000A, cnt_read_ms),
  IM32 (0x000C, last_read_ms),
  IMF  (0x0010, Accel_x),
  IMF  (0x0011, Accel_y),
  IMF  (0x0012, Accel_z),
  IM8  (0x0013, Orientation),
  IM8  (0x0014, Screen_rotation),
  IM8  (0x0015, fall_detected),
  IM32 (0x0016, cnt_fall_events),
  IM   (0x0020, battery_charge_percent),
  IM   (0x0021, battery_charge_volt),
  IM8  (0x0022, Sensors_ok),
  IM8  (0x0023, Sensors_reserve),
  IMF  (0x0030, Temperature_calib_offset),
  IMF  (0x0031, Temperature),
  IMF  (0x0032, Humidity),
  IMF  (0x0033, Pressure),
};

static uint16_t get_reg_span(const reg_map_t *m)
{
  switch (m->type)
  {
    case T_U32:
      return 2u;
    case T_STR:
      return (uint16_t)((m->len + 1u) / 2u);
    default:
      return 1u;
  }
}

static int16_t reg_find(const reg_map_t *map, uint16_t n, uint16_t addr, uint16_t *roff)
{
  uint16_t i;

  for (i = 0u; i < n; i++)
  {
    uint16_t span = get_reg_span(&map[i]);

    if ((addr >= map[i].reg) && (addr < (uint16_t)(map[i].reg + span)))
    {
      *roff = (uint16_t)(addr - map[i].reg);
      return (int16_t)i;
    }
  }
  return -1;
}

static uint16_t reg_get(const void *base, const reg_map_t *m, uint16_t roff, uint8_t input)
{
  const uint8_t *p = (const uint8_t *)base + m->off;

  if (input != 0u)
  {
    switch (m->type)
    {
      case T_U32:
      {
        uint32_t v = *(const volatile uint32_t *)p;
        return (roff == 0u) ? (uint16_t)(v & 0xFFFFu) : (uint16_t)(v >> 16);
      }
      case T_U8:
      {
        return (uint16_t)(*(const volatile uint8_t *)p);
      }
      case T_FLT:
      {
        float v = *(const volatile float *)p;

        if (v >  32000.0f)
        {
          v =  32000.0f;
        }
        if (v < -32000.0f)
        {
          v = -32000.0f;
        }
        return (uint16_t)(int16_t)((v >= 0.0f) ? (v * MB_DIV + 0.5f) : (v * MB_DIV - 0.5f));
      }
      default:
      {
        return *(const volatile uint16_t *)(p + (roff * 2u));
      }
    }
  }

  switch (m->type)
  {
    case T_U32:
    {
      uint32_t v = *(const uint32_t *)p;
      return (roff == 0u) ? (uint16_t)(v & 0xFFFFu) : (uint16_t)(v >> 16);
    }
    case T_U8:
    {
      return (uint16_t)(*(const uint8_t *)p);
    }
    default:
    {
      return *(const uint16_t *)(p + (roff * 2u));
    }
  }
}

static void reg_set(void *base, const reg_map_t *m, uint16_t roff, uint16_t val)
{
  uint8_t *p = (uint8_t *)base + m->off;

  if (m->type == T_U32)
  {
    uint32_t v = *(uint32_t *)p;

    if (roff == 0u)
    {
      v = (v & 0xFFFF0000u) | (uint32_t)val;
    }
    else
    {
      v = (v & 0x0000FFFFu) | ((uint32_t)val << 16);
    }
    *(uint32_t *)p = v;
  }
  else
  {
    *(uint16_t *)(p + (roff * 2u)) = val;
  }
}

/* Курсор ответа, как в Avis. Пишет только receiver_task. */
static uint16_t  Count;
static uint8_t  *Value;
static uint8_t  *Countuint8_t;
static uint16_t  AnswerLen;

static inline uint8_t CMD_PUT(uint16_t data)
{
  Value[0] = (uint8_t)(data >> 8);
  Value[1] = (uint8_t)data;
  (*Countuint8_t) += 2u;
  Value     += 2;
  AnswerLen += 2u;
  Count--;
  return (uint8_t)((Count == 0u) ? 1u : 0u);
}

static inline uint8_t CMD_SAVE(uint16_t *data)
{
  *data = (uint16_t)(((uint16_t)Value[0] << 8) | Value[1]);
  Value += 2;
  Count--;
  return (uint8_t)((Count == 0u) ? 1u : 0u);
}

/* ==========================================================================
 * Управляющие регистры 0x0F00+
 * ========================================================================== */
#define MB_REG_COMMIT         0x0F00u
#define MB_REG_SAVE_CFG       0x0F01u
#define MB_REG_SAVE_CFGDEF    0x0F02u
#define MB_REG_LOAD_CFGDEF    0x0F03u

#define MB_REG_FLASH_LO       0x0F10u
#define MB_REG_FLASH_HI       0x0F11u
#define MB_REG_SECTOR         0x0F12u
#define MB_REG_PAGE           0x0F13u
#define MB_REG_MAGIC_LO       0x0F14u
#define MB_REG_MAGIC_HI       0x0F15u
#define MB_REG_IMG_VER        0x0F16u
#define MB_REG_IMG_CNT        0x0F17u
#define MB_REG_IMG_SIZE_LO    0x0F18u
#define MB_REG_IMG_SIZE_HI    0x0F19u
#define MB_REG_IMG_RATE       0x0F1Au
#define MB_REG_IMG_OK         0x0F1Bu
#define MB_REG_IMG_REFRESH    0x0F1Cu

#define MB_REG_ERASE_SOUNDS   0x0F20u
#define MB_REG_ERASE_CONFIG   0x0F21u
#define MB_REG_ERASE_CFGDEF   0x0F22u
#define MB_REG_ERASE_LOG      0x0F23u
#define MB_REG_ERASE_LOGPTR   0x0F24u
#define MB_REG_ERASE_FW       0x0F25u
#define MB_REG_ERASE_BUSY     0x0F26u
#define MB_REG_ERASE_ERR      0x0F27u
#define MB_REG_ERASE_PROGRESS 0x0F28u

#define MB_REG_WIN_ADDR_LO    0x0F30u
#define MB_REG_WIN_ADDR_HI    0x0F31u
#define MB_REG_WIN_LEN        0x0F32u
#define MB_REG_WIN_CMD        0x0F33u
#define MB_REG_WIN_STATUS     0x0F34u
#define MB_REG_WIN_AUTOINC    0x0F35u
#define MB_REG_WIN_DATA       0x0F40u

#define ERASE_BIT_SOUNDS 0x01u
#define ERASE_BIT_CONFIG 0x02u
#define ERASE_BIT_CFGDEF 0x04u
#define ERASE_BIT_LOG    0x08u
#define ERASE_BIT_LOGPTR 0x10u
#define ERASE_BIT_FW     0x20u

static const struct
{
  uint8_t  bit;
  uint32_t base;
  uint32_t size;
} s_areas[] =
{
  { ERASE_BIT_SOUNDS, SF_SOUNDS_BASE,    SF_SOUNDS_SIZE         },
  { ERASE_BIT_CONFIG, SF_CONFIG_BASE,    SF_CONFIG_SIZE         },
  { ERASE_BIT_CFGDEF, SF_CONFIG_DEFAULT, SF_CONFIG_DEFAULT_SIZE },
  { ERASE_BIT_LOG,    SF_LOG_BASE,       SF_LOG_SIZE            },
  { ERASE_BIT_LOGPTR, SF_LOG_PTR_BASE,   SF_LOG_PTR_SIZE        },
  { ERASE_BIT_FW,     CMD_FW_BASE,       CMD_FW_SIZE            },
};

static void erase_begin(uint8_t bit, uint32_t base, uint32_t size)
{
  era.bit  = bit;
  era.from = base;
  era.cur  = base;
  era.end  = base + size;
  era.busy = bit;
  era.pct  = 0u;
  era.err &= (uint8_t)~bit;
  if (hooks.pause != 0)
  {
    hooks.pause(1u);
  }
}

static void erase_start(uint8_t bit)
{
  uint8_t i;

  if ((era.busy != 0u) || (era.req != 0u))
  {
    return;
  }
  for (i = 0u; i < (uint8_t)(sizeof(s_areas) / sizeof(s_areas[0])); i++)
  {
    if (s_areas[i].bit == bit)
    {
      erase_begin(bit, s_areas[i].base, s_areas[i].size);
      return;
    }
  }
}

void Command_vector_EraseService(void)
{
  if (era.busy == 0u)
  {
    if (era.req != 0u)
    {
      uint8_t bit = (uint8_t)(era.req & (uint8_t)(-(int8_t)era.req));

      era.req &= (uint8_t)~bit;
      erase_start(bit);
    }
    return;
  }

  if (sf_sector_erase(era.cur) != HAL_OK)
  {
    era.err |= era.bit;
  }
  era.cur += SF_SECTOR_SIZE;

  if (era.cur >= era.end)
  {
    era.busy = 0u;
    era.bit  = 0u;
    era.pct  = 100u;
    if (hooks.pause != 0)
    {
      hooks.pause(0u);
    }
    return;
  }
  era.pct = (uint8_t)(((era.cur - era.from) * 100u) / (era.end - era.from));
}

static void img_refresh(void)
{
  audio_img_entry_t e;

  memset(&img.hdr, 0, sizeof(img.hdr));
  memset(&e, 0, sizeof(e));
  img.rate  = 0u;
  img.ok    = 0u;
  img.valid = 1u;

  if (sf_read(AUDIO_IMG_BASE_ADDR, (uint8_t *)&img.hdr, sizeof(img.hdr)) == HAL_OK)
  {
    if (img.hdr.magic == AUDIO_IMG_MAGIC)
    {
      img.ok = 1u;
      if (img.hdr.count != 0u)
      {
        (void)sf_read(AUDIO_IMG_BASE_ADDR + img.hdr.table_off, (uint8_t *)&e, sizeof(e));
        img.rate = e.rate;
      }
    }
  }
}

static void win_load(void)
{
  if (win.dirty == 0u)
  {
    return;
  }
  if ((win.len == 0u) || (win.len > MB_WIN_BYTES) ||
      ((win.addr + win.len) > (uint32_t)SF_TOTAL_SIZE))
  {
    win.status = CMD_ERR_RANGE;
    return;
  }
  if (sf_read(win.addr, win.buf, win.len) != HAL_OK)
  {
    win.status = CMD_ERR_IO;
    return;
  }
  win.status = 0u;
  win.dirty  = 0u;
}

static void win_store(void)
{
  uint8_t rc;

  if ((win.len == 0u) || (win.len > MB_WIN_BYTES) ||
      ((win.addr + win.len) > (uint32_t)SF_TOTAL_SIZE))
  {
    win.status = CMD_ERR_RANGE;
    return;
  }
  if (hooks.pause != 0)
  {
    hooks.pause(1u);
  }
  rc = program_pages(win.addr, win.buf, win.len);
  if (rc == 0u)
  {
    if (sf_verify(win.addr, win.buf, win.len) != HAL_OK)
    {
      rc = CMD_ERR_VERIFY;
    }
  }
  if (hooks.pause != 0)
  {
    hooks.pause(0u);
  }
  win.status = rc;
  if ((rc == 0u) && (win.autoinc != 0u))
  {
    win.addr += win.len;
    win.dirty = 1u;
  }
}

static uint8_t ctrl_reg_read(uint16_t addr, uint16_t *val)
{
  if ((addr >= MB_REG_WIN_DATA) && (addr < (uint16_t)(MB_REG_WIN_DATA + MB_WIN_REGS)))
  {
    uint16_t off = (uint16_t)((addr - MB_REG_WIN_DATA) * 2u);

    win_load();
    *val = (uint16_t)(((uint16_t)win.buf[off] << 8) | win.buf[off + 1u]);
    return 1u;
  }

  switch (addr)
  {
    case MB_REG_FW_VERSION:
      *val = (uint16_t)(FIRMWARE_VERSION & 0xFFFFu);
      return 1u;
    case MB_REG_BOOT_STATUS:
      *val = (uint16_t)boot_flash_status;
      return 1u;
    case MB_REG_BOOT_ALLOWED:
      *val = (uint16_t)boot_flash_allowed();
      return 1u;
    case MB_REG_BOOT_ERASE:
      *val = 0u;
      return 1u;
    case MB_REG_FW_STATUS:
      *val = (uint16_t)sys_state.process;
      return 1u;

    case MB_REG_COMMIT:
    case MB_REG_SAVE_CFG:
    case MB_REG_SAVE_CFGDEF:
    case MB_REG_LOAD_CFGDEF:
    case MB_REG_IMG_REFRESH:
    case MB_REG_ERASE_SOUNDS:
    case MB_REG_ERASE_CONFIG:
    case MB_REG_ERASE_CFGDEF:
    case MB_REG_ERASE_LOG:
    case MB_REG_ERASE_LOGPTR:
    case MB_REG_ERASE_FW:
    case MB_REG_WIN_CMD:
      *val = 0u;
      return 1u;

    case MB_REG_FLASH_LO:
      *val = (uint16_t)((uint32_t)SF_TOTAL_SIZE & 0xFFFFu);
      return 1u;
    case MB_REG_FLASH_HI:
      *val = (uint16_t)((uint32_t)SF_TOTAL_SIZE >> 16);
      return 1u;
    case MB_REG_SECTOR:
      *val = (uint16_t)SF_SECTOR_SIZE;
      return 1u;
    case MB_REG_PAGE:
      *val = (uint16_t)SF_PAGE_SIZE;
      return 1u;

    case MB_REG_MAGIC_LO:
    case MB_REG_MAGIC_HI:
    case MB_REG_IMG_VER:
    case MB_REG_IMG_CNT:
    case MB_REG_IMG_SIZE_LO:
    case MB_REG_IMG_SIZE_HI:
    case MB_REG_IMG_RATE:
    case MB_REG_IMG_OK:
      if (img.valid == 0u)
      {
        img_refresh();
      }
      switch (addr)
      {
        case MB_REG_MAGIC_LO:    *val = (uint16_t)(img.hdr.magic & 0xFFFFu);      break;
        case MB_REG_MAGIC_HI:    *val = (uint16_t)(img.hdr.magic >> 16);          break;
        case MB_REG_IMG_VER:     *val = img.hdr.version;                          break;
        case MB_REG_IMG_CNT:     *val = img.hdr.count;                            break;
        case MB_REG_IMG_SIZE_LO: *val = (uint16_t)(img.hdr.total_size & 0xFFFFu); break;
        case MB_REG_IMG_SIZE_HI: *val = (uint16_t)(img.hdr.total_size >> 16);     break;
        case MB_REG_IMG_RATE:    *val = img.rate;                                 break;
        default:                 *val = (uint16_t)img.ok;                         break;
      }
      return 1u;

    case MB_REG_ERASE_BUSY:
      *val = (uint16_t)(era.busy | era.req);
      return 1u;
    case MB_REG_ERASE_ERR:
      *val = (uint16_t)era.err;
      return 1u;
    case MB_REG_ERASE_PROGRESS:
      *val = (uint16_t)era.pct;
      return 1u;

    case MB_REG_WIN_ADDR_LO:
      *val = (uint16_t)(win.addr & 0xFFFFu);
      return 1u;
    case MB_REG_WIN_ADDR_HI:
      *val = (uint16_t)(win.addr >> 16);
      return 1u;
    case MB_REG_WIN_LEN:
      *val = win.len;
      return 1u;
    case MB_REG_WIN_STATUS:
      *val = win.status;
      return 1u;
    case MB_REG_WIN_AUTOINC:
      *val = (uint16_t)win.autoinc;
      return 1u;

    default:
      return 0u;
  }
}

static uint8_t ctrl_reg_write(uint16_t addr, uint16_t val)
{
  if ((addr >= MB_REG_WIN_DATA) && (addr < (uint16_t)(MB_REG_WIN_DATA + MB_WIN_REGS)))
  {
    uint16_t off = (uint16_t)((addr - MB_REG_WIN_DATA) * 2u);

    win.buf[off]      = (uint8_t)(val >> 8);
    win.buf[off + 1u] = (uint8_t)val;
    return 1u;
  }

  switch (addr)
  {
    case MB_REG_BOOT_ERASE:
      if ((val == 1u) || (val == 2u))
      {
        if (boot_flash_allowed() == 0)
        {
          boot_flash_status = BOOT_ST_PROTECTED;
        }
        else
        {
          FlashErase = val;
        }
      }
      return 1u;
    case MB_REG_FW_APPLY:
      if (val != 0u)
      {
        sys_state.process = SYS_PROC_FW;
        fw_apply_request();
      }
      return 1u;

    case MB_REG_COMMIT:
      if (val != 0u)
      {
        if (hooks.commit != 0)
        {
          hooks.commit();
        }
        /* COMMIT закрывает сессию обмена: снимаем глушение лога UART. */
        rx.session = 0u;
#if VECTOR_LOG_ENABLE
        Command_vector_LogMute(0u);
#endif
      }
      return 1u;
    case MB_REG_SAVE_CFG:
      if (val != 0u)
      {
        SaveConfig = 1u;
      }
      return 1u;
    case MB_REG_SAVE_CFGDEF:
      if (val != 0u)
      {
        SaveConfigDefault = 1u;
      }
      return 1u;
    case MB_REG_LOAD_CFGDEF:
      if (val != 0u)
      {
        ReadConfigDefault = 1u;
      }
      return 1u;
    case MB_REG_IMG_REFRESH:
      if (val != 0u)
      {
        img_refresh();
      }
      return 1u;

    case MB_REG_ERASE_SOUNDS:
    case MB_REG_ERASE_CONFIG:
    case MB_REG_ERASE_CFGDEF:
    case MB_REG_ERASE_LOG:
    case MB_REG_ERASE_LOGPTR:
    case MB_REG_ERASE_FW:
      if (val != 0u)
      {
        /* FW - прошивка, остальное - архив (логи/конфиг/звуки). */
        sys_state.process = (addr == MB_REG_ERASE_FW) ? SYS_PROC_FW : SYS_PROC_ARCHIVE;
        era.req |= (uint8_t)(1u << (addr - MB_REG_ERASE_SOUNDS));
      }
      return 1u;

    case MB_REG_WIN_ADDR_LO:
      win.addr = (win.addr & 0xFFFF0000u) | (uint32_t)val;
      win.dirty = 1u;
      return 1u;
    case MB_REG_WIN_ADDR_HI:
      win.addr = (win.addr & 0x0000FFFFu) | ((uint32_t)val << 16);
      win.dirty = 1u;
      return 1u;
    case MB_REG_WIN_LEN:
      if ((val <= MB_WIN_BYTES) && ((val & 1u) == 0u))
      {
        win.len = val;
        win.dirty = 1u;
      }
      else
      {
        win.status = CMD_ERR_VALUE;
      }
      return 1u;
    case MB_REG_WIN_AUTOINC:
      win.autoinc = (uint8_t)((val != 0u) ? 1u : 0u);
      return 1u;
    case MB_REG_WIN_CMD:
      if (val == 1u)
      {
        win.dirty = 1u;
        win_load();
      }
      else if (val == 2u)
      {
        win_store();
      }
      return 1u;

    default:
      return 0u;
  }
}

/* ==========================================================================
 * CmdFunc3 / CmdFunc4 - чтение группы регистров
 * ========================================================================== */
static uint16_t read_regs(const uint8_t *mas, uint8_t *out, uint16_t cap,
                          const reg_map_t *map, uint16_t n,
                          const void *base, uint8_t input)
{
  uint16_t addr = (uint16_t)(((uint16_t)mas[2] << 8) | mas[3]);
  uint16_t cnt  = (uint16_t)(((uint16_t)mas[4] << 8) | mas[5]);
  uint16_t i;

  if ((cnt == 0u) || (cnt > MB_MAX_REGS) || (cap < (uint16_t)(3u + (cnt * 2u) + 2u)))
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t roff;
    uint16_t v;

    if (reg_find(map, n, a, &roff) >= 0)
    {
      continue;
    }
    if ((input == 0u) && (ctrl_reg_read(a, &v) != 0u))
    {
      continue;
    }
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  Count         = cnt;
  out[0]        = mas[0];
  out[1]        = mas[1];
  Countuint8_t  = &out[2];
  Value         = &out[3];
  AnswerLen     = 3u;
  *Countuint8_t = 0u;

  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t roff;
    uint16_t v = 0u;
    int16_t  idx = reg_find(map, n, a, &roff);

    if (idx >= 0)
    {
      v = reg_get(base, &map[idx], roff, input);
    }
    else if (input == 0u)
    {
      (void)ctrl_reg_read(a, &v);
    }
    (void)CMD_PUT(v);
  }
  return AnswerLen;
}

uint16_t CmdFunc3(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  (void)len;
  return read_regs(mas, out, cap, com_map,
                   (uint16_t)(sizeof(com_map) / sizeof(com_map[0])),
                   (const void *)&Sns_Cfg_struct.Config_common, 0u);
}

uint16_t CmdFunc4(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  (void)len;
  return read_regs(mas, out, cap, in_map,
                   (uint16_t)(sizeof(in_map) / sizeof(in_map[0])),
                   (const void *)&sensors_status, 1u);
}

/* ==========================================================================
 * CmdFunc16 - запись группы holding-регистров
 * ========================================================================== */
uint16_t CmdFunc16(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint16_t addr = (uint16_t)(((uint16_t)mas[2] << 8) | mas[3]);
  uint16_t cnt  = (uint16_t)(((uint16_t)mas[4] << 8) | mas[5]);
  uint8_t  nb   = mas[6];
  uint16_t n    = (uint16_t)(sizeof(com_map) / sizeof(com_map[0]));
  uint16_t i;
  uint16_t roff;
  uint8_t  touched = 0u;

  if ((cnt == 0u) || (cnt > MB_MAX_REGS) || (nb != (uint8_t)(cnt * 2u)) ||
      ((uint32_t)len != (9u + (uint32_t)nb)))
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t v;

    if (reg_find(com_map, n, a, &roff) >= 0)
    {
      continue;
    }
    if (ctrl_reg_read(a, &v) != 0u)
    {
      continue;
    }
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  Count = cnt;
  Value = (uint8_t *)&mas[7];

  Sys_Cfg_Lock();
  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t v = 0u;
    int16_t  idx;

    (void)CMD_SAVE(&v);
    idx = reg_find(com_map, n, a, &roff);
    if (idx >= 0)
    {
      reg_set(&Sns_Cfg_struct.Config_common, &com_map[idx], roff, v);
      touched = 1u;
    }
    else
    {
      (void)ctrl_reg_write(a, v);
    }
  }
  if (touched != 0u)
  {
    SaveConfig = 1u;
  }
  Sys_Cfg_Unlock();

  memcpy(out, mas, 6u);
  return 6u;
}

/* ==========================================================================
 * CmdFunc15 - блочная запись во внешнюю flash (как в Avis)
 * Кадр: [sl][0F][addr32 BE][size16 BE][data][CRC]
 * ========================================================================== */
uint16_t CmdFunc15(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint32_t addr = rd32(&mas[2]);
  uint16_t size = (uint16_t)(((uint16_t)mas[6] << 8) | mas[7]);
  uint8_t  rc;

  (void)len;
  memcpy(out, mas, CMD_FRAME_HDR);

  if ((size == 0u) || (size > CMD_MAX_PAYLOAD))
  {
    return CmdError(out, mas, CMD_ERR_LENGTH);
  }
  if ((addr >= (uint32_t)SF_TOTAL_SIZE) ||
      ((uint32_t)size > ((uint32_t)SF_TOTAL_SIZE - addr)))
  {
    return CmdError(out, mas, CMD_ERR_RANGE);
  }

  if (hooks.pause != 0)
  {
    hooks.pause(1u);
  }
  rc = program_pages(addr, &mas[8], size);
  if (rc == 0u)
  {
    if (sf_verify(addr, &mas[8], size) != HAL_OK)
    {
      rc = CMD_ERR_VERIFY;
    }
  }
  if (hooks.pause != 0)
  {
    hooks.pause(0u);
  }
  if (rc != 0u)
  {
    return CmdError(out, mas, rc);
  }

  wr16(&out[6], 0u);
  return CMD_FRAME_HDR;
}


/* ==========================================================================
 * CmdFunc12 (0x0C) - чтение журнала по номеру записи и количеству.
 * ========================================================================== */
uint16_t CmdFunc12(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint32_t ptr;
  uint16_t cnt;
  uint16_t i;
  uint16_t maxrec;
  uint32_t cur = Sns_Cfg_struct.Config_archive.CurrentAddrFile;
  uint32_t beg = Sns_Cfg_struct.Config_archive.BegginAddrFile;
  EVENT_LOG rec;

  (void)len;
  if (cap < (uint16_t)(3u + LOG_SLOT_SIZE + 2u))
  {
    return CmdError(out, mas, CMD_ERR_LENGTH);
  }
  ptr = rd32(&mas[2]);
  cnt = (uint16_t)(((uint16_t)mas[6] << 8) | mas[7]);
  maxrec = (uint16_t)((cap - 5u) / LOG_SLOT_SIZE);

  if ((cnt == 0u) || (cnt > maxrec))
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }
  if ((cur == beg) || ((uint32_t)(ptr - beg) >= (uint32_t)(cur - beg)) ||
      ((uint32_t)(cur - ptr) < (uint32_t)cnt))
  {
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  sys_state.process = SYS_PROC_ARCHIVE;

  out[0] = mas[0];
  out[1] = mas[1];
  out[2] = (uint8_t)cnt;

  for (i = 0u; i < cnt; i++)
  {
    int rc = log_read(&Sns_Cfg_struct, (uint32_t)(ptr + i), &rec);

    if (rc != 0)
    {
      return CmdError(out, mas,
                      (rc == 1) ? CMD_ERR_ADDR :
                      (rc == 2) ? CMD_ERR_VERIFY : CMD_ERR_IO);
    }
    memcpy(&out[3u + (i * LOG_SLOT_SIZE)], &rec, LOG_SLOT_SIZE);
  }
  return (uint16_t)(3u + (cnt * LOG_SLOT_SIZE));
}

/* ==========================================================================
 * CmdFunc20 (0x14) - поиск границ журнала по интервалу времени.
 * ========================================================================== */
uint16_t CmdFunc20(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint32_t t_from;
  uint32_t t_to;
  uint32_t pf = 0u;
  uint32_t pt = 0u;
  int rc;

  (void)len;
  if (cap < 12u)
  {
    return CmdError(out, mas, CMD_ERR_LENGTH);
  }
  t_from = rd32(&mas[2]);
  t_to   = rd32(&mas[6]);
  if (t_to < t_from)
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  sys_state.process = SYS_PROC_ARCHIVE;

  rc = log_find_by_time(&Sns_Cfg_struct, t_from, t_to, &pf, &pt);
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
  wr32le(&out[2], pf);
  wr32le(&out[6], pt);
  return 10u;
}

/* ==========================================================================
 * CmdFunc14 (0x0E) - запись во ВНУТРЕННЮЮ flash (область бутлоадера).
 * ========================================================================== */
uint16_t CmdFunc14(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint32_t addr = rd32(&mas[2]);
  uint8_t  size = mas[6];
  int rc;

  (void)len;
  memcpy(out, mas, 6u);

  if (boot_flash_allowed() == 0)
  {
    return CmdError(out, mas, CMD_ERR_PROTECTED);
  }
  if ((addr < FLASH_ADDRESS_START_BOOTLOADER) ||
      ((uint64_t)addr + size) > (uint64_t)FLASH_ADDRESS_STOP_BOOTLOADER)
  {
    return CmdError(out, mas, CMD_ERR_RANGE);
  }
  if ((size == 0u) || ((size & 0x0Fu) != 0u) || ((addr & 0x0Fu) != 0u))
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  sys_state.process = SYS_PROC_FW;

  rc = boot_flash_write(addr, &mas[7], size);
  if (rc != 0)
  {
    return CmdError(out, mas, (rc == -2) ? CMD_ERR_VERIFY : CMD_ERR_IO);
  }
  return 6u;
}


/* ==========================================================================
 * Диспетчер: проверяет CRC, зовёт CmdFuncN, дописывает CRC ответа.
 * ========================================================================== */
uint16_t parsing_command(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint16_t crc_calc;
  uint16_t crc_recv;
  uint16_t answer = 0u;
  uint8_t  cmd;

  if ((mas == 0) || (out == 0) || (len < 5u) || (cap < 5u))
  {
    return 0u;
  }
  crc_calc = CreateCRC_out(mas, (uint16_t)(len - 2u));
  crc_recv = (uint16_t)mas[len - 2u] | ((uint16_t)mas[len - 1u] << 8);
  if (crc_calc != crc_recv)
  {
    return 0u;
  }

  cmd = mas[1];
  switch (cmd)
  {
    case CMD_OP_READ_HOLD:
    case CMD_OP_READ_IN:
      if (len != 8u)
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }
      answer = (cmd == CMD_OP_READ_IN) ? CmdFunc4(mas, len, out, cap)
                                       : CmdFunc3(mas, len, out, cap);
      break;

    case CMD_OP_WRITE_MULTI:
      if (len < 9u)
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }
      answer = CmdFunc16(mas, len, out);
      break;

    case CMD_OP_WRITE:
      if (len < (CMD_FRAME_HDR + 2u))
      {
        return 0u;
      }
      if ((uint32_t)len != (CMD_FRAME_HDR +
          (uint32_t)(((uint16_t)mas[6] << 8) | mas[7]) + 2u))
      {
        return 0u;
      }
      answer = CmdFunc15(mas, len, out);
      break;

    case CMD_OP_READ_LOG:
      if (len != 10u)
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
      if ((len < 9u) || ((uint32_t)len != (7u + (uint32_t)mas[6] + 2u)))
      {
        answer = CmdError(out, mas, CMD_ERR_LENGTH);
        break;
      }
      answer = CmdFunc14(mas, len, out);
      break;

    default:
      answer = CmdError(out, mas, CMD_ERR_UNKNOWN);
      break;
  }

  if (answer != 0u)
  {
    (void)CreateCRC(out, answer);
    return (uint16_t)(answer + 2u);
  }
  return 0u;
}

/* ==========================================================================
 * Накопитель кадра. Конец кадра - по длине из шапки, не по паузе в линии.
 * ========================================================================== */
void Command_vector_RxFeed(const uint8_t *data, uint16_t len, uint32_t now_ms)
{
  uint16_t i;

  if ((data == 0) || (len == 0u))
  {
    return;
  }
  for (i = 0u; i < len; i++)
  {
    if (rx.ready != 0u)
    {
      break;
    }
    if (rx.len >= CMD_MAX_FRAME)
    {
      rx.len    = 0u;
      rx.expect = 0u;
    }
    rx.buf[rx.len] = data[i];
    rx.len++;

    if (rx.len == 2u)
    {
      uint8_t op = rx.buf[1];

      if ((op == CMD_OP_READ_HOLD) || (op == CMD_OP_READ_IN))
      {
        rx.expect = 8u;
      }
      else if (op == CMD_OP_READ_LOG)
      {
        rx.expect = 10u;
      }
      else if (op == CMD_OP_FIND_LOG)
      {
        rx.expect = 12u;
      }
    }
    if ((rx.len == 7u) && (rx.buf[1] == CMD_OP_WRITE_MULTI))
    {
      uint8_t nb = rx.buf[6];

      if (nb > 250u)
      {
        rx.len    = 0u;
        rx.expect = 0u;
      }
      else
      {
        rx.expect = (uint16_t)(9u + nb);
      }
    }
    if ((rx.len == 7u) && (rx.buf[1] == CMD_OP_WRITE_MCU))
    {
      rx.expect = (uint16_t)(7u + (uint16_t)rx.buf[6] + 2u);
    }
    if ((rx.len == CMD_FRAME_HDR) && (rx.expect == 0u))
    {
      uint16_t sz = (uint16_t)(((uint16_t)rx.buf[6] << 8) | rx.buf[7]);

      if (sz > CMD_MAX_PAYLOAD)
      {
        rx.len    = 0u;
        rx.expect = 0u;
      }
      else
      {
        rx.expect = (uint16_t)(CMD_FRAME_HDR + sz + 2u);
      }
    }
    if ((rx.expect != 0u) && (rx.len >= rx.expect))
    {
      rx.ready  = 1u;
      rx.expect = 0u;
    }
  }
  rx.ms = now_ms;
}

void Command_vector_Poll(uint32_t now_ms)
{
  uint8_t  rsp[CMD_MAX_FRAME];
  uint16_t n;

  if (rx.ready != 0u)
  {
    /* Сессию открываем ДО обработки: лог и эхо замолкают раньше первого ответа.
       Закрывает её запись в регистр 0x0F00 (COMMIT) или простой.            */
    if (rx.session == 0u)
    {
      rx.session = 1u;
#if VECTOR_LOG_ENABLE
      Command_vector_LogMute(1u);
#endif
    }
    rx.sess_ms = now_ms;

    n = parsing_command(rx.buf, rx.len, rsp, (uint16_t)sizeof(rsp));
    rx.len   = 0u;
    rx.expect = 0u;
    rx.ready = 0u;

    if ((n != 0u) && (hooks.tx != 0))
    {
      hooks.tx(rsp, n);
    }
    return;
  }

  if ((rx.len != 0u) && ((uint32_t)(now_ms - rx.ms) >= CMD_RESYNC_MS))
  {
    rx.len    = 0u;
    rx.expect = 0u;
  }
  if ((rx.session != 0u) && ((uint32_t)(now_ms - rx.sess_ms) >= CMD_SESSION_IDLE_MS))
  {
    rx.session = 0u;
#if VECTOR_LOG_ENABLE
    Command_vector_LogMute(0u);
#endif
  }
}

uint8_t Command_vector_SessionActive(void)
{
  return rx.session;
}

#if VECTOR_LOG_ENABLE
void Command_vector_LogMute(uint8_t mute)
{
  static uint8_t level_saved = 0xFFu;

  if (mute != 0u)
  {
    if (level_saved == 0xFFu)
    {
      level_saved = vlog_get_level();
    }
    vlog_set_level(0u);
  }
  else if (level_saved != 0xFFu)
  {
    vlog_set_level(level_saved);
    level_saved = 0xFFu;
  }
}
#else
void Command_vector_LogMute(uint8_t mute)
{
  (void)mute;
}
#endif

#if CMD_VECTOR_COM
#include "buffer.h"
#include "audio_player.h"
#include "vector_tick.h"
#include "boot_flash.h"

static void cmd_tx_com(const uint8_t *data, uint16_t len)
{
  transmit_buffer((uint8_t *)data, len, TYPE_USART);
}

static void cmd_pause_com(uint8_t pause)
{
  if (pause != 0u)
  {
    audio_stop();
  }
}

static void cmd_commit_com(void)
{
  audio_reload_image();
}

/* Сильное определение вместо weak-заглушки main_vector.c. Вызывается из
 * receiver_task каждые ~10 мс. Кольцо InputBuffer[TYPE_USART] наполняет
 * USART3_IRQHandler, здесь только читаем.                                  */
void command_message(void)
{
  uint8_t  batch[64];
  uint16_t n = 0u;
  uint8_t  b;
  uint32_t now = VTICK_MS();

  if (hooks.ready == 0u)
  {
    Command_vector_SetTx(cmd_tx_com);
    Command_vector_SetPauseHook(cmd_pause_com);
    Command_vector_SetCommitHook(cmd_commit_com);
    hooks.ready = 1u;
  }

  while (check_buffer(&InputBuffer[TYPE_USART]) != 0u)
  {
    if (receive_buffer(&InputBuffer[TYPE_USART], &b) == 0u)
    {
      break;
    }
    batch[n] = b;
    n++;
    if (n == (uint16_t)sizeof(batch))
    {
      Command_vector_RxFeed(batch, n, now);
      n = 0u;
    }
  }
  if (n != 0u)
  {
    Command_vector_RxFeed(batch, n, now);
  }

  Command_vector_Poll(now);
  Command_vector_EraseService();
}
#endif
