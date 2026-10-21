/*
 * Config_save_read.h
 *
 * Журнал событий v2 (LOG_FORMAT.md, слот 40 байт) + RAM-очередь записей
 * со снимком на момент события (§6). Прежний формат (слот 64 байта,
 * однослотовый флаг SaveEvent) ЗАМЕНЁН: миграции нет (решение автора,
 * LOG_FORMAT.md §2/§8.10) - при первом старте кольцо переинициализируется
 * (в странице LOG_ptr валидны только записи с маркером формата v2, старые
 * игнорируются -> cur/beg = 0; нечистые сектора кольца стираются лениво,
 * blank-пробой на границе сектора).
 *
 * Геометрия и очередь - static inline ЗДЕСЬ (чистая арифметика, без HAL/
 * ThreadX): прошивочный писатель и хост-тест tools/test_logq/ исполняют
 * ОДИН И ТОТ ЖЕ код.
 */
#ifndef CONFIG_SAVE_READ_H
#define CONFIG_SAVE_READ_H

#include <stdint.h>
#include <stddef.h>   /* offsetof: компиляторный контроль смещения ключа времени */
#include <string.h>   /* memset/memcpy: сборка записи (inline-хелперы)      */
#include "shared_types.h"   /* SNS_CFG */
#include "extstore.h"       /* EXT_BUSY - коды возврата */

#ifdef __cplusplus
extern "C" {
#endif

//===========================================================================================================================
// ТИПЫ ЗАПИСЕЙ (LOG_FORMAT.md §4, формат v2). Числовые значения - из
// спецификации; TYPE_LOG_PARAM_NOP прежнее имя PARAM (0x08) - оставлено
// алиасом (тест/заглушка обновления параметра).
typedef enum {
	TYPE_TURN_ON       = 0x00,
	TYPE_TURN_OFF      = 0x01,
	TYPE_FW_UPDATE     = 0x02,
	TYPE_READINGS      = 0x04,
	TYPE_PARAM         = 0x08,
	TYPE_LOG_PARAM_NOP = 0x08,	// алиас TYPE_PARAM (NOP-обновление)
	TYPE_GAS           = 0x10,
	TYPE_BEACONS       = 0x20,
	TYPE_PPE           = 0x30,
	TYPE_SNAPSHOT      = 0x40,
}TYPE_LOG;

//===========================================================================================================================
// СЛОТ v2 = 40 байт: заголовок 12 Б + payload 28 Б (LOG_FORMAT.md §3).
// Время СОБЫТИЯ (не момент записи!) - снимок делается при постановке в
// очередь (log_post). working_hours на фиксированном смещении +4 - ключ
// бинарного поиска без разбора payload.
typedef struct __attribute__((packed))
{
	uint8_t  Type_log;			// +0 тип записи (TYPE_LOG) - селектор payload
	uint8_t  Fmt_ver;			// +1 версия формата слота (= LOG_FMT_VER)
	uint8_t  Payload_len;		// +2 значимых байт payload (<= 28)
	uint8_t  Flags;				// +3 бит0 = «продолжение» (части записи), ост. rfu
	uint32_t working_hours;		// +4 ВРЕМЯ СОБЫТИЯ, с (offset + working_hours конфига)
	uint32_t CurrentAddrFile;	// +8 номер записи (абсолютная нумерация кольца)
} log_hdr_t;					// РОВНО 12 байт

#define LOG_FMT_VER			1u		// версия формата слота (Hdr.Fmt_ver)
#define LOG_PAYLOAD_MAX		28u		// максимальная длина payload
#define LOG_FLAG_CONT		0x01u	// Flags.бит0: запись - часть серии (BEACONS)

// Payload'ы по типам (§4.1-4.7) - ВСЕ ровно 28 байт, packed.
// §4.1 SNAPSHOT (0x40) - «где был + чем дышал», архивный тик.
typedef struct __attribute__((packed))
{
	int32_t  lat, lon;			// 8  GPS ×1e7; (0,0) = нет фикса
	uint8_t  battery_pct;		// 1  заряд трекера, %
	uint8_t  active_sensors;	// 1  маска каналов AVIS (нет AVIS -> 0, §9)
	uint16_t conc[8];			// 16 концентрации (pass-through, единицы отправителя)
	uint8_t  chan_thr_bits;		// 1  маска порогов на момент тика
	uint8_t  motion;			// 1  MOTION_BITS (см. ниже) - LIS3DH
} log_pl_snapshot_t;			// 28 байт

// §4.2 READINGS (0x04) - полная телеметрия трекера, архивный тик.
typedef struct __attribute__((packed))
{
	uint32_t state;				// 4  Config_common.State
	uint32_t state_err;			// 4  Config_common.StateErr
	uint8_t  battery_pct;		// 1  MAX17048
	uint16_t battery_mv;		// 2  MAX17048
	uint16_t aqi;				// 2  задел (датчика нет - пишем 0, §9)
	int16_t  temperature;		// 2  °C×100 - BME280
	uint16_t humidity;			// 2  %×100 - BME280 (драйвер отдаёт %×1024)
	uint16_t pressure;			// 2  гПа×10 - BME280
	uint8_t  motion;			// 1  MOTION_BITS - LIS3DH
	uint8_t  rfu[8];			// 8
} log_pl_readings_t;			// 28 байт

// §4.3 GAS (0x10) - полный срез AVIS, событийная (порог, антидребезг 10 с).
typedef struct __attribute__((packed))
{
	uint8_t  slot;				// 1  слот AVIS в MAC-таблице
	uint8_t  dev_charge;		// 1  заряд газоанализатора, %
	uint8_t  active_sensors;	// 1  маска каналов
	uint16_t conc[8];			// 16 концентрации (pass-through)
	uint8_t  chan_state[8];		// 8  StateErr каждого канала
	uint8_t  rfu[1];			// 1
} log_pl_gas_t;					// 28 байт

// §4.4 TURN_ON/TURN_OFF/FW_UPDATE (0x00/0x01/0x02) - записи питания.
typedef struct __attribute__((packed))
{
	uint16_t fw_version;		// 2  FIRMWARE_VERSION (только в записях питания, §8.7)
	uint8_t  hw_version;		// 1  Config_common.HardwareVersion
	uint8_t  brand;				// 1  MIRAX/BACS (current_build_type)
	uint32_t reset_reason;		// 4  флаги причины сброса / счётчик включений
	uint32_t state;				// 4  Config_common.State
	uint32_t state_err;			// 4  Config_common.StateErr
	uint8_t  battery_pct;		// 1
	uint16_t battery_mv;		// 2
	uint8_t  rfu[9];			// 9
} log_pl_power_t;				// 28 байт

// §4.5 PPE (0x30) - срез СИЗ, самодостаточная (архивный тик + изменение маски).
typedef struct __attribute__((packed))
{
	uint8_t  presence_mask;		// 1  бит k = СИЗ позиции k присутствует/надета
	uint8_t  charge[8];			// 8  заряд каждой позиции (0/1/2)
	uint8_t  ppe_type[8];		// 8  тип каждой позиции
	uint8_t  serial[8];			// 8  заводские идентификаторы (1 байт, протокол BLE)
	uint8_t  rfu[3];			// 3
} log_pl_ppe_t;					// 28 байт

// §4.6 BEACONS (0x20) - зарезервирован (периодика ВЫКЛЮЧЕНА решением автора).
typedef struct __attribute__((packed))
{
	uint8_t  count;				// 1  маяков в этой записи (<= 6)
	uint8_t  total;				// 1  всего видимых (одинаков в частях)
	struct __attribute__((packed))
	{
		uint16_t short_id;		// 2
		int8_t   rssi;			// 1
		uint8_t  battery;		// 1  0/1/2
	} b[6];						// 24
	uint8_t  rfu[2];			// 2
} log_pl_beacons_t;				// 28 байт

// §4.7 PARAM (0x08) - обновление параметра (задел; состав уточняется с
// протоколами COM/Modbus). NOP-запись теста - все нули.
typedef struct __attribute__((packed))
{
	uint16_t param_id;			// 2  идентификатор параметра (0 = NOP)
	uint8_t  source;			// 1  0 = COM, 1 = Modbus/BLE, 2 = LoRa downlink
	uint8_t  old_v[8];			// 8  прежнее значение
	uint8_t  new_v[8];			// 8  новое значение
	uint8_t  rfu[9];			// 9
} log_pl_param_t;				// 28 байт

// Запись журнала = СЛОТ кольца v2: РОВНО 40 байт (assert ниже).
typedef struct __attribute__((packed))
{
	log_hdr_t H;
	uint8_t   Pl[LOG_PAYLOAD_MAX];	// интерпретируется по H.Type_log
} EVENT_LOG;

// MOTION_BITS (§4.1/§4.2): поза - биты 0-1 (lis3dh_orientation), падение -
// бит2 (LIS3DH free-fall), удар - бит3, отсутствие активности - бит4.
// Удар/неактивность sources пока нет - не выставляем (TODO с переносом каналов).
#define LOG_MOTION_POSE_MASK	0x03u
#define LOG_MOTION_FALL			0x04u
#define LOG_MOTION_SHOCK		0x08u
#define LOG_MOTION_INACTIVE		0x10u

// Компиляторный самоконтроль раскладки (§3/§4).
typedef char log_hdr_size_assert[(sizeof(log_hdr_t) == 12u) ? 1 : -1];
typedef char log_slot_size_assert[(sizeof(EVENT_LOG) == 40u) ? 1 : -1];
typedef char log_pl_snapshot_assert[(sizeof(log_pl_snapshot_t) == LOG_PAYLOAD_MAX) ? 1 : -1];
typedef char log_pl_readings_assert[(sizeof(log_pl_readings_t) == LOG_PAYLOAD_MAX) ? 1 : -1];
typedef char log_pl_gas_assert[(sizeof(log_pl_gas_t) == LOG_PAYLOAD_MAX) ? 1 : -1];
typedef char log_pl_power_assert[(sizeof(log_pl_power_t) == LOG_PAYLOAD_MAX) ? 1 : -1];
typedef char log_pl_ppe_assert[(sizeof(log_pl_ppe_t) == LOG_PAYLOAD_MAX) ? 1 : -1];
typedef char log_pl_beacons_assert[(sizeof(log_pl_beacons_t) == LOG_PAYLOAD_MAX) ? 1 : -1];
typedef char log_pl_param_assert[(sizeof(log_pl_param_t) == LOG_PAYLOAD_MAX) ? 1 : -1];

//===========================================================================================================================
// ГЕОМЕТРИЯ КОЛЬЦА v2 (LOG_FORMAT.md §2) - чистая арифметика, static inline:
// слот 40 Б; в секторе 4 КБ - 102 слота (4080 Б, хвост 16 Б не используется);
// 512 секторов = 2 МБ = 52224 записи; phys = ptr mod 52224. Указатели
// CurrentAddrFile/BegginAddrFile монотонные u32 (естественный заворот;
// 2^32 не кратно 52224 - раз за ~27 тыс. лет кольцо «перескочит» хвост,
// на целостность не влияет: физпозиция определяется самим ptr).
#define LOG_SLOT_SIZE			40u
#define LOG_SLOTS_PER_SECTOR	102u
#define LOG_SECTOR_SIZE			4096u
#define LOG_RING_SLOTS			52224u	/* 512 × 102 */
#define LOG_USABLE_SLOTS		(LOG_RING_SLOTS - LOG_SLOTS_PER_SECTOR)	/* сектор запаса */
#define LOG_TIME_OFFSET			4u		/* offsetof(EVENT_LOG, H.working_hours) */

typedef char log_geom_assert[(LOG_SLOTS_PER_SECTOR * LOG_SLOT_SIZE <= LOG_SECTOR_SIZE) ? 1 : -1];
typedef char log_time_offset_assert[(offsetof(EVENT_LOG, H) + offsetof(log_hdr_t, working_hours) == LOG_TIME_OFFSET) ? 1 : -1];

// Физический адрес слота по номеру указателя (§2). КОНТЕКСТ: любой, чистая
// арифметика (хост-тестируемо).
static inline uint32_t log_slot_addr_v2(uint32_t log_base, uint32_t ptr)
{
	uint32_t phys = ptr % LOG_RING_SLOTS;
	return log_base
	     + (phys / LOG_SLOTS_PER_SECTOR) * LOG_SECTOR_SIZE
	     + (phys % LOG_SLOTS_PER_SECTOR) * LOG_SLOT_SIZE;
}

// Решение о стирании перед записью на границу сектора (§2 «Переполнение»):
// 0 = писать без стирания, 1 = стереть сектор (слот не пуст), 2 = стереть и
// сдвинуть BegginAddrFile на LOG_SLOTS_PER_SECTOR (кольцо полное).
// blank - результат blank-пробы первого слота сектора (1 = все 0xFF);
// при full не используется. КОНТЕКСТ: любой, чистая арифметика.
static inline int log_erase_decision(uint32_t cur, uint32_t beg, int blank)
{
	uint32_t phys = cur % LOG_RING_SLOTS;
	if ((phys % LOG_SLOTS_PER_SECTOR) != 0u)
	{
		return 0;						// не на границе сектора
	}
	if ((uint32_t)(cur - beg) >= LOG_USABLE_SLOTS)
	{
		return 2;						// кольцо полное: стирание + сдвиг начала
	}
	return (blank != 0) ? 0 : 1;		// мусор старого формата/прошлых кругов
}

// Archiveinterval ((мин<<8)|сек, §5) -> период архивного тика в секундах
// (uint16: максимум ~18 ч; countdown_time_rtc хранит uint16).
static inline uint16_t log_archive_period_s(uint16_t archive_interval)
{
	return (uint16_t)(((uint32_t)(archive_interval >> 8) * 60u)
	                + ((uint32_t)archive_interval & 0xFFu));
}

// Валидность записи v2 (§7.1): Fmt_ver, известный Type_log, Payload_len <= 28.
static inline int log_rec_valid(const EVENT_LOG *r)
{
	switch (r->H.Type_log)
	{
	case TYPE_TURN_ON:  case TYPE_TURN_OFF: case TYPE_FW_UPDATE:
	case TYPE_READINGS: case TYPE_PARAM:    case TYPE_GAS:
	case TYPE_BEACONS:  case TYPE_PPE:      case TYPE_SNAPSHOT:
		break;
	default:
		return 0;
	}
	return ((r->H.Fmt_ver == LOG_FMT_VER) && (r->H.Payload_len <= LOG_PAYLOAD_MAX)) ? 1 : 0;
}

//===========================================================================================================================
// RAM-ОЧЕРЕДЬ ЗАПИСЕЙ (LOG_FORMAT.md §6): кольцо 16 × 40 Б = 640 Б.
// Снимок (время/состояние/данные) делается В МОМЕНТ СОБЫТИЯ (log_post) -
// задержка записи во flash время записи НЕ искажает; всплески не теряются
// (FIFO сохраняет хронологию - бинарный поиск по времени остаётся корректным).
// Логика - static inline (общая для прошивки и хост-теста); критическая
// секция (TX_DISABLE) - на стороне .c (log_post/log_flush).
#ifndef LOGQ_SLOTS
#define LOGQ_SLOTS	16u
#endif

typedef struct
{
	EVENT_LOG rec[LOGQ_SLOTS];
	uint8_t   head;		// индекс самой СТАРОЙ записи (отсюда читает писатель)
	uint8_t   count;	// сколько записей в очереди (0..LOGQ_SLOTS)
} logq_ctx_t;

typedef char logq_slots_assert[(LOGQ_SLOTS <= 255u) ? 1 : -1];

static inline void logq_init(logq_ctx_t *q)
{
	q->head  = 0u;
	q->count = 0u;
}

static inline uint32_t logq_count(const logq_ctx_t *q)
{
	return (uint32_t)q->count;
}

static inline int logq_full(const logq_ctx_t *q)
{
	return (q->count >= (uint8_t)LOGQ_SLOTS) ? 1 : 0;
}

// Постановка в очередь. Возврат: 0 = поставлена, 1 = очередь была полна -
// СТАРЕЙШАЯ перезаписана (вызывающий считает потерю и печатает LOG_E, §6).
static inline int logq_push(logq_ctx_t *q, const EVENT_LOG *r)
{
	uint8_t tail = (uint8_t)((uint32_t)(q->head + q->count) % LOGQ_SLOTS);
	int over = 0;

	q->rec[tail] = *r;
	if (q->count >= (uint8_t)LOGQ_SLOTS)
	{
		// полная: tail == head (слот старейшей) - новая запись встаёт на её
		// место, head сдвигается: FIFO сохраняется, старейшая теряется.
		q->head = (uint8_t)((q->head + 1u) % LOGQ_SLOTS);
		over = 1;
	}
	else
	{
		q->count++;
	}
	return over;
}

// Старейшая запись без извлечения (NULL = очередь пуста).
static inline const EVENT_LOG *logq_peek(const logq_ctx_t *q)
{
	return (q->count != 0u) ? &q->rec[q->head] : (const EVENT_LOG *)0;
}

// Извлечь старейшую (после успешной записи во flash). Пустая - no-op.
static inline void logq_pop(logq_ctx_t *q)
{
	if (q->count != 0u)
	{
		q->head = (uint8_t)((q->head + 1u) % LOGQ_SLOTS);
		q->count--;
	}
}

//===========================================================================================================================
// API журнала v2
//===========================================================================================================================
// Построить запись type_log со снимком НА МОМЕНТ ВЫЗОВА (время = offset +
// working_hours; данные - sensors_status/конфиг/GPS по типу, §4) и поставить
// в RAM-очередь. Номер записи (CurrentAddrFile) проставляет ПИСАТЕЛЬ в момент
// переноса во flash (позиционный) - очередь номеров не потребляет.
// КОНТЕКСТ: поток (любой - очередь под критической секцией; из ISR не звать:
// LOG_E внутри при переполнении). Возврат: 0 = поставлена, 1 = поставлена,
// но старейшая потеряна (переполнение очереди), -1 = журнал выключен.
extern int log_post(TYPE_LOG type_log);

// Периодический архив (§4/§5): при ArchiveRecording != 0 и истечении
// TIMER_RTC_LOG поставить в очередь НАБОР из трёх записей SNAPSHOT +
// READINGS + PPE (общее working_hours - при разборе склеиваются в срез) и
// ПЕРЕЗАПУСТИТЬ таймер (якорь периода - момент события, а не момент записи:
// задержка flash в период не попадает). Звать из Vector_RunFlashMemory()
// каждый проход (~50 мс). КОНТЕКСТ: поток measure.
extern void log_archive_tick(SNS_CFG *pSnsCfg);

// Писатель (§6): перенести очередь во flash. program (1-6 мс) разрешён ПОД
// ЗВУКОМ (93-мс запас буфера плеера поглощает); стирание сектора ждёт окно
// тишины (EXT_BUSY - записи остаются в очереди, повтор на следующем проходе);
// LOG_PTR сохраняется ОДИН РАЗ на всю пачку. Номер записи проставляется
// здесь (позиционный). Возврат: число записанных записей (>= 0).
// КОНТЕКСТ: поток measure (Vector_RunFlashMemory), 50 мс.
extern int log_flush(SNS_CFG *pSnsCfg);

// «Предсмертная» запись (TURN_OFF/FW_UPDATE перед NVIC_SystemReset):
// поставить в очередь и синхронно дренировать с bounded-ретраем ~0,5 с
// (EXT_BUSY - звук/шина; очередь-drain сохраняет хронологию: запись уходит
// ПОСЛЕ уже queued событий). Возврат: 0 = очередь пуста (всё записано),
// EXT_BUSY = так и не слили (best-effort, вызывающий уходит в сброс).
// КОНТЕКСТ: поток receiver (документированное исключение).
extern int save_event_type_2(SNS_CFG *pSnsCfg, TYPE_LOG type_log);

// Чтение записи ПО НОМЕРУ УКАЗАТЕЛЯ: ptr в диапазоне [BegginAddrFile,
// CurrentAddrFile). Возврат: 0 = ok, 1 = ptr вне доступного диапазона (или
// журнал выключен/пуст), 2 = запись невалидна (Fmt_ver/тип/длина - мусор или
// старый формат 64 Б), -1 = ошибка шины (EXT_BUSY транзитом). КОНТЕКСТ: поток.
extern int log_read(SNS_CFG *pSnsCfg, uint32_t ptr, EVENT_LOG *out);

// Поиск ближайших записей ПО ДИАПАЗОНУ ВРЕМЕНИ (H.working_hours на +4, ключ
// единый для всех типов; журнал упорядочен - FIFO-очередь + монотонные часы):
// *ptr_from = первая запись с временем >= t_from (если таких нет - последняя),
// *ptr_to = последняя с временем <= t_to (если таких нет - первая).
// Возврат: 0 = диапазон найден (ptr_from <= ptr_to), 1 = точного попадания
// нет (указатели = ближайшие) / журнал пуст, 2 = попалась невалидная запись
// (кольцо повреждено), -1 = ошибка шины. КОНТЕКСТ: поток.
extern int log_find_by_time(SNS_CFG *pSnsCfg, uint32_t t_from, uint32_t t_to,
                            uint32_t *ptr_from, uint32_t *ptr_to);

// Состояние очереди (отчёты теста/диагностика).
extern volatile uint32_t logq_dropped;		// потерь на переполнении очереди (накопительно)
extern volatile uint32_t logq_dbg_pushes;	// всего постановок в очередь
extern uint32_t logq_depth(void);			// текущая глубина (под крит. секцией)

//--------------------------------------------------------------------------------------------------------------
// Коды возврата save_param: 0 = записано, EXT_BUSY (-2) = шина занята или
// стирание отложено (звук) - вызывающий НЕ гасит флаг и повторяет позже,
// -1 = ошибка flash (LOG_E уже напечатан).
// addr - область внешней flash (sfmap.h): SF_CONFIG_BASE = рабочий конфиг
// (SaveConfig, загрузка при старте), SF_CONFIG_DEFAULT = заводской конфиг
// (SaveConfigDefault записывает туда текущий; ReadConfigDefault - СБРОС К
// ЗАВОДСКИМ: читает область через буфер Cfg_structdef_read и копирует его
// в рабочий Sns_Cfg_struct).
extern int      save_param(uint8_t * RamData, uint16_t len, uint32_t addr);     // Сохранение параметров
extern uint8_t  read_param(uint8_t * RamData, uint16_t len, uint32_t addr);     // Чтение параметров (1 = CRC верен)
//--------------------------------------------------------------------------------------------------------------
// Сравнение текущего конфига с сохранённым (после приёма новых параметров)
//extern void     compare_param(SNS_CFG *pSnsCfgread, SNS_CFG *pSnsCfgCur);
//--------------------------------------------------------------------------------------------------------------
extern void     DefaultConfig(SNS_CFG *pSnsCfg);                                // Начальная или тестовая конфигурация
//--------------------------------------------------------------------------------------------------------------
// бренд прибора (MIRAX/BACS) и страница-кольцо указателей архива.
// Init_Build_Type(): прочитать current_build_type из последней страницы
// внутренней flash (FLASH_ADDRESS_BUILD_TYPE). КОНТЕКСТ: Measure Task
// (Vector_Run_Pre_Init). Возврат: нет.
extern void     Init_Build_Type(void);
extern uint16_t current_build_type;      // бренд: MIRAX=0 / BACS=1 (рантайм)
// log_ptr_load(): скан страницы SF_LOG_PTR (свежая запись = max seq c валидным
// CRC И маркером формата v2 в поле pad; записи прежней прошивки (pad=0xFFFF)
// игнорируются - «миграции нет», LOG_FORMAT.md §2). apply=1 - перенести
// cur/beg в pSnsCfg->Config_archive; если валидных записей v2 НЕТ (первый
// старт нового формата) - cur/beg = 0 (кольцо переинициализируется; нечистые
// сектора стираются лениво blank-пробой при записи). КОНТЕКСТ: Measure Task
// (Vector_Run_Pre_Init, после чтения конфига). Возврат: 1 найдена валидная
// запись v2 / 0 пуста или только старые / -1 ошибка шины.
extern int      log_ptr_load(SNS_CFG *pSnsCfg, uint8_t apply);
// log_ptr_save(): сохранить CurrentAddrFile/BegginAddrFile (16-байтная запись
// в кольцо SF_LOG_PTR); вызывается ОДИН РАЗ НА ПАЧКУ записей журнала из
// log_flush (§6 - износ страницы указателей не зависит от частоты записей).
// КОНТЕКСТ: Measure Task. Возврат: 0 ok / EXT_BUSY отложено (звук/шина;
// указатели в RAM актуальны, повторятся со следующей пачкой) / -1 ошибка.
extern int      log_ptr_save(const SNS_CFG *pSnsCfg);
extern volatile uint32_t log_ptr_dbg_saves;   // счётчики для Expressions
extern volatile uint32_t log_ptr_dbg_defers;
//===========================================================================================================================
extern uint8_t  SaveConfig;             	// 1 = сохранить конфигурацию (снимает Vector_RunFlashMemory)
extern uint8_t SaveConfigDefault;       	// 1 = записать ТЕКУЩИЙ конфиг в заводскую область SF_CONFIG_DEFAULT
extern uint8_t ReadConfigDefault;		// 1 = СБРОС К ЗАВОДСКИМ: прочитать рабочий Sns_Cfg_struct из области SF_CONFIG_DEFAULT
//===========================================================================================================================
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_SAVE_READ_H */
