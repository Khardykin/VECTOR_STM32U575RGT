/**
  * @file    audio_ids.h
  * @brief   СГЕНЕРИРОВАНО tools/pack_sounds.py — НЕ редактировать вручную.
  *
  *          Образ: tools/sounds.img  sha256 c0cc0b717feaae11cc48bb6bd64f153c5394129c999cd81695347bebb4ec8f25
  *          Порядковый номер звука = порядок файла в команде сборки.
  *          Вызывайте audio_play(SND_ИМЯ) — порядок не потеряется.
  *
  *          idx  имя             сэмплов     байт     Гц   секунд
  *            0  poeshl            62502   125004  16000    3.91
  *            1  b_click             960     1920  16000    0.06
  *            2  c_voice_gas       35200    70400  16000    2.20
  */
#ifndef AUDIO_IDS_H
#define AUDIO_IDS_H

enum {
  SND_POESHL = 0,
  SND_B_CLICK = 1,
  SND_C_VOICE_GAS = 2,
  SND_COUNT = 3
};

/* Значения по умолчанию для таблицы состояний плеера
   (ap_state_map в audio_player.c). Если звуков меньше двух,
   недостающие состояния = тишина (0xFFFF). */
#define SND_STATE_DEFAULT_0  SND_POESHL
#define SND_STATE_DEFAULT_1  SND_B_CLICK

#endif /* AUDIO_IDS_H */
