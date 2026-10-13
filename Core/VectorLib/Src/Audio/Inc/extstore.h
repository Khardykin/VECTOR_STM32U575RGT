/**
  ******************************************************************************
  * @file    extstore.h
  * @brief   Потокобезопасный доступ к внешней SPI flash + конфигурация + журнал
  *
  *          ЕДИНАЯ ТОЧКА ВХОДА во внешнюю flash. Все обращения из любых потоков
  *          идут через этот модуль и сериализуются мьютексом: SPI-шина и
  *          состояние чипа (WREN/WIP) не допускают параллельных операций.
  *
  *          Правило: НЕ вызывать sf_read/sf_program/sf_sector_erase напрямую
  *          из прикладного кода. Исключение - ранняя инициализация до старта
  *          планировщика (см. audio_init), когда потоков ещё нет.
  *
  *          Адреса брать из sfmap.h (SF_SOUNDS_BASE / SF_CONFIG_* / SF_LOG_BASE).
  ******************************************************************************
  */
#ifndef EXTSTORE_H
#define EXTSTORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Коды возврата (общие для модуля):
     0        успех
    -1        ошибка шины/данных/адреса
    EXT_BUSY  шина занята другим потоком (try-lock не удался) ИЛИ стирание
              отложено, потому что сейчас звучит звук (audio_is_busy()).
              Данные не тронуты - вызывающий должен повторить позже.        */
#define EXT_BUSY  (-2)

/* Вызывать один раз из tx_application_define(), ДО audio_init() и потоков.
   Создаёт мьютекс и сканирует журнал (находит текущий сектор записи). */
void ext_init(void);

/* --- сырой доступ (адреса - абсолютные по чипу, из sfmap.h) ---------------
 * Ожидание шины ограничено (см. VECTOR_EXT_LOCK_READ_MS / _WRITE_MS в
 * vector_config.h): чтения ждут до 250 мс, записи - try-lock.             */
int ext_read(uint32_t addr, uint8_t *dst, uint32_t len);            /* 0 = ok,
     -1 = ошибка/шину не дождались */
int ext_write(uint32_t addr, const uint8_t *src, uint32_t len);     /* 0 = ok,
     EXT_BUSY = шина занята, область должна быть стёрта */
int ext_erase_sector(uint32_t addr);                                /* 0 = ok,
     EXT_BUSY = занята шина или звучит звук (стирание отложено) */

/* --- конфигурация устройства ---------------------------------------------
 * Страница 4 КБ по адресу addr (SF_CONFIG_BASE = рабочий конфиг,
 * SF_CONFIG_DEFAULT = заводской; формат общий). Записи ДОБАВЛЯЮТСЯ в
 * страницу без стирания: свежая = максимальный seq среди валидных по CRC
 * (обрыв питания не убивает конфиг: ниже по странице живёт предыдущая
 * копия). Запись: [u32 seq][u16 len][u16 pad][u32 crc32][данные].
 * addr обязан быть выровнен на 4 КБ и указывать на страницу вне SOUNDS.
 */
int cfg_save(const void *data, uint16_t len, uint32_t addr);  /* 0 = ok,
     EXT_BUSY = занята шина или стирание страницы отложено (звук),
     -1 = ошибка/неверный адрес */
int cfg_load(void *data, uint16_t max_len, uint16_t *out_len, uint32_t addr);
                                                              /* 0 = ok,
     EXT_BUSY = шину не дождались (повторить), -1 = конфига ещё нет/ошибка */

/* --- кольцевой журнал (v25) -------------------------------------------------
 * Слой журнала (записи фиксированной длины EVENT_LOG = 64 байта, кольцо
 * 0x8000 слотов, указатели CurrentAddrFile/BegginAddrFile в SNS_CFG.
 * Config_archive) ПЕРЕНЕСЁН в Config_save_read.c: save_event() /
 * log_read() / log_find_by_time(). extstore предоставляет ему только сырые
 * операции ниже (ext_write/ext_erase_sector с EXT_BUSY и отсрочкой стирания
 * под звук). Прежние log_append/log_next (сектора с magic 'LOGS' и
 * переменной длиной записи) удалены - формат области LOG несовместим.      */

/* --- отладка -------------------------------------------------------------- */
extern volatile uint32_t ext_dbg_cfg_seq;      /* seq последнего конфига      */
extern volatile uint32_t ext_dbg_busy;         /* шина была занята при попытке
                                                  взять ext_mtx (lock_ms)     */
/* (v25: ext_dbg_log_sector/_offset удалены вместе со секторным журналом -
   состояние кольца теперь в Sns_Cfg_struct.Config_archive.CurrentAddrFile /
   BegginAddrFile, смотреть в отладчике их)                                  */
extern volatile uint32_t ext_dbg_erase_defer;  /* стираний отложено, потому
                                                  что звучал звук             */

#ifdef __cplusplus
}
#endif

#endif /* EXTSTORE_H */
