/**
  ******************************************************************************
  * @file    audio_beep.h
  * @brief   Аварийный писк во внутренней flash (фолбэк при мёртвой внешней)
  ******************************************************************************
  */
#ifndef AUDIO_BEEP_H
#define AUDIO_BEEP_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern const int16_t audio_beep_pcm[];
extern const uint32_t audio_beep_samples;   /* число сэмплов, НЕ байт */
#ifdef __cplusplus
}
#endif
#endif
