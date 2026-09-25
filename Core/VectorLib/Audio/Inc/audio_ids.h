/**
  * @file    audio_ids.h
  * @brief   СГЕНЕРИРОВАНО tools/pack_sounds.py — НЕ редактировать вручную.
  *
  *          Образ: tools\sounds.img  sha256 89109e170705a85c34c441bc54458f7d86d9bd6a91d5f053239cec097577e0d6
  *          Порядковый номер звука = порядок файла в команде сборки.
  *          Вызывайте audio_play(SND_ИМЯ) — порядок не потеряется.
  *
  *          idx  имя             сэмплов     байт     Гц   секунд
  *            0  b_click             960     1920  16000    0.06
  *            1  c_voice_gas       35200    70400  16000    2.20
  *            2  d_myvoice        129678   259356  16000    8.10
  */
#ifndef AUDIO_IDS_H
#define AUDIO_IDS_H

enum {
  SND_B_CLICK = 0,
  SND_C_VOICE_GAS = 1,
  SND_D_MYVOICE = 2,
  SND_COUNT = 3
};

/* Значения по умолчанию для таблицы состояний плеера
   (ap_state_map в audio_player.c). Если звуков меньше двух,
   недостающие состояния = тишина (0xFFFF). */
#define SND_STATE_DEFAULT_0  SND_B_CLICK
#define SND_STATE_DEFAULT_1  SND_C_VOICE_GAS

#endif /* AUDIO_IDS_H */
