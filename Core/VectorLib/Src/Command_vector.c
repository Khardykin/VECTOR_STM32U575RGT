/**
  ******************************************************************************
  * @file    Command_vector.c
  * @brief   Обмен по COM (USART3) в стиле Avis Command.c.
  *
  *          parsing_command() - диспетчер, CmdFuncN() - обработчики, где N это
  *          ДЕСЯТИЧНЫЙ номер команды (как в Avis: 0x03->CmdFunc3, 0x10->CmdFunc16).
  *          CRC добавляет сам parsing_command (CreateCRC), обработчики возвращают
  *          длину ответа БЕЗ CRC.
  *
  *          Команды:
  *            0x03 CmdFunc3  - чтение holding-регистров (Config_common)
  *            0x04 CmdFunc4  - чтение input-регистров   (sensors_status)
  *            0x10 CmdFunc16 - запись группы holding
  *            0x0F CmdFunc15 - запись блока во внешнюю flash (как в Avis)
  *            0x11 CmdFunc17 - чтение блока внешней flash
  *            0x0D CmdFunc13 - стирание диапазона внешней flash
  *            0x0B CmdFunc11 - сведения о flash и образе звуков
  *            0x0A CmdFunc10 - COMMIT: плеер перечитывает образ, сессия закрыта
  *            0x0E CmdFunc14 - запись прошивки во внутреннюю flash (бутлоадер, TODO)
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

#define CMD_SOUNDS_LIMIT    0x00400000u   /* sfmap.h: выше - CONFIG/LOG        */
#define CMD_ERASE_KEY       0xA5u         /* ключ стирания вне области SOUNDS  */

/* --- Хуки: ставятся ОДИН раз при первом проходе command_message() --------- */
static cmd_flash_pause_t s_pause  = 0;    /* стоп/пуск плеера на время записи  */
static cmd_commit_t      s_commit = 0;    /* вызывается по CmdFunc10 (COMMIT)  */
static cmd_tx_t          s_tx     = 0;    /* отправка ответа в COM             */

/* --- Накопитель кадра ----------------------------------------------------- */
static uint8_t  s_rx[CMD_MAX_FRAME];
static uint16_t s_rx_len = 0u;
static uint16_t s_expect = 0u;
static uint8_t  s_ready  = 0u;
static uint32_t s_rx_ms  = 0u;

/* --- Сессия обмена (эхо UART и лог молчат, пока идёт загрузка) ------------ */
static uint8_t  s_session = 0u;
static uint32_t s_sess_ms = 0u;

void Command_vector_SetPauseHook(cmd_flash_pause_t hook)
{
  s_pause = hook;
}

void Command_vector_SetCommitHook(cmd_commit_t hook)
{
  s_commit = hook;
}

void Command_vector_SetTx(cmd_tx_t hook)
{
  s_tx = hook;
}

/* ==========================================================================
 * CRC и мелкие помощники (имена как в Avis Command.c)
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

/* Дописывает CRC в конец пакета (Modbus: младший байт первый). */
uint16_t CreateCRC(uint8_t *data, uint16_t len)
{
  uint16_t crc = CreateCRC_out(data, len);

  data[len]      = (uint8_t)(crc & 0xFFu);
  data[len + 1u] = (uint8_t)(crc >> 8);
  return crc;
}

uint16_t Command_vector_CRC16(const uint8_t *data, uint16_t len)
{
  return CreateCRC_out(data, len);
}

static uint32_t rd32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void wr32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static void wr16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

/* Ответ-ошибка Modbus: [адрес][команда|0x80][код]. CRC добавит диспетчер. */
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
 * Карта регистров (как sn_map/com_map/in_map в Avis): адрес задан ЯВНО,
 * между группами зазоры - чтение сквозь дыру даёт ошибку 02.
 * ========================================================================== */
#define MB_MAX_REGS      125u
#define MB_MAX_BYTES     250u
#define MB_DIV           1000.0f

enum
{
  T_U16 = 0,   /* uint16_t, 1 регистр                    */
  T_U32,       /* uint32_t, 2 регистра (LO, HI)          */
  T_U8,        /* uint8_t,  1 регистр (старший байт = 0) */
  T_FLT,       /* float,    1 регистр, значение x1000    */
  T_STR        /* массив байт, len/2 регистров           */
};

typedef struct
{
  uint16_t reg;   /* адрес первого регистра Modbus */
  uint16_t off;   /* offsetof поля в структуре     */
  uint8_t  type;  /* T_*                           */
  uint8_t  len;   /* размер в байтах               */
} reg_map_t;

#define CM(r, f)        { (r), (uint16_t)offsetof(SNS_CFG_Type_common, f), T_U16, 2u }
#define CM32(r, f)      { (r), (uint16_t)offsetof(SNS_CFG_Type_common, f), T_U32, 4u }
#define CMSTR(r, f, n)  { (r), (uint16_t)offsetof(SNS_CFG_Type_common, f), T_STR, (n) }

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

/* --- Курсор ответа, как в Avis (Count/Value/Countuint8_t/AnswerLen) -------
 * Пишет только receiver_task, поэтому общие переменные безопасны.          */
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
 * УПРАВЛЯЮЩИЕ РЕГИСТРЫ (0x0F00+, далеко от конфигурации 0x0000..0x0063
 * и измерений 0x0000..0x0033). Всё делается штатными 0x03 (чтение) и
 * 0x10 (запись) - отдельных команд больше не нужно.
 *
 *   0x0F00 COMMIT        W: 1 = плеер перечитывает образ (audio_reload_image)
 *   0x0F01 SAVE_CFG      W: 1 = SaveConfig
 *   0x0F02 SAVE_CFGDEF   W: 1 = SaveConfigDefault
 *   0x0F03 LOAD_CFGDEF   W: 1 = ReadConfigDefault
 *
 *   0x0F10..0x0F1B       RO: сведения о flash и образе звуков
 *   0x0F1C IMG_REFRESH   W: 1 = перечитать заголовок из flash
 *
 *   0x0F20..0x0F25       W: 1 = стереть область (фоном, сектор за проход)
 *   0x0F26 BUSY / 0x0F27 ERR / 0x0F28 PROGRESS   RO
 *   0x0F29..0x0F2C       RW: адрес и длина произвольного стирания
 *   0x0F2D ERASE_KEY     RW: 0xA5 - разрешить стирание вне SOUNDS
 *   0x0F2E ERASE_START   W: 1 = запустить стирание заданного диапазона
 *
 *   0x0F30/31 WIN_ADDR   RW: адрес окна (32 бита, LO/HI)
 *   0x0F32 WIN_LEN       RW: длина окна в байтах (чётная, <= 250)
 *   0x0F33 WIN_CMD       W: 1 = прочитать окно из flash, 2 = записать + сверка
 *   0x0F34 WIN_STATUS    RO: 0 = ok, иначе код ошибки последней операции
 *   0x0F35 WIN_AUTOINC   RW: 1 = после записи адрес += WIN_LEN
 *   0x0F40..0x0FB7       RW: WIN_DATA, 125 регистров = 250 байт
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
#define MB_REG_ERASE_ADDR_LO  0x0F29u
#define MB_REG_ERASE_ADDR_HI  0x0F2Au
#define MB_REG_ERASE_LEN_LO   0x0F2Bu
#define MB_REG_ERASE_LEN_HI   0x0F2Cu
#define MB_REG_ERASE_KEY      0x0F2Du
#define MB_REG_ERASE_START    0x0F2Eu

#define MB_REG_WIN_ADDR_LO    0x0F30u
#define MB_REG_WIN_ADDR_HI    0x0F31u
#define MB_REG_WIN_LEN        0x0F32u
#define MB_REG_WIN_CMD        0x0F33u
#define MB_REG_WIN_STATUS     0x0F34u
#define MB_REG_WIN_AUTOINC    0x0F35u
#define MB_REG_WIN_DATA       0x0F40u
#define MB_WIN_REGS           125u
#define MB_WIN_BYTES          (MB_WIN_REGS * 2u)

#define ERASE_BIT_SOUNDS 0x01u
#define ERASE_BIT_CONFIG 0x02u
#define ERASE_BIT_CFGDEF 0x04u
#define ERASE_BIT_LOG    0x08u
#define ERASE_BIT_LOGPTR 0x10u
#define ERASE_BIT_FW     0x20u
#define ERASE_BIT_CUSTOM 0x40u

static uint8_t  s_erase_req  = 0u;
static uint8_t  s_erase_busy = 0u;
static uint8_t  s_erase_err  = 0u;
static uint8_t  s_erase_bit  = 0u;
static uint8_t  s_erase_pct  = 0u;
static uint32_t s_erase_cur  = 0u;
static uint32_t s_erase_end  = 0u;
static uint32_t s_erase_from = 0u;
static uint32_t s_erase_addr = 0u;
static uint32_t s_erase_len  = 0u;
static uint16_t s_erase_key  = 0u;

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
  s_erase_bit  = bit;
  s_erase_from = base;
  s_erase_cur  = base;
  s_erase_end  = base + size;
  s_erase_busy = bit;
  s_erase_pct  = 0u;
  s_erase_err &= (uint8_t)~bit;
  if (s_pause != 0)
  {
    s_pause(1u);                  /* плеер не читает flash во время стирания */
  }
}

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
      erase_begin(bit, s_areas[i].base, s_areas[i].size);
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

/* --- Кэш сведений об образе (читается один раз, обновляется по регистру) -- */
static audio_img_header_t s_img;
static uint16_t           s_img_rate  = 0u;
static uint8_t            s_img_ok    = 0u;
static uint8_t            s_img_valid = 0u;

static void img_refresh(void)
{
  audio_img_entry_t e;

  memset(&s_img, 0, sizeof(s_img));
  memset(&e, 0, sizeof(e));
  s_img_rate  = 0u;
  s_img_ok    = 0u;
  s_img_valid = 1u;

  if (sf_read(AUDIO_IMG_BASE_ADDR, (uint8_t *)&s_img, sizeof(s_img)) == HAL_OK)
  {
    if (s_img.magic == AUDIO_IMG_MAGIC)
    {
      s_img_ok = 1u;
      if (s_img.count != 0u)
      {
        (void)sf_read(AUDIO_IMG_BASE_ADDR + s_img.table_off, (uint8_t *)&e, sizeof(e));
        s_img_rate = e.rate;
      }
    }
  }
}

/* --- Окно данных во flash: адрес + длина + буфер 250 байт ---------------- */
static uint32_t s_win_addr    = 0u;
static uint16_t s_win_len     = 0u;
static uint16_t s_win_status  = 0u;
static uint8_t  s_win_autoinc = 0u;
static uint8_t  s_win_dirty   = 1u;   /* буфер не соответствует адресу */
static uint8_t  s_win[MB_WIN_BYTES];

static void win_load(void)
{
  if (s_win_dirty == 0u)
  {
    return;
  }
  if ((s_win_len == 0u) || (s_win_len > MB_WIN_BYTES) ||
      ((s_win_addr + s_win_len) > (uint32_t)SF_TOTAL_SIZE))
  {
    s_win_status = CMD_ERR_RANGE;
    return;
  }
  if (sf_read(s_win_addr, s_win, s_win_len) != HAL_OK)
  {
    s_win_status = CMD_ERR_IO;
    return;
  }
  s_win_status = 0u;
  s_win_dirty  = 0u;
}

static void win_store(void)
{
  uint8_t rc;

  if ((s_win_len == 0u) || (s_win_len > MB_WIN_BYTES) ||
      ((s_win_addr + s_win_len) > (uint32_t)SF_TOTAL_SIZE))
  {
    s_win_status = CMD_ERR_RANGE;
    return;
  }
  if (s_pause != 0)
  {
    s_pause(1u);
  }
  rc = program_pages(s_win_addr, s_win, s_win_len);
  if (rc == 0u)
  {
    if (sf_verify(s_win_addr, s_win, s_win_len) != HAL_OK)
    {
      rc = CMD_ERR_VERIFY;
    }
  }
  if (s_pause != 0)
  {
    s_pause(0u);
  }
  s_win_status = rc;
  if ((rc == 0u) && (s_win_autoinc != 0u))
  {
    s_win_addr += s_win_len;
    s_win_dirty = 1u;
  }
}

static uint8_t ctrl_reg_read(uint16_t addr, uint16_t *val)
{
  if ((addr >= MB_REG_WIN_DATA) && (addr < (uint16_t)(MB_REG_WIN_DATA + MB_WIN_REGS)))
  {
    uint16_t off = (uint16_t)((addr - MB_REG_WIN_DATA) * 2u);

    win_load();
    *val = (uint16_t)(((uint16_t)s_win[off] << 8) | s_win[off + 1u]);
    return 1u;
  }

  switch (addr)
  {
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
    case MB_REG_ERASE_START:
    case MB_REG_WIN_CMD:
      *val = 0u;                  /* регистры-триггеры всегда читаются нулём */
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
      if (s_img_valid == 0u)
      {
        img_refresh();
      }
      switch (addr)
      {
        case MB_REG_MAGIC_LO:    *val = (uint16_t)(s_img.magic & 0xFFFFu);      break;
        case MB_REG_MAGIC_HI:    *val = (uint16_t)(s_img.magic >> 16);          break;
        case MB_REG_IMG_VER:     *val = s_img.version;                          break;
        case MB_REG_IMG_CNT:     *val = s_img.count;                            break;
        case MB_REG_IMG_SIZE_LO: *val = (uint16_t)(s_img.total_size & 0xFFFFu); break;
        case MB_REG_IMG_SIZE_HI: *val = (uint16_t)(s_img.total_size >> 16);     break;
        case MB_REG_IMG_RATE:    *val = s_img_rate;                             break;
        default:                 *val = (uint16_t)s_img_ok;                     break;
      }
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
    case MB_REG_ERASE_ADDR_LO:
      *val = (uint16_t)(s_erase_addr & 0xFFFFu);
      return 1u;
    case MB_REG_ERASE_ADDR_HI:
      *val = (uint16_t)(s_erase_addr >> 16);
      return 1u;
    case MB_REG_ERASE_LEN_LO:
      *val = (uint16_t)(s_erase_len & 0xFFFFu);
      return 1u;
    case MB_REG_ERASE_LEN_HI:
      *val = (uint16_t)(s_erase_len >> 16);
      return 1u;
    case MB_REG_ERASE_KEY:
      *val = s_erase_key;
      return 1u;

    case MB_REG_WIN_ADDR_LO:
      *val = (uint16_t)(s_win_addr & 0xFFFFu);
      return 1u;
    case MB_REG_WIN_ADDR_HI:
      *val = (uint16_t)(s_win_addr >> 16);
      return 1u;
    case MB_REG_WIN_LEN:
      *val = s_win_len;
      return 1u;
    case MB_REG_WIN_STATUS:
      *val = s_win_status;
      return 1u;
    case MB_REG_WIN_AUTOINC:
      *val = (uint16_t)s_win_autoinc;
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

    s_win[off]      = (uint8_t)(val >> 8);
    s_win[off + 1u] = (uint8_t)val;
    return 1u;
  }

  switch (addr)
  {
    case MB_REG_COMMIT:
      if ((val != 0u) && (s_commit != 0))
      {
        s_commit();
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
        s_erase_req |= (uint8_t)(1u << (addr - MB_REG_ERASE_SOUNDS));
      }
      return 1u;

    case MB_REG_ERASE_ADDR_LO:
      s_erase_addr = (s_erase_addr & 0xFFFF0000u) | (uint32_t)val;
      return 1u;
    case MB_REG_ERASE_ADDR_HI:
      s_erase_addr = (s_erase_addr & 0x0000FFFFu) | ((uint32_t)val << 16);
      return 1u;
    case MB_REG_ERASE_LEN_LO:
      s_erase_len = (s_erase_len & 0xFFFF0000u) | (uint32_t)val;
      return 1u;
    case MB_REG_ERASE_LEN_HI:
      s_erase_len = (s_erase_len & 0x0000FFFFu) | ((uint32_t)val << 16);
      return 1u;
    case MB_REG_ERASE_KEY:
      s_erase_key = val;
      return 1u;
    case MB_REG_ERASE_START:
      if (val == 0u)
      {
        return 1u;
      }
      if ((s_erase_busy != 0u) || (s_erase_req != 0u))
      {
        return 1u;                /* занято - команда игнорируется, BUSY покажет */
      }
      if ((s_erase_len == 0u) || (s_erase_addr >= (uint32_t)SF_TOTAL_SIZE) ||
          (s_erase_len > ((uint32_t)SF_TOTAL_SIZE - s_erase_addr)))
      {
        s_erase_err |= ERASE_BIT_CUSTOM;
        return 1u;
      }
      if (((s_erase_addr + s_erase_len) > CMD_SOUNDS_LIMIT) && (s_erase_key != CMD_ERASE_KEY))
      {
        s_erase_err |= ERASE_BIT_CUSTOM;
        return 1u;
      }
      erase_begin(ERASE_BIT_CUSTOM,
                  s_erase_addr - (s_erase_addr % SF_SECTOR_SIZE),
                  (((s_erase_addr + s_erase_len - 1u) / SF_SECTOR_SIZE) + 1u) * SF_SECTOR_SIZE
                  - (s_erase_addr - (s_erase_addr % SF_SECTOR_SIZE)));
      return 1u;

    case MB_REG_WIN_ADDR_LO:
      s_win_addr = (s_win_addr & 0xFFFF0000u) | (uint32_t)val;
      s_win_dirty = 1u;
      return 1u;
    case MB_REG_WIN_ADDR_HI:
      s_win_addr = (s_win_addr & 0x0000FFFFu) | ((uint32_t)val << 16);
      s_win_dirty = 1u;
      return 1u;
    case MB_REG_WIN_LEN:
      if ((val <= MB_WIN_BYTES) && ((val & 1u) == 0u))
      {
        s_win_len = val;
        s_win_dirty = 1u;
      }
      else
      {
        s_win_status = CMD_ERR_VALUE;
      }
      return 1u;
    case MB_REG_WIN_AUTOINC:
      s_win_autoinc = (uint8_t)((val != 0u) ? 1u : 0u);
      return 1u;
    case MB_REG_WIN_CMD:
      if (val == 1u)
      {
        s_win_dirty = 1u;
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

  /* Фаза 1: провалов в диапазоне быть не должно (как в Avis). */
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

  /* Фаза 2: сбор данных через курсор CMD_PUT. */
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
 * CmdFunc15 - запись блока во внешнюю flash (адрес 32 бита, как в Avis)
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

  if (s_pause != 0)
  {
    s_pause(1u);
  }
  rc = program_pages(addr, &mas[8], size);
  if (rc == 0u)
  {
    if (sf_verify(addr, &mas[8], size) != HAL_OK)
    {
      rc = CMD_ERR_VERIFY;
    }
  }
  if (s_pause != 0)
  {
    s_pause(0u);
  }
  if (rc != 0u)
  {
    return CmdError(out, mas, rc);
  }

  wr16(&out[6], 0u);
  return CMD_FRAME_HDR;
}

/* CmdFunc17 - чтение блока внешней flash (для сверки и выгрузки). */
uint16_t CmdFunc17(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  (void)len;
  (void)cap;
  return CmdError(out, mas, CMD_ERR_UNKNOWN);
}

/* CmdFunc13 - стирание диапазона внешней flash (синхронно, малыми блоками). */
uint16_t CmdFunc13(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  (void)len;
  return CmdError(out, mas, CMD_ERR_UNKNOWN);
}

/* CmdFunc11 - геометрия flash и заголовок образа звуков (23 байта). */
uint16_t CmdFunc11(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  (void)len;
  (void)cap;
  return CmdError(out, mas, CMD_ERR_UNKNOWN);
}

/* CmdFunc10 - COMMIT: образ залит, плеер перечитывает заголовок. */
uint16_t CmdFunc10(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  (void)len;
  return CmdError(out, mas, CMD_ERR_UNKNOWN);
}

/* CmdFunc14 - запись прошивки во внутреннюю flash: место под бутлоадер.
 * Пока не реализовано (нет бутлоадера) - отвечаем ошибкой, чтобы ПК не думал,
 * что прошивка легла.                                                      */
uint16_t CmdFunc14(const uint8_t *mas, uint16_t len, uint8_t *out)
{
  (void)len;
  return CmdError(out, mas, CMD_ERR_UNKNOWN);
}

/* ==========================================================================
 * Диспетчер (как parsing_command в Avis): CRC добавляется ЗДЕСЬ.
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

    case CMD_OP_COMMIT:
    case CMD_OP_INFO:
    case CMD_OP_ERASE:
    case CMD_OP_WRITE:
    case CMD_OP_READ_FLASH:
    case CMD_OP_WRITE_MCU:
      if (len < (CMD_FRAME_HDR + 2u))
      {
        return 0u;
      }
      if ((uint32_t)len != (CMD_FRAME_HDR +
          (uint32_t)(((uint16_t)mas[6] << 8) | mas[7]) + 2u))
      {
        return 0u;
      }
      switch (cmd)
      {
        case CMD_OP_COMMIT:
          answer = CmdFunc10(mas, len, out);
          break;
        case CMD_OP_INFO:
          answer = CmdFunc11(mas, len, out, cap);
          break;
        case CMD_OP_ERASE:
          answer = CmdFunc13(mas, len, out);
          break;
        case CMD_OP_WRITE:
          answer = CmdFunc15(mas, len, out);
          break;
        case CMD_OP_READ_FLASH:
          answer = CmdFunc17(mas, len, out, cap);
          break;
        default:
          answer = CmdFunc14(mas, len, out);
          break;
      }
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

uint16_t Command_vector_Frame(const uint8_t *req, uint16_t req_len, uint8_t *rsp, uint16_t rsp_cap)
{
  return parsing_command(req, req_len, rsp, rsp_cap);
}

/* ==========================================================================
 * Накопитель кадра. Конец кадра - по ДЛИНЕ из шапки, а не по паузе в линии:
 * сервис зовётся раз в 10 мс, тайминг отдельных байтов недоступен.
 *   Modbus 3/4    : кадр всегда 8 байт;
 *   Modbus 16     : 9 + nb (шапка 7 + данные nb + CRC 2);
 *   flash-команды : 8 + size16 + 2.
 * CMD_RESYNC_MS нужен только чтобы выбросить недобитый кадр.
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
    if (s_ready != 0u)
    {
      break;                      /* прошлый кадр ещё не разобран */
    }
    if (s_rx_len >= CMD_MAX_FRAME)
    {
      s_rx_len = 0u;
      s_expect = 0u;
    }
    s_rx[s_rx_len] = data[i];
    s_rx_len++;

    if (s_rx_len == 2u)
    {
      uint8_t op = s_rx[1];

      if ((op == CMD_OP_READ_HOLD) || (op == CMD_OP_READ_IN))
      {
        s_expect = 8u;
      }
    }
    if ((s_rx_len == 7u) && (s_rx[1] == CMD_OP_WRITE_MULTI))
    {
      uint8_t nb = s_rx[6];

      if (nb > MB_MAX_BYTES)
      {
        s_rx_len = 0u;
        s_expect = 0u;
      }
      else
      {
        s_expect = (uint16_t)(9u + nb);
      }
    }
    if ((s_rx_len == CMD_FRAME_HDR) && (s_expect == 0u))
    {
      uint16_t sz = (uint16_t)(((uint16_t)s_rx[6] << 8) | s_rx[7]);

      if (sz > CMD_MAX_PAYLOAD)
      {
        s_rx_len = 0u;
        s_expect = 0u;
      }
      else
      {
        s_expect = (uint16_t)(CMD_FRAME_HDR + sz + 2u);
      }
    }
    if ((s_expect != 0u) && (s_rx_len >= s_expect))
    {
      s_ready  = 1u;
      s_expect = 0u;
    }
  }
  s_rx_ms = now_ms;
}

void Command_vector_Poll(uint32_t now_ms)
{
  uint8_t  rsp[CMD_MAX_FRAME];
  uint16_t n;

  if (s_ready != 0u)
  {
    uint8_t op = s_rx[1];

    /* Сессию открываем ДО обработки: эхо UART и лог должны замолчать раньше,
       чем уйдёт первый ответ. Закрывает её CmdFunc10 (COMMIT) или простой. */
    if (s_session == 0u)
    {
      s_session = 1u;
      Command_vector_LogMute(1u);
    }
    s_sess_ms = now_ms;

    n = parsing_command(s_rx, s_rx_len, rsp, (uint16_t)sizeof(rsp));
    s_rx_len = 0u;
    s_expect = 0u;
    s_ready  = 0u;

    if ((n != 0u) && (s_tx != 0))
    {
      s_tx(rsp, n);
    }
    if (op == CMD_OP_COMMIT)
    {
      s_session = 0u;
      Command_vector_LogMute(0u);
    }
    return;
  }

  if ((s_rx_len != 0u) && ((uint32_t)(now_ms - s_rx_ms) >= CMD_RESYNC_MS))
  {
    s_rx_len = 0u;
    s_expect = 0u;
  }
  if ((s_session != 0u) && ((uint32_t)(now_ms - s_sess_ms) >= CMD_SESSION_IDLE_MS))
  {
    s_session = 0u;
    Command_vector_LogMute(0u);
  }
}

uint8_t Command_vector_SessionActive(void)
{
  return s_session;
}

/* Слабая заглушка: перекрывается сильным определением под конкретный vlog. */
__attribute__((weak)) void Command_vector_LogMute(uint8_t mute)
{
  (void)mute;
}

#if CMD_VECTOR_COM
#include "buffer.h"
#include "audio_player.h"
#include "vector_tick.h"

/* s_hooks_ready - ОДНОРАЗОВАЯ привязка хуков к железу.
 * Статик инициализируется нулём при старте, ставится в 1 на первом проходе
 * command_message() и БОЛЬШЕ НЕ СБРАСЫВАЕТСЯ: SetTx/SetPauseHook/SetCommitHook
 * просто запоминают указатели на функции, вызывать их каждый тик незачем.
 * Дальше эти указатели ИСПОЛЬЗУЮТСЯ: s_tx - на каждый ответ, s_pause - на
 * время записи/стирания, s_commit - только когда придёт CmdFunc10 (COMMIT),
 * т.е. один раз в конце заливки: плеер перечитывает заголовок образа.       */
static uint8_t s_hooks_ready = 0u;

static void cmd_tx_com(const uint8_t *data, uint16_t len)
{
  transmit_buffer((uint8_t *)data, len, TYPE_USART);
}

static void cmd_pause_com(uint8_t pause)
{
  if (pause != 0u)
  {
    audio_stop();                 /* образ читать нельзя, пока пишем/стираем */
  }
}

static void cmd_commit_com(void)
{
  audio_reload_image();           /* плеер перечитает заголовок после заливки */
}

/* Сильное определение вместо weak-заглушки main_vector.c: вызывается из
 * receiver_task каждые VECTOR_TASKS_RECEIVER_DELAY_MS (10 мс). Кольцо
 * InputBuffer[TYPE_USART] наполняет USART3_IRQHandler - здесь только читаем.
 * Кольцо 254 Б при 115200 заполняется за ~22 мс, вычитываем его целиком.   */
void command_message(void)
{
  uint8_t  batch[64];
  uint16_t n = 0u;
  uint8_t  b;
  uint32_t now = VTICK_MS();

  if (s_hooks_ready == 0u)
  {
    Command_vector_SetTx(cmd_tx_com);
    Command_vector_SetPauseHook(cmd_pause_com);
    Command_vector_SetCommitHook(cmd_commit_com);
    s_hooks_ready = 1u;
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
  Command_vector_EraseService();  /* один сектор за проход, поток не блокируем */
}
#endif
