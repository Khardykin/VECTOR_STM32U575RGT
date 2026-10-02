/**
  * @file    audio_ids.h
  * @brief   СГЕНЕРИРОВАНО tools/pack_sounds.py — НЕ редактировать вручную.
  *
  *          Образ: tools\sounds.bin  sha256 0cf19b5466f3b9a738993b02da28249f17947a7f3e0dc126989af8d90e4d957c
  *          Порядковый номер звука = порядок файла в команде сборки.
  *          Вызывайте audio_play(SND_ИМЯ) — порядок не потеряется.
  *
  *          idx  имя             сэмплов     байт     Гц   секунд
  *            0  kolokol_1        176400   352800  44100    4.00
  *            1  kolokol_2        328943   657886  44100    7.46
  *            2  phone_1          412416   824832  44100    9.35
  *            3  phone_2          132300   264600  44100    3.00
  *            4  zvonok_1          88200   176400  44100    2.00
  */
#ifndef AUDIO_IDS_H
#define AUDIO_IDS_H

enum {
  SND_KOLOKOL_1 = 0,
  SND_KOLOKOL_2 = 1,
  SND_PHONE_1 = 2,
  SND_PHONE_2 = 3,
  SND_ZVONOK_1 = 4,
  SND_COUNT = 5
};

/* Первые два звука образа - удобные значения по умолчанию
   (например, для старта или тестов). Плеер играет звуки ПО ИНДЕКСУ
   (audio_play/audio_play_now), никакой таблицы состояний нет.
   Если звуков меньше двух, недостающее значение = 0xFFFF. */
#define SND_STATE_DEFAULT_0  SND_KOLOKOL_1
#define SND_STATE_DEFAULT_1  SND_KOLOKOL_2

#endif /* AUDIO_IDS_H */
