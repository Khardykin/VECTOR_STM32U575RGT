/**
  ******************************************************************************
  * @file    Command_vector.c
  * @brief   Обмен по COM (USART3) в стиле Avis Command.c.
  *
  *          parsing_command() - диспетчер, добавляет CRC. CmdFuncN - обработчики,
  *          где N = десятичный код команды (стандартный паттерн).
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

/* Экземпляр специальных регистров */


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
 * CRC и мелкие помощники (имена стандартный паттерн)
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

/* Курсор ответа, стандартный паттерн. Пишет только receiver_task. */
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
  /* --- Управляющие регистры (флаги, обрабатываются в фоновом цикле) --- */

  /* --- Специальные регистры (блочное чтение) --- */


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


/* ==========================================================================
 * MB_CTRL bank: простые управляющие регистры 0x0F00..0x0FFF.
 * ========================================================================== */

/**
  * @brief  Признак "простой управляющий регистр".
  *
  * Окно WIN_*, образ IMG_*, FLASH_*, SECTOR остаются в старой механике,
  * потому что они связаны с буфером/внешней flash/вычислениями.
  * Всё остальное в диапазоне 0x0F00..0x0FFF становится обычным регистром.
  */
static int mb_ctrl_is_simple(uint16_t a)
{
  if (a < MB_CTRL_BASE)
  {
    return 0;
  }
  if (a >= (MB_CTRL_BASE + MB_CTRL_N))
  {
    return 0;
  }

#if defined(MB_REG_WIN_ADDR_LO)
  if (a == MB_REG_WIN_ADDR_LO)
  {
    return 0;
  }
#endif
#if defined(MB_REG_WIN_ADDR_HI)
  if (a == MB_REG_WIN_ADDR_HI)
  {
    return 0;
  }
#endif
#if defined(MB_REG_WIN_LEN)
  if (a == MB_REG_WIN_LEN)
  {
    return 0;
  }
#endif
#if defined(MB_REG_WIN_AUTOINC)
  if (a == MB_REG_WIN_AUTOINC)
  {
    return 0;
  }
#endif
#if defined(MB_REG_WIN_CMD)
  if (a == MB_REG_WIN_CMD)
  {
    return 0;
  }
#endif

#if defined(MB_REG_WIN_DATA) && defined(MB_WIN_BYTES)
  if (a >= MB_REG_WIN_DATA && a < (uint16_t)(MB_REG_WIN_DATA + (MB_WIN_BYTES / 2u)))
  {
    return 0;
  }
#elif defined(MB_REG_WIN_DATA)
  if (a >= MB_REG_WIN_DATA)
  {
    return 0;
  }
#endif

#if defined(MB_REG_FW_IMG_MAGIC) && defined(MB_REG_FW_IMG_CALC_CRC)
  if (a >= MB_REG_FW_IMG_MAGIC && a <= MB_REG_FW_IMG_CALC_CRC)
  {
    return 0;
  }
#endif

#if defined(MB_REG_FLASH_LO)
  if (a == MB_REG_FLASH_LO || a == MB_REG_FLASH_HI)
  {
    return 0;
  }
#endif
#if defined(MB_REG_SECTOR)
  if (a == MB_REG_SECTOR)
  {
    return 0;
  }
#endif

  return 1;
}

/**
  * @brief  Обновить динамические статусные регистры перед чтением.
  */
static void mb_ctrl_sync(void)
{
#if defined(MB_REG_FW_VERSION) && defined(FIRMWARE_VERSION)
  mb_ctrl[MB_CTRL_IDX(MB_REG_FW_VERSION)] =
      (uint16_t)((uint32_t)FIRMWARE_VERSION & 0xFFFFu);
#endif

#if defined(MB_REG_FW_STATUS)
  mb_ctrl[MB_CTRL_IDX(MB_REG_FW_STATUS)] = (uint16_t)sys_state.process;
#endif

#if defined(MB_REG_BOOT_STATUS) && defined(BOOT_ST_IDLE)
  mb_ctrl[MB_CTRL_IDX(MB_REG_BOOT_STATUS)] = (uint16_t)boot_flash_status;
#endif

#if defined(MB_REG_BOOT_ALLOWED) && defined(BOOT_ST_IDLE)
  mb_ctrl[MB_CTRL_IDX(MB_REG_BOOT_ALLOWED)] = (uint16_t)boot_flash_allowed();
#endif
}

/**
  * @brief  Чтение управляющих регистров 0x0F00+ как обычного банка.
  *
  * Для диапазона MB_CTRL не используется switch и не плодятся return 1u
  * по каждому регистру. Один адрес банка читается по индексу.
  */

static uint8_t ctrl_reg_read(uint16_t addr, uint16_t *val)
{

  /* MB_CTRL read hook */
  if (mb_ctrl_is_simple(addr))
  {
    mb_ctrl_sync();
    *val = mb_ctrl[MB_CTRL_IDX(addr)];
    return 1u;
  }

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


/**
  * @brief  Запись управляющих регистров 0x0F00+ как обычного банка.
  *
  * Здесь только сохраняется значение. Действия выполняются позже
  * в Command_vector_ProcessControlRegisters().
  */

static uint8_t ctrl_reg_write(uint16_t addr, uint16_t val)
{

  /* MB_CTRL write hook */
  if (mb_ctrl_is_simple(addr))
  {
    mb_ctrl[MB_CTRL_IDX(addr)] = val;
    return 1u;
  }

  if ((addr >= MB_REG_WIN_DATA) && (addr < (uint16_t)(MB_REG_WIN_DATA + MB_WIN_REGS)))
  {
    uint16_t off = (uint16_t)((addr - MB_REG_WIN_DATA) * 2u);

    win.buf[off]      = (uint8_t)(val >> 8);
    win.buf[off + 1u] = (uint8_t)val;
    return 1u;
  }

  switch (addr)
  {
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
                case MB_REG_IMG_REFRESH:
      if (val != 0u)
      {
        img_refresh();
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


/* ==========================================================================
 * MB_CTRL_CLEAN_V4 macros fallback.
 * Если заголовок ещё не содержит эти макросы, вставляем безопасные значения.
 * ========================================================================== */
#endif

#endif

#endif

#endif


/* ==========================================================================
 * MB_CTRL_CLEAN_V4 read.
 *
 * Только банк mb_ctrl[].
 * Никаких switch/case по отдельным регистрам.
 * ========================================================================== */
static uint8_t ctrl_reg_read(uint16_t addr, uint16_t *val)
{
  if (!MB_CTRL_IN_RANGE(addr))
  {
    return 0u;
  }

  mb_ctrl_update();

  *val = mb_ctrl[MB_CTRL_IDX(addr)];
  return 1u;
}


/* ==========================================================================
 * MB_CTRL_CLEAN_V4 write.
 *
 * Запись только в банк mb_ctrl[].
 * Немедленные параметры окна применяются через mb_ctrl_apply_write().
 * Команды обрабатываются позже в Command_vector_ProcessControlRegisters().
 * ========================================================================== */
static uint8_t ctrl_reg_write(uint16_t addr, uint16_t val)
{
  if (!MB_CTRL_IN_RANGE(addr))
  {
    return 0u;
  }

  mb_ctrl[MB_CTRL_IDX(addr)] = val;
  mb_ctrl_apply_write(addr, val);
  return 1u;
}


/* ==========================================================================
 * MB_U: единый банк специальных/управляющих регистров 0x0F00..0x0FFF.
 *
 * Правила:
 * - любой адрес в диапазоне 0x0F00..0x0FFF читается/пишется как uint16_t;
 * - в ctrl_reg_read()/ctrl_reg_write() нет switch по отдельным регистрам;
 * - командные регистры только ставят действие в очередь;
 * - действия выполняются Command_vector_ExecutePendingActions();
 * - динамические статусы обновляются в mb_u_sync().
 * ========================================================================== */

#endif

#endif

#endif

#endif


static const mb_u_item_t mb_u_items[] =
{
#ifdef MB_REG_FW_VERSION
  { MB_REG_FW_VERSION,      MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_FW_STATUS
  { MB_REG_FW_STATUS,       MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_BOOT_STATUS
  { MB_REG_BOOT_STATUS,     MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_BOOT_ALLOWED
  { MB_REG_BOOT_ALLOWED,    MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_MAGIC_LO
  { MB_REG_MAGIC_LO,        MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_MAGIC_HI
  { MB_REG_MAGIC_HI,        MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_VER
  { MB_REG_IMG_VER,         MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_CNT
  { MB_REG_IMG_CNT,         MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_SIZE_LO
  { MB_REG_IMG_SIZE_LO,     MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_SIZE_HI
  { MB_REG_IMG_SIZE_HI,     MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_RATE
  { MB_REG_IMG_RATE,        MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_OK
  { MB_REG_IMG_OK,          MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_FLASH_LO
  { MB_REG_FLASH_LO,        MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_FLASH_HI
  { MB_REG_FLASH_HI,        MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_WIN_ADDR_LO
  { MB_REG_WIN_ADDR_LO,     MB_U_RW,  MB_U_ACT_WIN_ADDR,    0u, 0 },
#endif

#ifdef MB_REG_WIN_ADDR_HI
  { MB_REG_WIN_ADDR_HI,     MB_U_RW,  MB_U_ACT_WIN_ADDR,    1u, 0 },
#endif

#ifdef MB_REG_WIN_LEN
  { MB_REG_WIN_LEN,         MB_U_RW,  MB_U_ACT_WIN_LEN,     0u, 0 },
#endif

#ifdef MB_REG_WIN_AUTOINC
  { MB_REG_WIN_AUTOINC,     MB_U_RW,  MB_U_ACT_WIN_AUTOINC, 0u, 0 },
#endif

#ifdef MB_REG_WIN_CMD
  { MB_REG_WIN_CMD,         MB_U_CMD, MB_U_ACT_WIN_CMD,     0u, 0 },
#endif

#ifdef MB_REG_COMMIT
  { MB_REG_COMMIT,          MB_U_CMD, MB_U_ACT_COMMIT,      0u, 0 },
#endif

#ifdef MB_REG_SAVE_CFG
  { MB_REG_SAVE_CFG,        MB_U_CMD, MB_U_ACT_SET_FLAG,    0u, (void *)&SaveConfig },
#endif

#ifdef MB_REG_SAVE_CFGDEF
  { MB_REG_SAVE_CFGDEF,     MB_U_CMD, MB_U_ACT_SET_FLAG,    0u, (void *)&SaveConfigDefault },
#endif

#ifdef MB_REG_LOAD_CFGDEF
  { MB_REG_LOAD_CFGDEF,     MB_U_CMD, MB_U_ACT_SET_FLAG,    0u, (void *)&ReadConfigDefault },
#endif

#ifdef MB_REG_IMG_REFRESH
  { MB_REG_IMG_REFRESH,     MB_U_CMD, MB_U_ACT_IMG_REFRESH, 0u, 0 },
#endif

#ifdef MB_REG_ERASE_SOUNDS
  { MB_REG_ERASE_SOUNDS,    MB_U_CMD, MB_U_ACT_ERASE_BIT,   0u, 0 },
#endif

#ifdef MB_REG_ERASE_CONFIG
  { MB_REG_ERASE_CONFIG,    MB_U_CMD, MB_U_ACT_ERASE_BIT,   1u, 0 },
#endif

#ifdef MB_REG_ERASE_CFGDEF
  { MB_REG_ERASE_CFGDEF,    MB_U_CMD, MB_U_ACT_ERASE_BIT,   2u, 0 },
#endif

#ifdef MB_REG_ERASE_LOG
  { MB_REG_ERASE_LOG,       MB_U_CMD, MB_U_ACT_ERASE_BIT,   3u, 0 },
#endif

#ifdef MB_REG_ERASE_LOGPTR
  { MB_REG_ERASE_LOGPTR,    MB_U_CMD, MB_U_ACT_ERASE_BIT,   4u, 0 },
#endif

#ifdef MB_REG_ERASE_FW
  { MB_REG_ERASE_FW,        MB_U_CMD, MB_U_ACT_ERASE_BIT,   5u, 0 },
#endif

#ifdef MB_REG_FW_APPLY
  { MB_REG_FW_APPLY,        MB_U_CMD, MB_U_ACT_FW_APPLY,    0u, 0 },
#endif

#ifdef MB_REG_BOOT_ERASE
  { MB_REG_BOOT_ERASE,      MB_U_CMD, MB_U_ACT_BOOT_ERASE,  0u, 0 },
#endif

  { 0u, 0, 0, 0u, 0 }
};

static const mb_u_item_t *mb_u_find(uint16_t addr)
{
  uint16_t i;

  for (i = 0u; mb_u_items[i].addr != 0u; i++)
  {
    if (mb_u_items[i].addr == addr)
    {
      return &mb_u_items[i];
    }
  }

  return 0;
}

static int mb_u_push(uint8_t act, uint16_t arg, void *ptr, uint16_t value)
{
  if (mb_u_q_count >= MB_U_QUEUE_N)
  {
    return 0;
  }

  mb_u_queue[mb_u_q_tail].act   = act;
  mb_u_queue[mb_u_q_tail].arg   = arg;
  mb_u_queue[mb_u_q_tail].ptr   = ptr;
  mb_u_queue[mb_u_q_tail].value = value;

  mb_u_q_tail = (uint8_t)((mb_u_q_tail + 1u) % MB_U_QUEUE_N);
  mb_u_q_count++;

  return 1;
}

static int mb_u_pop(mb_u_evt_t *out)
{
  if (mb_u_q_count == 0u)
  {
    return 0;
  }

  *out = mb_u_queue[mb_u_q_head];

  mb_u_q_head = (uint8_t)((mb_u_q_head + 1u) % MB_U_QUEUE_N);
  mb_u_q_count--;

  return 1;
}

void Command_vector_ExecutePendingActions(void)
{
  mb_u_evt_t p;
  uint8_t did = 0u;

  while (mb_u_pop(&p) != 0)
  {
    did = 1u;

    switch (p.act)
    {
    case MB_U_ACT_NONE:
      break;

    case MB_U_ACT_SET_FLAG:
      if (p.ptr != 0)
      {
        *(uint8_t *)p.ptr = (p.value != 0u) ? 1u : 0u;
      }
      break;

#if defined(MB_REG_ERASE_SOUNDS) || defined(MB_REG_ERASE_CONFIG) || \
    defined(MB_REG_ERASE_CFGDEF) || defined(MB_REG_ERASE_LOG) || \
    defined(MB_REG_ERASE_LOGPTR) || defined(MB_REG_ERASE_FW)
    case MB_U_ACT_ERASE_BIT:
      era.req |= (uint8_t)(1u << p.arg);

#ifdef SYS_PROC_FW
#ifdef SYS_PROC_ARCHIVE
      sys_state.process = (p.arg == 5u) ? SYS_PROC_FW : SYS_PROC_ARCHIVE;
#endif
#endif
      break;
#endif

#ifdef MB_REG_COMMIT
    case MB_U_ACT_COMMIT:
      if (hooks.commit != 0)
      {
        hooks.commit();
      }

      rx.session = 0u;

#if defined(VECTOR_LOG_ENABLE) && VECTOR_LOG_ENABLE
      Command_vector_LogMute(0u);
#endif
      break;
#endif

#ifdef MB_REG_IMG_REFRESH
    case MB_U_ACT_IMG_REFRESH:
      img_refresh();
      break;
#endif

#if defined(MB_REG_WIN_ADDR_LO) || defined(MB_REG_WIN_ADDR_HI)
    case MB_U_ACT_WIN_ADDR:
      if (p.arg == 0u)
      {
        win.addr = (win.addr & 0xFFFF0000u) | (uint32_t)p.value;
      }
      else
      {
        win.addr = (win.addr & 0x0000FFFFu) | ((uint32_t)p.value << 16);
      }
      win.dirty = 1u;
      break;
#endif

#ifdef MB_REG_WIN_LEN
    case MB_U_ACT_WIN_LEN:
#if defined(MB_WIN_BYTES)
      if ((p.value <= MB_WIN_BYTES) && ((p.value & 1u) == 0u))
#endif
      {
        win.len = p.value;
        win.dirty = 1u;
      }
#if defined(MB_WIN_BYTES) && defined(CMD_ERR_VALUE)
      else
      {
        win.status = CMD_ERR_VALUE;
      }
#endif
      break;
#endif

#ifdef MB_REG_WIN_AUTOINC
    case MB_U_ACT_WIN_AUTOINC:
      win.autoinc = (uint8_t)(p.value != 0u);
      break;
#endif

#ifdef MB_REG_WIN_CMD
    case MB_U_ACT_WIN_CMD:
      if (p.value != 0u)
      {
        win.dirty = 1u;
        win_load();
      }
      break;
#endif

#ifdef MB_REG_FW_APPLY
    case MB_U_ACT_FW_APPLY:
#ifdef SYS_PROC_FW
      sys_state.process = SYS_PROC_FW;
#endif

      fw_apply_request();
#else
      sys_state.soft_reset = 1u;
#endif
      break;
#endif

#ifdef MB_REG_BOOT_ERASE
    case MB_U_ACT_BOOT_ERASE:
      if ((p.value == 1u) || (p.value == 2u))
      {
        if (boot_flash_allowed() == 0)
        {
          boot_flash_status = BOOT_ST_PROTECTED;
        }
        else
        {
          FlashErase = p.value;
        }
#else
        FlashErase = p.value;
#endif
      }
      break;
#endif

    default:
      break;
    }
  }

  if (did != 0u)
  {
    mb_u_sync_needed = 1u;
  }
}

static void mb_u_sync(void)
{
#ifdef MB_REG_FW_VERSION
#ifdef FIRMWARE_VERSION
  mb_u_val[MB_U_IDX(MB_REG_FW_VERSION)] =
      (uint16_t)((uint32_t)FIRMWARE_VERSION & 0xFFFFu);
#endif
#endif

#ifdef MB_REG_FW_STATUS
  mb_u_val[MB_U_IDX(MB_REG_FW_STATUS)] = (uint16_t)sys_state.process;
#endif

  mb_u_val[MB_U_IDX(MB_REG_BOOT_STATUS)] = (uint16_t)boot_flash_status;
#endif

  mb_u_val[MB_U_IDX(MB_REG_BOOT_ALLOWED)] = (uint16_t)boot_flash_allowed();
#endif

#if defined(MB_REG_MAGIC_LO) || defined(MB_REG_IMG_VER) || defined(MB_REG_IMG_OK)
  if (img.valid == 0u)
  {
    img_refresh();
  }

#ifdef MB_REG_MAGIC_LO
  mb_u_val[MB_U_IDX(MB_REG_MAGIC_LO)] = (uint16_t)(img.hdr.magic & 0xFFFFu);
#endif

#ifdef MB_REG_MAGIC_HI
  mb_u_val[MB_U_IDX(MB_REG_MAGIC_HI)] = (uint16_t)(img.hdr.magic >> 16);
#endif

#ifdef MB_REG_IMG_VER
  mb_u_val[MB_U_IDX(MB_REG_IMG_VER)] = img.hdr.version;
#endif

#ifdef MB_REG_IMG_CNT
  mb_u_val[MB_U_IDX(MB_REG_IMG_CNT)] = img.hdr.count;
#endif

#ifdef MB_REG_IMG_SIZE_LO
  mb_u_val[MB_U_IDX(MB_REG_IMG_SIZE_LO)] = (uint16_t)(img.hdr.total_size & 0xFFFFu);
#endif

#ifdef MB_REG_IMG_SIZE_HI
  mb_u_val[MB_U_IDX(MB_REG_IMG_SIZE_HI)] = (uint16_t)(img.hdr.total_size >> 16);
#endif

#ifdef MB_REG_IMG_RATE
  mb_u_val[MB_U_IDX(MB_REG_IMG_RATE)] = img.rate;
#endif

#ifdef MB_REG_IMG_OK
  mb_u_val[MB_U_IDX(MB_REG_IMG_OK)] = img.ok;
#endif
#endif

#if defined(MB_REG_WIN_ADDR_LO) || defined(MB_REG_WIN_ADDR_HI)
  mb_u_val[MB_U_IDX(MB_REG_WIN_ADDR_LO)] = (uint16_t)(win.addr & 0xFFFFu);
  mb_u_val[MB_U_IDX(MB_REG_WIN_ADDR_HI)] = (uint16_t)(win.addr >> 16);
#endif

#ifdef MB_REG_WIN_LEN
  mb_u_val[MB_U_IDX(MB_REG_WIN_LEN)] = win.len;
#endif

#ifdef MB_REG_WIN_AUTOINC
  mb_u_val[MB_U_IDX(MB_REG_WIN_AUTOINC)] = win.autoinc;
#endif

#if defined(MB_REG_FLASH_LO) && defined(SF_TOTAL_SIZE)
  mb_u_val[MB_U_IDX(MB_REG_FLASH_LO)] = (uint16_t)((uint32_t)SF_TOTAL_SIZE & 0xFFFFu);
#endif

#if defined(MB_REG_FLASH_HI) && defined(SF_TOTAL_SIZE)
  mb_u_val[MB_U_IDX(MB_REG_FLASH_HI)] = (uint16_t)((uint32_t)SF_TOTAL_SIZE >> 16);
#endif

  mb_u_sync_needed = 0u;
}

static void mb_u_prepare_read(void)
{
  Command_vector_ExecutePendingActions();
  mb_u_sync();
}

static uint8_t ctrl_reg_read(uint16_t addr, uint16_t *val)
{
  if (val == 0)
  {
    return 0u;
  }

  if (!MB_U_IN_RANGE(addr))
  {
    return 0u;
  }

  mb_u_prepare_read();

  *val = mb_u_val[MB_U_IDX(addr)];
  return 1u;
}

static uint8_t ctrl_reg_write(uint16_t addr, uint16_t val)
{
  const mb_u_item_t *item;
  uint16_t idx;

  if (!MB_U_IN_RANGE(addr))
  {
    return 0u;
  }

  idx = MB_U_IDX(addr);
  item = mb_u_find(addr);

  if (item != 0 && item->acc == MB_U_RO)
  {
    return 0u;
  }

  /*
   * Командный регистр:
   * - запись nonzero ставит действие в очередь;
   * - сам регистр сразу обнуляется, чтобы не висел липкий флаг.
   */
  if (item != 0 && item->acc == MB_U_CMD)
  {
    if (val == 0u)
    {
      mb_u_val[idx] = 0u;
      return 1u;
    }

    if (item->act == MB_U_ACT_NONE)
    {
      mb_u_val[idx] = val;
      return 1u;
    }

    if (mb_u_push(item->act, item->arg, item->ptr, val) == 0)
    {
      return 0u;
    }

    mb_u_val[idx] = 0u;
    return 1u;
  }

  /*
   * RW-параметр:
   * - значение сразу кладём в банк;
   * - если есть действие, оно будет выполнено после ответа Modbus.
   */
  mb_u_val[idx] = val;

  if (item != 0 && item->act != MB_U_ACT_NONE)
  {
    if (mb_u_push(item->act, item->arg, item->ptr, val) == 0)
    {
      return 0u;
    }
  }

  return 1u;
}

uint16_t CmdFunc3(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  #ifdef COM_MAP_N
  const uint16_t n = (uint16_t)COM_MAP_N;
  #else
  const uint16_t n = (uint16_t)(sizeof(com_map) / sizeof(com_map[0]));
  #endif

  uint16_t addr = (uint16_t)(((uint16_t)mas[2] << 8) | mas[3]);
  uint16_t cnt  = (uint16_t)(((uint16_t)mas[4] << 8) | mas[5]);
  uint16_t i, roff;
  uint8_t  *p_out = &out[2];

  if (cnt == 0u || cnt > 125u) return CmdError(out, mas, CMD_ERR_VALUE);
  if (cap < (uint16_t)(3u + cnt * 2u + 2u)) return CmdError(out, mas, CMD_ERR_LENGTH);

  /* Обновляем динамические статусы перед чтением */
  MB_SPECIAL_UPDATE_STATUS();

  out[0] = mas[0];
  out[1] = mas[1];
  out[2] = (uint8_t)(cnt * 2u);

  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t v = 0u;

    /* 1. БЫСТРЫЙ ПУТЬ: Чтение из com_map (включая mb_special) */
    if (reg_find(com_map, n, a, &roff) >= 0)
    {
      const reg_map_t *m = &com_map[roff];
      uint8_t *p_src = (uint8_t *)&mb_special + m->off;

      if (m->type == T_U16)
      {
        v = *(uint16_t *)(p_src + ((roff % 2u) * 2u));
      }
      else
      {
        v = 0u;
      }
    }
    /* 2. МЕДЛЕННЫЙ ПУТЬ: Динамические регистры вне com_map */
    else if (ctrl_reg_read(a, &v) != 0u)
    {
      return CmdError(out, mas, CMD_ERR_ADDR);
    }

    *p_out++ = (uint8_t)(v >> 8);
    *p_out++ = (uint8_t)v;
  }

  return (uint16_t)(3u + cnt * 2u);
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
  #ifdef COM_MAP_N
  const uint16_t n = (uint16_t)COM_MAP_N;
  #else
  const uint16_t n = (uint16_t)(sizeof(com_map) / sizeof(com_map[0]));
  #endif

  uint16_t addr = (uint16_t)(((uint16_t)mas[2] << 8) | mas[3]);
  uint16_t cnt  = (uint16_t)(((uint16_t)mas[4] << 8) | mas[5]);
  uint16_t i, roff;
  uint8_t byte_count = mas[6];
  const uint8_t *p_data = &mas[7];

  if (cnt == 0u || cnt > 123u) return CmdError(out, mas, CMD_ERR_VALUE);
  if (len != (uint16_t)(7u + byte_count + 2u)) return CmdError(out, mas, CMD_ERR_LENGTH);
  if (byte_count != (uint8_t)(cnt * 2u)) return CmdError(out, mas, CMD_ERR_VALUE);

  /* Запись с одним reg_find на регистр */
  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t v = (uint16_t)(((uint16_t)p_data[i * 2u] << 8) | p_data[i * 2u + 1u]);

    /* Ищем адрес в com_map ОДИН РАЗ */
    if (reg_find(com_map, n, a, &roff) >= 0)
    {
      /* Адрес найден - записываем напрямую (быстрый путь) */
      const reg_map_t *m = &com_map[roff];
      uint8_t *p_dst = (uint8_t *)&Sns_Cfg_struct + m->off;
      
      if (m->type == T_U16)
      {
        *(uint16_t *)(p_dst + ((roff % 2u) * 2u)) = v;
      }
      else if (m->type == T_U32)
      {
        if (roff % 2u == 0u)
        {
          *(uint32_t *)p_dst = (*(uint32_t *)p_dst & 0xFFFF0000u) | v;
        }
        else
        {
          *(uint32_t *)p_dst = (*(uint32_t *)p_dst & 0x0000FFFFu) | ((uint32_t)v << 16);
        }
      }
      continue;
    }

    /* Адрес не найден в com_map - передаем в обработчик специальных регистров */
    if (ctrl_reg_write(a, v) != 0u)
    {
      return CmdError(out, mas, CMD_ERR_ADDR);
    }
  }

  /* Успешный ответ Modbus 0x10: эхо первых 6 байт запроса */
  memcpy(out, mas, 6u);
  return 6u;
}



/* ==========================================================================
 * CmdFunc15 - блочная запись во внешнюю flash (стандартный паттерн)
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

/* ==========================================================================
 * MB_CTRL_LAYER_V3
 *
 * Все специальные/управляющие регистры 0x0F00+ работают как обычный банк.
 * Никаких switch/case в ctrl_reg_read()/ctrl_reg_write().
 * ========================================================================== */


static int mb_ctrl_addr_ok(uint16_t addr)
{
  if (!MB_CTRL_IN_RANGE(addr))
  {
    return 0;
  }

  /* Область данных окна не является простым управляющим регистром.
     Она обрабатывается отдельно в CmdFunc3/CmdFunc16. */
#if defined(MB_REG_WIN_DATA) && defined(MB_WIN_BYTES)
  if (addr >= MB_REG_WIN_DATA &&
      addr < (uint16_t)(MB_REG_WIN_DATA + (MB_WIN_BYTES / 2u)))
  {
    return 0;
  }
#endif

  return 1;
}

static void mb_ctrl_set(uint16_t addr, uint16_t value)
{
  if (mb_ctrl_addr_ok(addr))
  {
    mb_ctrl[MB_CTRL_IDX(addr)] = value;
  }
}

/* --------------------------------------------------------------------------
 * Обновление банка перед чтением.
 * Здесь нет switch по адресам. Просто заполняем известные статусы/параметры.
 * -------------------------------------------------------------------------- */

static void mb_ctrl_update_img(void)
{
#if defined(MB_REG_MAGIC_LO) || defined(MB_REG_IMG_VER) || defined(MB_REG_IMG_REFRESH)
  if (img.valid == 0u)
  {
    img_refresh();
  }
#endif

#ifdef MB_REG_MAGIC_LO
  mb_ctrl_set(MB_REG_MAGIC_LO, (uint16_t)(img.hdr.magic & 0xFFFFu));
#endif

#ifdef MB_REG_MAGIC_HI
  mb_ctrl_set(MB_REG_MAGIC_HI, (uint16_t)(img.hdr.magic >> 16));
#endif

#ifdef MB_REG_IMG_VER
  mb_ctrl_set(MB_REG_IMG_VER, img.hdr.version);
#endif

#ifdef MB_REG_IMG_CNT
  mb_ctrl_set(MB_REG_IMG_CNT, img.hdr.count);
#endif

#ifdef MB_REG_IMG_SIZE_LO
  mb_ctrl_set(MB_REG_IMG_SIZE_LO, (uint16_t)(img.hdr.total_size & 0xFFFFu));
#endif

#ifdef MB_REG_IMG_SIZE_HI
  mb_ctrl_set(MB_REG_IMG_SIZE_HI, (uint16_t)(img.hdr.total_size >> 16));
#endif

#ifdef MB_REG_IMG_RATE
  mb_ctrl_set(MB_REG_IMG_RATE, img.rate);
#endif

#ifdef MB_REG_IMG_OK
  mb_ctrl_set(MB_REG_IMG_OK, img.ok);
#endif
}

static void mb_ctrl_update_status(void)
{
#if defined(MB_REG_FW_VERSION) && defined(FIRMWARE_VERSION)
  mb_ctrl_set(MB_REG_FW_VERSION, (uint16_t)((uint32_t)FIRMWARE_VERSION & 0xFFFFu));
#endif

#ifdef MB_REG_FW_STATUS
  mb_ctrl_set(MB_REG_FW_STATUS, (uint16_t)sys_state.process);
#endif

#if defined(MB_REG_BOOT_STATUS) && defined(BOOT_FLASH_H)
  mb_ctrl_set(MB_REG_BOOT_STATUS, (uint16_t)boot_flash_status);
#endif

#if defined(MB_REG_BOOT_ALLOWED) && defined(BOOT_FLASH_H)
  mb_ctrl_set(MB_REG_BOOT_ALLOWED, (uint16_t)boot_flash_allowed());
#endif
}

static void mb_ctrl_update_params(void)
{
#ifdef MB_REG_WIN_ADDR_LO
  mb_ctrl_set(MB_REG_WIN_ADDR_LO, (uint16_t)(win.addr & 0xFFFFu));
#endif

#ifdef MB_REG_WIN_ADDR_HI
  mb_ctrl_set(MB_REG_WIN_ADDR_HI, (uint16_t)(win.addr >> 16));
#endif

#ifdef MB_REG_WIN_LEN
  mb_ctrl_set(MB_REG_WIN_LEN, win.len);
#endif

#ifdef MB_REG_WIN_AUTOINC
  mb_ctrl_set(MB_REG_WIN_AUTOINC, win.autoinc);
#endif

#if defined(MB_REG_FLASH_LO) && defined(SF_TOTAL_SIZE)
  mb_ctrl_set(MB_REG_FLASH_LO, (uint16_t)((uint32_t)SF_TOTAL_SIZE & 0xFFFFu));
#endif

#if defined(MB_REG_FLASH_HI) && defined(SF_TOTAL_SIZE)
  mb_ctrl_set(MB_REG_FLASH_HI, (uint16_t)((uint32_t)SF_TOTAL_SIZE >> 16));
#endif
}

static void mb_ctrl_update(void)
{
  mb_ctrl_update_img();
  mb_ctrl_update_status();
  mb_ctrl_update_params();
}

/* --------------------------------------------------------------------------
 * Таблица действий/параметров.
 *
 * on_write  - немедленная реакция на запись регистра (для параметров).
 * on_process- выполнение команды из Measure Task.
 * pulse     - после выполнения командный регистр обнуляется.
 * -------------------------------------------------------------------------- */

typedef void (*mb_ctrl_fn_t)(uint16_t value);

#define MB_ITEM_PARAM(a, w)       { (a), (w), 0, 0 }
#define MB_ITEM_CMD(a, p)         { (a), 0, (p), 1 }
#define MB_ITEM_BOTH(a, w, p)     { (a), (w), (p), 1 }

/* ---------------------- Параметры окна ---------------------- */

#ifdef MB_REG_WIN_ADDR_LO
static void mb_win_addr_lo_write(uint16_t value)
{
  win.addr = (win.addr & 0xFFFF0000u) | (uint32_t)value;
  win.dirty = 1u;
}
#endif

#ifdef MB_REG_WIN_ADDR_HI
static void mb_win_addr_hi_write(uint16_t value)
{
  win.addr = (win.addr & 0x0000FFFFu) | ((uint32_t)value << 16);
  win.dirty = 1u;
}
#endif

#ifdef MB_REG_WIN_LEN
static void mb_win_len_write(uint16_t value)
{
#if defined(MB_WIN_BYTES)
  if ((value <= MB_WIN_BYTES) && ((value & 1u) == 0u))
#endif
  {
    win.len = value;
    win.dirty = 1u;
  }
#if defined(CMD_ERR_VALUE) && defined(MB_WIN_BYTES)
  else
  {
    win.status = CMD_ERR_VALUE;
  }
#endif
}
#endif

#ifdef MB_REG_WIN_AUTOINC
static void mb_win_autoinc_write(uint16_t value)
{
  win.autoinc = (uint8_t)(value != 0u);
}
#endif

#ifdef MB_REG_WIN_CMD
static void mb_win_cmd_process(uint16_t value)
{
  if (value != 0u)
  {
    win.dirty = 1u;
    win_load();
  }
}
#endif

/* ---------------------- Общие команды ---------------------- */

#if defined(MB_REG_COMMIT)
static void mb_commit_process(uint16_t value)
{
  (void)value;

  if (hooks.commit != 0)
  {
    hooks.commit();
  }

  rx.session = 0u;
  Command_vector_LogMute(0u);
}
#endif

#if defined(MB_REG_SAVE_CFG) && defined(CONFIG_SAVE_READ_H)
static void mb_save_cfg_process(uint16_t value)
{
  (void)value;
  SaveConfig = 1u;
}
#endif

#if defined(MB_REG_SAVE_CFGDEF) && defined(CONFIG_SAVE_READ_H)
static void mb_save_cfgdef_process(uint16_t value)
{
  (void)value;
  SaveConfigDefault = 1u;
}
#endif

#if defined(MB_REG_LOAD_CFGDEF) && defined(CONFIG_SAVE_READ_H)
static void mb_load_cfgdef_process(uint16_t value)
{
  (void)value;
  ReadConfigDefault = 1u;
}
#endif

#ifdef MB_REG_IMG_REFRESH
static void mb_img_refresh_process(uint16_t value)
{
  (void)value;
  img_refresh();
}
#endif

/* ---------------------- Стирания ---------------------- */

#ifdef MB_REG_ERASE_SOUNDS
static void mb_erase_sounds_process(uint16_t value)
{
  (void)value;
  era.req |= (uint8_t)(1u << 0u);
  sys_state.process = SYS_PROC_ARCHIVE;
}
#endif

#ifdef MB_REG_ERASE_CONFIG
static void mb_erase_config_process(uint16_t value)
{
  (void)value;
  era.req |= (uint8_t)(1u << 1u);
  sys_state.process = SYS_PROC_ARCHIVE;
}
#endif

#ifdef MB_REG_ERASE_CFGDEF
static void mb_erase_cfgdef_process(uint16_t value)
{
  (void)value;
  era.req |= (uint8_t)(1u << 2u);
  sys_state.process = SYS_PROC_ARCHIVE;
}
#endif

#ifdef MB_REG_ERASE_LOG
static void mb_erase_log_process(uint16_t value)
{
  (void)value;
  era.req |= (uint8_t)(1u << 3u);
  sys_state.process = SYS_PROC_ARCHIVE;
}
#endif

#ifdef MB_REG_ERASE_LOGPTR
static void mb_erase_logptr_process(uint16_t value)
{
  (void)value;
  era.req |= (uint8_t)(1u << 4u);
  sys_state.process = SYS_PROC_ARCHIVE;
}
#endif

#ifdef MB_REG_ERASE_FW
static void mb_erase_fw_process(uint16_t value)
{
  (void)value;
  era.req |= (uint8_t)(1u << 5u);
  sys_state.process = SYS_PROC_FW;
}
#endif

/* ---------------------- Прошивка / бутлоадер ---------------------- */

#if defined(MB_REG_FW_APPLY) && defined(FW_UPDATE_H)
static void mb_fw_apply_process(uint16_t value)
{
  (void)value;
  sys_state.process = SYS_PROC_FW;
  fw_apply_request();
}
#endif

#if defined(MB_REG_BOOT_ERASE) && defined(BOOT_FLASH_H)
static void mb_boot_erase_process(uint16_t value)
{
  if ((value == 1u) || (value == 2u))
  {
    if (boot_flash_allowed() == 0)
    {
      boot_flash_status = BOOT_ST_PROTECTED;
    }
    else
    {
      FlashErase = value;
    }
  }
}
#endif

/* ---------------------- Таблица ---------------------- */

{
  /* Параметры идут раньше команд, чтобы WIN_CMD видел уже записанные addr/len. */

  MB_ITEM_PARAM(MB_REG_WIN_ADDR_LO, mb_win_addr_lo_write),


  MB_ITEM_PARAM(MB_REG_WIN_ADDR_HI, mb_win_addr_hi_write),

  MB_ITEM_PARAM(MB_REG_WIN_LEN, mb_win_len_write),

  MB_ITEM_PARAM(MB_REG_WIN_AUTOINC, mb_win_autoinc_write),

  MB_ITEM_CMD(MB_REG_WIN_CMD, mb_win_cmd_process),

  MB_ITEM_CMD(MB_REG_COMMIT, mb_commit_process),

  MB_ITEM_CMD(MB_REG_SAVE_CFG, mb_save_cfg_process),

  MB_ITEM_CMD(MB_REG_SAVE_CFGDEF, mb_save_cfgdef_process),

  MB_ITEM_CMD(MB_REG_LOAD_CFGDEF, mb_load_cfgdef_process),

  MB_ITEM_CMD(MB_REG_IMG_REFRESH, mb_img_refresh_process),

  MB_ITEM_CMD(MB_REG_ERASE_SOUNDS, mb_erase_sounds_process),

  MB_ITEM_CMD(MB_REG_ERASE_CONFIG, mb_erase_config_process),

  MB_ITEM_CMD(MB_REG_ERASE_CFGDEF, mb_erase_cfgdef_process),

  MB_ITEM_CMD(MB_REG_ERASE_LOG, mb_erase_log_process),

  MB_ITEM_CMD(MB_REG_ERASE_LOGPTR, mb_erase_logptr_process),

  MB_ITEM_CMD(MB_REG_ERASE_FW, mb_erase_fw_process),

  MB_ITEM_CMD(MB_REG_FW_APPLY, mb_fw_apply_process),

  MB_ITEM_CMD(MB_REG_BOOT_ERASE, mb_boot_erase_process),

  { 0u, 0, 0, 0 }
};

static void mb_ctrl_apply_write(uint16_t addr, uint16_t value)
{
  uint16_t i;

  for (i = 0u; mb_ctrl_items[i].addr != 0u; i++)
  {
    if (mb_ctrl_items[i].addr == addr)
    {
      if (mb_ctrl_items[i].on_write != 0)
      {
        mb_ctrl_items[i].on_write(value);
      }
      return;
    }
  }
}

void Command_vector_ProcessControlRegisters(void)
{
  uint16_t i;

  for (i = 0u; mb_ctrl_items[i].addr != 0u; i++)
  {
    uint16_t idx;
    uint16_t value;

    if (mb_ctrl_items[i].on_process == 0)
    {
      continue;
    }

    if (!mb_ctrl_addr_ok(mb_ctrl_items[i].addr))
    {
      continue;
    }

    idx = MB_CTRL_IDX(mb_ctrl_items[i].addr);
    value = mb_ctrl[idx];

    if (value != 0u)
    {
      if (mb_ctrl_items[i].pulse != 0u)
      {
        mb_ctrl[idx] = 0u;
      }

      mb_ctrl_items[i].on_process(value);
    }
  }
}


/* ==========================================================================
 * Новый ctrl_reg_read: только банк mb_ctrl[], без switch/case.
 * ========================================================================== */
static uint8_t ctrl_reg_read(uint16_t addr, uint16_t *val)
{
  if (!mb_ctrl_addr_ok(addr))
  {
    return 0u;
  }

  mb_ctrl_update();
  *val = mb_ctrl[MB_CTRL_IDX(addr)];
  return 1u;
}


/* ==========================================================================
 * Новый ctrl_reg_write: только запись в банк mb_ctrl[], без switch/case.
 * Действия выполняются позже в Command_vector_ProcessControlRegisters().
 * ========================================================================== */
static uint8_t ctrl_reg_write(uint16_t addr, uint16_t val)
{
  if (!mb_ctrl_addr_ok(addr))
  {
    return 0u;
  }

  mb_ctrl[MB_CTRL_IDX(addr)] = val;
  mb_ctrl_apply_write(addr, val);
  return 1u;
}

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

/* ==== MB_U_TOP_BEGIN ==== */
#if defined(__has_include)
# if __has_include("boot_flash.h")
#  include "boot_flash.h"
#  define MB_HAVE_BOOT_FLASH 1
# endif
# if __has_include("fw_update.h")
#  include "fw_update.h"
#  define MB_HAVE_FW_UPDATE 1
# endif
# if __has_include("Config_save_read.h")
#  include "Config_save_read.h"
# endif
#endif

extern uint16_t FlashErase;

static uint8_t ctrl_reg_read(uint16_t addr, uint16_t *val);
static uint8_t ctrl_reg_write(uint16_t addr, uint16_t val);
void Command_vector_ExecutePendingActions(void);
/* ==== MB_U_TOP_END ==== */


#if defined(__has_include)
# if __has_include("boot_flash.h")
#  include "boot_flash.h"
# endif
# if __has_include("fw_update.h")
#  include "fw_update.h"
# endif
# if __has_include("Config_save_read.h")
#  include "Config_save_read.h"
# endif
#endif

extern uint16_t FlashErase;


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

/* ==========================================================================
 * MB_CTRL processor.
 *
 * Вызывается из Measure Task (Vector_Options_System).
 * Забирает ненулевые командные флаги из mb_ctrl, выполняет действия
 * и сбрасывает флаги.
 * ========================================================================== */


/* ==== MB_U_CANONICAL_BEGIN ==== */
/* ==========================================================================
 * MB_U canonical bank for 0x0F00..0x0FFF.
 *
 * Rules:
 * - all special/control registers are plain uint16_t values in mb_u_val[];
 * - ctrl_reg_read()/ctrl_reg_write() have no per-register switch;
 * - command writes only enqueue an action;
 * - actions are executed by Command_vector_ExecutePendingActions();
 * - dynamic status values are refreshed by mb_u_sync().
 * ========================================================================== */

#ifndef MB_U_BASE
#define MB_U_BASE                 0x0F00u
#endif

#ifndef MB_U_COUNT
#define MB_U_COUNT                0x100u
#endif

#ifndef MB_U_IDX
#define MB_U_IDX(addr)            ((uint16_t)((addr) - MB_U_BASE))
#endif

#ifndef MB_U_IN_RANGE
#define MB_U_IN_RANGE(addr)       (((addr) >= MB_U_BASE) && ((addr) < (MB_U_BASE + MB_U_COUNT)))
#endif

typedef enum
{
  MB_U_RO  = 0,
  MB_U_RW  = 1,
  MB_U_CMD = 2
} mb_u_acc_t;

typedef enum
{
  MB_U_ACT_NONE = 0,

  MB_U_ACT_SET_FLAG,
  MB_U_ACT_ERASE_BIT,

  MB_U_ACT_COMMIT,
  MB_U_ACT_IMG_REFRESH,

  MB_U_ACT_WIN_ADDR,
  MB_U_ACT_WIN_LEN,
  MB_U_ACT_WIN_AUTOINC,
  MB_U_ACT_WIN_CMD,

  MB_U_ACT_FW_APPLY,
  MB_U_ACT_BOOT_ERASE
} mb_u_act_t;

typedef struct
{
  uint16_t addr;
  uint8_t  acc;
  uint8_t  act;
  uint16_t arg;
  void    *ptr;
} mb_u_item_t;

typedef struct
{
  uint8_t  act;
  uint16_t arg;
  void    *ptr;
  uint16_t value;
} mb_u_evt_t;

#define MB_U_QUEUE_N 32

static uint16_t mb_u_val[MB_U_COUNT] = {0};

static mb_u_evt_t mb_u_queue[MB_U_QUEUE_N];
static uint8_t    mb_u_q_head;
static uint8_t    mb_u_q_tail;
static uint8_t    mb_u_q_count;
static uint8_t    mb_u_sync_needed = 1u;

static const mb_u_item_t mb_u_items[] =
{
#ifdef MB_REG_FW_VERSION
  { MB_REG_FW_VERSION,      MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_FW_STATUS
  { MB_REG_FW_STATUS,       MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_BOOT_STATUS
  { MB_REG_BOOT_STATUS,     MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_BOOT_ALLOWED
  { MB_REG_BOOT_ALLOWED,    MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_MAGIC_LO
  { MB_REG_MAGIC_LO,        MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_MAGIC_HI
  { MB_REG_MAGIC_HI,        MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_VER
  { MB_REG_IMG_VER,         MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_CNT
  { MB_REG_IMG_CNT,         MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_SIZE_LO
  { MB_REG_IMG_SIZE_LO,     MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_SIZE_HI
  { MB_REG_IMG_SIZE_HI,     MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_RATE
  { MB_REG_IMG_RATE,        MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_IMG_OK
  { MB_REG_IMG_OK,          MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_FLASH_LO
  { MB_REG_FLASH_LO,        MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_FLASH_HI
  { MB_REG_FLASH_HI,        MB_U_RO,  MB_U_ACT_NONE,        0u, 0 },
#endif

#ifdef MB_REG_WIN_ADDR_LO
  { MB_REG_WIN_ADDR_LO,     MB_U_RW,  MB_U_ACT_WIN_ADDR,    0u, 0 },
#endif

#ifdef MB_REG_WIN_ADDR_HI
  { MB_REG_WIN_ADDR_HI,     MB_U_RW,  MB_U_ACT_WIN_ADDR,    1u, 0 },
#endif

#ifdef MB_REG_WIN_LEN
  { MB_REG_WIN_LEN,         MB_U_RW,  MB_U_ACT_WIN_LEN,     0u, 0 },
#endif

#ifdef MB_REG_WIN_AUTOINC
  { MB_REG_WIN_AUTOINC,     MB_U_RW,  MB_U_ACT_WIN_AUTOINC, 0u, 0 },
#endif

#ifdef MB_REG_WIN_CMD
  { MB_REG_WIN_CMD,         MB_U_CMD, MB_U_ACT_WIN_CMD,     0u, 0 },
#endif

#ifdef MB_REG_COMMIT
  { MB_REG_COMMIT,          MB_U_CMD, MB_U_ACT_COMMIT,      0u, 0 },
#endif

#ifdef MB_REG_SAVE_CFG
  { MB_REG_SAVE_CFG,        MB_U_CMD, MB_U_ACT_SET_FLAG,    0u, (void *)&SaveConfig },
#endif

#ifdef MB_REG_SAVE_CFGDEF
  { MB_REG_SAVE_CFGDEF,     MB_U_CMD, MB_U_ACT_SET_FLAG,    0u, (void *)&SaveConfigDefault },
#endif

#ifdef MB_REG_LOAD_CFGDEF
  { MB_REG_LOAD_CFGDEF,     MB_U_CMD, MB_U_ACT_SET_FLAG,    0u, (void *)&ReadConfigDefault },
#endif

#ifdef MB_REG_IMG_REFRESH
  { MB_REG_IMG_REFRESH,     MB_U_CMD, MB_U_ACT_IMG_REFRESH, 0u, 0 },
#endif

#ifdef MB_REG_ERASE_SOUNDS
  { MB_REG_ERASE_SOUNDS,    MB_U_CMD, MB_U_ACT_ERASE_BIT,   0u, 0 },
#endif

#ifdef MB_REG_ERASE_CONFIG
  { MB_REG_ERASE_CONFIG,    MB_U_CMD, MB_U_ACT_ERASE_BIT,   1u, 0 },
#endif

#ifdef MB_REG_ERASE_CFGDEF
  { MB_REG_ERASE_CFGDEF,    MB_U_CMD, MB_U_ACT_ERASE_BIT,   2u, 0 },
#endif

#ifdef MB_REG_ERASE_LOG
  { MB_REG_ERASE_LOG,       MB_U_CMD, MB_U_ACT_ERASE_BIT,   3u, 0 },
#endif

#ifdef MB_REG_ERASE_LOGPTR
  { MB_REG_ERASE_LOGPTR,    MB_U_CMD, MB_U_ACT_ERASE_BIT,   4u, 0 },
#endif

#ifdef MB_REG_ERASE_FW
  { MB_REG_ERASE_FW,        MB_U_CMD, MB_U_ACT_ERASE_BIT,   5u, 0 },
#endif

#ifdef MB_REG_FW_APPLY
  { MB_REG_FW_APPLY,        MB_U_CMD, MB_U_ACT_FW_APPLY,    0u, 0 },
#endif

#ifdef MB_REG_BOOT_ERASE
  { MB_REG_BOOT_ERASE,      MB_U_CMD, MB_U_ACT_BOOT_ERASE,  0u, 0 },
#endif

  { 0u, 0, 0, 0u, 0 }
};

static const mb_u_item_t *mb_u_find(uint16_t addr)
{
  uint16_t i;

  for (i = 0u; mb_u_items[i].addr != 0u; i++)
  {
    if (mb_u_items[i].addr == addr)
    {
      return &mb_u_items[i];
    }
  }

  return 0;
}

static int mb_u_push(uint8_t act, uint16_t arg, void *ptr, uint16_t value)
{
  if (mb_u_q_count >= MB_U_QUEUE_N)
  {
    return 0;
  }

  mb_u_queue[mb_u_q_tail].act   = act;
  mb_u_queue[mb_u_q_tail].arg   = arg;
  mb_u_queue[mb_u_q_tail].ptr   = ptr;
  mb_u_queue[mb_u_q_tail].value = value;

  mb_u_q_tail = (uint8_t)((mb_u_q_tail + 1u) % MB_U_QUEUE_N);
  mb_u_q_count++;

  return 1;
}

static int mb_u_pop(mb_u_evt_t *out)
{
  if (mb_u_q_count == 0u)
  {
    return 0;
  }

  *out = mb_u_queue[mb_u_q_head];

  mb_u_q_head = (uint8_t)((mb_u_q_head + 1u) % MB_U_QUEUE_N);
  mb_u_q_count--;

  return 1;
}

void Command_vector_ExecutePendingActions(void)
{
  mb_u_evt_t p;
  uint8_t did = 0u;

  while (mb_u_pop(&p) != 0)
  {
    did = 1u;

    switch (p.act)
    {
    case MB_U_ACT_NONE:
      break;

    case MB_U_ACT_SET_FLAG:
      if (p.ptr != 0)
      {
        *(uint8_t *)p.ptr = (p.value != 0u) ? 1u : 0u;
      }
      break;

#if defined(MB_REG_ERASE_SOUNDS) || defined(MB_REG_ERASE_CONFIG) || \
    defined(MB_REG_ERASE_CFGDEF) || defined(MB_REG_ERASE_LOG) || \
    defined(MB_REG_ERASE_LOGPTR) || defined(MB_REG_ERASE_FW)
    case MB_U_ACT_ERASE_BIT:
      era.req |= (uint8_t)(1u << p.arg);

#if defined(SYS_PROC_FW) && defined(SYS_PROC_ARCHIVE)
      sys_state.process = (p.arg == 5u) ? SYS_PROC_FW : SYS_PROC_ARCHIVE;
#endif
      break;
#endif

#ifdef MB_REG_COMMIT
    case MB_U_ACT_COMMIT:
      if (hooks.commit != 0)
      {
        hooks.commit();
      }

      rx.session = 0u;

#if defined(VECTOR_LOG_ENABLE) && VECTOR_LOG_ENABLE
      Command_vector_LogMute(0u);
#endif
      break;
#endif

#ifdef MB_REG_IMG_REFRESH
    case MB_U_ACT_IMG_REFRESH:
      img_refresh();
      break;
#endif

#if defined(MB_REG_WIN_ADDR_LO) || defined(MB_REG_WIN_ADDR_HI)
    case MB_U_ACT_WIN_ADDR:
      if (p.arg == 0u)
      {
        win.addr = (win.addr & 0xFFFF0000u) | (uint32_t)p.value;
      }
      else
      {
        win.addr = (win.addr & 0x0000FFFFu) | ((uint32_t)p.value << 16);
      }
      win.dirty = 1u;
      break;
#endif

#ifdef MB_REG_WIN_LEN
    case MB_U_ACT_WIN_LEN:
#if defined(MB_WIN_BYTES)
      if ((p.value <= MB_WIN_BYTES) && ((p.value & 1u) == 0u))
#endif
      {
        win.len = p.value;
        win.dirty = 1u;
      }
#if defined(MB_WIN_BYTES) && defined(CMD_ERR_VALUE)
      else
      {
        win.status = CMD_ERR_VALUE;
      }
#endif
      break;
#endif

#ifdef MB_REG_WIN_AUTOINC
    case MB_U_ACT_WIN_AUTOINC:
      win.autoinc = (uint8_t)(p.value != 0u);
      break;
#endif

#ifdef MB_REG_WIN_CMD
    case MB_U_ACT_WIN_CMD:
      if (p.value != 0u)
      {
        win.dirty = 1u;
        win_load();
      }
      break;
#endif

#if defined(MB_REG_FW_APPLY) && defined(MB_HAVE_FW_UPDATE)
    case MB_U_ACT_FW_APPLY:
#if defined(SYS_PROC_FW)
      sys_state.process = SYS_PROC_FW;
#endif
      fw_apply_request();
      break;
#endif

#if defined(MB_REG_BOOT_ERASE) && defined(MB_HAVE_BOOT_FLASH)
    case MB_U_ACT_BOOT_ERASE:
      if ((p.value == 1u) || (p.value == 2u))
      {
        if (boot_flash_allowed() == 0)
        {
          boot_flash_status = BOOT_ST_PROTECTED;
        }
        else
        {
          FlashErase = p.value;
        }
      }
      break;
#endif

    default:
      break;
    }
  }

  if (did != 0u)
  {
    mb_u_sync_needed = 1u;
  }
}

static void mb_u_sync(void)
{
#ifdef MB_REG_FW_VERSION
#ifdef FIRMWARE_VERSION
  mb_u_val[MB_U_IDX(MB_REG_FW_VERSION)] =
      (uint16_t)((uint32_t)FIRMWARE_VERSION & 0xFFFFu);
#endif
#endif

#ifdef MB_REG_FW_STATUS
  mb_u_val[MB_U_IDX(MB_REG_FW_STATUS)] = (uint16_t)sys_state.process;
#endif

#if defined(MB_REG_BOOT_STATUS) && defined(MB_HAVE_BOOT_FLASH)
  mb_u_val[MB_U_IDX(MB_REG_BOOT_STATUS)] = (uint16_t)boot_flash_status;
#endif

#if defined(MB_REG_BOOT_ALLOWED) && defined(MB_HAVE_BOOT_FLASH)
  mb_u_val[MB_U_IDX(MB_REG_BOOT_ALLOWED)] = (uint16_t)boot_flash_allowed();
#endif

#if defined(MB_REG_MAGIC_LO) || defined(MB_REG_IMG_VER) || defined(MB_REG_IMG_OK)
  if (img.valid == 0u)
  {
    img_refresh();
  }

#ifdef MB_REG_MAGIC_LO
  mb_u_val[MB_U_IDX(MB_REG_MAGIC_LO)] = (uint16_t)(img.hdr.magic & 0xFFFFu);
#endif

#ifdef MB_REG_MAGIC_HI
  mb_u_val[MB_U_IDX(MB_REG_MAGIC_HI)] = (uint16_t)(img.hdr.magic >> 16);
#endif

#ifdef MB_REG_IMG_VER
  mb_u_val[MB_U_IDX(MB_REG_IMG_VER)] = img.hdr.version;
#endif

#ifdef MB_REG_IMG_CNT
  mb_u_val[MB_U_IDX(MB_REG_IMG_CNT)] = img.hdr.count;
#endif

#ifdef MB_REG_IMG_SIZE_LO
  mb_u_val[MB_U_IDX(MB_REG_IMG_SIZE_LO)] = (uint16_t)(img.hdr.total_size & 0xFFFFu);
#endif

#ifdef MB_REG_IMG_SIZE_HI
  mb_u_val[MB_U_IDX(MB_REG_IMG_SIZE_HI)] = (uint16_t)(img.hdr.total_size >> 16);
#endif

#ifdef MB_REG_IMG_RATE
  mb_u_val[MB_U_IDX(MB_REG_IMG_RATE)] = img.rate;
#endif

#ifdef MB_REG_IMG_OK
  mb_u_val[MB_U_IDX(MB_REG_IMG_OK)] = img.ok;
#endif
#endif

#if defined(MB_REG_WIN_ADDR_LO) || defined(MB_REG_WIN_ADDR_HI)
#ifdef MB_REG_WIN_ADDR_LO
  mb_u_val[MB_U_IDX(MB_REG_WIN_ADDR_LO)] = (uint16_t)(win.addr & 0xFFFFu);
#endif

#ifdef MB_REG_WIN_ADDR_HI
  mb_u_val[MB_U_IDX(MB_REG_WIN_ADDR_HI)] = (uint16_t)(win.addr >> 16);
#endif
#endif

#ifdef MB_REG_WIN_LEN
  mb_u_val[MB_U_IDX(MB_REG_WIN_LEN)] = win.len;
#endif

#ifdef MB_REG_WIN_AUTOINC
  mb_u_val[MB_U_IDX(MB_REG_WIN_AUTOINC)] = win.autoinc;
#endif

#if defined(MB_REG_FLASH_LO) && defined(SF_TOTAL_SIZE)
  mb_u_val[MB_U_IDX(MB_REG_FLASH_LO)] = (uint16_t)((uint32_t)SF_TOTAL_SIZE & 0xFFFFu);
#endif

#if defined(MB_REG_FLASH_HI) && defined(SF_TOTAL_SIZE)
  mb_u_val[MB_U_IDX(MB_REG_FLASH_HI)] = (uint16_t)((uint32_t)SF_TOTAL_SIZE >> 16);
#endif

  mb_u_sync_needed = 0u;
}

static void mb_u_prepare_read(void)
{
  Command_vector_ExecutePendingActions();

  if (mb_u_sync_needed != 0u)
  {
    mb_u_sync();
  }
}

static uint8_t ctrl_reg_read(uint16_t addr, uint16_t *val)
{
  if (val == 0)
  {
    return 0u;
  }

  if (!MB_U_IN_RANGE(addr))
  {
    return 0u;
  }

  mb_u_prepare_read();

  *val = mb_u_val[MB_U_IDX(addr)];
  return 1u;
}

static uint8_t ctrl_reg_write(uint16_t addr, uint16_t val)
{
  const mb_u_item_t *item;
  uint16_t idx;

  if (!MB_U_IN_RANGE(addr))
  {
    return 0u;
  }

  idx = MB_U_IDX(addr);
  item = mb_u_find(addr);

  if (item != 0 && item->acc == MB_U_RO)
  {
    return 0u;
  }

  if (item != 0 && item->acc == MB_U_CMD)
  {
    if (val == 0u)
    {
      mb_u_val[idx] = 0u;
      return 1u;
    }

    if (item->act == MB_U_ACT_NONE)
    {
      mb_u_val[idx] = val;
      return 1u;
    }

    if (mb_u_push(item->act, item->arg, item->ptr, val) == 0)
    {
      return 0u;
    }

    mb_u_val[idx] = 0u;
    return 1u;
  }

  mb_u_val[idx] = val;

  if (item != 0 && item->act != MB_U_ACT_NONE)
  {
    if (mb_u_push(item->act, item->arg, item->ptr, val) == 0)
    {
      return 0u;
    }
  }

  return 1u;
}
/* ==== MB_U_CANONICAL_END ==== */

