/**
  ******************************************************************************
  * @file    audio_factory.h
  * @brief   Заводская запись образа звуков во внешнюю flash из самой прошивки
  *
  *          Позволяет "попробовать зашить дорожки" без внешних загрузчиков:
  *          образ sounds.img линкуется во внутреннюю flash как const-массив
  *          (генерируется tools/bin2c.py в audio_factory_image.c) и при
  *          старте, если во внешней flash образа нет или он битый,
  *          программируется туда сектор за сектором с верификацией.
  *
  *          Включается макросом VECTOR_AUDIO_FACTORY_EMBED (vector_config.h).
  *          В серии ставится 0 - образ во внутреннюю flash не линкуется,
  *          внешняя пишется на производстве (см. FLASH_PROGRAMMING.md).
  ******************************************************************************
  */
#ifndef AUDIO_FACTORY_H
#define AUDIO_FACTORY_H

#include "vector_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if VECTOR_AUDIO_FACTORY_EMBED

/** Стереть нужную область внешней flash и записать туда встроенный образ,
    затем верифицировать побайтово. Возвращает 0 при успехе.
    Вызывать ПОСЛЕ старта планировщика (использует мьютекс extstore). */
int audio_factory_program(void);

#endif /* VECTOR_AUDIO_FACTORY_EMBED */

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_FACTORY_H */
