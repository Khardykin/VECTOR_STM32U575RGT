/**
  * @file    audio_ids.h
  * @brief   СГЕНЕРИРОВАНО tools/pack_sounds.py — НЕ редактировать вручную.
  *
  *          Образ: tools\sounds.bin  sha256 4dcfbc277b5b959d66cd9980260788720ab4b3ab48c2431211c26343d0180a73
  *          Порядковый номер звука = порядок файла в команде сборки.
  *          Вызывайте audio_play(SND_ИМЯ) — порядок не потеряется.
  *
  *          idx  имя             сэмплов     байт     Гц   секунд
  *            0  kolokol_1        176400   352800  44100    4.00
  *            1  zvuk-po-sinusoi  736563  1473126  44100   16.70
  *            2  kolokol_2        328943   657886  44100    7.46
  *            3  tone_1k          132300   264600  44100    3.00
  *            4  phone_2          132300   264600  44100    3.00
  *            5  zvonok_1          88200   176400  44100    2.00
  */
#ifndef AUDIO_IDS_H
#define AUDIO_IDS_H

enum {
  SND_KOLOKOL_1 = 0,
  SND_ZVUK_PO_SINUSOIDE = 1,
  SND_KOLOKOL_2 = 2,
  SND_TONE_1K = 3,
  SND_PHONE_2 = 4,
  SND_ZVONOK_1 = 5,
  SND_COUNT = 6
};

/* Плеер играет звуки ПО ИНДЕКСУ (audio_play/audio_play_now) -
   enum выше и есть единственная привязка имени к номеру.       */

#endif /* AUDIO_IDS_H */
