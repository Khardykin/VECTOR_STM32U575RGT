/**
  * @file    audio_ids.h
  * @brief   СГЕНЕРИРОВАНО tools/pack_sounds.py — НЕ редактировать вручную.
  *
  *          Образ: tools\test_small.bin  sha256 df0b9043b5319fdafa14770afbc34f3bb8473f719b48eb463181795978edd767
  *          Порядковый номер звука = порядок файла в команде сборки.
  *          Вызывайте audio_play(SND_ИМЯ) — порядок не потеряется.
  *
  *          idx  имя             сэмплов     байт     Гц   секунд
  *            0  kolokol_1        176400   352800  44100    4.00
  *            1  kolokol_2        328943   657886  44100    7.46
  *            2  phone_1          412416   824832  44100    9.35
  */
#ifndef AUDIO_IDS_H
#define AUDIO_IDS_H

enum {
  SND_KOLOKOL_1 = 0,
  SND_KOLOKOL_2 = 1,
  SND_PHONE_1 = 2,
  SND_COUNT = 3
};

/* Плеер играет звуки ПО ИНДЕКСУ (audio_play/audio_play_now) -
   enum выше и есть единственная привязка имени к номеру.       */

#endif /* AUDIO_IDS_H */
