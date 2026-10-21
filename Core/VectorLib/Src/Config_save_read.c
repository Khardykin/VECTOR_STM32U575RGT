/*
 * Config_save_read.c - конфигурация прибора и КОЛЬЦЕВОЙ журнал событий v2
 * (LOG_FORMAT.md): слот 40 байт (заголовок 12 Б + payload 28 Б), 102 слота
 * в секторе, кольцо 52224 записи = 2 МБ; RAM-очередь 16×40 Б со снимком НА
 * МОМЕНТ СОБЫТИЯ (§6) - писатель (Vector_RunFlashMemory, 50 мс) переносит
 * её во flash: program под звуком разрешён, erase только в окно тишины,
 * LOG_PTR - один раз на пачку. Чтение по номеру (log_read) и поиск по
 * времени (log_find_by_time, ключ working_hours на +4).
 * СТРАНИЦА-КОЛЬЦО указателей архива SF_LOG_PTR (cur/beg после каждой ПАЧКИ
 * записей; запись 16 Б c маркером формата v2 в поле pad - записи прежней
 * прошивки игнорируются, «миграции нет», §2/§8.10: первый старт v2
 * переинициализирует кольцо) + Init_Build_Type()/current_build_type
 * (бренд прибора, последняя страница внутренней flash - как в Avis;
 * закрывает висячую ссылку TFT_indicator.c и нужна имени BLE-модуля).
 */
#include "Vector_main.h"          /* HAL, SNS_CFG, макросы статусов/таймеров, лог */
#include "tx_api.h"               /* TX_DISABLE/TX_RESTORE: критические секции очереди */
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

uint8_t SaveConfig = 0;                 // Сохранение конфигурации
uint8_t	SaveConfigDefault = 0;          // Сохранение заводской конфигурации
uint8_t	ReadConfigDefault = 0;		// Чтение заводской конфигурации
/* SaveEvent/TypeLogEvent (однослотовое отложенное событие прежней схемы)
   УДАЛЕНЫ: журнал v2 работает через RAM-очередь log_post()/log_flush()
   (LOG_FORMAT.md §6) - снимок на момент события, всплески не теряются.  */

//===========================================================================================================================
// Бренд прибора: current_build_type читается Init_Build_Type() из
// последней страницы внутренней flash (FLASH_ADDRESS_BUILD_TYPE, зарезервирована
// линкером LENGTH=1016K). Объявлена здесь по комментарию config_device.h -
// ранее переменная была висячей (TFT_indicator.c её использует).
uint16_t current_build_type = MIRAX;	// MIRAX=0 / BACS=1

//===========================================================================================================================
//  (замечание автора п.4): СТРАНИЦА-КОЛЬЦО указателей архива.
// После каждой записи журнала CurrentAddrFile/BegginAddrFile сохраняются в
// отдельную страницу SF_LOG_PTR (не вместе со всем конфигом!). Запись 16 байт
// {seq, cur, beg, crc16, pad}, 4096/16 = 256 записей на цикл стирания,
// свежая = максимальный seq с валидным CRC (та же схема, что в конфиг-
// страницах). Стирание - только в "окне тишины" (ext_erase_sector сам
// откладывает при звуке -> EXT_BUSY, указатели ждут в RAM следующей записи).
typedef struct __attribute__((packed)) {
	uint32_t seq;		// порядковый номер записи (растёт, в т.ч. после стирания)
	uint32_t cur;		// CurrentAddrFile
	uint32_t beg;		// BegginAddrFile
	uint16_t crc;		// CRC16 (полином 0xA001, как CALC_CRC_CONFIG) по seq+cur+beg
	uint16_t pad;		// маркер формата: LOG_PTR_FMT2 (CRC его НЕ покрывает)
} log_ptr_rec_t;
typedef char log_ptr_rec_assert[(sizeof(log_ptr_rec_t) == 16u) ? 1 : -1];

// Маркер формата v2 в поле pad: у записей прежней прошивки pad=0xFFFF, и они
// ИГНОРИРУЮТСЯ загрузчиком - «миграции нет» (LOG_FORMAT.md §2/§8.10): первый
// старт прошивки с журналом v2 переинициализирует кольцо (cur=beg=0), старые
// 64-байтные записи стираются лениво (blank-проба на границе сектора).
#define LOG_PTR_FMT2	0x0002u

#define LOG_PTR_SLOTS	(SF_LOG_PTR_SIZE / sizeof(log_ptr_rec_t))	// 256

static uint16_t log_ptr_slot = 0;	// слот для следующей записи
static uint32_t log_ptr_seq  = 0;	// счётчик порядка (последний восстановленный)
volatile uint32_t log_ptr_dbg_saves  = 0;	// сколько раз указатели сохранены
volatile uint32_t log_ptr_dbg_defers = 0;	// сколько раз отложено (EXT_BUSY)

// CRC16 записи указателей (полином 0xA001, как у конфига), считается по
// первым 12 байтам (seq/cur/beg). КОНТЕКСТ: поток. Возврат: CRC.
static uint16_t crc16_ptr(const log_ptr_rec_t *r)
{
	uint16_t crc = 0xFFFF;
	const uint8_t *p = (const uint8_t *)r;
	for (uint16_t i = 0; i < 12u; i++)
	{
		crc ^= (uint16_t)p[i];
		for (uint8_t k = 0; k < 8u; k++)
		{
			crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u)
							 : (uint16_t)(crc >> 1);
		}
	}
	return crc;
}

// Чтение бренда прибора из последней страницы внутренней flash (аналог
// Init_Build_Type в Avis). Пусто (0xFFFF) или мусор -> MIRAX.
// КОНТЕКСТ: Measure Task (Vector_Run_Pre_Init, первым делом). Возврат: нет.
void Init_Build_Type(void)
{
	uint16_t v = *(volatile uint16_t *)FLASH_ADDRESS_BUILD_TYPE;
	current_build_type = ((v == (uint16_t)MIRAX) || (v == (uint16_t)BACS))
						 ? v : (uint16_t)MIRAX;
}

// Сканирование страницы указателей (аналог ReadCurrentAddrFile из Avis):
// ищет свежую запись (max seq с валидным CRC), определяет позицию следующей
// записи. apply=1 - перенести cur/beg в pSnsCfg->Config_archive (вызывается
// после успешного чтения конфига; при DefaultConfig - только скан позиции,
// как в Avis, где ReadCurrentAddrFile в else-ветке закомментирован).
// КОНТЕКСТ: Measure Task (Vector_Run_Pre_Init, один раз).
// Возврат: 1 = валидная запись найдена, 0 = страница пуста/побита, -1 = шина.
int log_ptr_load(SNS_CFG *pSnsCfg, uint8_t apply)
{
#if CONFIG_SAVE_PARAM_LOG
	log_ptr_rec_t rec;
	uint32_t best_seq = 0, best_cur = 0, best_beg = 0;
	uint8_t found = 0;
	uint16_t slot = 0;

	for (slot = 0; slot < (uint16_t)LOG_PTR_SLOTS; slot++)
	{
		if (ext_read(SF_LOG_PTR_BASE + (uint32_t)slot * sizeof(log_ptr_rec_t),
					 (uint8_t *)&rec, sizeof rec) != 0)
		{
			LOG_E(VLOG_M_SYS, "log_ptr: read FAIL at slot %u", (uint32_t)slot);
			return -1;
		}
		if ((rec.seq == 0xFFFFFFFFu) && (rec.cur == 0xFFFFFFFFu))
		{
			break;	// первый пустой слот - записи кончились
		}
		if ((rec.crc == crc16_ptr(&rec)) && (rec.pad == LOG_PTR_FMT2)
				&& (rec.seq > best_seq))
		{
			found   = 1;
			best_seq = rec.seq;
			best_cur = rec.cur;
			best_beg = rec.beg;
		}
	}
	log_ptr_slot = slot;
	log_ptr_seq  = best_seq;
	if (apply != 0u)
	{
		if (found != 0u)
		{
			pSnsCfg->Config_archive.CurrentAddrFile = best_cur;
			pSnsCfg->Config_archive.BegginAddrFile  = best_beg;
			LOG_I(VLOG_M_SYS, "log_ptr: cur=%u beg=%u (seq %u)",
				  best_cur, best_beg, best_seq);
		}
		else if ((pSnsCfg->Config_archive.CurrentAddrFile != 0u)
		      || (pSnsCfg->Config_archive.BegginAddrFile != 0u))
		{
			// ПЕРВЫЙ СТАРТ формата v2: валидных записей v2 в странице нет, а
			// конфиг несёт указатели ПРЕЖНЕГО формата (слот 64 Б, кольцо
			// 32768) - в новой геометрии они бессмысленны. Кольцо
			// переинициализируется (LOG_FORMAT.md §2 «миграции нет»).
			pSnsCfg->Config_archive.CurrentAddrFile = 0u;
			pSnsCfg->Config_archive.BegginAddrFile  = 0u;
			LOG_W(VLOG_M_SYS, "log v2: ring reinit (old ptrs dropped)");
		}
	}
	return (int)found;
#else
	(void)pSnsCfg; (void)apply;
	return 0;
#endif
}

// Сохранение указателей архива (аналог SaveCurrentAddrFile из Avis) - ОДИН
// РАЗ НА ПАЧКУ записей журнала (из log_flush, §6: износ страницы указателей
// не зависит от частоты записей). Страница заполнилась -> стирание (только в
// "окне тишины": EXT_BUSY = звук/шина, сохранение повторится со следующей
// пачкой - указатели живут в RAM). КОНТЕКСТ: Measure Task.
// Возврат: 0 = ok, EXT_BUSY = отложено, -1 = ошибка flash.
int log_ptr_save(const SNS_CFG *pSnsCfg)
{
#if CONFIG_SAVE_PARAM_LOG
	log_ptr_rec_t rec;
	int rc;

	if (log_ptr_slot >= (uint16_t)LOG_PTR_SLOTS)
	{
		rc = ext_erase_sector(SF_LOG_PTR_BASE);
		if (rc == EXT_BUSY)
		{
			log_ptr_dbg_defers++;
			return EXT_BUSY;
		}
		if (rc != 0)
		{
			LOG_E(VLOG_M_SYS, "log_ptr: erase FAIL");
			return -1;
		}
		log_ptr_slot = 0;
	}

	log_ptr_seq++;
	rec.seq = log_ptr_seq;
	rec.cur = pSnsCfg->Config_archive.CurrentAddrFile;
	rec.beg = pSnsCfg->Config_archive.BegginAddrFile;
	rec.pad = LOG_PTR_FMT2;	// маркер формата v2 (старые записи: 0xFFFF)
	rec.crc = crc16_ptr(&rec);

	rc = ext_write(SF_LOG_PTR_BASE + (uint32_t)log_ptr_slot * sizeof(log_ptr_rec_t),
				   (const uint8_t *)&rec, sizeof rec);
	if (rc != 0)
	{
		log_ptr_seq--;					// seq не потрачен - повтор с тем же номером
		log_ptr_dbg_defers++;
		if (rc != EXT_BUSY)
		{
			LOG_E(VLOG_M_SYS, "log_ptr: write FAIL");
		}
		return rc;
	}
	log_ptr_slot++;
	log_ptr_dbg_saves++;
	return 0;
#else
	(void)pSnsCfg;
	return 0;
#endif
}

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
// КОЛЬЦЕВОЙ ЖУРНАЛ v2 (LOG_FORMAT.md): слот 40 байт (заголовок 12 Б +
// payload 28 Б), 102 слота в секторе 4 КБ (хвост 16 Б не используется),
// кольцо 52224 записи = 2 МБ (SF_LOG_BASE..SF_LOG_PTR_BASE). Геометрия и
// очередь - static inline в Config_save_read.h: тот же код исполняет
// хост-тест tools/test_logq/ (регрессия арифметики кольца и FIFO).
// Указатели CurrentAddrFile/BegginAddrFile монотонные u32 (в
// SNS_CFG.Config_archive + зеркало-кольцо SF_LOG_PTR), физическая позиция -
// log_slot_addr_v2() (phys = ptr mod 52224). Доступный диапазон чтения:
// [BegginAddrFile, CurrentAddrFile).
// ПЕРЕПОЛНЕНИЕ: на границе сектора (log_erase_decision): кольцо полное ->
// стирание сектора + сдвиг начала на 102 записи; слот не пуст (мусор старого
// 64-байтного формата или прошлых кругов) -> стирание БЕЗ сдвига. Первый
// старт v2: кольцо переинициализировано (log_ptr_load), flash не чистая -
// каждый сектор стирается при первом пересечении (лениво).
// ПОЛИТИКА ЗАПИСИ (§6): program (1-6 мс) разрешён ПОД ЗВУКОМ (93-мс запас
// буфера плеера поглощает; шина - под мьютексом extstore, try-lock);
// erase - только в окно тишины (ext_erase_sector -> EXT_BUSY -> записи ЖДУТ
// В ОЧЕРЕДИ, писатель повторит через 50 мс). RAM-очередь 16×40 Б хранит
// снимок события (время/состояние/данные НА МОМЕНТ СОБЫТИЯ) - задержка
// записи время НЕ искажает; номер записи (CurrentAddrFile) проставляет
// ПИСАТЕЛЬ позиционно (нумерация непрерывна даже при потерях в очереди).
#define log_slot_addr(ptr)   log_slot_addr_v2(SF_LOG_BASE, (ptr))

//---------------------------------------------------------------------------------------------------------------------------
// RAM-очередь записей (§6). Экземпляр нуль-инициализирован (head=count=0 -
// валидное пустое состояние; logq_init не обязателен). Производители:
// measure (тест/архивный тик), receiver (COM/BLE/«предсмертная»); писатель
// ОДИН - measure (Vector_RunFlashMemory). Критическая секция TX_DISABLE:
// секции короткие (копирование 40 Б), ISR не блокируют надолго.
static logq_ctx_t logq;
static volatile uint8_t logq_draining = 0;	// защита от двух одновременных drain'ов
volatile uint32_t logq_dropped    = 0;		// потери на переполнении (накопительно)
volatile uint32_t logq_dbg_pushes = 0;		// всего постановок

uint32_t logq_depth(void)
{
	TX_INTERRUPT_SAVE_AREA;
	uint32_t n;
	TX_DISABLE;
	n = logq_count(&logq);
	TX_RESTORE;
	return n;
}

// Время записи (H.working_hours на +4, ключ поиска §7.2) по номеру
// указателя: читаются только 4 байта ключа, не весь слот - единое смещение
// для всех типов, payload не разбирается. Возврат: 0 = ok, -1 = ошибка шины.
// КОНТЕКСТ: поток.
static int log_time_at(uint32_t ptr, uint32_t *t)
{
  uint8_t b[4];
  if (ext_read(log_slot_addr(ptr) + LOG_TIME_OFFSET, b, sizeof b) != 0)
  {
    return -1;
  }
  *t = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  return 0;
}

//---------------------------------------------------------------------------------------------------------------------------
// ПОСТРОЕНИЕ ЗАПИСЕЙ (§4): снимок данных НА МОМЕНТ ВЫЗОВА (log_post) -
// задержка переноса во flash время записи не искажает. Источники: SNS_CFG,
// sensors_status, GPS (g_lat/g_lon_scaled, ×1e7). Чего пока нет (каналы
// AVIS, СИЗ по BLE, aqi, удар/неактивность в motion) - нули (§9: «SNAPSHOT
// при отсутствии AVIS: active_sensors=0, conc[]=0, chan_thr_bits=0»).
#if CONFIG_GPS
extern int32_t g_lat_scaled;	// Gps.c: координата ×1e7 (0 = нет фикса)
extern int32_t g_lon_scaled;
#endif

// Байт MOTION_BITS (§4.1/§4.2): поза - Orientation (биты 0-1), падение -
// fall_detected (бит2). Удар (бит3)/неактивность (бит4) - источника пока нет.
static uint8_t log_motion_byte(void)
{
	uint8_t m = (uint8_t)(sensors_status.Orientation & LOG_MOTION_POSE_MASK);
	if (sensors_status.fall_detected != 0u)
	{
		m |= LOG_MOTION_FALL;
	}
	return m;
}

// Общий заголовок. Номер записи - ЗАГЛУШКА (0): позиционный номер проставит
// писатель (log_write_rec) в момент переноса во flash.
static void log_build_hdr(EVENT_LOG *r, TYPE_LOG t, const SNS_CFG *cfg)
{
	r->H.Type_log        = (uint8_t)t;
	r->H.Fmt_ver         = LOG_FMT_VER;
	r->H.Payload_len     = LOG_PAYLOAD_MAX;	// payload - фиксированная структура, целиком
	r->H.Flags           = 0u;
	r->H.working_hours   = cfg->Working_data.working_hours_offset
	                     + cfg->Working_data.working_hours;	// ВРЕМЯ СОБЫТИЯ, с
	r->H.CurrentAddrFile = 0u;
	memset(r->Pl, 0, sizeof r->Pl);
}

static void log_build(EVENT_LOG *r, TYPE_LOG t, const SNS_CFG *cfg)
{
	log_build_hdr(r, t, cfg);
	switch (t)
	{
	case TYPE_TURN_ON:
	case TYPE_TURN_OFF:
	case TYPE_FW_UPDATE:
	{
		log_pl_power_t p;
		memset(&p, 0, sizeof p);
		p.fw_version   = (uint16_t)FIRMWARE_VERSION;
		p.hw_version   = (uint8_t)cfg->Config_common.HardwareVersion;
		p.brand        = (uint8_t)current_build_type;
		p.reset_reason = 0u;	// TODO: флаги RCC->CSR / счётчик включений (PLAN п.58)
		p.state        = cfg->Config_common.State;
		p.state_err    = cfg->Config_common.StateErr;
		p.battery_pct  = (uint8_t)sensors_status.battery_charge_percent;
		p.battery_mv   = sensors_status.battery_charge_volt;
		memcpy(r->Pl, &p, sizeof p);
		break;
	}
	case TYPE_READINGS:
	{
		log_pl_readings_t p;
		memset(&p, 0, sizeof p);
		p.state       = cfg->Config_common.State;
		p.state_err   = cfg->Config_common.StateErr;
		p.battery_pct = (uint8_t)sensors_status.battery_charge_percent;
		p.battery_mv  = sensors_status.battery_charge_volt;
		p.aqi         = 0u;		// задел (§9): датчика качества воздуха нет
		p.temperature = (int16_t)sensors_status.Temperature;					// °C×100
		p.humidity    = (uint16_t)(sensors_status.Humidity * 100.0f / 1024.0f);	// %×1024 -> %×100
		p.pressure    = (uint16_t)sensors_status.Pressure;						// гПа×10
		p.motion      = log_motion_byte();
		memcpy(r->Pl, &p, sizeof p);
		break;
	}
	case TYPE_SNAPSHOT:
	{
		log_pl_snapshot_t p;
		memset(&p, 0, sizeof p);
#if CONFIG_GPS
		p.lat            = g_lat_scaled;	// ×1e7; (0,0) = нет фикса (§4.1)
		p.lon            = g_lon_scaled;
#endif
		p.battery_pct    = (uint8_t)sensors_status.battery_charge_percent;
		p.active_sensors = 0u;				// AVIS в комплекте нет (§9)
		// conc[8]/chan_thr_bits - нули до переноса каналов (§9)
		p.motion         = log_motion_byte();
		memcpy(r->Pl, &p, sizeof p);
		break;
	}
	case TYPE_PPE:
		// нули: СИЗ придут с переносом BLE-каналов (AVIS/MAC-таблица) - TODO §4.5
		break;
	case TYPE_GAS:
		// нули: до переноса каналов AVIS - TODO §4.3 (антидребезг 10 с - на стороне детектора порога)
		break;
	case TYPE_BEACONS:
		// зарезервирован (§4.6): периодика ВЫКЛЮЧЕНА, ручной снимок - TODO
		break;
	case TYPE_PARAM:
	default:
		// PARAM (в т.ч. NOP теста): param_id=0; состав уточняется с протоколами COM/Modbus (§4.7)
		break;
	}
}

//---------------------------------------------------------------------------------------------------------------------------
// Постановка события в очередь (снимок на текущий момент). КОНТЕКСТ: поток
// (любой; из ISR не звать - при переполнении печатается LOG_E).
// Возврат: 0 = поставлено, 1 = поставлено, но СТАРЕЙШЕЕ потеряно (очередь
// полна, §6), -1 = журнал выключен (CONFIG_SAVE_PARAM_LOG=0).
int log_post(TYPE_LOG type_log)
{
#if CONFIG_SAVE_PARAM_LOG
	EVENT_LOG r;
	TX_INTERRUPT_SAVE_AREA;
	int over;

	log_build(&r, type_log, &Sns_Cfg_struct);

	TX_DISABLE;
	over = logq_push(&logq, &r);
	logq_dbg_pushes++;
	TX_RESTORE;

	if (over != 0)
	{
		logq_dropped++;
		LOG_E(VLOG_M_SYS, "logq: FULL, oldest dropped (type %u)", (unsigned)type_log);
		return 1;
	}
	return 0;
#else
	(void)type_log;
	return -1;
#endif
}

//---------------------------------------------------------------------------------------------------------------------------
// Архивный тик (§4/§5): при ArchiveRecording != 0 и истечении TIMER_RTC_LOG
// поставить в очередь НАБОР из трёх записей SNAPSHOT + READINGS + PPE (общее
// working_hours - при разборе склеиваются в один «срез момента») и
// ПЕРЕЗАПУСТИТЬ таймер. Якорь периода - МОМЕНТ СОБЫТИЯ (постановка в
// очередь), а не факт записи во flash: задержка окна тишины в период больше
// не накапливается (прежняя схема RESET+START после записи давала «то 5, то
// 6 секунд» и систематический уход - PLAN п.56-58). Период: Archiveinterval
// = (мин<<8)|сек (§5: по умолчанию 10 мин 0 с). Звать из
// Vector_RunFlashMemory() каждый проход (~50 мс). КОНТЕКСТ: поток measure.
void log_archive_tick(SNS_CFG *pSnsCfg)
{
#if CONFIG_SAVE_PARAM_LOG
	if ((pSnsCfg->Config_archive.ArchiveRecording != 0u) &&
	    (TEST_TIMER_RTC(TIMER_RTC_LOG) != 0))
	{
		(void)log_post(TYPE_SNAPSHOT);
		(void)log_post(TYPE_READINGS);
		(void)log_post(TYPE_PPE);
		RESET_TIMER_RTC(TIMER_RTC_LOG);
		START_TIMER_RTC(TIMER_RTC_LOG,
		                log_archive_period_s(pSnsCfg->Config_archive.Archiveinterval));
	}
#else
	(void)pSnsCfg;
#endif
}

//---------------------------------------------------------------------------------------------------------------------------
// Запись ОДНОЙ готовой записи в кольцо на позицию CurrentAddrFile (ядро
// писателя). Номер проставляется ЗДЕСЬ позиционно (запись могла ждать в
// очереди - нумерация всё равно непрерывна). Граница сектора - стирание по
// log_erase_decision(); под звуком стирание EXT_BUSY -> запись остаётся В
// ОЧЕРЕДИ (писатель повторит). Возврат: 0 = записано, EXT_BUSY = шина/
// стирание отложено, -1 = ошибка flash. КОНТЕКСТ: поток measure.
static int log_write_rec(SNS_CFG *pSnsCfg, EVENT_LOG *rec)
{
  uint32_t cur = pSnsCfg->Config_archive.CurrentAddrFile;
  uint32_t beg = pSnsCfg->Config_archive.BegginAddrFile;
  int rc;

  rec->H.CurrentAddrFile = cur;		// позиционный номер - по месту записи

  if (((cur % LOG_RING_SLOTS) % LOG_SLOTS_PER_SECTOR) == 0u)	// граница сектора
  {
    int blank = 0;		// кольцо полное - проба не нужна (всё равно стирать)
    int dec;

    if ((uint32_t)(cur - beg) < LOG_USABLE_SLOTS)
    {
      uint8_t probe[4];
      blank = ((ext_read(log_slot_addr(cur), probe, sizeof probe) == 0) &&
               (probe[0] == 0xFFu) && (probe[1] == 0xFFu) &&
               (probe[2] == 0xFFu) && (probe[3] == 0xFFu));
      // ошибку чтения трактуем как "не пусто" - лишнее стирание безопаснее
    }

    dec = log_erase_decision(cur, beg, blank);
    if (dec != 0)
    {
      rc = ext_erase_sector(log_slot_addr(cur));
      if (rc == EXT_BUSY)
      {
        return EXT_BUSY;   // тихо: звук/шина - запись ЖДЁТ В ОЧЕРЕДИ, повтор на следующем проходе
      }
      if (rc != 0)
      {
        LOG_E(VLOG_M_SYS, "log: sector erase FAIL");
        return -1;
      }
      if (dec == 2)
      {
        // кольцо полное: начало сдвигается на число записей в секторе (§2)
        pSnsCfg->Config_archive.BegginAddrFile = beg + LOG_SLOTS_PER_SECTOR;
      }
    }
  }

  rc = ext_write(log_slot_addr(cur), (const uint8_t *)rec, LOG_SLOT_SIZE);
  if (rc == EXT_BUSY)
  {
    return EXT_BUSY;   // тихо: писатель повторит (запись в очереди)
  }
  if (rc != 0)
  {
    LOG_E(VLOG_M_SYS, "log: slot write FAIL");
    return -1;
  }
  pSnsCfg->Config_archive.CurrentAddrFile = cur + 1u;
  return 0;
}

//---------------------------------------------------------------------------------------------------------------------------
// ПИСАТЕЛЬ (§6): дренировать очередь во flash. Вызывается КАЖДЫЙ проход
// Vector_RunFlashMemory (~50 мс). program под звуком, erase в окно тишины
// (EXT_BUSY - записи остаются в очереди, порядок FIFO сохраняется);
// LOG_PTR - ОДИН РАЗ на пачку. Жёсткая ошибка (-1): запись теряется
// (счётчик cnt_flash_errors), очередь дренируется дальше - один битый слот
// не должен клинить журнал. logq_draining - защита от параллельного drain
// (measure + «предсмертный» из receiver при мягкой перезагрузке).
// Возврат: число записанных записей (>= 0).
int log_flush(SNS_CFG *pSnsCfg)
{
#if CONFIG_SAVE_PARAM_LOG
  int written = 0;
  TX_INTERRUPT_SAVE_AREA;

  TX_DISABLE;
  if (logq_draining != 0u)
  {
    TX_RESTORE;
    return 0;		// другой писатель уже дренирует - не мешаем
  }
  logq_draining = 1u;
  TX_RESTORE;

  for (;;)
  {
    EVENT_LOG rec;
    const EVENT_LOG *peek;
    int rc;

    TX_DISABLE;
    peek = logq_peek(&logq);
    if (peek != (const EVENT_LOG *)0)
    {
      rec = *peek;
    }
    TX_RESTORE;
    if (peek == (const EVENT_LOG *)0)
    {
      break;						// очередь пуста
    }

    rc = log_write_rec(pSnsCfg, &rec);
    if (rc == EXT_BUSY)
    {
      tasks_status.cnt_flash_defers++;	// звук/шина: запись осталась в очереди
      break;
    }

    TX_DISABLE;
    logq_pop(&logq);
    TX_RESTORE;

    if (rc != 0)
    {
      tasks_status.cnt_flash_errors++;	// жёсткая ошибка: запись потеряна, drain продолжается
      continue;
    }
    written++;
  }

  TX_DISABLE;
  logq_draining = 0u;
  TX_RESTORE;

  if (written > 0)
  {
    (void)log_ptr_save(pSnsCfg);	// ОДИН РАЗ на пачку (§6); EXT_BUSY - отложится до следующей
  }
  return written;
#else
  (void)pSnsCfg;
  return 0;
#endif
}

//===========================================================================================================================
// «Предсмертная» запись (TURN_OFF/FW_UPDATE перед NVIC_SystemReset): ставится
// В ТУ ЖЕ очередь (хронология сохраняется: запись уходит ПОСЛЕ уже queued
// событий) и дренируется синхронно с bounded-ретраем ~0,5 с (LOG_FORMAT §6:
// «потеря питания: теряется только содержимое очереди; предсмертная запись -
// bounded-ретрай»). Не слилось (звук/шина) - EXT_BUSY, вызывающий уходит в
// сброс без записи (best-effort, как прежде).
// КОНТЕКСТ: поток receiver_task (документированное исключение из правила
// «flash пишет только measure» - см. Vector_Options_System).
// Возврат: 0 = очередь пуста (всё записано), EXT_BUSY = не успели.
int save_event_type_2(SNS_CFG *pSnsCfg, TYPE_LOG type_log)
{
#if CONFIG_SAVE_PARAM_LOG
  uint8_t tries = 0;

  (void)log_post(type_log);

  do
  {
    (void)log_flush(pSnsCfg);
    if (logq_depth() == 0u)
    {
      return 0;
    }
    VTICK_SLEEP_MS(50);				// ждать окно тишины/освобождение шины
    tries++;
  } while (tries < 10u);

  return EXT_BUSY;
#else
  (void)pSnsCfg; (void)type_log;
  return 0;
#endif
}

//===========================================================================================================================
// Чтение записи журнала ПО НОМЕРУ УКАЗАТЕЛЯ (§7.1). Валидный ptr:
// [BegginAddrFile, CurrentAddrFile). КОНТЕКСТ: поток.
// Возврат: 0 = ok (запись валидна: Fmt_ver/тип/длина), 1 = указатель вне
// доступного диапазона (или журнал выключен/пуст), 2 = запись невалидна
// (мусор/старый 64-байтный формат), -1 = ошибка шины (включая EXT_BUSY от
// ext_read - повторить).
int log_read(SNS_CFG *pSnsCfg, uint32_t ptr, EVENT_LOG *out)
{
#if CONFIG_SAVE_PARAM_LOG
  uint32_t cur = pSnsCfg->Config_archive.CurrentAddrFile;
  uint32_t beg = pSnsCfg->Config_archive.BegginAddrFile;

  if (out == (EVENT_LOG *)0) { return 1; }
  if (((int32_t)(ptr - beg) < 0) || ((int32_t)(cur - ptr) <= 0)) { return 1; }
  if (ext_read(log_slot_addr(ptr), (uint8_t *)out, LOG_SLOT_SIZE) != 0) { return -1; }
  if (log_rec_valid(out) == 0) { return 2; }
  return 0;
#else
  (void)pSnsCfg; (void)ptr; (void)out;
  return 1;
#endif
}

//===========================================================================================================================
// Поиск ближайших записей ПО ДИАПАЗОНУ ВРЕМЕНИ (§7.2). Время записи =
// H.working_hours (суммарные секунды: offset + часы; монотонны: FIFO-очередь
// сохраняет хронологию, часы зеркалят календарь RTC). ЖУРНАЛ УПОРЯДОЧЕН ПО
// ВРЕМЕНИ -> два
// бинарных поиска по 4-байтному ключу на смещении +4 (не читая слот целиком;
// смещение едино для всех типов). Результат - НОМЕРА УКАЗАТЕЛЕЙ:
//   *ptr_from = первая запись с временем >= t_from (все раньше -> последняя);
//   *ptr_to   = последняя запись с временем <= t_to (все позже -> первая).
// Записи одного архивного тика делят одну секунду (набор SNAPSHOT+READINGS+
// PPE) - поиск >=/<= это корректно обрабатывает (§7.2).
// Возврат: 0 = диапазон найден (ptr_from <= ptr_to), 1 = точного попадания
// нет (указатели = ближайшие) / журнал пуст или выключен, -1 = ошибка шины
// (повторить). КОНТЕКСТ: поток (COM/BLE-модуль чтения журнала).
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
//  	  type_log |= TYPE_LOG_PARAM_NOP; // TYPE_LOG_PARAM_UPDATE_* удалены автором;
//  	                                  // когда перенесётся COM-модуль - вернуть типы обновлений
//    }
//  }
//  if(type_log){
//	  /* v2 (LOG_FORMAT §6): снимок события - в RAM-очередь (log_post); во
//	     flash перенесёт писатель Vector_RunFlashMemory() (поток measure,
//	     50 мс). Очередь вбирает всплески - «новое побеждает старое» больше
//	     не нужно. */
//	  (void)log_post((TYPE_LOG)type_log);
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
	// (поля журнала/архива переехали в Config_archive)
	pSnsCfg->Config_archive.CurrentAddrFile                               = 0;
	pSnsCfg->Config_archive.BegginAddrFile                                = 0;
	pSnsCfg->Config_archive.ArchiveRecording                              = 0;
	// (мин<<8)|сек; ПО УМОЛЧАНИЮ 10 мин 0 с (LOG_FORMAT.md §5/§8.9, диапазон
	// 10-15 мин). Для стендовых прогонов можно уменьшить (например 0x0005 =
	// 5 с) - через меню/отладчик, не меняя дефолт.
	pSnsCfg->Config_archive.Archiveinterval                               = 0x0A00;
	// Working_data (часы/смещение) НЕ трогаем: часы - рантайм (RTC BKP0),
	// дефолтный конфиг их обнулять не должен.
	//----------------------------------------------------------------------------
	// Настройка Lora
#if CONFIG_LORA
	Lora_Cfg_ApplyDefaults(pSnsCfg);
	pSnsCfg->Config_common.PeriodTimeLora |= TIME_RTC_LORA_DATA_SET;			// Период сообщений
	pSnsCfg->Config_common.PeriodTimeLora |= (TIME_RTC_LORA_DATA_SET<<8)/3;	// Период аварийный сообщ
#endif
}
