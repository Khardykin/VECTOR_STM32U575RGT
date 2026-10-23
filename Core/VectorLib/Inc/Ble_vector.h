/**
 * @file Ble_vector.h
 * @brief BLE-модуль MK01: AT-протокол (сторона УПРАВЛЕНИЯ модулем) + ПРОТОКОЛ
 *        ОБМЕНА v1 (docs/Mirax-Ble-protocol-v1.md).
 *
 * Часть 1 - управление модулем: порт Ble_mk01.c/h из Avis по решению автора
 * (п.2): "полностью копируешь весь функционал, кроме BLE_CMD_ADVMSDDATASET,
 * которую заменяем обычным запросом BLE_CMD_ADVMSDDATAGET, и на которую нам
 * отвечают структурой. И так периодически опрашиваем". Полный набор команд
 * модуля сохранён (AT/имя/период/мощность/старт/сон/DFU/SYSTEMOFF/BLEADDR?/
 * MODINFO?/ADVMSDDATAGET).
 *
 * Часть 2 - ПРОТОКОЛ ДАННЫХ v1: что именно приходит в ответе
 * ADVMSDDATAGET. Трекер читает по BLE данные, которые затем уходят в LoRa v4
 * (Mirax-Lora-protocol-v4.md): BLE-маяки (блок BLEBeacons), СИЗ (блок PPE) и
 * под-устройства (одно из них - газоанализатор AVIS, секция SUB_TYPE_GAS).
 * Первый байт любого пакета - Header: биты 7..4 = тип пакета, биты 3..0 =
 * версия протокола (у нас = BLE_PROTO_VER = 1). Legacy-пакет Avis начинается
 * с 0x02 (data_struct_ver=2, тип 0) - по старшему nibble 0 он однозначно
 * отличается от пакетов v1 и принимается КАК ЕСТЬ (существующие приборы AVIS
 * перепрошивать не нужно). Ограничение автора: пакет <= 256 байт
 * (BLE_DATA_SET_MAX_SIZE); по UART ответ приходит hex-ASCII, поэтому буфер
 * приёма модуля 520 байт.
 *
 * Принятые пакеты раскладываются в: Connected_device (SNS_Dev_Type - каналы
 * AVIS, shared_types.h) и Ble_telemetry (маяки/СИЗ/признак AVIS - туда же).
 * Читатель обоих - LoRa v4 (Lora_S7678S.c) и индикация; свежесть данных -
 * по *_ms меткам (BLE_TELEMETRY_TTL_MS).
 *
 * Обмен СО СВОИМ модулем - синхронный (Ble_TransmitReceive: 4 попытки,
 * контроль OK/ERROR, как в Avis). Байты USART3 складывает ISR в ring
 * InputBuffer[TYPE_BLE] (Vector_main.c); читает ring ТОЛЬКО Ble_TransmitReceive
 * - публичный Ble_Receive() (вызов из Receiver Task) оставлен пустым, чтобы
 * не выедать ответы модуля (в Avis Receiver Task BLE тоже не обслуживает).
 *
 * Управление: CONFIG_BLE (config_device.h), USART_BLE (huart3, PC4/PC5,
 * 115200), пин BLE_RESET (PC12). Запуск из Measure Task - Ble_Run()
 * (переопределяет weak-заглушку Vector_main.c), скелет - Avis Ble_Run():
 * INIT-таймер -> Ble_Init(serial); SET-таймер -> Ble_MsdDataGet() (опрос);
 * SLEEP-таймер -> Ble_MsdDataSleep(); ошибка 3 серий подряд ->
 * ST_COMMON_BIT_ERR_BLE (общая ошибка BLE).
 *
 * Имя модуля: BLE_Label[сборка] + серийный номер (SerialHi/SerialLo, формат
 * %04d%04d как у Avis Device2_1). НАБОР МЕТКИ VECTOR - за автором (TODO,
 * временно "VEC").
 *
 * ВНИМАНИЕ: файл реализует ПРЕЖНЮЮ редакцию протокола данных BLE (ответ
 * DATAGET с Header-байтом, пакеты BEACONS/PPE/SUBDEV/AGG + legacy Avis).
 * ДЕЙСТВУЮЩАЯ спецификация - docs/Mirax-Ble-protocol-v1.md (MAC-фильтр по
 * слотам, MSDGETALL/MSDGET, маяки Eddystone, мост конфигуратора Modbus RTU);
 * код будет приведён к ней, комментарии ниже описывают ТЕКУЩЕЕ состояние.
 */
#ifndef __BLE_VECTOR_H
#define __BLE_VECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32u5xx_hal.h"
#include "config_device.h"   /* CONFIG_BLE - ОБЯЗАТЕЛЬНО до #if (без него
                                макрос не определён в точке #if и весь модуль
                                молча вырезается даже при CONFIG_BLE=1)       */
#include <stdint.h>
#include <string.h>

#if CONFIG_BLE
#include "shared_types.h"    /* ble_beacon_t/ble_ppe_item_t/BLE_TELEMETRY  */

#define BLE_TRANSMIT_REPEAT		(4)			// Количество попыток отправить пакет
#define BLE_ERROR_DATA_REPEAT	(3)			// Допустимое количество серий с ошибками

// Максимальный размер пакета протокола v1 (ограничение автора). Ответ
// ADVMSDDATAGET приходит hex-ASCII (2 символа на байт + эхо + "OK") - буфер
// приёма модуля 520 байт (см. Ble_vector.c).
#define BLE_DATA_SET_MAX_SIZE	(256)

#define BLE_WAIT_UART_TIMEOUT	(35u)		// мс ожидания ответа модуля (как в Avis)
#define BLE_WAIT_UART_DELAY		(5u)		// мс добавки на дотекание хвоста ответа

// Команды AT-протокола модуля MK01 (строковый индекс = BLE_CMD_t)
typedef enum {
	CMD_AT = 0,				// Проверка связи
	CMD_MODINFO,			// Запрос информации о модуле
	CMD_ADVBLENAMESET,		// Задать имя модуля в сети
	CMD_ADVMSDDATAGET,		// ЗАПРОС принятых данных (ответ - пакет протокола v1)
	CMD_ADVSTART,			// Включить/выключить рекламу
	CMD_ADVPERIODMS,		// Период рекламы, мс
	CMD_ADVPOWER,			// Мощность рекламы
	CMD_SLEEP,				// Сон модуля
	CMD_SYSTEMOFF,			// Выключение модуля
	CMD_DFU,				// Режим прошивки модуля
	CMD_BLEADDR,			// Запрос MAC-адреса модуля
	CMD_COUNT,
	CMD_UNKNOWN
} BLE_CMD_t;

//===========================================================================================================================
// ПРОТОКОЛ ДАННЫХ v1 (docs/Mirax-Ble-protocol-v1.md)
// Header каждого пакета: биты 7..4 = тип (BLE_PKT_*), биты 3..0 = версия.
// Пакет Avis (str_ble_data_set_t) начинается с байта data_struct_ver: тип 0
// (GAS_LEGACY). ВЕРСИЯ 3 = канал 6 байт (Setting_channel u16, решение автора,
// п.65); версия 2 (5 байт/канал) - старые приборы AVIS, принимается как есть.
//===========================================================================================================================
#define BLE_PROTO_VER			(1u)	// версия протокола v1 (нижний nibble Header)

typedef enum {
	BLE_PKT_GAS_LEGACY = 0,		// пакет Avis v2 (str_ble_data_set_t, байт 0x02)
	BLE_PKT_BEACONS    = 1,		// список видимых маяков (модуль -> MCU), Header 0x11
	BLE_PKT_PPE        = 2,		// список видимых СИЗ (модуль -> MCU), Header 0x21
	BLE_PKT_SUBDEV     = 3,		// расширенный пакет под-устройства (с серийником), Header 0x31
	BLE_PKT_BEACON_ADV = 4,		// реклама ОДНОГО маяка (маяк -> модуль), Header 0x41
	BLE_PKT_PPE_ADV    = 5,		// реклама ОДНОЙ метки СИЗ (метка -> модуль), Header 0x51
	BLE_PKT_AGG        = 15		// агрегат: BlockMask + блоки (всё одним пакетом), Header 0xF1
} BLE_PKT_TYPE;

#define BLE_PKT_HEADER(type)	((uint8_t)(((type) << 4) | BLE_PROTO_VER))

#define COUNT_CHAN_BLE			(7u)	// каналов в газовом пакете (как COUNT_CHAN_BLE в Avis)

// Один канал газового пакета - 6 БАЙТ (решение автора, PLAN п.65): как
// sensor_data_t Avis (код газа + концентрация LE + состояние), НО установки
// канала - 2 байта LE: в них живут биты DEVICE_SETTING_CHAN (8..12: EC/
// OXYGEN/FID/UART/TK) и маски MEASURE_*/дискретность - в u8 биты 8..12 не
// помещались. Legacy-пакеты ver 2 (5 байт/канал, Setting u8) трекер
// по-прежнему принимает (Ble_ParsePacket): Setting = младший байт.
typedef struct
{
	uint8_t Type_gas;			// Код типа газа/канала (num_formula_gas отправителя)
	uint8_t Concentration[2];	// Концентрация LE (единицы отправителя: он уже
								//  применил Discreteness*conversion_factor_rf)
	uint8_t State_channel;		// Статус канала отправителя (StateErr & 0x7F)
	uint16_t Setting_channel;	// Установки канала LE: DEVICE_SETTING_CHAN + MEASURE_* (shared_types.h)
} ble_sensor_data_t;			// 6 байт

// Пакет под-устройства AVIS (наследник str_ble_data_set_t Avis): шапка 6 байт
// (версия, id, версии железа/ПО, заряд, маска каналов) + 6 байт на канал
// (Setting_channel u16 - п.65). Максимум 6 + 6*7 = 48 байт; отправитель
// передаёт 6 + 6*popcount(sensors_enabled). Первый байт = Header v1-стиля
// (тип 0 GAS_LEGACY): 0x03 = текущий формат; 0x02 = legacy 5 байт/канал
// (старые приборы AVIS - разбираются отдельной веткой Ble_ParsePacket).
typedef struct __attribute__((packed)) str_ble_data_set_t
{
	uint8_t data_struct_ver;	// = 3 (BLE_DATA_STRUCT_VER); 2 = legacy (5 байт/канал)
	uint8_t dev_id;				// идентификатор модели отправителя (DIVICE_ID_BLE Avis)
	uint8_t dev_hw_ver;			// версия железа отправителя
	uint8_t dev_fw_ver;			// версия ПО отправителя
	uint8_t charge;				// заряд батареи отправителя, %
	uint8_t sensors_enabled:5;	// битовая маска включённых каналов (бит0 = канал0)
	uint8_t rfu:3;				// резерв
	ble_sensor_data_t sensors[COUNT_CHAN_BLE];
} str_ble_data_set_t;

#define BLE_DATA_STRUCT_VER			(3u)	// текущая версия пакета AVIS (Setting u16, п.65)
#define SIZE_STR_BLE_DATA_SET_HDR	(6u)	// шапка пакета AVIS (без каналов)
#define SIZE_BLE_SENSOR_REC_V3		(6u)	// канал ver 3 (Setting_channel u16)
#define SIZE_BLE_SENSOR_REC_V2		(5u)	// канал ver 2 legacy (Setting_channel u8)

// Пакет BEACONS (модуль -> MCU): список маяков, отфильтрованных по нашему
// UUID. Запись ble_beacon_t (shared_types.h) = 4 байта и БАЙТ-В-БАЙТ совпадает
// с ble_beacon_t LoRa v4 п.4.7 (ShortID u16 LE + Rssi i8 + Battery u8).
typedef struct __attribute__((packed))
{
	uint8_t Header;					// 0x11 = BLE_PKT_HEADER(BLE_PKT_BEACONS)
	uint8_t Count;					// число маяков в пакете (<= BLE_BEACONS_MAX)
	ble_beacon_t Beacons[BLE_BEACONS_MAX];
} ble_pkt_beacons_t;

// Реклама ОДНОГО маяка (маяк -> модуль, в manufacturer data): наш общий UUID
// (фильтр модуля) + этот пакет. ShortID зашит при монтаже, RSSI добавляет
// модуль-приёмник (в список BEACONS).
typedef struct __attribute__((packed))
{
	uint8_t  Header;				// 0x41 = BLE_PKT_HEADER(BLE_PKT_BEACON_ADV)
	uint16_t ShortID;				// короткий ID (minor/instance), LE
	uint8_t  Battery;				// 0 разряжен / 1 предупреждение / 2 норма
} ble_pkt_beacon_adv_t;				// 4 байта

// Пакет PPE (модуль -> MCU): список видимых меток СИЗ. Запись 6 байт.
typedef struct __attribute__((packed))
{
	uint8_t  PpeType;				// тип СИЗ (приложение LoRa v4: 0 каска, 1 привязь...)
	uint16_t Serial;				// заводской идентификатор единицы, LE
	uint8_t  Charge;				// 0 разряжен / 1 предупреждение / 2 норма
	int8_t   Rssi;					// уровень сигнала, дБм
	uint8_t  Flags;					// бит0 = СИЗ надето/присутствует (Present)
} ble_ppe_wire_t;

typedef struct __attribute__((packed))
{
	uint8_t Header;					// 0x21 = BLE_PKT_HEADER(BLE_PKT_PPE)
	uint8_t Count;					// число меток (<= BLE_PPE_MAX)
	ble_ppe_wire_t Items[BLE_PPE_MAX];
} ble_pkt_ppe_t;

// Реклама ОДНОЙ метки СИЗ (метка -> модуль, в manufacturer data).
typedef struct __attribute__((packed))
{
	uint8_t  Header;				// 0x51 = BLE_PKT_HEADER(BLE_PKT_PPE_ADV)
	uint8_t  PpeType;				// тип СИЗ
	uint16_t Serial;				// заводской идентификатор, LE
	uint8_t  State;					// биты 0..1 = заряд (0/1/2), бит7 = надето
} ble_pkt_ppe_adv_t;				// 5 байт

// Пакет SUBDEV (расширенный, под-устройство -> MCU): то же, что пакет Avis,
// ПЛЮС заводской серийник (legacy его не несёт). Каналы - 6 байт (Setting
// u16, п.65); трекер принимает и legacy Avis ver 2/3, и SUBDEV.
typedef struct __attribute__((packed))
{
	uint8_t  Header;				// 0x31 = BLE_PKT_HEADER(BLE_PKT_SUBDEV)
	uint16_t SerialLo;				// заводской номер (младшая часть), LE
	uint16_t SerialHi;				// заводской номер (старшая часть), LE
	uint8_t  dev_hw_ver;
	uint8_t  dev_fw_ver;
	uint8_t  charge;
	uint8_t  sensors_enabled:5;
	uint8_t  rfu:3;
	ble_sensor_data_t sensors[COUNT_CHAN_BLE];
} ble_pkt_subdev_t;					// 51 байт (9 + 6*7)

// Пакет AGG (модуль -> MCU): всё одним пакетом (предпочтительный вариант
// автора). BlockMask: бит0 = GAS (str_ble_data_set_t ЦЕЛИКОМ, до 48 байт),
// бит1 = BEACONS (Count + 4×Count, БЕЗ байта Header), бит2 = PPE
// (Count + 6×Count, БЕЗ байта Header). Блоки по возрастанию битов.
#define BLE_AGG_BLOCK_GAS		(1u << 0)
#define BLE_AGG_BLOCK_BEACONS	(1u << 1)
#define BLE_AGG_BLOCK_PPE		(1u << 2)
typedef struct __attribute__((packed))
{
	uint8_t Header;					// 0xF1 = BLE_PKT_HEADER(BLE_PKT_AGG)
	uint8_t BlockMask;				// BLE_AGG_BLOCK_*
	uint8_t Blocks[BLE_DATA_SET_MAX_SIZE - 2];
} ble_pkt_agg_t;

// Строки команд модуля MK01
extern const char *str_BLE_CMD[];
// MAC-адрес своего модуля (ответ на BLEADDR?)
extern uint8_t MACAddrBle[6];
// Информация о модуле (ответ на MODINFO?)
extern uint8_t ModInfo[40];
// Данные, принятые последним успешным опросом ADVMSDDATAGET (сырьё пакета AVIS)
extern str_ble_data_set_t ble_rx_data_set;
// Флаг режима прошивки модуля (DFU)
extern uint8_t BleFlagDFU;

// Состояние BLE-подсистемы (диагностика/отладка, поле "ble" в tasks_status)
typedef struct {
	uint32_t cnt_tx;			// отправлено AT-команд (Ble_TransmitReceive)
	uint32_t cnt_ok;			// получено OK
	uint32_t cnt_error;			// получено ERROR от модуля
	uint32_t cnt_noanswer;		// нет ответа за BLE_WAIT_UART_TIMEOUT
	uint32_t cnt_unknown;		// прочий мусор вместо OK/ERROR
	uint32_t cnt_datasets;		// успешно принятых пакетов AVIS (legacy/SUBDEV)
	uint32_t cnt_beacons;		// принятых пакетов BEACONS/AGG с маяками
	uint32_t cnt_ppe;			// принятых пакетов PPE/AGG с СИЗ
	uint8_t  init_flag;			// модуль инициализирован (Ble_Init вернул 1)
	uint8_t  flag_err;			// ошибка обмена (Avis: Ble_flag_err)
	uint8_t  counter_err;		// серии ошибок подряд (>= BLE_ERROR_DATA_REPEAT -> общая ошибка)
	uint8_t  cmd_last;			// последняя команда (BLE_CMD_t)
	uint8_t  last_pkt_type;		// тип последнего разобранного пакета (BLE_PKT_TYPE)
	uint8_t  last_rx_len;		// длина последнего сырого ответа
	uint8_t  last_rx[32];		// последний сырой ответ (диагностика, ASCII)
} ble_status_t;
extern ble_status_t ble_status;

// --- API (повторяет Ble_mk01.h; DatSet заменён на DatGet) ------------------
extern uint8_t Ble_Init(uint32_t serial);
extern void Ble_DeInit(uint8_t init);
extern void Ble_MsdDataGet(void);		// периодический опрос принятых данных
extern void Ble_MsdDataSleep(void);
extern void Ble_AdvStart(uint8_t data);
extern void Ble_AdvPeriodms(uint16_t data);
extern void Ble_AdvPower(uint8_t data);
extern void Ble_SystemOff(void);
extern void Ble_SystemDFU(void);
extern void SetBleFlagDFU(uint8_t data);
extern uint8_t get_state_err_ble(void);
extern uint8_t get_state_init_flag_ble(void);
extern void BLE_InitName(uint32_t serial);

// Публичная заглушка для Receiver Task (ring TYPE_BLE читает только
// Ble_TransmitReceive - см. комментарий файла).
extern void Ble_Receive(void);
// Периодика из Measure Task (переопределяет weak из Vector_main.c).
extern void Ble_Run(void);

#endif /* CONFIG_BLE */

#ifdef __cplusplus
}
#endif

#endif /* __BLE_VECTOR_H */
