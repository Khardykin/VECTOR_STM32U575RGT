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
#include "vector_tick.h"
#include <string.h>

/* генерируется tools/bin2c.py из tools/sounds.img */
extern const uint8_t  vector_factory_img[];
extern const uint32_t vector_factory_img_size;

volatile uint32_t factory_dbg_sector = 0;   /* на каком секторе сейчас стоим */

/* Попыток прочитать один верификационный кусок (>= 1). См. vector_config.h:
   VECTOR_AUDIO_FACTORY_VERIFY_RETRY. Ниже - защита от невыгодной конфигурации,
   чтобы цикл верификации не вырождался в "не читать вообще" (0 попыток) или
   бесконечный ретрай. */
#if (VECTOR_AUDIO_FACTORY_VERIFY_RETRY < 1)
#define FACTORY_VERIFY_TRIES  1u
#elif (VECTOR_AUDIO_FACTORY_VERIFY_RETRY > 16)
#define FACTORY_VERIFY_TRIES  16u
#else
#define FACTORY_VERIFY_TRIES  ((uint32_t)VECTOR_AUDIO_FACTORY_VERIFY_RETRY)
#endif

#define FACTORY_VERIFY_RETRY_DELAY_MS  2u   /* дать halt'у отладчика пройти */

/* Заводская запись встроенного образа sounds.img во внешнюю flash.
   КОНТЕКСТ: поток плеера (ap_thread_entry), ПОСЛЕ старта планировщика -
   внутри ext_erase/ext_write/ext_read берут мьютекс шины.
   Три шага: стирание области SOUNDS -> запись секторными кусками ->
   побайтная верификация. factory_dbg_sector показывает прогресс (номер
   сектора), если процесс покажется зависшим: стирание 48 секторов + запись
   195 КБ - это секунды, а не миллисекунды.
   Возврат: 0 = образ записан и проверен; отрицательный код = ошибка:
     -1 = стирание/запись не удались (образа во flash фактически нет);
     -2 = верификация нашла РАСХОЖДЕНИЕ байт (реальная порча записи);
     -3 = верификация не смогла ПРОЧИТАТЬ кусок даже после повторов. Почти
          всегда это транзиентный таймаут SPI под отладчиком (ядро halt'ится
          на Live Watch), сами данные при этом целы - вызывающий должен
          перечитать образ и решить по факту, а не по этому коду.
   ВАЖНО: вызывается ТОЛЬКО если audio_init() не нашёл валидного образа.
   Раньше load_image() не вызывался, ap_img_ok всегда был 0, и эта функция
   стирала и писала 195 КБ на КАЖДОЙ перезагрузке (см. docs/archive/FIX_REPORT.md).  */
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

  /* 3. Верификация побайтно.

     Чтение здесь блокирующим опросом SPI (256 Б < VECTOR_SPI_DMA_MIN_LEN),
     поэтому под отладчиком оно изредка возвращается по таймауту HAL (st=3,
     0 байт): ядро halt'ится на Live Watch/Expressions refresh прямо во время
     обмена. Это глюк ЧТЕНИЯ, а не записи - байты во flash уже лежат. Раньше
     такой один сбой обрывал всю factory-запись и плеер уходил в beep only.
     Поэтому сбойное чтение повторяем VECTOR_AUDIO_FACTORY_VERIFY_RETRY раз,
     а РАСХОЖДЕНИЕ данных (реальную порчу) не ретраим и отличаем кодом -2.  */
  for (off = 0; off < vector_factory_img_size; off += (uint32_t)sizeof vb)
  {
    uint32_t tries;

    chunk = vector_factory_img_size - off;
    if (chunk > (uint32_t)sizeof vb) { chunk = (uint32_t)sizeof vb; }

    for (tries = 0; tries < FACTORY_VERIFY_TRIES; tries++)
    {
      if (ext_read(SF_SOUNDS_BASE + off, vb, chunk) == 0)
      {
        break;
      }
      if ((tries + 1u) < FACTORY_VERIFY_TRIES)
      {
        LOG_W(VLOG_M_FLASH, "factory: verify read retry %u at %u",
              tries + 1u, off);
        if (VTICK_IN_THREAD())
        {
          VTICK_SLEEP_MS(FACTORY_VERIFY_RETRY_DELAY_MS);
        }
      }
    }
    if (tries >= FACTORY_VERIFY_TRIES)
    {
      LOG_E(VLOG_M_FLASH, "factory: verify read fail at %u after %u tries",
            off, FACTORY_VERIFY_TRIES);
      return -3;
    }
    if (memcmp(vb, &vector_factory_img[off], chunk) != 0)
    {
      LOG_E(VLOG_M_FLASH, "factory: verify MISMATCH at %u", off);
      return -2;
    }
  }
  LOG_I(VLOG_M_FLASH, "factory: done, %u b written and verified", vector_factory_img_size);
  return 0;
}

/* Отпечаток встроенного образа против того, что лежит во внешней flash.
   Подробное описание и причина появления - в audio_factory.h.

   Заголовок копируем через memcpy, а не приводим указатель: vector_factory_img
   объявлен как const uint8_t[], компилятор не обязан выравнивать его на 4, а
   audio_img_header_t содержит uint32_t. На Cortex-M33 невыровненное чтение
   слова допустимо, но memcpy снимает вопрос целиком и стоит 24 байта.        */
int audio_factory_is_stale(const audio_img_header_t *stored)
{
  audio_img_header_t emb;

  if (stored == 0)
  {
    return -1;
  }
  if (vector_factory_img_size < AUDIO_IMG_HEADER_SIZE)
  {
    return -1;   /* встроенного образа фактически нет */
  }

  memcpy(&emb, vector_factory_img, sizeof emb);

  /* magic/version не сравниваем нарочно: их несовпадение ловит load_image(),
     а здесь нас интересует именно "та же ли это СБОРКА звуков".            */
  if ((emb.total_size   != stored->total_size)   ||
      (emb.count        != stored->count)        ||
      (emb.table_crc32  != stored->table_crc32))
  {
    LOG_W(VLOG_M_FLASH,
          "factory: flash image is STALE (flash size=%u crc=%x n=%u, "
          "embedded size=%u crc=%x n=%u)",
          stored->total_size, stored->table_crc32, (uint32_t)stored->count,
          emb.total_size, emb.table_crc32, (uint32_t)emb.count);
    return 1;
  }
  return 0;
}

#endif /* VECTOR_AUDIO_FACTORY_EMBED */
