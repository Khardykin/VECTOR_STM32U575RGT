/**
  ******************************************************************************
  * @file    extstore.c
  * @brief   Потокобезопасный доступ к внешней flash + конфиг + журнал
  *
  *          Все публичные функции берут мьютекс на всё время операции,
  *          включая стирание: SPI-шина одна, чип допускает одну операцию
  *          за раз. Цена - операция стирания (~50-100 мс) может отложить
  *          старт звука на это же время; для данного устройства это приемлемо.
  ******************************************************************************
  */
#include "extstore.h"
#include "sfmap.h"
#include "spiflash.h"
#include "tx_api.h"
#include <string.h>

static TX_MUTEX ext_mtx;
static uint8_t  ext_ready = 0;

volatile uint32_t ext_dbg_log_sector = 0;
volatile uint32_t ext_dbg_log_offset = 0;
volatile uint32_t ext_dbg_cfg_seq    = 0;

/* ------------------------------------------------------------------ CRC --- */
static uint32_t crc32(const uint8_t *p, uint32_t n)
{
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t i;
  int k;
  for (i = 0; i < n; i++)
  {
    crc ^= p[i];
    for (k = 0; k < 8; k++)
    {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)); }
static void     wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void     wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* ------------------------------------------------------------- блокировка - */
static void lock(void)   { if (ext_ready) { (void)tx_mutex_get(&ext_mtx, TX_WAIT_FOREVER); } }
static void unlock(void) { if (ext_ready) { (void)tx_mutex_put(&ext_mtx); } }

/* ======================================================== сырой доступ ==== */
int ext_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
  int rc;
  lock();
  rc = (sf_read(addr, dst, len) == HAL_OK) ? 0 : -1;
  unlock();
  return rc;
}

int ext_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
  int rc;
  lock();
  rc = (sf_program(addr, src, len) == HAL_OK) ? 0 : -1;
  unlock();
  return rc;
}

int ext_erase_sector(uint32_t addr)
{
  int rc;
  lock();
  rc = (sf_sector_erase(addr) == HAL_OK) ? 0 : -1;
  unlock();
  return rc;
}

/* ========================================================= конфигурация ===
 * Кольцо из 32 секторов по 4 КБ внутри SF_CONFIG_BASE.
 * В секторе одна запись: [u32 seq][u16 len][u16 pad][u32 crc][данные]
 * Пишем в следующий сектор по кругу, предварительно стирая его:
 * второй-по-свежести сектор всегда остаётся нетронутым => защита от сбоя.
 */
#define CFG_SECTORS      (SF_CONFIG_SIZE / SF_SECTOR_SIZE)      /* 32 */
#define CFG_HDR          12u
#define CFG_MAX_DATA     (SF_SECTOR_SIZE - CFG_HDR)

static uint32_t cfg_sector_addr(uint32_t i) { return SF_CONFIG_BASE + i * SF_SECTOR_SIZE; }

/* читает заголовок сектора; возвращает 1 если запись валидна */
static int cfg_peek(uint32_t i, uint32_t *seq, uint16_t *len)
{
  uint8_t h[CFG_HDR];
  if (sf_read(cfg_sector_addr(i), h, sizeof h) != HAL_OK) { return 0; }
  *seq = rd32(h);
  *len = rd16(h + 4);
  if ((*seq == 0xFFFFFFFFu) || (*len == 0xFFFFu) || (*len > CFG_MAX_DATA)) { return 0; }
  return 1;
}

int cfg_load(void *data, uint16_t max_len, uint16_t *out_len)
{
  uint32_t best_i = 0xFFFFFFFFu, best_seq = 0;
  uint32_t i;
  int found = 0;

  lock();
  for (i = 0; i < CFG_SECTORS; i++)
  {
    uint32_t seq; uint16_t len;
    if (cfg_peek(i, &seq, &len) && (!found || (int32_t)(seq - best_seq) > 0))
    {
      best_i = i; best_seq = seq; found = 1;
    }
  }
  if (found)
  {
    uint8_t h[CFG_HDR];
    uint16_t len;
    sf_read(cfg_sector_addr(best_i), h, sizeof h);
    len = rd16(h + 4);
    if (len <= max_len)
    {
      uint8_t *body = (uint8_t *)data;
      if (sf_read(cfg_sector_addr(best_i) + CFG_HDR, body, len) == HAL_OK)
      {
        uint32_t c = crc32(body, len);
        if (c == rd32(h + 8))
        {
          if (out_len) { *out_len = len; }
          ext_dbg_cfg_seq = best_seq;
          unlock();
          return 0;
        }
      }
    }
    found = 0;   /* CRC/размер не сошлись - считаем, что конфига нет */
  }
  unlock();
  return found ? 0 : -1;
}

int cfg_save(const void *data, uint16_t len)
{
  uint32_t i, seq = 0, cur_i = 0xFFFFFFFFu;
  uint8_t h[CFG_HDR];
  int rc = -1;

  if (len > CFG_MAX_DATA) { return -1; }

  lock();
  for (i = 0; i < CFG_SECTORS; i++)
  {
    uint32_t s; uint16_t l;
    if (cfg_peek(i, &s, &l) && (cur_i == 0xFFFFFFFFu || (int32_t)(s - seq) > 0))
    {
      cur_i = i; seq = s;
    }
  }
  {
    uint32_t target = (cur_i == 0xFFFFFFFFu) ? 0u : ((cur_i + 1u) % CFG_SECTORS);
    if (sf_sector_erase(cfg_sector_addr(target)) == HAL_OK)
    {
      wr32(h, seq + 1u);
      wr16(h + 4, len);
      wr16(h + 6, 0);
      wr32(h + 8, crc32((const uint8_t *)data, len));
      if (sf_program(cfg_sector_addr(target), h, sizeof h) == HAL_OK)
      {
        rc = (sf_program(cfg_sector_addr(target) + CFG_HDR, (const uint8_t *)data, len) == HAL_OK) ? 0 : -1;
        if (rc == 0) { ext_dbg_cfg_seq = seq + 1u; }
      }
    }
  }
  unlock();
  return rc;
}

/* ================================================================ журнал ==
 * Сектор: [u32 magic 'LOGS'][u32 seq_sector] далее записи подряд:
 *         [u16 len][u16 pad][u32 crc][данные]
 * Заполнен до конца -> следующий сектор стирается, seq + 1.
 */
#define LOG_HDR       8u
#define LOG_REC_HDR   8u
#define LOG_MAGIC     0x53474F4Cu   /* 'LOGS' */
#define LOG_MAX_DATA  (SF_SECTOR_SIZE - LOG_HDR - LOG_REC_HDR)

static uint32_t log_sector = 0;    /* индекс текущего сектора                */
static uint32_t log_off    = LOG_HDR;   /* смещение следующей записи         */
static uint32_t log_seq    = 0;

static uint32_t log_addr(uint32_t i) { return SF_LOG_BASE + i * SF_SECTOR_SIZE; }

static int log_peek_hdr(uint32_t i, uint32_t *seq)
{
  uint8_t h[LOG_HDR];
  if (sf_read(log_addr(i), h, sizeof h) != HAL_OK) { return 0; }
  if (rd32(h) != LOG_MAGIC) { return 0; }
  *seq = rd32(h + 4);
  return 1;
}

/* ищет конец записей в секторе i: первое смещение, где len == 0xFFFF */
static uint32_t log_scan_end(uint32_t i)
{
  uint32_t off = LOG_HDR;
  while (off + LOG_REC_HDR <= SF_SECTOR_SIZE)
  {
    uint8_t rh[LOG_REC_HDR];
    uint16_t len;
    if (sf_read(log_addr(i) + off, rh, sizeof rh) != HAL_OK) { break; }
    len = rd16(rh);
    if ((len == 0xFFFFu) || (len > LOG_MAX_DATA)) { break; }
    off += LOG_REC_HDR + len;
    if (off > SF_SECTOR_SIZE) { off = SF_SECTOR_SIZE; break; }
  }
  return off;
}

static int log_open_sector(uint32_t i, uint32_t seq)
{
  uint8_t h[LOG_HDR];
  if (sf_sector_erase(log_addr(i)) != HAL_OK) { return -1; }
  wr32(h, LOG_MAGIC);
  wr32(h + 4, seq);
  if (sf_program(log_addr(i), h, sizeof h) != HAL_OK) { return -1; }
  log_sector = i;
  log_off    = LOG_HDR;
  log_seq    = seq;
  ext_dbg_log_sector = i;
  ext_dbg_log_offset = log_off;
  return 0;
}

int log_append(const void *data, uint16_t len)
{
  int rc = -1;
  if (len > LOG_MAX_DATA) { return -1; }

  lock();
  if (log_off + LOG_REC_HDR + len > SF_SECTOR_SIZE)
  {
    /* текущий сектор полон -> следующий по кругу */
    if (log_open_sector((log_sector + 1u) % SF_LOG_SECTORS, log_seq + 1u) != 0)
    {
      unlock();
      return -1;
    }
  }
  {
    uint8_t rh[LOG_REC_HDR];
    wr16(rh, len);
    wr16(rh + 2, 0);
    wr32(rh + 4, crc32((const uint8_t *)data, len));
    if (sf_program(log_addr(log_sector) + log_off, rh, sizeof rh) == HAL_OK)
    {
      if (sf_program(log_addr(log_sector) + log_off + LOG_REC_HDR,
                     (const uint8_t *)data, len) == HAL_OK)
      {
        log_off += LOG_REC_HDR + len;
        ext_dbg_log_offset = log_off;
        rc = 0;
      }
    }
  }
  unlock();
  return rc;
}

/* курсор: (sector << 13) | offset */
int log_next(uint32_t *handle, void *data, uint16_t max_len, uint16_t *out_len)
{
  uint32_t cur, sector, off;
  int rc = 1;

  lock();
  cur = *handle;
  if (cur == 0u)
  {
    /* старт с самого старого доступного: следующий после текущего */
    sector = (log_sector + 1u) % SF_LOG_SECTORS;
    off = LOG_HDR;
    /* пропускаем пустые (нестёртые/стёртые без magic) сектора */
    while (sector != log_sector)
    {
      uint32_t s;
      if (log_peek_hdr(sector, &s)) { break; }
      sector = (sector + 1u) % SF_LOG_SECTORS;
    }
    if (sector == log_sector) { off = LOG_HDR; }   /* журнал пуст: начнём с текущего */
  }
  else
  {
    sector = (cur >> 13) & 0x7FFFFu;
    off    = cur & 0x1FFFu;
  }

  while (rc == 1)
  {
    uint8_t rh[LOG_REC_HDR];
    uint16_t len;
    uint32_t limit = (sector == log_sector) ? log_off : SF_SECTOR_SIZE;

    if (off + LOG_REC_HDR > limit)
    {
      if (sector == log_sector) { break; }             /* дошли до конца нового */
      sector = (sector + 1u) % SF_LOG_SECTORS;
      off = LOG_HDR;
      continue;
    }
    if (sf_read(log_addr(sector) + off, rh, sizeof rh) != HAL_OK) { rc = -1; break; }
    len = rd16(rh);
    if ((len == 0xFFFFu) || (len > LOG_MAX_DATA))
    {
      if (sector == log_sector) { break; }
      sector = (sector + 1u) % SF_LOG_SECTORS;
      off = LOG_HDR;
      continue;
    }
    if (off + LOG_REC_HDR + len > limit)
    {
      if (sector == log_sector) { break; }
      sector = (sector + 1u) % SF_LOG_SECTORS;
      off = LOG_HDR;
      continue;
    }
    /* валидная запись */
    if (len <= max_len)
    {
      if (sf_read(log_addr(sector) + off + LOG_REC_HDR, (uint8_t *)data, len) == HAL_OK)
      {
        uint32_t c = crc32((const uint8_t *)data, len);
        if (c == rd32(rh + 4))
        {
          if (out_len) { *out_len = len; }
          off += LOG_REC_HDR + len;
          *handle = (sector << 13) | off;
          rc = 0;
          break;
        }
      }
      rc = -1;
      break;
    }
    rc = -2;   /* запись длиннее буфера вызывающего */
    off += LOG_REC_HDR + len;
    *handle = (sector << 13) | off;
    break;
  }

  if (rc == 1) { *handle = 0u; }   /* обход завершён - сброс курсора */
  unlock();
  return rc;
}

/* =============================================================== init ===== */
void ext_init(void)
{
  uint32_t i, best = 0xFFFFFFFFu, best_seq = 0;

  (void)tx_mutex_create(&ext_mtx, "sf bus");
  ext_ready = 1;

  /* найти самый свежий сектор журнала */
  for (i = 0; i < SF_LOG_SECTORS; i++)
  {
    uint32_t s;
    if (log_peek_hdr(i, &s) && (best == 0xFFFFFFFFu || (int32_t)(s - best_seq) > 0))
    {
      best = i; best_seq = s;
    }
  }
  if (best == 0xFFFFFFFFu)
  {
    (void)log_open_sector(0, 1u);
  }
  else
  {
    log_sector = best;
    log_seq    = best_seq;
    log_off    = log_scan_end(best);
    if (log_off >= SF_SECTOR_SIZE)   /* сектор полон - перейдём на следующий */
    {
      (void)log_open_sector((best + 1u) % SF_LOG_SECTORS, best_seq + 1u);
    }
    ext_dbg_log_sector = log_sector;
    ext_dbg_log_offset = log_off;
  }
}
