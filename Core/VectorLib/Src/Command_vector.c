#include <string.h>
#include <stdint.h>

#include "Command_vector.h"
#include "main_vector.h"
#include "shared_macros.h"
#include "Config_save_read.h"
#include "extstore.h"
#include "sfmap.h"
#include "boot_flash.h"
#include "fw_update.h"
#include "audio_player.h"
#include "buffer.h"

/* ==========================================================================
 * Backing objects (declared by the project headers):
 *   Sns_Cfg_struct, Connected_device, tempsensor_calib, sys_state
 *   sensors_status; appkey / deveui_com only when CONFIG_LORA
 * ========================================================================== */

cmd_state_t cmd_state;


/* ==========================================================================
 * LoRa key fields.
 *
 * appkey[16] and deveui_com[8] are defined by the LoRa module and exist only
 * when CONFIG_LORA == 1. With LoRa off the register map must stay intact, so
 * the fields are backed by local RAM stubs: reads return zero, writes are
 * accepted but not persisted. Sizes come from the protocol (CV_*_SIZE).
 * ========================================================================== */
#if CONFIG_LORA

#define CV_APPKEY_PTR   appkey
#define CV_DEVEUI_PTR   deveui_com

typedef char cv_appkey_size_chk[(sizeof(appkey)     == CV_APPKEY_SIZE) ? 1 : -1];
typedef char cv_deveui_size_chk[(sizeof(deveui_com) == CV_DEVEUI_SIZE) ? 1 : -1];

#else

static uint8_t cv_appkey_stub[CV_APPKEY_SIZE];
static uint8_t cv_deveui_stub[CV_DEVEUI_SIZE];

#define CV_APPKEY_PTR   cv_appkey_stub
#define CV_DEVEUI_PTR   cv_deveui_stub

#endif /* CONFIG_LORA */

/* ==========================================================================
 * com_map - holding registers, sequential from 0x0001.
 * One map covers all configuration structures via the obj selector.
 * ========================================================================== */

#define CM(f,a,t)  { (uint16_t)(a), (uint16_t)offsetof(SNS_CFG_Type_common,f), (t), 0u, OBJ_COMMON }
#define AR(f,a,t)  { (uint16_t)(a), (uint16_t)offsetof(SNS_Archive_Type,f),    (t), 0u, OBJ_ARCHIVE }
#define WK(f,a,t)  { (uint16_t)(a), (uint16_t)offsetof(SNS_Working_Type,f),    (t), 0u, OBJ_WORKING }
#define TC(f,a,t)  { (uint16_t)(a), (uint16_t)offsetof(TEMPSENSOR_CALIB,f),    (t), 0u, OBJ_TEMPCAL }
#define CS(f,a,t)  { (uint16_t)(a), (uint16_t)offsetof(cmd_state_t,f),         (t), 0u, OBJ_CMD }
#define SS(f,a,t)  { (uint16_t)(a), (uint16_t)offsetof(sys_state_t,f),         (t), 0u, OBJ_SYS }
#define ST(f,a,t)  { (uint16_t)(a), (uint16_t)offsetof(sensors_status_t,f),    (t), 0u, OBJ_SENSORS }
#define DV(f,a,t)  { (uint16_t)(a), (uint16_t)offsetof(SNS_Dev_Type,f),        (t), 0u, OBJ_DEVICE }

/* Arrays: element count is the only extra datum. */
#define TC_N(f,a,t,n) { (uint16_t)(a), (uint16_t)offsetof(TEMPSENSOR_CALIB,f), (t), (n), OBJ_TEMPCAL }
#define ST_N(f,a,t,n) { (uint16_t)(a), (uint16_t)offsetof(sensors_status_t,f), (t), (n), OBJ_SENSORS }
#define ARR(a,t,n,obj) { (uint16_t)(a), 0u, (t), (n), (obj) }

static const reg_map_t com_map[] =
{
  CM(Build_type,                0x0001, T_U16),
  CM(current_build_type,        0x0002, T_U16),
  CM(State,                     0x0003, T_U32),
  CM(StateErr,                  0x0005, T_U32),
  CM(SerialLo,                  0x0007, T_U16),
  CM(SerialHi,                  0x0008, T_U16),
  CM(HardwareVersion,           0x0009, T_U16),
  CM(ProductionDate,            0x000A, T_U32),
  CM(TimeLedStatePulse,         0x000C, T_U16),
  CM(TimeLedStatePeriod,        0x000D, T_U16),
  CM(battery_charge_volt_max,   0x000E, T_U16),
  CM(battery_charge_volt_min,   0x000F, T_U16),
  CM(battery_charge_volt_lim_1, 0x0010, T_U16),
  CM(battery_charge_volt_lim_2, 0x0011, T_U16),
  CM(Gps_type_nav,              0x0012, T_U32),
  CM(PeriodTimeLora,            0x0014, T_U16),
  CM(Lora_Config_Flags,         0x0015, T_U16),
  CM(Lora_freq_rx2,             0x0016, T_U32),
  CM(Lora_dr_rx2,               0x0018, T_U16),
  CM(Immobility_Enable,         0x0019, T_U16),
  CM(Immobility_Window_s,       0x001A, T_U16),
  CM(Immobility_PreAlarm_s,     0x001B, T_U16),
  CM(Immobility_Ths_mg,         0x001C, T_U16),
  CM(Devices_Expected,          0x001D, T_U16),
  CM(Alert_Flags,               0x001E, T_U16),

  WK(working_hours,             0x0020, T_U32),
  WK(working_hours_offset,      0x0022, T_U32),

  AR(Archiveinterval,           0x0024, T_U16),
  AR(ArchiveRecording,          0x0025, T_U8),
  AR(BegginAddrFile,            0x0026, T_U32),
  AR(CurrentAddrFile,           0x0028, T_U32),

  { 0x002Au, (uint16_t)offsetof(SNS_CFG, application_language), T_ENUM, 0u, OBJ_CFG },

  TC_N(temperature,             0x002B, T_FLT4, 1),   /* one float  */
  TC(flag,                      0x002D, T_U16),

  ARR(0x002E, T_STR, CV_APPKEY_SIZE, OBJ_APPKEY),          /* 16 bytes   */
  ARR(0x0036, T_STR, CV_DEVEUI_SIZE, OBJ_DEVEUI),          /*  8 bytes   */

  CS(flash_target,              0x0040, T_U16),
  CS(flash_addr,                0x0041, T_U32),
  CS(flash_len,                 0x0043, T_U32),
  CS(flash_key,                 0x0045, T_U32),
  CS(flash_status,              0x0047, T_U16),
  CS(erase_mode,                0x0048, T_U16),
  CS(erase_req,                 0x0049, T_U8),
  CS(save_config,               0x004A, T_U8),
  CS(save_config_default,       0x004B, T_U8),
  CS(load_config_default,       0x004C, T_U8),
  CS(fw_apply_req,              0x004D, T_U8),
  CS(int_write_pending,         0x004E, T_U8),
  CS(ext_write_pending,         0x004F, T_U8),
  CS(write_addr,                0x0050, T_U32),
  CS(write_len,                 0x0052, T_U16),

  SS(soft_reset,                0x0053, T_U8),
  SS(power,                     0x0054, T_ENUM),
  SS(process,                   0x0055, T_ENUM),
  SS(fw_pending,                0x0056, T_U8),
  SS(data_exchange,             0x0057, T_U8),
  SS(block_sound,               0x0058, T_U8),
  CS(audio_reload_pending,      0x0059, T_U8),

  { 0u, 0u, 0u, 0u, 0u }
};

static const reg_map_t in_map[] =
{
  ST(Sensors_ok,                0x0001, T_U8),
  ST(Orientation,               0x0002, T_U8),
  ST(Screen_rotation,           0x0003, T_U8),
  ST(fall_detected,             0x0004, T_U8),
  ST(battery_charge_percent,    0x0005, T_U16),
  ST(battery_charge_volt,       0x0006, T_U16),
  ST_N(Temperature,             0x0007, T_FLT4, 1),
  ST_N(Humidity,                0x0009, T_FLT4, 1),
  ST_N(Pressure,                0x000B, T_FLT4, 1),
  ST_N(Accel_x,                 0x000D, T_FLT4, 1),
  ST_N(Accel_y,                 0x000F, T_FLT4, 1),
  ST_N(Accel_z,                 0x0011, T_FLT4, 1),
  ST_N(Temperature_calib_offset,0x0013, T_FLT4, 1),
  ST(cnt_init_ok,               0x0015, T_U32),
  ST(cnt_bme_reads,             0x0017, T_U32),
  ST(cnt_accel_reads,           0x0019, T_U32),
  ST(cnt_battery_reads,         0x001B, T_U32),
  ST(cnt_rotation_events,       0x001D, T_U32),
  ST(cnt_fall_events,           0x001F, T_U32),
  ST(cnt_read_ms,               0x0021, T_U32),
  ST(last_read_ms,              0x0023, T_U32),
  DV(dev_id,                    0x0025, T_U16),
  DV(State,                     0x0026, T_U32),
  DV(StateErr,                  0x0028, T_U32),
  DV(battery_charge_percent,    0x002A, T_U16),
  DV(HardwareVersion,           0x002B, T_U16),
  DV(FirmwareVersion,           0x002C, T_U16),

  { 0u, 0u, 0u, 0u, 0u }
};

/* ==========================================================================
 * Byte helpers.
 * ========================================================================== */

static uint16_t rd16be(const uint8_t *p)
{
  return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t rd32be(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void wr16be(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static void wr32le(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

/* volatile-safe copy: qualifier dropped through uintptr_t, no warning. */
static void cp_from(volatile const void *src, void *dst, uint32_t n)
{
  memcpy(dst, (const void *)(uintptr_t)src, (size_t)n);
}

static void cp_to(volatile void *dst, const void *src, uint32_t n)
{
  memcpy((void *)(uintptr_t)dst, src, (size_t)n);
}

uint16_t CreateCRC(uint8_t *DataCRC, uint16_t len)
{
  uint16_t crc = 0xFFFFu;
  uint16_t i;
  uint16_t q;

  for (q = 0u; q < len; q++)
  {
    crc ^= (uint16_t)DataCRC[q];
    for (i = 0u; i < 8u; i++)
    {
      if ((crc & 0x0001u) != 0u) { crc = (uint16_t)((crc >> 1) ^ 0xA001u); }
      else                      { crc = (uint16_t)(crc >> 1); }
    }
  }

  DataCRC[len]      = (uint8_t)(crc & 0xFFu);
  DataCRC[len + 1u] = (uint8_t)(crc >> 8);
  return crc;
}

static uint8_t crc_ok(const uint8_t *buf, uint16_t len)
{
  uint8_t  tmp[300];
  uint16_t crc;

  if ((len < 4u) || (len > (uint16_t)sizeof(tmp))) { return 0u; }

  memcpy(tmp, buf, (size_t)(len - 2u));
  crc = CreateCRC(tmp, (uint16_t)(len - 2u));
  (void)crc;

  if (tmp[len - 2u] != buf[len - 2u]) { return 0u; }
  if (tmp[len - 1u] != buf[len - 1u]) { return 0u; }
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
 * Backing object resolution: one place, no per-register code.
 * ========================================================================== */

static volatile void *obj_ptr(uint8_t obj)
{
  switch (obj)
  {
    case OBJ_COMMON:  return (volatile void *)&Sns_Cfg_struct.Config_common;
    case OBJ_ARCHIVE: return (volatile void *)&Sns_Cfg_struct.Config_archive;
    case OBJ_WORKING: return (volatile void *)&Sns_Cfg_struct.Working_data;
    case OBJ_CFG:     return (volatile void *)&Sns_Cfg_struct;
    case OBJ_TEMPCAL: return (volatile void *)&tempsensor_calib;
    case OBJ_APPKEY:  return (volatile void *)CV_APPKEY_PTR;
    case OBJ_DEVEUI:  return (volatile void *)CV_DEVEUI_PTR;
    case OBJ_DEVICE:  return (volatile void *)&Connected_device;
    case OBJ_SENSORS: return (volatile void *)&sensors_status;
    case OBJ_CMD:     return (volatile void *)&cmd_state;
    case OBJ_SYS:     return (volatile void *)&sys_state;
    default:          return 0;
  }
}

static uint16_t reg_regs(const reg_map_t *m)
{
  switch (m->type)
  {
    case T_U8:
    case T_U16:
    case T_ENUM: return 1u;
    case T_U32:  return 2u;
    case T_FLT4: return (uint16_t)(2u * m->n);
    case T_STR:  return (uint16_t)((m->n + 1u) / 2u);
    default:     return 0u;
  }
}

/* Byte size of one element in RAM. */
static uint16_t reg_esz(const reg_map_t *m)
{
  switch (m->type)
  {
    case T_U8:   return 1u;
    case T_U16:  return 2u;
    case T_STR:  return 1u;
    default:     return 4u;   /* T_U32, T_ENUM, T_FLT4 */
  }
}

static const reg_map_t *map_find(const reg_map_t *map, uint16_t addr)
{
  uint16_t i;

  for (i = 0u; map[i].addr != 0u; i++)
  {
    uint16_t regs = reg_regs(&map[i]);

    if (regs == 0u) { continue; }

    if ((addr >= map[i].addr) && (addr < (uint16_t)(map[i].addr + regs)))
    {
      return &map[i];
    }
  }

  return 0;
}

static uint8_t reg_get(const reg_map_t *m, uint16_t addr, uint16_t *val)
{
  volatile uint8_t *p;
  uint16_t k   = (uint16_t)(addr - m->addr);
  uint16_t esz = reg_esz(m);

  p = (volatile uint8_t *)obj_ptr(m->obj);
  if (p == 0) { return 0u; }
  p += m->off;

  switch (m->type)
  {
    case T_U8:
    case T_U16:
    case T_ENUM:
    {
      uint32_t v = 0u;
      cp_from(p, &v, esz);
      *val = (uint16_t)v;
      return 1u;
    }

    case T_U32:
    {
      uint32_t v;
      cp_from(p, &v, esz);
      *val = (k == 0u) ? (uint16_t)(v & 0xFFFFu) : (uint16_t)(v >> 16);
      return 1u;
    }

    case T_FLT4:
    {
      uint16_t idx = (uint16_t)(k / 2u);
      uint32_t v;

      if (idx >= m->n) { return 0u; }

      cp_from(p + (uint32_t)idx * esz, &v, esz);
      *val = ((k & 1u) == 0u) ? (uint16_t)(v & 0xFFFFu) : (uint16_t)(v >> 16);
      return 1u;
    }

    case T_STR:
    {
      uint32_t b = (uint32_t)k * 2u;
      uint8_t hi = 0u;
      uint8_t lo = 0u;

      if (b >= m->n) { return 0u; }

      cp_from(p + b, &hi, 1u);
      if ((b + 1u) < m->n) { cp_from(p + b + 1u, &lo, 1u); }

      *val = (uint16_t)(((uint16_t)hi << 8) | lo);
      return 1u;
    }

    default: return 0u;
  }
}

static uint8_t reg_set(const reg_map_t *m, uint16_t addr, uint16_t val)
{
  volatile uint8_t *p;
  uint16_t k   = (uint16_t)(addr - m->addr);
  uint16_t esz = reg_esz(m);

  p = (volatile uint8_t *)obj_ptr(m->obj);
  if (p == 0) { return 0u; }
  p += m->off;

  switch (m->type)
  {
    case T_U8:
    {
      uint8_t v = (uint8_t)val; cp_to(p, &v, esz); return 1u;
    }

    case T_U16:
      cp_to(p, &val, esz); return 1u;

    case T_ENUM:
    {
      uint32_t v = (uint32_t)val; cp_to(p, &v, esz); return 1u;
    }

    case T_U32:
    {
      uint32_t v;
      cp_from(p, &v, esz);
      v = (k == 0u) ? ((v & 0xFFFF0000u) | (uint32_t)val)
                    : ((v & 0x0000FFFFu) | ((uint32_t)val << 16));
      cp_to(p, &v, esz);
      return 1u;
    }

    case T_FLT4:
    {
      uint16_t idx = (uint16_t)(k / 2u);
      volatile uint8_t *q;
      uint32_t v;

      if (idx >= m->n) { return 0u; }

      q = p + (uint32_t)idx * esz;
      cp_from(q, &v, esz);
      v = ((k & 1u) == 0u) ? ((v & 0xFFFF0000u) | (uint32_t)val)
                           : ((v & 0x0000FFFFu) | ((uint32_t)val << 16));
      cp_to(q, &v, esz);
      return 1u;
    }

    case T_STR:
    {
      uint32_t b = (uint32_t)k * 2u;
      uint8_t hi = (uint8_t)(val >> 8);
      uint8_t lo = (uint8_t)val;

      if (b >= m->n) { return 0u; }

      cp_to(p + b, &hi, 1u);
      if ((b + 1u) < m->n) { cp_to(p + b + 1u, &lo, 1u); }
      return 1u;
    }

    default: return 0u;
  }
}
/* ==========================================================================
 * Register access. No switch by address anywhere.
 * ========================================================================== */

static uint8_t hold_read(uint16_t addr, uint16_t *val)
{
  const reg_map_t *m = map_find(com_map, addr);

  if (m == 0) { return 0u; }
  return reg_get(m, addr, val);
}

static uint8_t hold_write(uint16_t addr, uint16_t val)
{
  const reg_map_t *m = map_find(com_map, addr);

  if (m == 0) { return 0u; }
  return reg_set(m, addr, val);
}

static uint8_t input_read(uint16_t addr, uint16_t *val)
{
  const reg_map_t *m = map_find(in_map, addr);

  if (m != 0) { return reg_get(m, addr, val); }
  return hold_read(addr, val);
}

/* ==========================================================================
 * 0x03 / 0x04 / 0x06 / 0x10
 * ========================================================================== */

uint16_t CmdFunc3(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint16_t addr = rd16be(&mas[2]);
  uint16_t cnt  = rd16be(&mas[4]);
  uint16_t need;
  uint16_t i;

  (void)len;

  if ((cnt == 0u) || (cnt > 125u)) { return CmdError(out, mas, CMD_ERR_VALUE); }

  need = (uint16_t)(3u + cnt * 2u);
  if (cap < need) { return CmdError(out, mas, CMD_ERR_VALUE); }

  out[0] = mas[0];
  out[1] = mas[1];
  out[2] = (uint8_t)(cnt * 2u);

  for (i = 0u; i < cnt; i++)
  {
    uint16_t v;

    if (hold_read((uint16_t)(addr + i), &v) == 0u)
    {
      return CmdError(out, mas, CMD_ERR_ADDR);
    }
    wr16be(&out[3u + i * 2u], v);
  }

  return need;
}

uint16_t CmdFunc4(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint16_t addr = rd16be(&mas[2]);
  uint16_t cnt  = rd16be(&mas[4]);
  uint16_t need;
  uint16_t i;

  (void)len;

  if ((cnt == 0u) || (cnt > 125u)) { return CmdError(out, mas, CMD_ERR_VALUE); }

  need = (uint16_t)(3u + cnt * 2u);
  if (cap < need) { return CmdError(out, mas, CMD_ERR_VALUE); }

  out[0] = mas[0];
  out[1] = mas[1];
  out[2] = (uint8_t)(cnt * 2u);

  for (i = 0u; i < cnt; i++)
  {
    uint16_t v;

    if (input_read((uint16_t)(addr + i), &v) == 0u)
    {
      return CmdError(out, mas, CMD_ERR_ADDR);
    }
    wr16be(&out[3u + i * 2u], v);
  }

  return need;
}

uint16_t CmdFunc6(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint16_t addr = rd16be(&mas[2]);
  uint16_t val  = rd16be(&mas[4]);

  (void)len;

  if (hold_write(addr, val) == 0u) { return CmdError(out, mas, CMD_ERR_ADDR); }

  memcpy(out, mas, 6u);
  return 6u;
}

uint16_t CmdFunc16(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint16_t addr  = rd16be(&mas[2]);
  uint16_t cnt   = rd16be(&mas[4]);
  uint16_t bytes = (uint16_t)mas[6];
  uint16_t i;

  if ((cnt == 0u) || (cnt > 123u))        { return CmdError(out, mas, CMD_ERR_VALUE); }
  if (bytes != (uint16_t)(cnt * 2u))      { return CmdError(out, mas, CMD_ERR_VALUE); }
  if (len != (uint16_t)(7u + bytes + 2u)) { return CmdError(out, mas, CMD_ERR_VALUE); }

  for (i = 0u; i < cnt; i++)
  {
    uint16_t a = (uint16_t)(addr + i);
    uint16_t v = rd16be(&mas[7u + i * 2u]);

    if (hold_write(a, v) == 0u)
    {
      return CmdError(out, mas, CMD_ERR_ADDR);
    }
  }

  memcpy(out, mas, 6u);
  return 6u;
}

/* ==========================================================================
 * 0x0C - event log read (log v2, slot LOG_SLOT_SIZE bytes).
 * ========================================================================== */

uint16_t CmdFunc12(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint32_t ptr = (uint32_t)rd16be(&mas[2]);
  uint16_t cnt = rd16be(&mas[4]);
  uint32_t cur = Sns_Cfg_struct.Config_archive.CurrentAddrFile;
  uint32_t beg = Sns_Cfg_struct.Config_archive.BegginAddrFile;
  uint32_t payload;
  uint16_t i;
  EVENT_LOG rec;

  (void)len;

  if (cnt == 0u) { return CmdError(out, mas, CMD_ERR_VALUE); }

  payload = (uint32_t)cnt * LOG_SLOT_SIZE;
  if ((payload > 3000u) || (cap < (uint16_t)(3u + payload)))
  {
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if ((cur == beg) ||
      ((uint32_t)(ptr - beg) >= (uint32_t)(cur - beg)) ||
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
      return CmdError(out, mas, (rc == 1) ? CMD_ERR_ADDR : CMD_ERR_IO);
    }
    memcpy(&out[3u + (uint32_t)i * LOG_SLOT_SIZE], &rec, LOG_SLOT_SIZE);
  }

  return (uint16_t)(3u + payload);
}

/* ==========================================================================
 * 0x14 - log search by time interval.
 * ========================================================================== */
uint16_t CmdFunc20(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint32_t t_from = rd32be(&mas[2]);
  uint32_t t_to   = rd32be(&mas[6]);
  uint32_t pf = 0u;
  uint32_t pt = 0u;
  int rc;
  (void)len;

  if (cap < 12u)     { return CmdError(out, mas, CMD_ERR_VALUE); }
  if (t_to < t_from) { return CmdError(out, mas, CMD_ERR_VALUE); }

  sys_state.process = SYS_PROC_ARCHIVE;

  rc = log_find_by_time(&Sns_Cfg_struct, t_from, t_to, &pf, &pt);
  if (rc < 0) { return CmdError(out, mas, CMD_ERR_IO); }

  out[0] = mas[0];
  out[1] = mas[1];
  wr32le(&out[2], pf);
  wr32le(&out[6], pt);
  return 10u;
}

/* ==========================================================================
 * 0x0E - internal flash staging (bootloader area).
 * ========================================================================== */

uint16_t CmdFunc14(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint32_t addr = rd32be(&mas[2]);
  uint16_t size = (uint16_t)mas[6];

  (void)len;

  memcpy(out, mas, 6u);

  if ((size == 0u) || (size > CV_WRITE_BUF_SIZE))
  {
    cmd_state.flash_status = CV_ST_ADDR;
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  /* STM32U5 programs quad-word only. */
  if (((addr & 0x0Fu) != 0u) || ((size & 0x0Fu) != 0u))
  {
    cmd_state.flash_status = CV_ST_ADDR;
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if (cmd_state.flash_key == 0u)
  {
    cmd_state.flash_status = CV_ST_PROT;
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if (boot_flash_allowed() == 0)
  {
    cmd_state.flash_status = CV_ST_PROT;
    boot_flash_status = BOOT_ST_PROTECTED;
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if ((addr < (uint32_t)FLASH_ADDRESS_START_BOOTLOADER) ||
      ((addr + size) > (uint32_t)FLASH_ADDRESS_STOP_BOOTLOADER))
  {
    cmd_state.flash_status = CV_ST_ADDR;
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  if (cmd_state.int_write_pending != 0u)
  {
    cmd_state.flash_status = CV_ST_BUSY;
    return CmdError(out, mas, CMD_ERR_BUSY);
  }

  cmd_state.write_addr = addr;
  cmd_state.write_len  = size;
  memcpy(cmd_state.write_buf, &mas[7], size);
  cmd_state.int_write_pending = 1u;
  cmd_state.flash_status      = CV_ST_BUSY;
  sys_state.process           = SYS_PROC_FW;

  return 6u;
}

/* ==========================================================================
 * 0x0F - external flash staging (sounds / firmware).
 * ========================================================================== */

uint16_t CmdFunc15(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  uint32_t addr = rd32be(&mas[2]);
  uint16_t size = (uint16_t)mas[6];
  uint32_t base;
  uint32_t limit;

  (void)len;

  memcpy(out, mas, 6u);

  if ((size == 0u) || (size > CV_WRITE_BUF_SIZE))
  {
    cmd_state.flash_status = CV_ST_ADDR;
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if (cmd_state.flash_key == 0u)
  {
    cmd_state.flash_status = CV_ST_PROT;
    return CmdError(out, mas, CMD_ERR_VALUE);
  }

  if (cmd_state.flash_target == CV_TGT_SOUND)
  {
    base  = SF_SOUNDS_BASE;
    limit = SF_SOUNDS_SIZE;
  }
  else if (cmd_state.flash_target == CV_TGT_FW)
  {
    base  = SF_BOOT_BASE;
    limit = SF_BOOT_SIZE;
  }
  else
  {
    cmd_state.flash_status = CV_ST_ADDR;
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  if ((cmd_state.flash_len != 0u) && ((addr + size) > cmd_state.flash_len))
  {
    cmd_state.flash_status = CV_ST_ADDR;
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  if ((addr + size) > limit)
  {
    cmd_state.flash_status = CV_ST_ADDR;
    return CmdError(out, mas, CMD_ERR_ADDR);
  }

  if (cmd_state.ext_write_pending != 0u)
  {
    cmd_state.flash_status = CV_ST_BUSY;
    return CmdError(out, mas, CMD_ERR_BUSY);
  }

  cmd_state.write_addr = base + addr;
  cmd_state.write_len  = size;
  memcpy(cmd_state.write_buf, &mas[7], size);
  cmd_state.ext_write_pending = 1u;
  cmd_state.flash_status      = CV_ST_BUSY;
  sys_state.process = (cmd_state.flash_target == CV_TGT_FW) ? SYS_PROC_FW
                                                            : SYS_PROC_ARCHIVE;

  return 6u;
}


/* ==========================================================================
 * Modbus slave address.
 *
 * Derived from the device serial number exactly like the reference design:
 *     address = ((SerialHi << 16) | SerialLo) % 100,  0 -> 100
 * Frames addressed to another slave are ignored (the receiver stays silent).
 * Broadcast addresses 0x00 and 0xFF are always accepted.
 * ========================================================================== */
static uint16_t cv_slave_addr(void)
{
  uint32_t sn = ((uint32_t)Sns_Cfg_struct.Config_common.SerialHi << 16) |
                (uint32_t)Sns_Cfg_struct.Config_common.SerialLo;
  uint16_t a  = (uint16_t)(sn % 100u);

  return (a == 0u) ? 100u : a;
}

/* ==========================================================================
 * Dispatcher. CmdFuncN return body length without CRC.
 * ========================================================================== */

uint16_t parsing_command(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint16_t answer = 0u;
  uint16_t slave;
  uint8_t  op;
  uint8_t  locked = 0u;

  if (len < 5u)
  {
    answer = CmdError(out, mas, CMD_ERR_VALUE);
    (void)CreateCRC(out, answer);
    return (uint16_t)(answer + 2u);
  }

  /* Slave-address filter: our address, or broadcast 0x00 / 0xFF.
     Anything else is ignored - return 0, the caller sends no answer. */
  slave = cv_slave_addr();
  if ((mas[0] != (uint8_t)slave) && (mas[0] != 0x00u) && (mas[0] != 0xFFu))
  {
    return 0u;
  }

  op = mas[1];

  if ((op == CMD_OP_WRITE_SINGLE) || (op == CMD_OP_WRITE_MULTI))
  {
    Sys_Cfg_Lock();
    locked = 1u;
  }

  switch (op)
  {
    case CMD_OP_READ_HOLD:
      answer = (len == 8u) ? CmdFunc3(mas, len, out, cap)
                           : CmdError(out, mas, CMD_ERR_VALUE);
      break;

    case CMD_OP_READ_IN:
      answer = (len == 8u) ? CmdFunc4(mas, len, out, cap)
                           : CmdError(out, mas, CMD_ERR_VALUE);
      break;

    case CMD_OP_WRITE_SINGLE:
      answer = (len == 8u) ? CmdFunc6(mas, len, out)
                           : CmdError(out, mas, CMD_ERR_VALUE);
      break;

    case CMD_OP_WRITE_MULTI:
      answer = CmdFunc16(mas, len, out);
      break;

    case CMD_OP_READ_LOG:
      answer = (len == 8u) ? CmdFunc12(mas, len, out, cap)
                           : CmdError(out, mas, CMD_ERR_VALUE);
      break;

    case CMD_OP_FIND_LOG:
      answer = (len == 12u) ? CmdFunc20(mas, len, out, cap)
                            : CmdError(out, mas, CMD_ERR_VALUE);
      break;

    case CMD_OP_WRITE_MCU:
      answer = ((len >= 9u) && ((uint32_t)len == (7u + (uint32_t)mas[6] + 2u)))
               ? CmdFunc14(mas, len, out) : CmdError(out, mas, CMD_ERR_VALUE);
      break;

    case CMD_OP_WRITE_EXT:
      answer = ((len >= 9u) && ((uint32_t)len == (7u + (uint32_t)mas[6] + 2u)))
               ? CmdFunc15(mas, len, out) : CmdError(out, mas, CMD_ERR_VALUE);
      break;

    default:
      /* Неизвестная функция: игнорируем, не отвечаем */
      answer = 0u;
      break;
  }

  if (locked != 0u) { Sys_Cfg_Unlock(); }

  if (answer == 0u) { answer = CmdError(out, mas, CMD_ERR_IO); }

  (void)CreateCRC(out, answer);
  return (uint16_t)(answer + 2u);
}

/* ==========================================================================
 * Receiver (UART / NFC bridge feeds bytes here).
 * ========================================================================== */

#define CV_RX_SIZE 300u
#define CV_TX_SIZE 560u

/* Inter-byte timeout: 3.5 character times at 115200 baud is ~0.3 ms.
   10 ms is well above one character (87 us) and well below any normal
   gap between Modbus frames. Timer_Tick_1ms() increments the counter,
   RxFeed() resets it on every received byte.                          */
#define CV_RX_TIMEOUT_MS 50u

static struct
{
  uint8_t  buf[CV_RX_SIZE];
  uint16_t len;
  uint16_t expect;
} rx;

static volatile uint16_t cv_rx_idle_ms;

static uint8_t tx_buf[CV_TX_SIZE];

static uint16_t expect_len(const uint8_t *b, uint16_t len)
{
  if (len < 2u) { return 0u; }

  switch (b[1])
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
    	if (len < 7u) { return 7u; }
    	return (uint16_t)(7u + (uint16_t)b[6] + 2u);

    default:
      return 0u;
  }
}

static void cv_rx_reset(void)
{
  rx.len       = 0u;
  rx.expect    = 0u;
  cv_rx_idle_ms = 0u;
}

void Command_vector_RxFeed(uint8_t byte)
{
  if (rx.len >= CV_RX_SIZE)
  {
    rx.len    = 0u;
    rx.expect = 0u;
  }

  cv_rx_idle_ms = 0u;
  rx.buf[rx.len++] = byte;

  if (rx.len == 2u)
  {
    rx.expect = expect_len(rx.buf, rx.len);

    if (rx.expect == 0u)
    {
      rx.len    = 0u;
      return;
    }
  }

  if ((rx.len == 7u) &&
      ((rx.buf[1] == CMD_OP_WRITE_MULTI) ||
       (rx.buf[1] == CMD_OP_WRITE_MCU)   ||
       (rx.buf[1] == CMD_OP_WRITE_EXT)))
  {
    rx.expect = expect_len(rx.buf, rx.len);
  }

  if ((rx.expect != 0u) && (rx.len >= rx.expect))
  {
    if (crc_ok(rx.buf, rx.expect) != 0u)
    {
      uint16_t n = parsing_command(rx.buf, rx.expect, tx_buf, CV_TX_SIZE);

      if (n != 0u) { Command_vector_Transmit(tx_buf, n); }
    }

    rx.len    = 0u;
    rx.expect = 0u;
  }

  /* The frame grew past the expected length (byte lost inside a
     variable-length frame, or expect was computed from garbage).
     Reset so the next frame starts clean. */
  if ((rx.expect != 0u) && (rx.len > rx.expect))
  {
    rx.len    = 0u;
    rx.expect = 0u;
  }
}


/* Frame timeout: called every 1 ms from Timer_Tick_1ms(). If no bytes
   arrived for CV_RX_TIMEOUT_MS the in-progress frame is abandoned.
   This is the Modbus 3.5-character-time re-synchronisation mechanism. */
void Uart_Command_Receive_Timer_Inc(void)
{
  if (rx.len == 0u) { return; }

  cv_rx_idle_ms++;

  if (cv_rx_idle_ms >= CV_RX_TIMEOUT_MS)
  {
    cv_rx_reset();
  }
}

void Command_vector_Init(void)
{
  memset(&rx, 0, sizeof(rx));
  memset(&cmd_state, 0, sizeof(cmd_state));
}

/* Answer goes out through the project transport: TYPE_USART -> USART_COM
   (huart5). Context: Receiver Task, never ISR. */
void Command_vector_Transmit(const uint8_t *data, uint16_t len)
{
  transmit_buffer((uint8_t *)data, len, TYPE_USART);
}


/* ==========================================================================
 * COM consumer. Strong override of the weak stub in main_vector.c.
 *
 * Producer : UART5_IRQHandler -> add_to_buffer(&InputBuffer[TYPE_USART])
 * Consumer : this function, Receiver Task, every VECTOR_TASKS_RECEIVER_DELAY_MS
 * Answer   : Command_vector_Transmit() -> transmit_buffer(..., TYPE_USART)
 *
 * The ring is SPSC (buffer.h): ISR owns 'end', this thread owns 'begin',
 * so no critical section is needed. Parsing, Sys_Cfg_Lock() and the answer
 * transmission all happen here in thread context, never in the ISR.
 * ========================================================================== */
void command_message(void)
{
#if CONFIG_UART
  static uint8_t cv_started = 0u;
  uint8_t b;

  if (cv_started == 0u)
  {
    cv_started = 1u;
    Command_vector_Init();
  }

  /* Drain everything the ISR has collected since the previous pass. */
  while (check_buffer(&InputBuffer[TYPE_USART]) != 0u)
  {
    if (receive_buffer(&InputBuffer[TYPE_USART], &b) == 0u) { break; }
    Command_vector_RxFeed(b);
  }
#endif
}

/* ==========================================================================
 * Measure Task service - executes the flags raised by register writes.
 * Heavy flash work only in the quiet window.
 * ========================================================================== */

void Command_vector_ExecutePendingActions(void)
{
  /* Soft reset flag written through register 0x0053: arm the timer once.
     The reset itself is done by the existing logic in Vector_Options_System. */
  if ((sys_state.soft_reset != 0u) &&
      (countdown_time_rtc.Timers[TIMER_RTC_SOFT_RESET_PROGRAM] == 0u))
  {
    START_TIMER_RTC(TIMER_RTC_SOFT_RESET_PROGRAM, TIME_RTC_SOFT_RESET_PROGRAM);
  }

  /* Configuration flags are consumed by Vector_RunFlashMemory(). */

  /* --- firmware apply -------------------------------------------------- */
  if (cmd_state.fw_apply_req != 0u)
  {
    cmd_state.fw_apply_req = 0u;
    sys_state.process = SYS_PROC_FW;
    fw_apply_request();
    cmd_state.flash_status = CV_ST_OK;
    return;
  }

  /* --- quiet window gate ---------------------------------------------- */
  if (audio_is_busy() != 0u) { return; }

  /* --- erase ---------------------------------------------------------- */
  if (cmd_state.erase_req != 0u)
  {
    uint16_t mode   = cmd_state.erase_mode;
    uint16_t target = cmd_state.flash_target;

    cmd_state.erase_req    = 0u;
    cmd_state.erase_mode   = 0u;
    cmd_state.flash_status = CV_ST_BUSY;

    if (cmd_state.flash_key == 0u)
    {
      cmd_state.flash_status = CV_ST_PROT;
      return;
    }

    if (target == CV_TGT_BOOT)
    {
      int rc;

      if (boot_flash_allowed() == 0)
      {
        cmd_state.flash_status = CV_ST_PROT;
        return;
      }

      rc = (mode == 2u) ? boot_flash_restore() : boot_flash_erase();
      cmd_state.flash_status = (rc == 0) ? CV_ST_OK : CV_ST_IO;
      return;
    }

    if ((target == CV_TGT_SOUND) || (target == CV_TGT_FW) || (target == CV_TGT_LOG))
    {
      uint32_t base;
      uint32_t limit;
      uint32_t a;

      if (target == CV_TGT_SOUND)   { base = SF_SOUNDS_BASE; limit = SF_SOUNDS_SIZE; }
      else if (target == CV_TGT_FW) { base = SF_BOOT_BASE;   limit = SF_BOOT_SIZE;   }
      else                          { base = SF_LOG_BASE;    limit = SF_LOG_SIZE;    }

      if ((cmd_state.flash_len == 0u) || (cmd_state.flash_len > limit))
      {
        cmd_state.flash_len = limit;
      }
      
      for (a = 0u; a < cmd_state.flash_len; a += 4096u)
      {
        int rc = ext_erase_sector(base + cmd_state.flash_addr + a);

        if (rc == EXT_BUSY)
        {
          /* resume from this offset on the next pass */
          cmd_state.flash_addr  += a;
          cmd_state.flash_len   -= a;
          cmd_state.erase_req    = 1u;
          cmd_state.erase_mode   = mode;
          cmd_state.flash_status = CV_ST_BUSY;
          return;
        }

        if (rc != 0)
        {
          cmd_state.flash_status = CV_ST_IO;
          return;
        }
      }
      
      // Все сектора стерты успешно
      cmd_state.flash_status = CV_ST_OK;
      return;
    }
  }

  /* --- internal flash program ----------------------------------------- */
  if (cmd_state.int_write_pending != 0u)
  {
    int rc;

    cmd_state.int_write_pending = 0u;
    rc = boot_flash_write(cmd_state.write_addr, cmd_state.write_buf, cmd_state.write_len);

    if (rc == 0)       { cmd_state.flash_status = CV_ST_OK; }
    else if (rc == -2) { cmd_state.flash_status = CV_ST_VERIFY; }
    else               { cmd_state.flash_status = CV_ST_IO; }
    return;
  }

  /* --- external flash program + verify -------------------------------- */
  if (cmd_state.ext_write_pending != 0u)
  {
    uint8_t verify[CV_WRITE_BUF_SIZE];
    int rc;

    rc = ext_write(cmd_state.write_addr, cmd_state.write_buf, cmd_state.write_len);
    if (rc == EXT_BUSY)
    {
      cmd_state.flash_status = CV_ST_BUSY;
      return;                       /* keep pending, retry next pass */
    }

    cmd_state.ext_write_pending = 0u;

    if (rc != 0)
    {
      cmd_state.flash_status = CV_ST_IO;
      return;
    }

    rc = ext_read(cmd_state.write_addr, verify, cmd_state.write_len);
    if (rc != 0)
    {
      cmd_state.flash_status = CV_ST_IO;
      return;
    }

    cmd_state.flash_status =
        (memcmp(verify, cmd_state.write_buf, cmd_state.write_len) == 0)
        ? CV_ST_OK : CV_ST_VERIFY;
    return;
  }

  /* --- audio reload image --------------------------------------------- */
  if (cmd_state.audio_reload_pending != 0u)
  {
    cmd_state.audio_reload_pending = 0u;
    audio_reload_image();
    cmd_state.flash_status = CV_ST_OK;
    return;
  }
}
