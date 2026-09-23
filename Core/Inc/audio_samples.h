/**
  ******************************************************************************
  * @file    audio_samples.h
  * @brief   Доступ к PCM-массивам из audio_samples.c
  ******************************************************************************
  *  sound_gas_warning[] теперь содержит ЧИСТЫЕ PCM-данные, БЕЗ WAV-заголовка.
  *  С нулевого элемента идут сэмплы, ничего пропускать не нужно.
  *
  *  Параметры (прочитаны из fmt-чанка исходного файла):
  *      PCM, 1 канал, 16 бит, 16000 Гц, 29536 сэмплов = 1.846 с
  *      пик 22.9% от full scale, DC offset +103
  *
  *  Файл сгенерирован tools/wav2c.py из assets/sound_gas_warning.wav.
  *  Прежняя версия (массив вместе с WAV-заголовком) лежит в
  *  backup/audio_samples_WITH_wav_header.c.bak
  ******************************************************************************
  */
#ifndef AUDIO_SAMPLES_H
#define AUDIO_SAMPLES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- PCM-массив ------------------------------------------------------------
 * sound_gas_warning_size = 29536 = ЧИСЛО СЭМПЛОВ.
 * Это ровно то, что нужно передать третьим параметром в
 * HAL_SAI_Transmit_DMA(): там Size — число сэмплов, а НЕ байт.
 * ------------------------------------------------------------------------- */
extern const uint16_t sound_gas_warning[];
extern const uint32_t sound_gas_warning_size;

/* --- параметры записи ----------------------------------------------------- */
#define SOUND_GAS_WARNING_SAMPLE_RATE   16000u
#define SOUND_GAS_WARNING_CHANNELS      1u
#define SOUND_GAS_WARNING_BITS          16u

/* Сколько слов WAV-заголовка нужно пропустить в начале массива.
 * Для текущего (перегенерированного) файла — ноль.
 * Если вернёте backup/audio_samples_WITH_wav_header.c.bak — поставьте 154. */
#define SOUND_GAS_WARNING_HDR_SAMPLES   0u

/* --- то, что отдаём в HAL_SAI_Transmit_DMA() ------------------------------ */
#define SOUND_GAS_WARNING_PCM           (&sound_gas_warning[SOUND_GAS_WARNING_HDR_SAMPLES])
#define SOUND_GAS_WARNING_PCM_SAMPLES   (sound_gas_warning_size - SOUND_GAS_WARNING_HDR_SAMPLES)

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SAMPLES_H */
