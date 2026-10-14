/*
 * Config_save_read.c - конфигурация прибора и КОЛЬЦЕВОЙ журнал событий (v21;
 * v25 - журнал перенесён сюда из extstore: слоты фиксированной длины 64 байта,
 * кольцо 0x8000 слотов по маске 0x7FFF, указатели в SNS_CFG.Config_archive,
 * чтение по номеру (log_read) и поиск по времени (log_find_by_time)).
 */
#include "Vector_main.h"          /* HAL, SNS_CFG, макросы статусов/таймеров, лог */
#include "extstore.h"             /* cfg_save/cfg_load + ext_read/ext_write/ext_erase_sector */
#include "sfmap.h"                /* SF_CONFIG_BASE / SF_CONFIG_DEFAULT - области */
#include "spiflash.h"             /* SF_SECTOR_SIZE / SF_TOTAL_SIZE: геометрия кольца журнала */
#include "vector_log.h"           /* LOG_E/LOG_I */
#include "Config_save_read.h"
#include <string.h>
#include <stddef.h>   /* offsetof: ключ времени в слоте журнала */

#define CALC_CRC_CONFIG(CFG, _CRC_)						\
        {									\
          _CRC_ = 0xFFFF;							\
          for(uint16_t i=0; i < (sizeof(SNS_CFG)); i++)			\
          {									\
            _CRC_ = _CRC_ ^ ((uint8_t*)&CFG)[i];				\
            for(uint8_t k=0; k<8; k++)						\
            {									\
              if((_CRC_ & 0x0001)==1) {_CRC_ >>= 1; _CRC_ ^= 0xA001;}		\
              else {_CRC_ >>= 1;}						\
            }									\
          }									\
        }

EVENT_LOG               Event_log;                      // Структура хранения лога журнала
// todo то записать водну структуру
uint8_t SaveConfig = 0;                 // Сохранение конфигурации
uint8_t	SaveConfigDefault = 0;          // Сохранение заводской конфигурации
uint8_t	ReadConfigDefault = 0;		// Чтение заводской конфигурации
uint8_t	SaveEvent = 0;			// Отложенная запись события в журнал

TYPE_LOG TypeLogEvent = TYPE_TURN_ON;	// Тип отложенного события

//===========================================================================================================================
// Банк и страница
static uint32_t GetBank(uint32_t addr)
{
	return (addr < 0x08080000u) ? FLASH_BANK_1 : FLASH_BANK_2;
}

static uint32_t GetPage(uint32_t addr)
{
	uint32_t base = (addr < 0x08080000u) ? 0x08000000u : 0x08080000u;
	return (addr - base) / FLASH_PAGE_SIZE;
}

//===========================================================================================================================
// Сохранение параметров конфигурации (внешняя flash, extstore cfg_save)
// addr - область: SF_CONFIG_BASE (рабочий) или SF_CONFIG_DEFAULT (заводской).
// Возврат: 0 = ok, EXT_BUSY = шина занята/стирание отложено (звук) - флаг у
// вызывающего должен остаться взведённым, повтор позже; -1 = ошибка flash.
int save_param(uint8_t * RamData, uint16_t len, uint32_t addr)
{
  uint16_t CRCcalc = 0;
  SNS_CFG *cfg_ptr = (SNS_CFG*)RamData;
  int rc;

  cfg_ptr->CRC_CONFIG = 0; // Обнуляем перед расчетом
  CALC_CRC_CONFIG((*cfg_ptr), CRCcalc);
  cfg_ptr->CRC_CONFIG = CRCcalc; // Записываем обратно

  rc = cfg_save(RamData, len, addr);
  if (rc == EXT_BUSY)
  {
    return EXT_BUSY;   // тихо: не ошибка, вызывающий повторит (флаг жив)
  }
  if (rc != 0)
  {
    LOG_E(VLOG_M_SYS, "config: cfg_save FAIL a=%x (ext flash)", addr);
    return -1;
  }
  return 0;
}

//===========================================================================================================================
// Чтение параметров конфигурации: 1 = прочитано и CRC верен, 0 = пусто/бито/
// шина занята (EXT_BUSY от cfg_load). addr - область: SF_CONFIG_BASE (рабочий)
// или SF_CONFIG_DEFAULT (заводской).
uint8_t read_param(uint8_t * RamData, uint16_t len, uint32_t addr)
{
  uint16_t out_len = 0;
  if ((cfg_load(RamData, len, &out_len, addr) != 0) || (out_len != len))
  {
	  return 0; // конфига ещё нет ИЛИ размер от другой версии прошивки
  }

  uint16_t CRCcalc = 0;
  uint16_t CRCrec = 0;
  SNS_CFG *cfg_ptr = (SNS_CFG*)RamData;
  CRCrec = cfg_ptr->CRC_CONFIG;

  cfg_ptr->CRC_CONFIG = 0;              // Обнуляем для проверки
  CALC_CRC_CONFIG((*cfg_ptr), CRCcalc);
  cfg_ptr->CRC_CONFIG = CRCrec;         // Возвращаем считанный из памяти CRC

  if(CRCrec != CRCcalc){
	  return 0; // Ошибка: Данные повреждены
  }

  return 1; // Все успешно прочитано и CRC совпал
}

//===========================================================================================================================
//===========================================================================================================================
// КОЛЬЦЕВОЙ ЖУРНАЛ (схема автора, v25)
//---------------------------------------------------------------------------------------------------------------------------
// Область LOG (sfmap.h) поделена на слоты ФИКСИРОВАННОЙ длины sizeof(EVENT_LOG)
// = 64 байта: длина записи одинаковая, поэтому per-record заголовок (LOG_REC_HDR
// прежней схемы) не нужен. Кольцо - LOG_RING_SLOTS (0x8000 = 32768) слотов;
// физическая позиция записи = (указатель & LOG_RING_MASK) * 64. Указатели
// монотонно растут (u32, естественный заворот) и живут в SNS_CFG.Config_archive:
//   CurrentAddrFile - куда пишется следующая запись;
//   BegginAddrFile  - самая старая доступная запись.
// Доступный диапазон чтения: [BegginAddrFile, CurrentAddrFile).
// ПЕРЕПОЛНЕНИЕ: когда кольцо полное и указатель пришёл на границу сектора,
// сектор следующей записи стирается (в нём умерли записи круга назад), а
// BegginAddrFile сдвигается на ЧИСЛО ЗАПИСЕЙ В СЕКТОРЕ (LOG_SLOTS_PER_SECTOR
// = 4096/64 = 64 - чётное, сектор делится нацело, как просил автор). Ёмкость
// = 32768 - 64 = 32704 записи (сектор запаса, чтобы стирание не задело живые).
// Первый круг: flash стёрта с завода (0xFF) - стирание не нужно; слот сектора
// дополнительно проверяется на пустоту (мог остаться мусор журнала старого
// формата до v25) и при необходимости стирается без сдвига BegginAddrFile.
// Стирание идёт через ext_erase_sector(): try-lock + отсрочка под звучащий
// звук (EXT_BUSY) - Vector_RunFlashMemory повторит на следующем проходе.
// Указатели сохраняются в flash вместе с конфигом (save_param) и
// восстанавливаются при старте (read_param); первый пуск - DefaultConfig
// обнуляет оба, кольцо стартует с чистой области.
#define LOG_SLOT_SIZE         ((uint32_t)sizeof(EVENT_LOG))           /* 64  */
#define LOG_RING_MASK         0x7FFFu                                 /* кольцо на 0x7FFF (автор) */
#define LOG_RING_SLOTS        (LOG_RING_MASK + 1u)                    /* 32768 */
#define LOG_SLOTS_PER_SECTOR  (SF_SECTOR_SIZE / LOG_SLOT_SIZE)        /* 64 - чётное */
#define LOG_USABLE_SLOTS      (LOG_RING_SLOTS - LOG_SLOTS_PER_SECTOR) /* 32704 */

/* Компиляторный самоконтроль: слот РОВНО 64 байта и сектор делится на слоты
   нацело - иначе геометрия кольца (маска/сдвиг начала) разъедется.          */
typedef char event_log_slot_size_assert[(sizeof(EVENT_LOG) == 64u) ? 1 : -1];
typedef char event_log_sector_assert[((SF_SECTOR_SIZE % 64u) == 0u) ? 1 : -1];

// Физический адрес слота по номеру указателя. КОНТЕКСТ: любой, чистая
// арифметика.
static uint32_t log_slot_addr(uint32_t ptr)
{
  return SF_LOG_BASE + ((ptr & LOG_RING_MASK) * LOG_SLOT_SIZE);
}

// Время записи (Event_log.working_hours, ключ поиска) по номеру указателя:
// читаются только 4 байта ключа, не весь слот. Возврат: 0 = ok, -1 = ошибка
// шины. КОНТЕКСТ: поток.
static int log_time_at(uint32_t ptr, uint32_t *t)
{
  uint8_t b[4];
  if (ext_read(log_slot_addr(ptr) + (uint32_t)offsetof(EVENT_LOG, working_hours),
               b, sizeof b) != 0)
  {
    return -1;
  }
  *t = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  return 0;
}

//===========================================================================================================================
// Сохранение записи журнала событий (внешняя flash, кольцо LOG, v25).
// ЕДИНАЯ точка записи журнала: через неё идут и единичные события
// (TYPE_TURN_ON/TYPE_TURN_OFF/TYPE_FW_UPDATE/TYPE_LOG_PARAM_NOP), и
// периодический архив (TYPE_READINGS по таймеру TIMER_RTC_LOG при
// ArchiveRecording == 1) - см. Vector_RunFlashMemory().
// Возврат: 0 = ok, EXT_BUSY = шина занята/стирание сектора отложено (звук) -
// вызывающий повторит позже; -1 = ошибка flash (LOG_E напечатан).
int save_event(SNS_CFG *pSnsCfg, TYPE_LOG type_log)
{
#if CONFIG_SAVE_PARAM_LOG
  int rc;
  uint32_t cur = pSnsCfg->Config_archive.CurrentAddrFile;
  uint32_t beg = pSnsCfg->Config_archive.BegginAddrFile;

  Event_log.Type_log                                            = (uint16_t)type_log;
  Event_log.Firmware_version                                    = FIRMWARE_VERSION;
  Event_log.CurrentAddrFile                                     = cur;   // номер (указатель) этой записи
  Event_log.State                                               = pSnsCfg->Config_common.State;
  Event_log.StateErr                                            = pSnsCfg->Config_common.StateErr;
  Event_log.working_hours                                       = pSnsCfg->Working_data.working_hours_offset
                                                                + pSnsCfg->Working_data.working_hours;   // ВРЕМЯ, с
  Event_log.battery_charge_percent                              = sensors_status.battery_charge_percent;
  Event_log.battery_charge_volt                                 = sensors_status.battery_charge_volt;
  Event_log.temperature                                         = (int16_t)(sensors_status.Temperature*100);

  // Начало сектора: стереть его, если кольцо полное (сдвинуть начало) или
  // слот не пустой (первый круг после журнала старого формата).
  if ((cur & (LOG_SLOTS_PER_SECTOR - 1u)) == 0u)
  {
    int full  = ((uint32_t)(cur - beg) >= LOG_USABLE_SLOTS);
    int blank = 1;

    if (!full)
    {
      uint8_t probe[4];
      blank = ((ext_read(log_slot_addr(cur), probe, sizeof probe) == 0) &&
               (probe[0] == 0xFFu) && (probe[1] == 0xFFu) &&
               (probe[2] == 0xFFu) && (probe[3] == 0xFFu));
      // ошибку чтения трактуем как "не пусто" - лишнее стирание безопаснее
    }

    if (full || !blank)
    {
      rc = ext_erase_sector(log_slot_addr(cur));
      if (rc == EXT_BUSY)
      {
        return EXT_BUSY;   // тихо: звук/шина, вызывающий повторит (флаг жив)
      }
      if (rc != 0)
      {
        LOG_E(VLOG_M_SYS, "log: sector erase FAIL");
        return -1;
      }
      if (full)
      {
        // начало сдвигается на число записей в секторе (автор, v25)
        pSnsCfg->Config_archive.BegginAddrFile = beg + LOG_SLOTS_PER_SECTOR;
      }
    }
  }

  rc = ext_write(log_slot_addr(cur), (const uint8_t *)&Event_log, LOG_SLOT_SIZE);
  if (rc == EXT_BUSY)
  {
    return EXT_BUSY;   // тихо: не ошибка, вызывающий повторит (флаг жив)
  }
  if (rc != 0)
  {
    LOG_E(VLOG_M_SYS, "log: slot write FAIL");
    return -1;
  }
  pSnsCfg->Config_archive.CurrentAddrFile = cur + 1u;
  LOG_D(VLOG_M_SYS, "log: rec %u (beg %u)", cur,
        pSnsCfg->Config_archive.BegginAddrFile);
  return 0;
#else
  (void)pSnsCfg; (void)type_log;
  return 0;
#endif
}

//===========================================================================================================================
// Чтение записи журнала ПО НОМЕРУ УКАЗАТЕЛЯ (пункт 3 автора, v25).
// Валидный ptr: [BegginAddrFile, CurrentAddrFile). КОНТЕКСТ: поток.
// Возврат: 0 = ok, 1 = указатель вне доступного диапазона (или журнал
// выключен/пуст), -1 = ошибка шины (включая EXT_BUSY от ext_read - повторить).
int log_read(SNS_CFG *pSnsCfg, uint32_t ptr, EVENT_LOG *out)
{
#if CONFIG_SAVE_PARAM_LOG
  uint32_t cur = pSnsCfg->Config_archive.CurrentAddrFile;
  uint32_t beg = pSnsCfg->Config_archive.BegginAddrFile;

  if (out == (EVENT_LOG *)0) { return 1; }
  if (((int32_t)(ptr - beg) < 0) || ((int32_t)(cur - ptr) <= 0)) { return 1; }
  if (ext_read(log_slot_addr(ptr), (uint8_t *)out, LOG_SLOT_SIZE) != 0) { return -1; }
  return 0;
#else
  (void)pSnsCfg; (void)ptr; (void)out;
  return 1;
#endif
}

//===========================================================================================================================
// Поиск ближайших записей ПО ДИАПАЗОНУ ВРЕМЕНИ (пункт 3 автора, v25).
// Время записи = Event_log.working_hours (суммарные секунды: Working_data
// offset + часы; монотонно благодаря зеркалу в RTC BKP0). Журнал упорядочен
// по времени -> два бинарных поиска по 4-байтному ключу (не читая слот
// целиком). Результат - НОМЕРА УКАЗАТЕЛЕЙ:
//   *ptr_from = первая запись с временем >= t_from (все раньше -> последняя);
//   *ptr_to   = последняя запись с временем <= t_to (все позже -> первая).
// Возврат: 0 = диапазон найден (ptr_from <= ptr_to), 1 = точного попадания
// нет (указатели = ближайшие) / журнал пуст или выключен, -1 = ошибка шины
// (повторить). ОГРАНИЧЕНИЕ: монотонность времени предполагает живой VBAT;
// после потери питания BKP0 обнуляется и порядок времени может нарушиться.
// КОНТЕКСТ: поток (COM/BLE-модуль чтения журнала).
int log_find_by_time(SNS_CFG *pSnsCfg, uint32_t t_from, uint32_t t_to,
                     uint32_t *ptr_from, uint32_t *ptr_to)
{
#if CONFIG_SAVE_PARAM_LOG
  uint32_t beg = pSnsCfg->Config_archive.BegginAddrFile;
  uint32_t cur = pSnsCfg->Config_archive.CurrentAddrFile;
  uint32_t n, lo, hi, mid, t, idx_from, idx_to;

  if ((ptr_from == (uint32_t *)0) || (ptr_to == (uint32_t *)0)) { return 1; }
  if (cur == beg) { return 1; }                  // журнал пуст
  n = (uint32_t)(cur - beg);

  // первая запись с временем >= t_from
  lo = 0u; hi = n;
  while (lo < hi)
  {
    mid = lo + ((hi - lo) / 2u);
    if (log_time_at(beg + mid, &t) != 0) { return -1; }
    if (t >= t_from) { hi = mid; } else { lo = mid + 1u; }
  }
  idx_from = (lo < n) ? lo : (n - 1u);           // все раньше t_from -> последняя

  // последняя запись с временем <= t_to (ищем первую с временем > t_to)
  lo = 0u; hi = n;
  while (lo < hi)
  {
    mid = lo + ((hi - lo) / 2u);
    if (log_time_at(beg + mid, &t) != 0) { return -1; }
    if (t <= t_to) { lo = mid + 1u; } else { hi = mid; }
  }
  idx_to = (lo > 0u) ? (lo - 1u) : 0u;           // все позже t_to -> первая

  *ptr_from = beg + idx_from;
  *ptr_to   = beg + idx_to;
  return (idx_from <= idx_to) ? 0 : 1;
#else
  (void)pSnsCfg; (void)t_from; (void)t_to; (void)ptr_from; (void)ptr_to;
  return 1;
#endif
}

//// Сравнение текущих параметров и сохранённых (после приёма новых по COM/BLE)
//void compare_param(SNS_CFG *pSnsCfgread, SNS_CFG *pSnsCfgCur)
//{
//  uint16_t type_log = 0;
//
//  // Чтение конфигурационных параметров (рабочая область SF_CONFIG_BASE)
//  if(read_param((uint8_t*)pSnsCfgread, sizeof(SNS_CFG), SF_CONFIG_BASE)){
//    if(pSnsCfgread->Config_archive.Archiveinterval != pSnsCfgCur->Config_archive.Archiveinterval){
//  	  END_TIMER_RTC(TIMER_RTC_LOG);   // форсируем запись архива с новым периодом
//  	  type_log |= TYPE_LOG_PARAM_NOP; // v25: TYPE_LOG_PARAM_UPDATE_* удалены автором;
//  	                                  // когда перенесётся COM-модуль - вернуть типы обновлений
//    }
//  }
//  if(type_log){
//	  /* Запись НЕ отсюда (поток приёма), а отложенно через Measure Task:
//	     по правилам проекта (SYSTEM.md) во внешнюю flash пишет только
//	     Vector_RunFlashMemory() - и только в «окно тишины» (звук не играет).
//	     Если событие уже отложено - новое побеждает (журнал best-effort). */
//	  TypeLogEvent = (TYPE_LOG)type_log;
//	  SaveEvent    = 1u;
//  }
//}

//===========================================================================================================================
// Начальная или тестовая конфигурация
void DefaultConfig(SNS_CFG *pSnsCfg)
{
	//----------------------------------------------------------------------------
	// Зеленый светодиод
	pSnsCfg->Config_common.TimeLedStatePulse = TIME_RTC_LED_STATE_RUN_PULSE;
	pSnsCfg->Config_common.TimeLedStatePeriod = TIME_RTC_LED_STATE_RUN_PERIOD;
	//----------------------------------------------------------------------------
	// Пороги срабатывания сигнализации батареи, мВ (Li-ion)
	pSnsCfg->Config_common.battery_charge_volt_max = BAT_MAX_DEF;
	pSnsCfg->Config_common.battery_charge_volt_min = BAT_MIN_DEF;
	pSnsCfg->Config_common.battery_charge_volt_lim_1 = BAT_LIM_1_DEF;
	pSnsCfg->Config_common.battery_charge_volt_lim_2 = BAT_LIM_2_DEF;
	//----------------------------------------------------------------------------
	// Инициализация текущего положения, начального положения и запись логов
	// (v25: поля журнала/архива переехали в Config_archive)
	pSnsCfg->Config_archive.CurrentAddrFile                               = 0;
	pSnsCfg->Config_archive.BegginAddrFile                                = 0;
	pSnsCfg->Config_archive.ArchiveRecording                              = 0;
	pSnsCfg->Config_archive.Archiveinterval                               = 0x0500|0x0005;// (мин<<8)|с - как в Avis
	// Working_data (часы/смещение) НЕ трогаем: часы - рантайм (RTC BKP0),
	// дефолтный конфиг их обнулять не должен (v25).
	//----------------------------------------------------------------------------
	// Настройка Lora
#if CONFIG_LORA
	Lora_Cfg_ApplyDefaults(pSnsCfg);
	pSnsCfg->Config_common.PeriodTimeLora |= TIME_RTC_LORA_DATA_SET;			// Период сообщений
	pSnsCfg->Config_common.PeriodTimeLora |= (TIME_RTC_LORA_DATA_SET<<8)/3;	// Период аварийный сообщ
#endif
}
