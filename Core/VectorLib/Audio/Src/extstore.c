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
#include "vector_log.h"
#include "tx_api.h"
#include <string.h>

static TX_MUTEX ext_mtx;
static uint8_t  ext_ready = 0;

volatile uint32_t ext_dbg_log_sector = 0;
volatile uint32_t ext_dbg_log_offset = 0;
volatile uint32_t ext_dbg_cfg_seq    = 0;

/* ------------------------------------------------------------------ CRC --- */
/* CRC32 (полином zlib/IEEE, как в pack_sounds.py) по буферу в RAM.
   Контекст: поток, под мьютексом шины. Медленная побитовая версия без
   таблицы - экономим 1 КБ flash, объёмы здесь небольшие.                   */
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

/* Чтение/запись little-endian полей записей конфига и журнала из байтового
   буфера. Контекст: любой, чистая арифметика.                              */
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)); }
static void     wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void     wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* ------------------------------------------------------------- блокировка - */
/* Взять/отпустить мьютекс шины flash. До ext_init() (ext_ready == 0)
   блокировка не делается: в этот момент исполнитель ровно один.
   КОНТЕКСТ: только поток - из ISR tx_mutex_get вызывать нельзя.            */
static void lock(void)   { if (ext_ready) { (void)tx_mutex_get(&ext_mtx, TX_WAIT_FOREVER); } }
static void unlock(void) { if (ext_ready) { (void)tx_mutex_put(&ext_mtx); } }

/* ======================================================== сырой доступ ==== */
/* Публичное чтение из внешней flash под мьютексом. Именно его зовёт плеер для
   подкачки звука, поэтому большие блоки внутри уходят в DMA (см. sf_read):
   мьютекс держится на всё чтение, но CPU в это время свободен.
   Возврат: 0 = ok, -1 = ошибка шины/адреса.                                */
int ext_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
  int rc;
  lock();
  rc = (sf_read(addr, dst, len) == HAL_OK) ? 0 : -1;
  unlock();
  return rc;
}

/* Публичная запись (БЕЗ стирания) под мьютексом. Область должна быть стёрта.
   Возврат: 0 = ok, -1 = ошибка.                                            */
int ext_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
  int rc;
  lock();
  rc = (sf_program(addr, src, len) == HAL_OK) ? 0 : -1;
  unlock();
  return rc;
}

/* Стирание сектора 4 КБ под мьютексом (~40-100 мс, поток крутится в опросе
   WIP). Возврат: 0 = ok, -1 = ошибка/неверный адрес.                       */
int ext_erase_sector(uint32_t addr)
{
  int rc;
  lock();
  rc = (sf_sector_erase(addr) == HAL_OK) ? 0 : -1;
  unlock();
  return rc;
}

/* ========================================================= конфигурация ===
 * РОВНО ОДНА страница 4 КБ (SF_CONFIG_BASE), см. sfmap.h.
 * Запись: [u32 seq][u16 len][u16 pad][u32 crc][данные]
 * Записи ДОБАВЛЯЮТСЯ в страницу без стирания: свежая = максимальный seq
 * среди валидных по CRC. Страница заполнилась -> стираем и пишем одну
 * актуальную. Благодаря нескольким копиям в странице оборванная запись
 * не убивает конфиг: ниже по странице живёт предыдущая валидная.
 */
#define CFG_HDR       12u
#define CFG_MAX_DATA  (SF_SECTOR_SIZE - CFG_HDR - CFG_HDR)

/* CRC32 потоком, без большого буфера (вызывать под мьютексом) */
/* CRC32 данных ПРЯМО ИЗ FLASH потоком по 64 байта, без большого буфера.
   Возвращает 0 при ошибке чтения (в нашей схеме 0 не бывает валидным CRC).
   Контекст: поток, вызывается под мьютексом.                               */
static uint32_t crc32_stream(uint32_t addr, uint32_t len)
{
  uint8_t buf[64];
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t done = 0;
  while (done < len)
  {
    uint32_t chunk = (len - done > sizeof buf) ? sizeof buf : (len - done);
    uint32_t i;
    int k;
    if (sf_read(addr + done, buf, chunk) != HAL_OK) { return 0; }
    for (i = 0; i < chunk; i++)
    {
      crc ^= buf[i];
      for (k = 0; k < 8; k++)
      {
        crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
      }
    }
    done += chunk;
  }
  return crc ^ 0xFFFFFFFFu;
}

/* Проход по записям страницы. Возвращает смещение первой НЕ-записи (конец),
   попутно находя самую свежую валидную: *best_off / *best_seq / *best_len. */
/* Обход страницы конфига: ищет самую свежую валидную по CRC запись
   (best_off / best_seq / best_len) и возвращает смещение конца записей -
   туда пишется следующая. Контекст: поток, под мьютексом.                  */
static uint32_t cfg_scan(uint32_t *best_off, uint32_t *best_seq, uint16_t *best_len)
{
  uint32_t off = 0;
  *best_off = 0xFFFFFFFFu;
  *best_seq = 0;
  *best_len = 0;

  while (off + CFG_HDR <= SF_SECTOR_SIZE)
  {
    uint8_t h[CFG_HDR];
    uint32_t seq, crc;
    uint16_t len;
    if (sf_read(SF_CONFIG_BASE + off, h, sizeof h) != HAL_OK) { break; }
    seq = rd32(h);
    len = rd16(h + 4);
    crc = rd32(h + 8);
    if ((seq == 0xFFFFFFFFu) || (len == 0xFFFFu) ||
        (len > CFG_MAX_DATA) || ((off + CFG_HDR + len) > SF_SECTOR_SIZE))
    {
      break;                                   /* стёрто/испорчено - конец */
    }
    if (crc32_stream(SF_CONFIG_BASE + off + CFG_HDR, len) == crc)
    {
      if ((*best_off == 0xFFFFFFFFu) || ((int32_t)(seq - *best_seq) > 0))
      {
        *best_off = off;
        *best_seq = seq;
        *best_len = len;
      }
    }
    off += CFG_HDR + len;
  }
  return off;
}

/* Пишет одну запись конфига [заголовок 12 Б][данные] по смещению off.
   Две отдельные sf_program: NOR нельзя программировать "вразнобой" по
   одному месту дважды. Возврат: 0 = ok, -1 = ошибка.                       */
static int cfg_write_record(uint32_t off, uint32_t seq, const void *data, uint16_t len)
{
  uint8_t h[CFG_HDR];
  wr32(h, seq);
  wr16(h + 4, len);
  wr16(h + 6, 0);
  wr32(h + 8, crc32((const uint8_t *)data, len));
  if (sf_program(SF_CONFIG_BASE + off, h, sizeof h) != HAL_OK) { return -1; }
  return (sf_program(SF_CONFIG_BASE + off + CFG_HDR, (const uint8_t *)data, len) == HAL_OK) ? 0 : -1;
}

/* Прочитать актуальный конфиг в буфер вызывающего. Если валидной записи нет
   (чистая flash) - вернёт -1, и вызывающий обязан подставить значения по
   умолчанию. Возврат: 0 = ok, -1 = нет конфига/ошибка (буфер не тронут).   */
int cfg_load(void *data, uint16_t max_len, uint16_t *out_len)  /* лог в конце */
{
  uint32_t best_off, best_seq;
  uint16_t best_len;
  int rc = -1;

  lock();
  (void)cfg_scan(&best_off, &best_seq, &best_len);
  if ((best_off != 0xFFFFFFFFu) && (best_len <= max_len))
  {
    if (sf_read(SF_CONFIG_BASE + best_off + CFG_HDR, (uint8_t *)data, best_len) == HAL_OK)
    {
      if (out_len) { *out_len = best_len; }
      ext_dbg_cfg_seq = best_seq;
      rc = 0;
    }
  }
  if (rc == 0) { LOG_I(VLOG_M_FLASH, "cfg loaded: seq=%u len=%u", best_seq, (uint32_t)best_len); }
  else         { LOG_W(VLOG_M_FLASH, "cfg not found - будут значения по умолчанию"); }
  unlock();
  return rc;
}

/* Сохранить конфиг: добавляет запись в хвост страницы, а когда место кончилось
   - стирает страницу и пишет одну актуальную запись (seq + 1). Обрыв питания
   посреди записи не страшен: предыдущая копия ниже по странице валидна.
   Возврат: 0 = ok, -1 = ошибка/слишком длинно.                             */
int cfg_save(const void *data, uint16_t len)
{
  uint32_t end, best_off, best_seq;
  uint16_t best_len;
  int rc;

  if (len > CFG_MAX_DATA) { return -1; }

  lock();
  end = cfg_scan(&best_off, &best_seq, &best_len);
  if ((end + CFG_HDR + len) > SF_SECTOR_SIZE)
  {
    /* страница заполнилась: стираем и пишем одну актуальную запись */
    if (sf_sector_erase(SF_CONFIG_BASE) != HAL_OK) { unlock(); return -1; }
    rc = cfg_write_record(0, best_seq + 1u, data, len);
  }
  else
  {
    rc = cfg_write_record(end, best_seq + 1u, data, len);
  }
  if (rc == 0)
  {
    ext_dbg_cfg_seq = best_seq + 1u;
    LOG_I(VLOG_M_FLASH, "cfg saved: seq=%u len=%u%s", ext_dbg_cfg_seq, (uint32_t)len,
          ((end + CFG_HDR + len) > SF_SECTOR_SIZE) ? " (страница стёрта)" : "");
  }
  else
  {
    LOG_E(VLOG_M_FLASH, "cfg save FAIL len=%u", (uint32_t)len);
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

/* Адрес i-го сектора журнала. */
static uint32_t log_addr(uint32_t i) { return SF_LOG_BASE + i * SF_SECTOR_SIZE; }

/* Прочитать заголовок сектора журнала (magic 'LOGS' + seq).
   Возврат: 1 = сектор валиден (*seq = его номер), 0 = пустой/чужой.        */
static int log_peek_hdr(uint32_t i, uint32_t *seq)
{
  uint8_t h[LOG_HDR];
  if (sf_read(log_addr(i), h, sizeof h) != HAL_OK) { return 0; }
  if (rd32(h) != LOG_MAGIC) { return 0; }
  *seq = rd32(h + 4);
  return 1;
}

/* ищет конец записей в секторе i: первое смещение, где len == 0xFFFF */
/* Найти конец записей в секторе i: первое смещение, где длина = 0xFFFF
   (стёртая flash). Возврат: смещение под следующую запись.                 */
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

/* Сделать сектор i текущим: стереть, записать magic+seq, сбросить курсор.
   Возврат: 0 = ok, -1 = ошибка стирания/записи.                            */
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

/* Дописать запись в журнал. Если текущий сектор полон - автоматически
   открывается следующий по кругу (со стиранием, ~100 мс под мьютексом).
   Возврат: 0 = ok, -1 = ошибка/слишком длинно.                             */
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
        LOG_D(VLOG_M_FLASH, "log append %u b -> sec %u off %u", (uint32_t)len,
              log_sector, log_off);
      }
    }
  }
  unlock();
  return rc;
}

/* курсор: (sector << 13) | offset */
/* Итератор журнала от старого к новому. *handle - курсор (0 = начать с самого
   старого доступного). Возврат: 0 = запись выдана, 1 = обход завершён
   (курсор сброшен), -1 = ошибка чтения, -2 = запись не влезла в буфер.     */
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
/* Инициализация хранилища. КОНТЕКСТ: tx_application_define(), ДО audio_init()
   и до потоков. Делает три вещи:
     1) мьютекс шины flash;
     2) семафор SPI-DMA (sf_dma_init) - создаётся до потоков, а спать на нём
        начнут только потоки: до старта планировщика sf_read() ходит опросом;
     3) сканирование журнала: ищет сектор с максимальным seq и ставит курсор
        записи в его конец; журнал пуст - открывает сектор 0.
   Здесь же возможны стирание и много мелких чтений по SPI (сотни мс),
   поэтому ext_init() и вызывается до планировщика.                          */
void ext_init(void)
{
  uint32_t i, best = 0xFFFFFFFFu, best_seq = 0;

  (void)tx_mutex_create(&ext_mtx, "sf bus", TX_NO_INHERIT);
  ext_ready = 1;
  sf_dma_init();          /* семафор для DMA-чтений spiflash.c */

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
