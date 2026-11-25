/**
  * @file    audio_ids.h
  * @brief   СГЕНЕРИРОВАНО tools/pack_sounds.py — НЕ редактировать вручную.
  *
  *          Образ: tools\sounds.bin  sha256 fcf99c8d5a9fb3bb0fc8c696dc779b912ced88fbb52c2c9184903e44f42a3ab8
  *          Порядковый номер звука = порядок файла в команде сборки.
  *          Вызывайте audio_play(SND_ИМЯ) — порядок не потеряется.
  *
  *          idx  имя             сэмплов     байт     Гц   секунд
  *            0  kolokol_1        176400   352800  44100    4.00
  *            1  kolokol_2        328943   657886  44100    7.46
  *            2  tone_1k          132300   264600  44100    3.00
  *            3  zvonok_1          88200   176400  44100    2.00
  *            4  phone_2          132300   264600  44100    3.00
  *            5  kolokol_3        328943   657886  44100    7.46
  */
#ifndef AUDIO_IDS_H
#define AUDIO_IDS_H

enum {
  SND_KOLOKOL_1 = 0,
  SND_KOLOKOL_2 = 1,
  SND_TONE_1K = 2,
  SND_ZVONOK_1 = 3,
  SND_PHONE_2 = 4,
  SND_KOLOKOL_3 = 5,
  SND_COUNT = 6
};

/* Плеер играет звуки ПО ИНДЕКСУ (audio_play/audio_play_now) -
   enum выше и есть единственная привязка имени к номеру.       */

#endif /* AUDIO_IDS_H */
