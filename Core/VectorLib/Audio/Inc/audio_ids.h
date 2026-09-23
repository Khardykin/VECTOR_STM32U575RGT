/**
  * @file    audio_ids.h
  * @brief   СГЕНЕРИРОВАНО tools/pack_sounds.py — НЕ редактировать вручную.
  *
  *          Образ: tools\sounds.img  sha256 4ce106aae888591da009413469bad8725eb81a0bb9420d82fee92e2eab831915
  *          Порядковый номер звука = порядок файла в команде сборки.
  *          Вызывайте audio_play(SND_ИМЯ) — порядок не потеряется.
  *
  *          idx  имя             сэмплов     байт     Гц   секунд
  *            0  a_gas             14768    29536   8000    1.85
  *            1  b_click             480      960   8000    0.06
  *            2  c_voice_gas       17600    35200   8000    2.20
  *            3  d_myvoice         64839   129678   8000    8.10
  */
#ifndef AUDIO_IDS_H
#define AUDIO_IDS_H

enum {
  SND_A_GAS = 0,
  SND_B_CLICK = 1,
  SND_C_VOICE_GAS = 2,
  SND_D_MYVOICE = 3,
  SND_COUNT = 4
};

/* Значения по умолчанию для таблицы состояний плеера
   (ap_state_map в audio_player.c). Если звуков меньше двух,
   недостающие состояния = тишина (0xFFFF). */
#define SND_STATE_DEFAULT_0  SND_A_GAS
#define SND_STATE_DEFAULT_1  SND_B_CLICK

#endif /* AUDIO_IDS_H */
