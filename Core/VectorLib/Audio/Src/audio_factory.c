/**
  ******************************************************************************
  * @file    audio_factory.c
  * @brief   Заводская запись образа звуков во внешнюю flash (см. заголовок)
  ******************************************************************************
  */
#include "audio_factory.h"

#if VECTOR_AUDIO_FACTORY_EMBED

#include "extstore.h"
#include "sfmap.h"
#include "spiflash.h"
#include "vector_log.h"
#include <string.h>

/* генерируется tools/bin2c.py из tools/sounds.img */
extern const uint8_t  vector_factory_img[];
extern const uint32_t vector_factory_img_size;

volatile uint32_t factory_dbg_sector = 0;   /* на каком секторе сейчас стоим */

/* Заводская запись встроенного образа sounds.img во внешнюю flash.
   КОНТЕКСТ: поток плеера (ap_thread_entry), ПОСЛЕ старта планировщика -
   внутри ext_erase/ext_write/ext_read берут мьютекс шины.
   Три шага: стирание области SOUNDS -> запись секторными кусками ->
   побайтная верификация. factory_dbg_sector показывает прогресс (номер
   сектора), если процесс покажется зависшим: стирание 48 секторов + запись
   195 КБ - это секунды, а не миллисекунды.
   Возврат: 0 = образ записан и проверен, -1 = ошибка на любом шаге.
   ВАЖНО: вызывается ТОЛЬКО если audio_init() не нашёл валидного образа.
   Раньше load_image() не вызывался, ap_img_ok всегда был 0, и эта функция
   стирала и писала 195 КБ на КАЖДОЙ перезагрузке (см. docs/FIX_REPORT.md).  */
int audio_factory_program(void)
{
  static uint8_t vb[256];
  uint32_t off, chunk;

  if ((vector_factory_img_size == 0u) || (vector_factory_img_size > SF_SOUNDS_SIZE))
  {
    LOG_E(VLOG_M_FLASH, "factory: bad image size %u", vector_factory_img_size);
    return -1;
  }

  LOG_I(VLOG_M_FLASH, "factory: programming %u b (%u sectors), takes seconds",
        vector_factory_img_size, (vector_factory_img_size + SF_SECTOR_SIZE - 1u) / SF_SECTOR_SIZE);

  /* 1. Стирание: NOR стирается секторами по 4 КБ, только свою область SOUNDS */
  for (off = 0; off < vector_factory_img_size; off += SF_SECTOR_SIZE)
  {
    factory_dbg_sector = off / SF_SECTOR_SIZE;
    if (ext_erase_sector(SF_SOUNDS_BASE + off) != 0)
    {
      LOG_E(VLOG_M_FLASH, "factory: erase fail at sector %u", factory_dbg_sector);
      return -1;
    }
    if ((factory_dbg_sector % 8u) == 0u)
    {
      LOG_I(VLOG_M_FLASH, "factory: erased up to sector %u", factory_dbg_sector);
    }
  }

  /* 2. Запись секторными кусками (ext_write сам дробит по страницам 256 Б) */
  for (off = 0; off < vector_factory_img_size; off += SF_SECTOR_SIZE)
  {
    chunk = vector_factory_img_size - off;
    if (chunk > SF_SECTOR_SIZE) { chunk = SF_SECTOR_SIZE; }
    factory_dbg_sector = off / SF_SECTOR_SIZE;
    if (ext_write(SF_SOUNDS_BASE + off, &vector_factory_img[off], chunk) != 0)
    {
      LOG_E(VLOG_M_FLASH, "factory: write fail at sector %u", factory_dbg_sector);
      return -1;
    }
  }

  /* 3. Верификация побайтно */
  for (off = 0; off < vector_factory_img_size; off += (uint32_t)sizeof vb)
  {
    chunk = vector_factory_img_size - off;
    if (chunk > (uint32_t)sizeof vb) { chunk = (uint32_t)sizeof vb; }
    if (ext_read(SF_SOUNDS_BASE + off, vb, chunk) != 0)
    {
      LOG_E(VLOG_M_FLASH, "factory: verify read fail at %u", off);
      return -1;
    }
    if (memcmp(vb, &vector_factory_img[off], chunk) != 0)
    {
      LOG_E(VLOG_M_FLASH, "factory: verify MISMATCH at %u", off);
      return -1;
    }
  }
  LOG_I(VLOG_M_FLASH, "factory: done, %u b written and verified", vector_factory_img_size);
  return 0;
}

#endif /* VECTOR_AUDIO_FACTORY_EMBED */
