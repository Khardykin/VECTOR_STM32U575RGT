/**
  ******************************************************************************
  * @file    audio_image.h
  * @brief   Формат образа sounds.img для внешней SPI flash
  *
  *          Образ собирает tools/pack_sounds.py. Прошивка читает заголовок и
  *          таблицу прямо из флеш, поэтому отдельные C-массивы не нужны.
  *
  *          Раскладка (little-endian):
  *
  *          смещение  размер  поле
  *          --------  ------  --------------------------------------------
  *          0         u32     magic = 'S','N','D','I'
  *          4         u16     version = 1
  *          6         u16     count   (число звуков)
  *          8         u32     table_off  (= 24)
  *          12        u32     data_off   (= 24 + count * 36)
  *          16        u32     total_size (весь образ, байт)
  *          20        u32     table_crc32 (CRC32 таблицы записей)
  *          24        ...     записи AUDIO_IMG_ENTRY[count], по 36 байт
  *          data_off  ...     PCM-блоки (каждый выровнен на 4 байта)
  *
  *          Запись AUDIO_IMG_ENTRY (36 байт):
  *          0   u32  offset   от начала образа
  *          4   u32  length   длина PCM в байтах
  *          8   u32  samples  число сэмплов
  *          12  u16  rate     частота, Гц
  *          14  u8   channels 1 = моно
  *          15  u8   format   AUDIO_FMT_PCM16 = 0
  *          16  u32  crc32    CRC32 PCM-блока
  *          20  char name[16] имя (NUL-дополнено)
  ******************************************************************************
  */
#ifndef AUDIO_IMAGE_H
#define AUDIO_IMAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_IMG_MAGIC        0x49444E53u   /* 'S','N','D','I' в little-endian */
#define AUDIO_IMG_VERSION      1u
#define AUDIO_IMG_HEADER_SIZE  24u
#define AUDIO_IMG_ENTRY_SIZE   36u
#define AUDIO_IMG_NAME_LEN     16u

#define AUDIO_FMT_PCM16        0u   /* 16-битное little-endian PCM            */
#define AUDIO_FMT_ADPCM_IMA    1u   /* зарезервировано под IMA-ADPCM          */

/* База, с которой образ лежит во внешней флеш.
   8 МБ чип: образ кладём с начала, прошивку внутренней флеш не трогаем. */
#define AUDIO_IMG_BASE_ADDR    0x00000000u

typedef struct __attribute__((packed))
{
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t table_off;
  uint32_t data_off;
  uint32_t total_size;
  uint32_t table_crc32;
} audio_img_header_t;

typedef struct __attribute__((packed))
{
  uint32_t offset;
  uint32_t length;
  uint32_t samples;
  uint16_t rate;
  uint8_t  channels;
  uint8_t  format;
  uint32_t crc32;
  char     name[AUDIO_IMG_NAME_LEN];
} audio_img_entry_t;

/* Проверка размеров на этапе компиляции: если формат разъедется с pack_sounds.py,
   сборка упадёт здесь, а не на железе. */
typedef char audio_img_static_assert_header
    [(sizeof(audio_img_header_t) == AUDIO_IMG_HEADER_SIZE) ? 1 : -1];
typedef char audio_img_static_assert_entry
    [(sizeof(audio_img_entry_t) == AUDIO_IMG_ENTRY_SIZE) ? 1 : -1];

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_IMAGE_H */
