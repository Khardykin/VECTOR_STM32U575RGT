/**
  ******************************************************************************
  * @file    Command_vector.c
  * @brief   Загрузка данных во внешнюю SPI flash по COM. См. Command_vector.h.
  *          Приём: USART3_IRQHandler -> InputBuffer[TYPE_USART] (кольцо 254 Б),
  *          разбор: сильное command_message() из receiver_task (10 мс).
  ******************************************************************************
  */
#include <string.h>
#include "Command_vector.h"
#include "spiflash.h"
#include "audio_image.h"
#include <stddef.h>
#include "shared_types.h"
#include "main_vector.h"
#include "Config_save_read.h"
#include "sfmap.h"

#define CMD_SOUNDS_LIMIT    0x00400000u   /* sfmap.h: выше - CONFIG/CFG_DEF/LOG */
#define CMD_ERASE_KEY       0xA5u

static cmd_flash_pause_t s_pause  = 0;
static cmd_commit_t      s_commit = 0;
static cmd_tx_t          s_tx     = 0;
static uint8_t           s_rx[CMD_MAX_FRAME];
static uint16_t          s_rx_len = 0u;
static uint16_t          s_expect = 0u;
static uint8_t           s_ready  = 0u;
static uint32_t          s_rx_ms  = 0u;
static uint8_t           s_session = 0u;
static uint32_t          s_sess_ms = 0u;

void Command_vector_SetPauseHook(cmd_flash_pause_t h) { s_pause = h; }
void Command_vector_SetCommitHook(cmd_commit_t h)     { s_commit = h; }
void Command_vector_SetTx(cmd_tx_t h)                 { s_tx = h; }

uint16_t Command_vector_CRC16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFu; uint16_t i; uint8_t b;
  for (i = 0u; i < len; i++) {
    crc ^= (uint16_t)data[i];
    for (b = 0u; b < 8u; b++) {
      if ((crc & 1u) != 0u) { crc = (uint16_t)((crc >> 1) ^ 0xA001u); }
      else                  { crc = (uint16_t)(crc >> 1); }
    }
  }
  return crc;
}
static uint32_t rd32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3]; }
static void wr32(uint8_t *p,uint32_t v){ p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v; }
static void wr16(uint8_t *p,uint16_t v){ p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
static uint16_t finish(uint8_t *rsp,uint16_t len)
{
  uint16_t crc = Command_vector_CRC16(rsp,len);
  rsp[len]=(uint8_t)(crc&0xFFu); rsp[len+1u]=(uint8_t)(crc>>8);
  return (uint16_t)(len+2u);
}
static uint16_t err(uint8_t *rsp,uint8_t slave,uint8_t cmd,uint8_t code)
{ rsp[0]=slave; rsp[1]=(uint8_t)(cmd|0x80u); rsp[2]=code; return finish(rsp,3u); }

/* Программируем постранично: sf_program может не делить буфер сам. */
static uint8_t program_pages(uint32_t addr,const uint8_t *src,uint32_t len)
{
  while (len != 0u) {
    uint32_t room = SF_PAGE_SIZE - (addr % SF_PAGE_SIZE);
    uint32_t n = (len < room) ? len : room;
    if (sf_program(addr,src,n) != HAL_OK) { return CMD_ERR_IO; }
    addr += n; src += n; len -= n;
  }
  return 0u;
}


/* ==========================================================================
 * MODBUS-регистры прибора (как Avis Command.c).
 *   0x03 - чтение holding  -> SNS_CFG.Config_common
 *   0x04 - чтение input    -> sensors_status
 *   0x10 - запись группы holding
 * Адрес КАЖДОГО регистра задан в таблице явно; между группами оставлены
 * ЗАЗОРЫ - чтение сквозь дыру отбивается кодом 02 (illegal data address),
 * поэтому "прочитать всё одним запросом" нельзя.
 * u32 занимает 2 регистра: младшее слово по меньшему адресу (как в Avis).
 * float отдаётся целым x1000 (мг, м°C, м%), u8 - словом.
 * ========================================================================== */
#define MB_MAX_REGS      125u
#define MB_MAX_BYTES     250u
#define MB_DIV           1000.0f

enum
{
  T_U16 = 0,   /* uint16_t, 1 регистр                          */
  T_U32,       /* uint32_t, 2 регистра (LO, HI)                */
  T_U8,        /* uint8_t,  1 регистр (старший байт = 0)       */
  T_FLT,       /* float,    1 регистр, значение x1000          */
  T_STR        /* массив байт, len/2 регистров                 */
};

typedef struct
{
  uint16_t reg;   /* адрес первого регистра Modbus            */
  uint16_t off;   /* offsetof поля в структуре                */
  uint8_t  type;  /* T_*                                      */
  uint8_t  len;   /* размер в байтах                          */
} reg_map_t;

#define CM(r, f)        { (r), (uint16_t)offsetof(SNS_CFG_Type_common, f), T_U16, 2u }
#define CM32(r, f)      { (r), (uint16_t)offsetof(SNS_CFG_Type_common, f), T_U32, 4u }
#define CMSTR(r, f, n)  { (r), (uint16_t)offsetof(SNS_CFG_Type_common, f), T_STR, (n) }

/* --- HOLDING: конфигурация (0x03 чтение / 0x10 запись) ------------------- */
static const reg_map_t com_map[] =
{
  CM   (0x0000, Build_type),
  CM32 (0x0001, State),                 /* 0x0001..0x0002 */
  CM32 (0x0003, StateErr),              /* 0x0003..0x0004 */
  /* зазор 0x0005..0x000F */
  CM   (0x0010, TimeLedStatePulse),
  CM   (0x0011, TimeLedStatePeriod),
  /* зазор 0x0012..0x001F */
  CM   (0x0020, battery_charge_volt_max),
  CM   (0x0021, battery_charge_volt_min),
  CM   (0x0022, battery_charge_volt_lim_1),
  CM   (0x0023, battery_charge_volt_lim_2),
  /* зазор 0x0024..0x002F */
  CM   (0x0030, SerialLo),
  CM   (0x0031, SerialHi),
  CM   (0x0032, HardwareVersion),
  CM32 (0x0033, ProductionDate),        /* 0x0033..0x0034 */
  /* зазор 0x0035..0x003F */
  CM32 (0x0040, Gps_type_nav),          /* 0x0040..0x0041 */
  CM   (0x0042, PeriodTimeLora),
  CM   (0x0043, Lora_Config_Flags),
  CM32 (0x0044, Lora_freq_rx2),         /* 0x0044..0x0045 */
  CM   (0x0046, Lora_dr_rx2),
  CM   (0x0047, current_build_type),
  /* зазор 0x0048..0x004F */
  CM   (0x0050, Immobility_Enable),
  CM   (0x0051, Immobility_Window_s),
  CM   (0x0052, Immobility_PreAlarm_s),
  CM   (0x0053, Immobility_Ths_mg),
  CM   (0x0054, Devices_Expected),
  CM   (0x0055, Alert_Flags),
  /* зазор 0x0056..0x005F */
  CMSTR(0x0060, Reserve, 8u),           /* 0x0060..0x0063 */
};

#define IM(r, f)     { (r), (uint16_t)offsetof(sensors_status_t, f), T_U16, 2u }
#define IM32(r, f)   { (r), (uint16_t)offsetof(sensors_status_t, f), T_U32, 4u }
#define IM8(r, f)    { (r), (uint16_t)offsetof(sensors_status_t, f), T_U8,  1u }
#define IMF(r, f)    { (r), (uint16_t)offsetof(sensors_status_t, f), T_FLT, 4u }

/* --- INPUT: измерения (0x04, только чтение) ------------------------------ */
static const reg_map_t in_map[] =
{
  IM32 (0x0000, cnt_init_ok),
  IM32 (0x0002, cnt_bme_reads),
  IM32 (0x0004, cnt_accel_reads),
  IM32 (0x0006, cnt_battery_reads),
  IM32 (0x0008, cnt_rotation_events),
  IM32 (0x000A, cnt_read_ms),
  IM32 (0x000C, last_read_ms),
  /* зазор 0x000E..0x000F */
  IMF  (0x0010, Accel_x),               /* мг */
  IMF  (0x0011, Accel_y),
  IMF  (0x0012, Accel_z),
  IM8  (0x0013, Orientation),
  IM8  (0x0014, Screen_rotation),
  IM8  (0x0015, fall_detected),
  IM32 (0x0016, cnt_fall_events),       /* 0x0016..0x0017 */
  /* зазор 0x0018..0x001F */
  IM   (0x0020, battery_charge_percent),
  IM   (0x0021, battery_charge_volt),
  IM8  (0x0022, Sensors_ok),
  IM8  (0x0023, Sensors_reserve),
  /* зазор 0x0024..0x002F */
  IMF  (0x0030, Temperature_calib_offset),
  IMF  (0x0031, Temperature),           /* м°C  */
  IMF  (0x0032, Humidity),              /* м%   */
  IMF  (0x0033, Pressure),              /* мкПа */
};

/* Сколько регистров занимает запись карты. */
static uint16_t get_reg_span(const reg_map_t *m)
{
  switch (m->type)
  {
    case T_U32:
      return 2u;                               /* LO + HI              */
    case T_STR:
      return (uint16_t)((m->len + 1u) / 2u);   /* массив байт -> слова */
    default:
      return 1u;                               /* T_U16, T_U8, T_FLT   */
  }
}

/* Поиск записи по адресу; roff - номер регистра внутри записи. */
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

/* Чтение одного регистра записи. input != 0 - поля volatile (sensors_status). */
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
        if (v >  32000.0f) { v =  32000.0f; }
        if (v < -32000.0f) { v = -32000.0f; }
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

/* Запись одного регистра записи (только holding). */
static void reg_set(void *base, const reg_map_t *m, uint16_t roff, uint16_t val)
{
  uint8_t *p = (uint8_t *)base + m->off;

  if (m->type == T_U32)
  {
    uint32_t v = *(uint32_t *)p;

    v = (roff == 0u) ? ((v & 0xFFFF0000u) | (uint32_t)val)
                     : ((v & 0x0000FFFFu) | ((uint32_t)val << 16));
    *(uint32_t *)p = v;
  }
  else
  {
    *(uint16_t *)(p + (roff * 2u)) = val;
  }
}
/* --- Управляющие регистры: запись 1 = действие, статус читается отдельно -- */
#define MB_REG_ERASE_SOUNDS   0x00F0u
#define MB_REG_ERASE_CONFIG   0x00F1u
#define MB_REG_ERASE_CFGDEF   0x00F2u
#define MB_REG_ERASE_LOG      0x00F3u
#define MB_REG_ERASE_LOGPTR   0x00F4u
#define MB_REG_ERASE_FW       0x00F5u
#define MB_REG_ERASE_BUSY     0x00F6u   /* RO: маска областей в работе  */
#define MB_REG_ERASE_ERR      0x00F7u   /* RO: маска областей с ошибкой */
#define MB_REG_ERASE_PROGRESS 0x00F8u   /* RO: % текущей области        */
#define MB_REG_SAVE_CFG       0x00F9u
#define MB_REG_SAVE_CFGDEF    0x00FAu
#define MB_REG_LOAD_CFGDEF    0x00FBu

#define ERASE_BIT_SOUNDS 0x01u
#define ERASE_BIT_CONFIG 0x02u
#define ERASE_BIT_CFGDEF 0x04u
#define ERASE_BIT_LOG    0x08u
#define ERASE_BIT_LOGPTR 0x10u
#define ERASE_BIT_FW     0x20u

static uint8_t  s_erase_req  = 0u;
static uint8_t  s_erase_busy = 0u;
static uint8_t  s_erase_err  = 0u;
static uint8_t  s_erase_bit  = 0u;
static uint8_t  s_erase_pct  = 0u;
static uint32_t s_erase_cur  = 0u;
static uint32_t s_erase_end  = 0u;
static uint32_t s_erase_from = 0u;

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

static void erase_start(uint8_t bit)
{
  uint8_t i;

  if ((s_erase_busy != 0u) || (s_erase_req != 0u))
  {
    return;                       /* одна область за раз, остальные в очереди */
  }
  for (i = 0u; i < (uint8_t)(sizeof(s_areas) / sizeof(s_areas[0])); i++)
  {
    if (s_areas[i].bit == bit)
    {
      s_erase_bit  = bit;
      s_erase_from = s_areas[i].base;
      s_erase_cur  = s_areas[i].base;
      s_erase_end  = s_areas[i].base + s_areas[i].size;
      s_erase_busy = bit;
      s_erase_pct  = 0u;
      s_erase_err &= (uint8_t)~bit;
      if (s_pause != 0)
      {
        s_pause(1u);              /* плеер не читает flash во время стирания */
      }
      return;
    }
  }
}

/* Один сектор за вызов: receiver_task не должен стоять секунды. */
void Command_vector_EraseService(void)
{
  if (s_erase_busy == 0u)
  {
    if (s_erase_req != 0u)
    {
      uint8_t bit = (uint8_t)(s_erase_req & (uint8_t)(-(int8_t)s_erase_req));
      s_erase_req &= (uint8_t)~bit;
      erase_start(bit);
    }
    return;
  }

  if (sf_sector_erase(s_erase_cur) != HAL_OK)
  {
    s_erase_err |= s_erase_bit;
  }
  s_erase_cur += SF_SECTOR_SIZE;

  if (s_erase_cur >= s_erase_end)
  {
    s_erase_busy = 0u;
    s_erase_bit  = 0u;
    s_erase_pct  = 100u;
    if (s_pause != 0)
    {
      s_pause(0u);
    }
    return;
  }
  s_erase_pct = (uint8_t)(((s_erase_cur - s_erase_from) * 100u) / (s_erase_end - s_erase_from));
}

static uint8_t mb_ctrl_read(uint16_t addr, uint16_t *val)
{
  switch (addr)
  {
    case MB_REG_ERASE_SOUNDS:
    case MB_REG_ERASE_CONFIG:
    case MB_REG_ERASE_CFGDEF:
    case MB_REG_ERASE_LOG:
    case MB_REG_ERASE_LOGPTR:
    case MB_REG_ERASE_FW:
    case MB_REG_SAVE_CFG:
    case MB_REG_SAVE_CFGDEF:
    case MB_REG_LOAD_CFGDEF:
      *val = 0u;
      return 1u;
    case MB_REG_ERASE_BUSY:
      *val = (uint16_t)(s_erase_busy | s_erase_req);
      return 1u;
    case MB_REG_ERASE_ERR:
      *val = (uint16_t)s_erase_err;
      return 1u;
    case MB_REG_ERASE_PROGRESS:
      *val = (uint16_t)s_erase_pct;
      return 1u;
    default:
      return 0u;
  }
}

static uint8_t mb_ctrl_write(uint16_t addr, uint16_t val)
{
  if (val == 0u)
  {
    return 1u;                    /* 0 - ничего не делаем */
  }
  switch (addr)
  {
    case MB_REG_ERASE_SOUNDS: s_erase_req |= ERASE_BIT_SOUNDS; return 1u;
    case MB_REG_ERASE_CONFIG: s_erase_req |= ERASE_BIT_CONFIG; return 1u;
    case MB_REG_ERASE_CFGDEF: s_erase_req |= ERASE_BIT_CFGDEF; return 1u;
    case MB_REG_ERASE_LOG:    s_erase_req |= ERASE_BIT_LOG;    return 1u;
    case MB_REG_ERASE_LOGPTR: s_erase_req |= ERASE_BIT_LOGPTR; return 1u;
    case MB_REG_ERASE_FW:     s_erase_req |= ERASE_BIT_FW;     return 1u;
    case MB_REG_SAVE_CFG:     SaveConfig        = 1u;          return 1u;
    case MB_REG_SAVE_CFGDEF:  SaveConfigDefault = 1u;          return 1u;
    case MB_REG_LOAD_CFGDEF:  ReadConfigDefault = 1u;          return 1u;
    default:
      return 0u;
  }
}

/* --- 0x03 / 0x04 --------------------------------------------------------- */
static uint16_t mb_read(const uint8_t *req, uint8_t *rsp, uint16_t cap, uint8_t input)
{
  uint16_t        addr = (uint16_t)(((uint16_t)req[2] << 8) | req[3]);
  uint16_t        cnt  = (uint16_t)(((uint16_t)req[4] << 8) | req[5]);
  const reg_map_t *map;
  const void     *base;
  uint16_t        n, i, out = 3u;

  if ((cnt == 0u) || (cnt > MB_MAX_REGS) || (cap < (uint16_t)(3u + (cnt * 2u) + 2u)))
  {
    return err(rsp, req[0], req[1], CMD_ERR_VALUE);
  }

  if (input != 0u)
  {
    map  = in_map;
    n    = (uint16_t)(sizeof(in_map) / sizeof(in_map[0]));
    base = (const void *)&sensors_status;
  }
  else
  {
    map  = com_map;
    n    = (uint16_t)(sizeof(com_map) / sizeof(com_map[0]));
    base = (const void *)&Sns_Cfg_struct.Config_common;
  }

  /* Фаза 1: весь диапазон должен существовать - зазоры запрещены (Avis). */
  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t roff, v;

    if (reg_find(map, n, a, &roff) >= 0)
    {
      continue;
    }
    if ((input == 0u) && (mb_ctrl_read(a, &v) != 0u))
    {
      continue;
    }
    return err(rsp, req[0], req[1], CMD_ERR_ADDR);
  }

  /* Фаза 2: сбор данных. */
  rsp[0] = req[0];
  rsp[1] = req[1];
  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t roff, v = 0u;
    int16_t  idx = reg_find(map, n, a, &roff);

    if (idx >= 0)
    {
      v = reg_get(base, &map[idx], roff, input);
    }
    else if (input == 0u)
    {
      (void)mb_ctrl_read(a, &v);
    }
    rsp[out++] = (uint8_t)(v >> 8);
    rsp[out++] = (uint8_t)v;
  }
  rsp[2] = (uint8_t)(cnt * 2u);
  return finish(rsp, out);
}

/* --- 0x10 ---------------------------------------------------------------- */
static uint16_t mb_write(const uint8_t *req, uint16_t req_len, uint8_t *rsp)
{
  uint16_t addr = (uint16_t)(((uint16_t)req[2] << 8) | req[3]);
  uint16_t cnt  = (uint16_t)(((uint16_t)req[4] << 8) | req[5]);
  uint8_t  nb   = req[6];
  uint16_t n    = (uint16_t)(sizeof(com_map) / sizeof(com_map[0]));
  uint16_t i, roff;
  uint8_t  touched = 0u;

  if ((cnt == 0u) || (cnt > MB_MAX_REGS) || (nb != (uint8_t)(cnt * 2u)) ||
      ((uint32_t)req_len != (9u + (uint32_t)nb)))
  {
    return err(rsp, req[0], req[1], CMD_ERR_VALUE);
  }

  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t v;

    if (reg_find(com_map, n, a, &roff) >= 0)
    {
      continue;
    }
    if (mb_ctrl_read(a, &v) != 0u)
    {
      continue;
    }
    return err(rsp, req[0], req[1], CMD_ERR_ADDR);
  }

  Sys_Cfg_Lock();
  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t v = (uint16_t)(((uint16_t)req[7 + (i * 2u)] << 8) | req[8 + (i * 2u)]);
    int16_t  idx = reg_find(com_map, n, a, &roff);

    if (idx >= 0)
    {
      reg_set(&Sns_Cfg_struct.Config_common, &com_map[idx], roff, v);
      touched = 1u;
    }
    else
    {
      (void)mb_ctrl_write(a, v);
    }
  }
  if (touched != 0u)
  {
    SaveConfig = 1u;              /* конфиг изменился - запишется во flash */
  }
  Sys_Cfg_Unlock();

  memcpy(rsp, req, 6u);
  return finish(rsp, 6u);
}

uint16_t Command_vector_Frame(const uint8_t *req,uint16_t req_len,uint8_t *rsp,uint16_t rsp_cap)
{
  uint8_t slave,cmd; uint32_t addr; uint16_t size,cc,cr;
  if ((req==0)||(rsp==0)||(req_len < 5u)) { return 0u; }
  if (rsp_cap < 5u) { return 0u; }
  cc = Command_vector_CRC16(req,(uint16_t)(req_len-2u));
  cr = (uint16_t)req[req_len-2u] | ((uint16_t)req[req_len-1u]<<8);
  if (cc != cr) { return 0u; }
  slave=req[0]; cmd=req[1];
  /* Modbus: свой формат кадра (addr16/cnt16), не addr32/size16. */
  if ((cmd==CMD_OP_READ_HOLD)||(cmd==CMD_OP_READ_IN)) {
    if (req_len != 8u) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    return mb_read(req,rsp,rsp_cap,(uint8_t)((cmd==CMD_OP_READ_IN)?1u:0u));
  }
  if (cmd==CMD_OP_WRITE_MULTI) {
    if (req_len < 9u) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    return mb_write(req,req_len,rsp);
  }
  if (req_len < (CMD_FRAME_HDR+2u)) { return 0u; }
  addr=rd32(&req[2]);
  size=(uint16_t)(((uint16_t)req[6]<<8)|req[7]);
  if ((uint32_t)req_len != (CMD_FRAME_HDR+(uint32_t)size+2u)) { return 0u; }
  memcpy(rsp,req,CMD_FRAME_HDR);          /* эхо шапки - как в Avis CmdFunc15 */

  switch (cmd) {
  case CMD_OP_INFO: {
    audio_img_header_t h; audio_img_entry_t e; uint8_t ok=0u; uint16_t rate=0u;
    if (rsp_cap < (CMD_FRAME_HDR+23u+2u)) { return 0u; }
    memset(&h,0,sizeof(h)); memset(&e,0,sizeof(e));
    if (sf_read(AUDIO_IMG_BASE_ADDR,(uint8_t*)&h,sizeof(h)) == HAL_OK) {
      ok = (h.magic == AUDIO_IMG_MAGIC) ? 1u : 0u;
      if ((ok!=0u)&&(h.count!=0u)) {
        (void)sf_read(AUDIO_IMG_BASE_ADDR+h.table_off,(uint8_t*)&e,sizeof(e));
        rate = e.rate;
      }
    }
    wr32(&rsp[8],(uint32_t)SF_TOTAL_SIZE); wr16(&rsp[12],(uint16_t)SF_SECTOR_SIZE);
    wr16(&rsp[14],(uint16_t)SF_PAGE_SIZE); wr32(&rsp[16],h.magic);
    wr16(&rsp[20],h.version); wr16(&rsp[22],h.count); wr32(&rsp[24],h.total_size);
    wr16(&rsp[28],rate); rsp[30]=ok; wr16(&rsp[6],23u);
    return finish(rsp,(uint16_t)(CMD_FRAME_HDR+23u));
  }
  case CMD_OP_COMMIT:
    if (s_commit != 0) { s_commit(); }
    wr16(&rsp[6],0u); return finish(rsp,CMD_FRAME_HDR);

  case CMD_OP_ERASE: {
    uint32_t len,first,last,a;
    if (size != 5u) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    len = rd32(&req[8]);
    if ((len==0u)||(addr>=(uint32_t)SF_TOTAL_SIZE)||(len>((uint32_t)SF_TOTAL_SIZE-addr)))
    { return err(rsp,slave,cmd,CMD_ERR_RANGE); }
    if (((addr+len)>CMD_SOUNDS_LIMIT)&&(req[12]!=CMD_ERASE_KEY))
    { return err(rsp,slave,cmd,CMD_ERR_PROTECTED); }
    first = addr-(addr%SF_SECTOR_SIZE);
    last  = (addr+len-1u); last = last-(last%SF_SECTOR_SIZE);
    if (s_pause!=0) { s_pause(1u); }
    for (a=first;a<=last;a+=SF_SECTOR_SIZE) {
      if (sf_sector_erase(a)!=HAL_OK) {
        if (s_pause!=0) { s_pause(0u); }
        return err(rsp,slave,cmd,CMD_ERR_IO);
      }
    }
    if (s_pause!=0) { s_pause(0u); }
    wr16(&rsp[6],0u); return finish(rsp,CMD_FRAME_HDR);
  }
  case CMD_OP_WRITE: {
    uint8_t rc;
    if ((size==0u)||(size>CMD_MAX_PAYLOAD)) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    if ((addr>=(uint32_t)SF_TOTAL_SIZE)||((uint32_t)size>((uint32_t)SF_TOTAL_SIZE-addr)))
    { return err(rsp,slave,cmd,CMD_ERR_RANGE); }
    /* Запись вне SOUNDS разрешена - той же командой льём и другие данные. */
    if (s_pause!=0) { s_pause(1u); }
    rc = program_pages(addr,&req[8],size);
    if (rc==0u) { if (sf_verify(addr,&req[8],size)!=HAL_OK) { rc=CMD_ERR_VERIFY; } }
    if (s_pause!=0) { s_pause(0u); }
    if (rc!=0u) { return err(rsp,slave,cmd,rc); }
    wr16(&rsp[6],0u); return finish(rsp,CMD_FRAME_HDR);
  }
  case CMD_OP_READ: {
    uint16_t want;
    if (size!=2u) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    want=(uint16_t)(((uint16_t)req[8]<<8)|req[9]);
    if ((want==0u)||(want>CMD_MAX_PAYLOAD)) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    if (rsp_cap < (uint16_t)(CMD_FRAME_HDR+want+2u)) { return err(rsp,slave,cmd,CMD_ERR_LENGTH); }
    if ((addr>=(uint32_t)SF_TOTAL_SIZE)||((uint32_t)want>((uint32_t)SF_TOTAL_SIZE-addr)))
    { return err(rsp,slave,cmd,CMD_ERR_RANGE); }
    if (sf_read(addr,&rsp[CMD_FRAME_HDR],want)!=HAL_OK) { return err(rsp,slave,cmd,CMD_ERR_IO); }
    wr16(&rsp[6],want); return finish(rsp,(uint16_t)(CMD_FRAME_HDR+want));
  }
  default: return err(rsp,slave,cmd,CMD_ERR_UNKNOWN);
  }
}

/* Накопитель: конец кадра - по длине из шапки (size16), а НЕ по паузе в линии:
   поток зовёт нас каждые 10 мс, и тайминг байтов недоступен. CMD_RESYNC_MS
   нужен только чтобы выбросить недобитый кадр.                             */
void Command_vector_RxFeed(const uint8_t *data,uint16_t len,uint32_t now_ms)
{
  uint16_t i;
  if ((data==0)||(len==0u)) { return; }
  for (i=0u;i<len;i++) {
    if (s_ready!=0u) { break; }                 /* прошлый кадр ещё не разобран */
    if (s_rx_len>=CMD_MAX_FRAME) { s_rx_len=0u; s_expect=0u; }
    s_rx[s_rx_len++]=data[i];
    if (s_rx_len==2u) {                    /* Modbus 3/4: кадр всегда 8 Б */
      uint8_t op=s_rx[1];
      if ((op==CMD_OP_READ_HOLD)||(op==CMD_OP_READ_IN)) { s_expect=8u; }
    }
    if ((s_rx_len==7u)&&(s_rx[1]==CMD_OP_WRITE_MULTI)) {   /* Modbus 16: 9+nb */
      uint8_t nb=s_rx[6];
      if (nb>MB_MAX_BYTES) { s_rx_len=0u; s_expect=0u; }
      else { s_expect=(uint16_t)(9u+nb); }
    }
    if ((s_rx_len==CMD_FRAME_HDR)&&(s_expect==0u)) {       /* flash-операции */
      uint16_t sz=(uint16_t)(((uint16_t)s_rx[6]<<8)|s_rx[7]);
      if (sz>CMD_MAX_PAYLOAD) { s_rx_len=0u; s_expect=0u; }
      else { s_expect=(uint16_t)(CMD_FRAME_HDR+sz+2u); }
    }
    if ((s_expect!=0u)&&(s_rx_len>=s_expect)) { s_ready=1u; s_expect=0u; }
  }
  s_rx_ms=now_ms;
}

void Command_vector_Poll(uint32_t now_ms)
{
  uint8_t rsp[CMD_MAX_FRAME]; uint16_t n;
  if (s_ready!=0u) {
    uint8_t op = s_rx[1];
    if (s_session==0u) { s_session=1u; Command_vector_LogMute(1u); }
    s_sess_ms = now_ms;
    n = Command_vector_Frame(s_rx,s_rx_len,rsp,(uint16_t)sizeof(rsp));
    s_rx_len=0u; s_expect=0u; s_ready=0u;
    if ((n!=0u)&&(s_tx!=0)) { s_tx(rsp,n); }
    if (op==CMD_OP_COMMIT) { s_session=0u; Command_vector_LogMute(0u); }
    return;
  }
  if ((s_rx_len!=0u)&&((uint32_t)(now_ms-s_rx_ms)>=CMD_RESYNC_MS))
  { s_rx_len=0u; s_expect=0u; }
  if ((s_session!=0u)&&((uint32_t)(now_ms-s_sess_ms)>=CMD_SESSION_IDLE_MS))
  { s_session=0u; Command_vector_LogMute(0u); }
}

uint8_t Command_vector_SessionActive(void) { return s_session; }
__attribute__((weak)) void Command_vector_LogMute(uint8_t mute) { (void)mute; }

#if CMD_VECTOR_COM
#include "buffer.h"
#include "audio_player.h"
#include "vector_tick.h"

static uint8_t s_wired = 0u;

static void cmd_tx_com(const uint8_t *d,uint16_t n)
{ transmit_buffer((uint8_t*)d,n,TYPE_USART); }

static void cmd_pause_com(uint8_t pause)
{ if (pause!=0u) { audio_stop(); } }   /* образ читать нельзя, пока пишем/стираем */

static void cmd_commit_com(void)
{ audio_reload_image(); }              /* плеер перечитает заголовок после заливки */

/* Сильное определение вместо weak-заглушки main_vector.c: вызывается из
   receiver_task каждые VECTOR_TASKS_RECEIVER_DELAY_MS (10 мс). Кольцо
   InputBuffer[TYPE_USART] наполняет USART3_IRQHandler - здесь только читаем.
   Кольцо 254 Б при 115200 заполняется за ~22 мс, вычитываем его целиком.   */
void command_message(void)
{
  uint8_t  batch[64];
  uint16_t n = 0u;
  uint8_t  b;
  uint32_t now = VTICK_MS();

  if (s_wired==0u) {
    Command_vector_SetTx(cmd_tx_com);
    Command_vector_SetPauseHook(cmd_pause_com);
    Command_vector_SetCommitHook(cmd_commit_com);
    s_wired = 1u;
  }
  while (check_buffer(&InputBuffer[TYPE_USART])!=0u) {
    if (receive_buffer(&InputBuffer[TYPE_USART],&b)==0u) { break; }
    batch[n++]=b;
    if (n==(uint16_t)sizeof(batch)) { Command_vector_RxFeed(batch,n,now); n=0u; }
  }
  if (n!=0u) { Command_vector_RxFeed(batch,n,now); }
  Command_vector_Poll(now);
  Command_vector_EraseService();   /* 1 сектор за проход */
}
#endif
