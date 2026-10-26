/**
 * @file Ble_vector.c
 * @brief BLE-модуль MK01: порт Ble_mk01.c из Avis (сторона управления модулем),
 *         по замечанию автора: "полностью копируешь весь функционал, кроме
 *        BLE_CMD_ADVMSDDATASET, которую заменяем обычным запросом
 *        BLE_CMD_ADVMSDDATAGET, и на которую нам отвечают структурой. И так
 *        периодически опрашиваем".
 *
 * Что скопировано из Avis один в один (логика/тайминги/повторы):
 *  - Ble_Init(serial): AT -> AT -> имя (ADVBLENAMESET) -> BLEADDR? -> период
 *    рекламы 300 мс -> мощность 8 -> старт рекламы;
 *  - Ble_TransmitReceive(): 4 попытки, ожидание ответа <= 35 мс + 5 мс,
 *    контроль OK / ERROR / данных, серия из 3 ошибок -> Ble_flag_err;
 *  - Ble_Receive(command): разбор ответа модуля (MODINFO? / BLEADDR? /
 *    ADVMSDDATAGET / OK / ERROR);
 *  - Ble_MsdDataSleep / Ble_AdvStart / Ble_AdvPeriodms / Ble_AdvPower /
 *    Ble_SystemOff (импульс RESET_BLE + переинит UART) / Ble_SystemDFU /
 *    SetBleFlagDFU / get_state_err_ble / get_state_init_flag_ble;
 *  - Ble_Run(): скелет из Avis_main.c — INIT-таймер (Ble_DeInit + 2 с ->
 *    Ble_Init), SET-таймер (60 с) -> вместо Ble_MsdDataSet periodic
 *    Ble_MsdDataGet() (ОПРОС принятых данных), SLEEP-таймер (30 с) ->
 *    Ble_MsdDataSleep(); ST_COMMON_BIT_TURN_ON_BLE управляет, ошибка ->
 *    ST_COMMON_BIT_ERR_BLE.
 *
 * Отличия от Avis (адаптация под VECTOR):
 *  - ADVMSDDATASET (пуш своих данных в рекламу) заменён на ADVMSDDATAGET:
 *    запрос -> ответ hex-строкой -> str_ble_data_set_t -> Ble_ApplyConnectedDevice()
 *    -> отметка в ble_stg; публикация в Connected_device (SNS_Dev_Type, включая
 *    per-канальные поля и OR ошибок) - Ble_Control_States_Update (п.66);
 *  - транспорт: ring InputBuffer[TYPE_BLE] (USART3 ISR, Vector_main.c) вместо
 *    прямого UART; время/паузы — GetTick()/Delay() (vector_macros);
 *    пин RESET_BLE Avis -> BLE_RESET (PC12); BLE_UART_Init -> Uart_SetBaudrate;
 *  - публичный Ble_Receive() (вызов из BLE Task, п.65) — ПУСТОЙ: ring
 *    читает только Ble_TransmitReceive (в Avis Receiver Task BLE не обслуживает);
 *  - имя модуля: BLE_Label[сборка] + SerialHi/SerialLo %04d%04d; метка VECTOR
 *    предварительно "VEC" (TODO автор);
 *  - диагностический счётчик ble_status (поле "ble" в tasks_status).
 *
 * ПРОТОКОЛ ДАННЫХ v1 (docs/Mirax-Ble-protocol-v1.md): первый байт пакета
 * — Header (биты 7..4 тип BLE_PKT_*, 3..0 версия). Legacy-пакет Avis (0x02) —
 * байт-в-байт str_ble_data_set_t из Ble_mk01.h (6+5N); пакеты v1: BEACONS
 * (0x11), PPE (0x21), SUBDEV (0x31, с серийником), AGG (0xF1, BlockMask +
 * блоки) — раскладываются в промежуточное хранилище ble_stg, оттуда выходным
 * мостом Ble_Control_States_Update (п.66) — в Connected_device и
 * Ble_telemetry (shared_types.h) для LoRa v4. ДОПУЩЕНИЯ по проволоке UART (уточнить у автора/модуля):
 *  - запрос отправляется строкой "AT+ADVMSDDATAGET" БЕЗ '=' и без payload;
 *  - ответ модуля: hex-ASCII принятых данных, начиная с receive[2] (как у
 *    BLEADDR? — первые 2 байта считаем эхом/терминаторами), длина = len-2.
 *
 * МОСТЫ С ОСНОВНЫМИ СТРУКТУРАМИ (п.66, решение автора — симметрично LoRa
 * п.63/65): ВХОДНОЙ Ble_UpdateInputData() — ЕДИНСТВЕННОЕ место, где модуль
 * читает основные структуры (серийник Sns_Cfg_struct, бит TURN_ON_BLE, тип
 * сборки) в свой ble_in; ВЫХОДНОЙ Ble_Control_States_Update() — ЕДИНСТВЕННОЕ
 * место, где принятые данные обновляются в основные массивы: Ble_Apply*
 * раскладывают пакеты в промежуточный ble_stg (только BLE-поток, без
 * блокировок), публикация в Connected_device/Ble_telemetry — целыми блоками
 * под TX_DISABLE, бит ERR_BLE в Config_common.StateErr — под Sys_Cfg_Lock
 * (StateErr пишут и другие потоки). Оба моста зовутся из Ble_Run() каждый
 * проход. Прочий код модуля внешние структуры НЕ читает и НЕ пишет.
 *
 * Контекст: всё — поток "BLE Task" (п.65: Ble_Run + Ble_Receive, ритм 5 мс;
 * AT-цикл потенциально блокирующий до ~0.5 с на команду); Ble_Receive
 * (публичный) — пустая заглушка, ring TYPE_BLE читает только
 * Ble_TransmitReceive. Возврат: см. функции. Управление: CONFIG_BLE.
 *
 * Версии: первая версия; полный порт мастерской стороны Ble_mk01,
 * DATAGET-опрос вместо DATASET (замечание автора п.2); протокол данных
 * v1 (маяки/СИЗ/под-устройства/агрегат), str_ble_data_set_t приведена к
 * реальному проводному формату Avis (6+5N), хранилище Ble_telemetry (задача
 * автора п.2: "по ble считать данные, которые потом пойдут на lora —
 * BLEBeacons, PPE и под-устройства"); пакет канала 6 байт Setting u16
 * (п.65); мосты Ble_UpdateInputData/Ble_Control_States_Update, приём в
 * промежуточный ble_stg, ERR_BLE под Sys_Cfg_Lock (п.66).
 */
#include "config_device.h"   /* CONFIG_BLE до #if - см. Ble_vector.h  */
#include "Ble_vector.h"

#if CONFIG_BLE

#include "Vector_main.h"     /* buffer.h: InputBuffer/check_buffer/receive_buffer */
#include "vector_macros.h"   /* GetTick/Delay */
#include "vector_log.h"      /* LOG_W/LOG_I + VLOG_M_COM */
#include "tx_api.h"          /* TX_DISABLE: целые снимки Connected_device/Ble_telemetry (п.65) */
#include <stdio.h>
#include <string.h>

/* --- Прототипы внутренних (static) функций --------------------------------
 * Ble_ApplyConnectedDevice: пакет GAS -> отметка в ble_stg (публикует
 *                           Ble_Control_States_Update, п.66).
 * Ble_TransmitReceive:      отправить команду и дождаться ответа (4 попытки).
 * Ble_ReadAnswer(command):  разобрать один ответ модуля из ring TYPE_BLE
 *                           (static; публичный Ble_Receive(void) - пустая
 *                           заглушка vector_tasks.h, зовётся из BLE Task).
 * --------------------------------------------------------------------------*/
static void Ble_ApplyConnectedDevice(const str_ble_data_set_t *src);
static void Ble_ApplyBeacons(const uint8_t *p, uint16_t len);
static void Ble_ApplyPPE(const uint8_t *p, uint16_t len);
static void Ble_ApplySubdev(const uint8_t *raw, uint16_t len);
static void Ble_ApplyAgg(const uint8_t *raw, uint16_t len);
static void Ble_ParsePacket(const uint8_t *raw, uint16_t len);
static uint8_t Ble_TransmitReceive(BLE_CMD_t command, uint16_t data);
static uint8_t Ble_ReadAnswer(BLE_CMD_t command);

const char *str_BLE_CMD[] = {
		"AT",					// Проверка связи
		"AT+MODINFO?",			// Запрос информации о модуле
		"AT+ADVBLENAMESET=",	// Задать имя модуля в сети
		"AT+ADVMSDDATAGET",		// Запрос принятых данных (вместо ...DATASET=)
		"AT+ADVSTART=",			// Включить/выключить рекламу
		"AT+ADVPERIODMS=",		// Период рекламы
		"AT+ADVPOWER=",			// Мощность рекламы
		"AT+SLEEP",				// Сон модуля
		"AT+SYSTEMOFF",			// Выключение модуля
		"AT+DFU",				// Режим прошивки модуля
		"AT+BLEADDR?"			// Запрос MAC-адреса модуля
};

uint8_t MACAddrBle[6] = {0};
uint8_t ModInfo[40] = {0};
str_ble_data_set_t ble_rx_data_set = {0};
uint8_t BleFlagDFU = 0;
ble_status_t ble_status = {0};

// Внутреннее состояние обмена (аналог static-переменных Ble_mk01.c)
static uint8_t Ble_init_flag = 0;		// модуль инициализирован
static uint8_t Ble_flag_err = 0;		// ошибка обмена (серия >= 3)
static uint8_t Ble_counter_err = 0;		// серии ошибок подряд
static char Ble_string[80];				// строка команды/имени
static uint8_t ble_rx_raw[BLE_DATA_SET_MAX_SIZE];	// сырые байты пакета DATAGET (<= 256)
static uint16_t ble_rx_raw_len = 0;

// Буфер одного ответа модуля (аналог buffer_uart_ble в Ble_mk01.c).
// 520 байт — ответ DATAGET = эхо(2) + hex(2*256=512) + "\r\nOK\r"(5);
// пакет протокола v1 ограничен 256 байтами (BLE_DATA_SET_MAX_SIZE, автор).
typedef struct {
	uint8_t receive[520];
	uint8_t check;
	uint16_t len;
} buffer_uart_ble_t;
static buffer_uart_ble_t buffer_uart_ble = {{0}, 0, 0};

//===========================================================================================================================
// ВХОДНЫЕ ДАННЫЕ МОДУЛЯ (п.66, симметрично lora_in п.63): ЕДИНСТВЕННЫЙ мост -
// Ble_UpdateInputData() копирует всё нужное из Sns_Cfg_struct (серийник, бит
// State) и current_build_type в ble_in; остальной код модуля внешние
// структуры НЕ читает. Зовётся из Ble_Run() (BLE-поток) каждый проход ПЕРЕД
// остальными вызовами модуля. Одиночные чтения выровненных u16/u32 на
// Cortex-M33 атомарны - блокировка не нужна (правило п.65, Vector_main.h).
typedef struct {
	uint32_t serial;		/* (SerialHi<<16)|SerialLo - имя модуля (ADVBLENAMESET)   */
	uint8_t  label;			/* метка сборки: 0 = MIRAX ("VEC"), 1 = BACS ("22.")      */
	uint8_t  turn_on;		/* снимок ST_COMMON_BIT_TURN_ON_BLE (Config_common.State) */
} ble_input_t;

static ble_input_t ble_in = {0};

void Ble_UpdateInputData(void)
{
	ble_in.serial  = (uint32_t)(((uint32_t)Sns_Cfg_struct.Config_common.SerialHi << 16) |
								(uint32_t)Sns_Cfg_struct.Config_common.SerialLo);
	ble_in.label   = (uint8_t)((current_build_type == BACS) ? 1u : 0u);
	ble_in.turn_on = (uint8_t)(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_BLE) ? 1u : 0u);
}

//===========================================================================================================================
// ПРОМЕЖУТОЧНОЕ ХРАНИЛИЩЕ принятых данных (п.66): Ble_Apply* разбирают
// пакеты ЗДЕСЬ (только BLE-поток, без блокировок - писатель один), а
// публикует их в основные массивы (Connected_device/Ble_telemetry) ЕДИНАЯ
// функция Ble_Control_States_Update() - целыми блоками под TX_DISABLE,
// симметрично выходному мосту LoRa (Lora_Control_States_Update). Цена
// хранилища ~250 байт .bss; сырьё GAS-пакета переиспользует уже существовавшую
// глобальную ble_rx_data_set (Expressions/отладка).
typedef struct {
	/* GAS: пакет legacy ver 2/3 уже приведён Ble_ParsePacket к ver 3 и
	   лежит в глобальной ble_rx_data_set - здесь только флаг и метка.   */
	uint8_t  gas_dirty;						/* 1 = ble_rx_data_set не опубликован     */
	uint32_t gas_ms;						/* GetTick() на момент приёма (свежесть)  */
	/* SUBDEV: копия сырого пакета (серийник в шапке); фактическая длина
	   пакета - для контроля границ каналов при публикации.              */
	ble_pkt_subdev_t subdev;
	uint16_t subdev_len;
	uint8_t  subdev_dirty;
	uint32_t subdev_ms;
	uint16_t gas_serial;					/* серийник AVIS -> Ble_telemetry         */
	/* BEACONS */
	ble_beacon_t beacons[BLE_BEACONS_MAX];
	uint8_t  beacon_count;
	uint8_t  beacons_dirty;
	uint32_t beacons_ms;
	/* PPE (СИЗ) */
	ble_ppe_item_t ppe[BLE_PPE_MAX];
	uint8_t  ppe_count;
	uint8_t  ppe_dirty;
	uint32_t ppe_ms;
} ble_stage_t;

static ble_stage_t ble_stg = {0};

/**
 * @brief  Пакет GAS (legacy Avis ver 2/3, уже приведён к ver 3) -> отметка
 *         в промежуточном хранилище (публикация - Ble_Control_States_Update).
 * @param  src  структура пакета AVIS (str_ble_data_set_t); ВСЕГДА
 *         &ble_rx_data_set - Ble_ParsePacket копирует/конвертирует пакет в
 *         неё до вызова (сырая копия остаётся для Expressions/отладки).
 * @retval Нет. Контекст: BLE-поток (Ble_ReadAnswer -> Ble_ParsePacket).
 * Соответствие полей wire -> SNS_Dev_Type см. в Ble_Control_States_Update
 * (п.66: разбор пишет только ble_stg, основные массивы - за выходным мостом).
 */
/* Legacy Avis ver 2: канал 5 байт (Setting u8). Принимается наряду с ver 3
   (6 байт, Setting_channel u16) - старые приборы AVIS перепрошивать не
   нужно: ParsePacket конвертирует ver 2 в текущую структуру.             */
typedef struct
{
	uint8_t Type_gas;
	uint8_t Concentration[2];
	uint8_t State_channel;
	uint8_t Setting_channel;
} ble_sensor_data_v2_t;

typedef struct __attribute__((packed))
{
	uint8_t data_struct_ver;	// = 2
	uint8_t dev_id;
	uint8_t dev_hw_ver;
	uint8_t dev_fw_ver;
	uint8_t charge;
	uint8_t sensors_enabled:5;
	uint8_t rfu:3;
	ble_sensor_data_v2_t sensors[COUNT_CHAN_BLE];
} str_ble_data_set_v2_t;			// 41 байт (6 + 5*7)

static str_ble_data_set_v2_t ble_rx_data_set_v2 = {0};

static void Ble_ApplyConnectedDevice(const str_ble_data_set_t *src)
{
	(void)src;						/* пакет уже в ble_rx_data_set (копия ParsePacket) */
	ble_stg.gas_dirty = 1u;
	ble_stg.gas_ms    = GetTick();	/* свежесть - с момента приёма, не публикации     */
	ble_status.cnt_datasets++;
}

/**
 * @brief  Список маяков (пакет BEACONS / блок AGG) -> ble_stg.beacons.
 * @param  p  указатель на Count; len - доступная длина от p до конца пакета.
 * @retval Нет. Контекст: BLE-поток (Ble_ParsePacket).
 * Запись ble_beacon_t = 4 байта (ShortID u16 LE + Rssi i8 + Battery u8) -
 * совпадает с проводной раскладкой LoRa v4 п.4.7 (копия memcpy). Усечённый
 * пакет разбирается по целым записям (толерантность как у Vector_HexStringToBytes).
 * Публикация в Ble_telemetry - Ble_Control_States_Update (п.66).
 */
static void Ble_ApplyBeacons(const uint8_t *p, uint16_t len)
{
	uint8_t count;

	if (len < 1u) {
		return;
	}
	count = p[0];
	if (count > BLE_BEACONS_MAX) {
		count = BLE_BEACONS_MAX;
	}
	if ((uint16_t)(1u + (uint16_t)(sizeof(ble_beacon_t) * count)) > len) {
		count = (uint8_t)((len - 1u) / sizeof(ble_beacon_t));
	}
	for (uint8_t i = 0; i < count; i++) {
		memcpy(&ble_stg.beacons[i], &p[1u + (uint16_t)(sizeof(ble_beacon_t) * i)],
			   sizeof(ble_beacon_t));
	}
	ble_stg.beacon_count  = count;
	ble_stg.beacons_dirty = 1u;
	ble_stg.beacons_ms    = GetTick();
	ble_status.cnt_beacons++;
}

/**
 * @brief  Список меток СИЗ (пакет PPE / блок AGG) -> ble_stg.ppe.
 * @param  p  указатель на Count; len - доступная длина от p до конца пакета.
 * @retval Нет. Контекст: BLE-поток (Ble_ParsePacket).
 * Запись ble_ppe_wire_t = 6 байт: PpeType u8 + Serial u16 LE + Charge u8 +
 * Rssi i8 + Flags u8 (бит0 = надето/присутствует -> Present).
 * Публикация в Ble_telemetry - Ble_Control_States_Update (п.66).
 */
static void Ble_ApplyPPE(const uint8_t *p, uint16_t len)
{
	uint8_t count;

	if (len < 1u) {
		return;
	}
	count = p[0];
	if (count > BLE_PPE_MAX) {
		count = BLE_PPE_MAX;
	}
	if ((uint16_t)(1u + (uint16_t)(sizeof(ble_ppe_wire_t) * count)) > len) {
		count = (uint8_t)((len - 1u) / sizeof(ble_ppe_wire_t));
	}
	for (uint8_t i = 0; i < count; i++) {
		const ble_ppe_wire_t *w = (const ble_ppe_wire_t *)&p[1u + (uint16_t)(sizeof(ble_ppe_wire_t) * i)];

		ble_stg.ppe[i].PpeType = w->PpeType;
		ble_stg.ppe[i].Serial  = w->Serial;
		ble_stg.ppe[i].Charge  = w->Charge;
		ble_stg.ppe[i].Rssi    = w->Rssi;
		ble_stg.ppe[i].Present = (uint8_t)(w->Flags & 0x01u);
	}
	ble_stg.ppe_count  = count;
	ble_stg.ppe_dirty  = 1u;
	ble_stg.ppe_ms     = GetTick();
	ble_status.cnt_ppe++;
}

/**
 * @brief  Расширенный пакет под-устройства SUBDEV (с серийником) -> ble_stg.
 * @param  raw  начало пакета (Header 0x31); len - длина.
 * @retval Нет. Контекст: BLE-поток (Ble_ParsePacket).
 * Шапка 9 байт: Header + SerialLo u16 + SerialHi u16 + hw + fw + charge +
 * sensors_enabled(:5)/rfu(:3); далее 6 байт на включённый канал (Setting
 * u16, п.65). Пакет копируется в ble_stg.subdev ЦЕЛИКОМ (с фактической
 * длиной); конвертация в Connected_device и Ble_telemetry.gas_serial -
 * в Ble_Control_States_Update (п.66).
 */
static void Ble_ApplySubdev(const uint8_t *raw, uint16_t len)
{
	const ble_pkt_subdev_t *pkt = (const ble_pkt_subdev_t *)raw;

	if (len < 9u) {
		return;
	}
	memset(&ble_stg.subdev, 0, sizeof(ble_stg.subdev));
	memcpy(&ble_stg.subdev, raw,
		   (len < (uint16_t)sizeof(ble_stg.subdev)) ? len : (uint16_t)sizeof(ble_stg.subdev));
	ble_stg.subdev_len   = len;
	ble_stg.subdev_dirty = 1u;
	ble_stg.subdev_ms    = GetTick();
	ble_stg.gas_serial   = pkt->SerialLo;	// TODO: 32-битный номер (SerialHi), если понадобится
	ble_status.cnt_datasets++;
}

/**
 * @brief  Агрегированный пакет AGG (всё одним пакетом) -> разбор блоков.
 * @param  raw  начало пакета (Header 0xF1, BlockMask); len - длина.
 * @retval Нет. Контекст: BLE-поток (Ble_ParsePacket).
 * BlockMask: бит0 = GAS (str_ble_data_set_t ЦЕЛИКОМ, 41 байт), бит1 =
 * BEACONS (Count + 4×Count, без Header), бит2 = PPE (Count + 6×Count, без
 * Header). Блоки идут по возрастанию битов (docs/Mirax-Ble-protocol-v1.md).
 */
static void Ble_ApplyAgg(const uint8_t *raw, uint16_t len)
{
	uint8_t mask;
	uint16_t pos = 2u;

	if (len < 2u) {
		return;
	}
	mask = raw[1];

	if ((mask & BLE_AGG_BLOCK_GAS) != 0u) {
		if ((uint16_t)(pos + sizeof(str_ble_data_set_t)) <= len) {
			memcpy(&ble_rx_data_set, &raw[pos], sizeof(str_ble_data_set_t));
			Ble_ApplyConnectedDevice(&ble_rx_data_set);
		}
		pos = (uint16_t)(pos + sizeof(str_ble_data_set_t));
	}
	if ((mask & BLE_AGG_BLOCK_BEACONS) != 0u) {
		if (pos < len) {
			uint8_t cnt = raw[pos];
			uint16_t blk = (uint16_t)(1u + (uint16_t)(sizeof(ble_beacon_t) * cnt));

			Ble_ApplyBeacons(&raw[pos],
							 ((uint16_t)(pos + blk) <= len) ? blk : (uint16_t)(len - pos));
			pos = (uint16_t)(pos + blk);
		}
	}
	if ((mask & BLE_AGG_BLOCK_PPE) != 0u) {
		if (pos < len) {
			uint8_t cnt = raw[pos];
			uint16_t blk = (uint16_t)(1u + (uint16_t)(sizeof(ble_ppe_wire_t) * cnt));

			Ble_ApplyPPE(&raw[pos],
						 ((uint16_t)(pos + blk) <= len) ? blk : (uint16_t)(len - pos));
			pos = (uint16_t)(pos + blk);
		}
	}
}

/**
 * @brief  Диспетчер пакетов протокола v1 (ответ ADVMSDDATAGET).
 * @param  raw  сырые байты пакета (после Vector_HexStringToBytes); len - длина (<= 256).
 * @retval Нет. Контекст: BLE-поток (Ble_ReadAnswer, команда DATAGET).
 * Первый байт - Header: биты 7..4 тип (BLE_PKT_*), 3..0 версия. Тип 0 -
 * пакет Avis: первый байт = data_struct_ver, ВЕРСИЯ 3 = 6 байт/канал
 * (Setting_channel u16 - биты DEVICE_SETTING_CHAN 8..12 + MEASURE_*,
 * п.65), ВЕРСИЯ 2 = legacy 5 байт/канал (старые приборы AVIS - принимаются,
 * конвертируются в ver 3). Остальные типы - только версия BLE_PROTO_VER. Пакеты BEACON_ADV/PPE_ADV - эфирные рекламы
 * одиночных устройств, модуль сам собирает из них списки; в ответе DATAGET
 * их не ждём (игнор).
 */
static void Ble_ParsePacket(const uint8_t *raw, uint16_t len)
{
	uint8_t hdr;
	uint8_t ptype;
	uint8_t pver;

	if (len == 0u) {
		return;
	}
	hdr   = raw[0];
	ptype = (uint8_t)(hdr >> 4);
	pver  = (uint8_t)(hdr & 0x0Fu);
	ble_status.last_pkt_type = ptype;

	if (ptype == BLE_PKT_GAS_LEGACY) {
		/* Пакет Avis: первый байт = data_struct_ver. Ver 3 - 6 байт/канал
		   (Setting u16); ver 2 (legacy) - 5 байт/канал. Копируем что пришло
		   (остаток нулится), неполный пакет логируем, но разбираем
		   (толерантность).                                                 */
		if ((pver == BLE_DATA_STRUCT_VER) && (len >= SIZE_STR_BLE_DATA_SET_HDR)) {
			uint16_t copy = (len > (uint16_t)sizeof(str_ble_data_set_t))
								? (uint16_t)sizeof(str_ble_data_set_t) : len;
			uint8_t n_ch = 0;

			memset(&ble_rx_data_set, 0, sizeof(ble_rx_data_set));
			memcpy(&ble_rx_data_set, raw, copy);
			for (uint8_t b = 0; b < 5u; b++) {
				if ((ble_rx_data_set.sensors_enabled & (uint8_t)(1u << b)) != 0u) {
					n_ch++;
				}
			}
			if (len < (uint16_t)(SIZE_STR_BLE_DATA_SET_HDR + SIZE_BLE_SENSOR_REC_V3 * (uint16_t)n_ch)) {
				LOG_W(VLOG_M_COM, "BLE: gas pkt short %u", (unsigned)len);
			}
			Ble_ApplyConnectedDevice(&ble_rx_data_set);
		}
		else if ((pver == 2u) && (len >= SIZE_STR_BLE_DATA_SET_HDR)) {
			/* LEGACY ver 2 (5 байт/канал, Setting u8): конвертируем в ver 3
			   (Setting = младший байт; биты 8..12 у старых приборов absent) -
			   дальше ЕДИНЫЙ путь применения.                               */
			uint16_t copy = (len > (uint16_t)sizeof(str_ble_data_set_v2_t))
								? (uint16_t)sizeof(str_ble_data_set_v2_t) : len;
			uint8_t n_ch = 0;

			memset(&ble_rx_data_set_v2, 0, sizeof(ble_rx_data_set_v2));
			memcpy(&ble_rx_data_set_v2, raw, copy);
			for (uint8_t b = 0; b < 5u; b++) {
				if ((ble_rx_data_set_v2.sensors_enabled & (uint8_t)(1u << b)) != 0u) {
					n_ch++;
				}
			}
			if (len < (uint16_t)(SIZE_STR_BLE_DATA_SET_HDR + SIZE_BLE_SENSOR_REC_V2 * (uint16_t)n_ch)) {
				LOG_W(VLOG_M_COM, "BLE: gas pkt(v2) short %u", (unsigned)len);
			}
			memset(&ble_rx_data_set, 0, sizeof(ble_rx_data_set));
			ble_rx_data_set.data_struct_ver = ble_rx_data_set_v2.data_struct_ver;
			ble_rx_data_set.dev_id          = ble_rx_data_set_v2.dev_id;
			ble_rx_data_set.dev_hw_ver      = ble_rx_data_set_v2.dev_hw_ver;
			ble_rx_data_set.dev_fw_ver      = ble_rx_data_set_v2.dev_fw_ver;
			ble_rx_data_set.charge          = ble_rx_data_set_v2.charge;
			ble_rx_data_set.sensors_enabled = ble_rx_data_set_v2.sensors_enabled;
			for (uint8_t i = 0; i < COUNT_CHAN_BLE; i++) {
				ble_rx_data_set.sensors[i].Type_gas           = ble_rx_data_set_v2.sensors[i].Type_gas;
				ble_rx_data_set.sensors[i].Concentration[0]   = ble_rx_data_set_v2.sensors[i].Concentration[0];
				ble_rx_data_set.sensors[i].Concentration[1]   = ble_rx_data_set_v2.sensors[i].Concentration[1];
				ble_rx_data_set.sensors[i].State_channel      = ble_rx_data_set_v2.sensors[i].State_channel;
				ble_rx_data_set.sensors[i].Setting_channel    = ble_rx_data_set_v2.sensors[i].Setting_channel;
			}
			Ble_ApplyConnectedDevice(&ble_rx_data_set);
		}
		else {
			LOG_W(VLOG_M_COM, "BLE: bad gas pkt len=%u ver=%u", (unsigned)len, (unsigned)pver);
		}
		return;
	}

	if (pver != BLE_PROTO_VER) {
		LOG_W(VLOG_M_COM, "BLE: pkt ver %u unknown", (unsigned)pver);
		return;
	}
	switch (ptype) {
	case BLE_PKT_BEACONS:
		Ble_ApplyBeacons(&raw[1], (uint16_t)(len - 1u));
		break;
	case BLE_PKT_PPE:
		Ble_ApplyPPE(&raw[1], (uint16_t)(len - 1u));
		break;
	case BLE_PKT_SUBDEV:
		Ble_ApplySubdev(raw, len);
		break;
	case BLE_PKT_AGG:
		Ble_ApplyAgg(raw, len);
		break;
	case BLE_PKT_BEACON_ADV:
	case BLE_PKT_PPE_ADV:
		/* Эфирные рекламы одиночных маяков/СИЗ: в DATAGET-ответе не ждём. */
		break;
	default:
		LOG_W(VLOG_M_COM, "BLE: pkt type %u unknown", (unsigned)ptype);
		break;
	}
}

/**
 * @brief  ВЫХОДНОЙ МОСТ (п.66, симметрично Lora_Control_States_Update):
 *         принятые данные из промежуточного хранилища -> основные массивы,
 *         состояния модуля -> Config_common.StateErr.
 * @param  Нет. @retval Нет. Контекст: BLE-поток (конец каждого Ble_Run).
 *
 * ДАННЫЕ: блоки ble_stg публикуются ЦЕЛЫМИ снимками под TX_DISABLE -
 * читатели из других потоков (LoRa-мост, индикация) снимают копии под той же
 * блокировкой и не увидят половинное состояние (правило п.65). Метки *_ms -
 * с момента ПРИЁМА пакета (сняты в Ble_Apply*), не публикации.
 *
 * Соответствие полей (формат Avis байт-в-байт) -> SNS_Dev_Type:
 *   dev_id -> dev_id; charge -> battery_charge_percent;
 *   dev_hw_ver -> HardwareVersion; dev_fw_ver -> FirmwareVersion;
 *   sensors[i].Type_gas -> Sensor[i].num_formula_gas (код газа/формулы);
 *   sensors[i].Concentration[0..1] (LE u16) -> Sensor[i].ConcentrationVal
 *     (float, БЕЗ повторного масштабирования: отправитель уже применил
 *     Discreteness*Get_Conversion_factor_rf - pass-through в LoRa v4);
 *   sensors[i].State_channel -> Sensor[i].StateErr;
 *   sensors[i].Setting_channel -> Sensor[i].Setting;
 *   State/StateErr сводные = OR по включённым каналам (маска sensors_enabled),
 *   StateErr дополнительно фильтруется ERROR_MASK_ST_1.
 * Выключенные каналы помечаются channel = 0xFF, чтобы признак "channel ==
 * индекс" не срабатывал на нулевом слоте. Серийник SUBDEV ->
 * Ble_telemetry.gas_serial (уходит в sub_descr_t.Serial TYPE_STATIC LoRa v4;
 * legacy-пакет его не несёт).
 *
 * СОСТОЯНИЯ: бит ERR_BLE - RMW Config_common.StateErr под Sys_Cfg_Lock
 * (StateErr пишут и другие потоки: measure/индикация - потеря обновления
 * исключена; одиночные TEST-чтения атомарны, правило п.65).
 */
void Ble_Control_States_Update(void)
{
	TX_INTERRUPT_SAVE_AREA;

	/* --- GAS (legacy Avis ver 2/3, конвертирован в ble_rx_data_set) -------- */
	if (ble_stg.gas_dirty != 0u) {
		const str_ble_data_set_t *src = &ble_rx_data_set;
		uint32_t state_or = 0, err_or = 0;

		TX_DISABLE;		/* запись ЦЕЛЫМ снимком (п.65) */
		Connected_device.dev_id                 = src->dev_id;
		Connected_device.battery_charge_percent = src->charge;
		Connected_device.HardwareVersion        = src->dev_hw_ver;
		Connected_device.FirmwareVersion        = src->dev_fw_ver;

		/* У отправителя 7 слотов и 5-битная маска sensors_enabled - больше
		   min(7, COUNT_CHAN) каналов всё равно не придёт.                  */
		for (uint8_t i = 0; i < (uint8_t)COUNT_CHAN; i++) {
			if ((i < COUNT_CHAN_BLE) &&
				((src->sensors_enabled & (uint8_t)(1u << i)) != 0u)) {
				uint16_t conc = (uint16_t)((uint16_t)src->sensors[i].Concentration[0] |
										   ((uint16_t)src->sensors[i].Concentration[1] << 8));

				Connected_device.Sensor[i].channel          = i;
				Connected_device.Sensor[i].num_formula_gas  = src->sensors[i].Type_gas;
				Connected_device.Sensor[i].ConcentrationVal = (float)conc;
				Connected_device.Sensor[i].StateErr         = src->sensors[i].State_channel;
				Connected_device.Sensor[i].Setting          = src->sensors[i].Setting_channel;
				state_or |= src->sensors[i].State_channel;
				err_or   |= ((uint32_t)src->sensors[i].State_channel & ERROR_MASK_ST_1);
			} else {
				Connected_device.Sensor[i].channel = 0xFFu;	// канал отсутствует
			}
		}
		Connected_device.State    = state_or;
		Connected_device.StateErr = err_or;

		/* свежесть данных под-устройства для LoRa v4 (секция SUB_TYPE_GAS и
		   TYPE_STATIC собираются из Connected_device, пока gas_ms не устарел) */
		Ble_telemetry.gas_online = 1u;
		Ble_telemetry.gas_ms     = ble_stg.gas_ms;
		TX_RESTORE;
		ble_stg.gas_dirty = 0u;
	}

	/* --- SUBDEV (расширенный пакет с серийником) --------------------------- */
	if (ble_stg.subdev_dirty != 0u) {
		const ble_pkt_subdev_t *pkt = &ble_stg.subdev;
		uint16_t len = ble_stg.subdev_len;
		uint32_t state_or = 0, err_or = 0;

		TX_DISABLE;		/* целый снимок для читателей из других потоков (п.65) */
		Ble_telemetry.gas_serial      = ble_stg.gas_serial;
		Connected_device.dev_id                 = pkt->SerialLo;
		Connected_device.battery_charge_percent = pkt->charge;
		Connected_device.HardwareVersion        = pkt->dev_hw_ver;
		Connected_device.FirmwareVersion        = pkt->dev_fw_ver;

		for (uint8_t i = 0; i < (uint8_t)COUNT_CHAN; i++) {
			if ((i < COUNT_CHAN_BLE) &&
				((pkt->sensors_enabled & (uint8_t)(1u << i)) != 0u) &&
				(len >= (uint16_t)(9u + SIZE_BLE_SENSOR_REC_V3 * (uint16_t)(i + 1u)))) {
				uint16_t conc = (uint16_t)((uint16_t)pkt->sensors[i].Concentration[0] |
										   ((uint16_t)pkt->sensors[i].Concentration[1] << 8));

				Connected_device.Sensor[i].channel          = i;
				Connected_device.Sensor[i].num_formula_gas  = pkt->sensors[i].Type_gas;
				Connected_device.Sensor[i].ConcentrationVal = (float)conc;
				Connected_device.Sensor[i].StateErr         = pkt->sensors[i].State_channel;
				Connected_device.Sensor[i].Setting          = pkt->sensors[i].Setting_channel;
				state_or |= pkt->sensors[i].State_channel;
				err_or   |= ((uint32_t)pkt->sensors[i].State_channel & ERROR_MASK_ST_1);
			} else {
				Connected_device.Sensor[i].channel = 0xFFu;
			}
		}
		Connected_device.State    = state_or;
		Connected_device.StateErr = err_or;

		Ble_telemetry.gas_online = 1u;
		Ble_telemetry.gas_ms     = ble_stg.subdev_ms;
		TX_RESTORE;
		ble_stg.subdev_dirty = 0u;
	}

	/* --- Маяки -------------------------------------------------------------- */
	if (ble_stg.beacons_dirty != 0u) {
		TX_DISABLE;
		for (uint8_t i = 0; i < ble_stg.beacon_count; i++) {
			memcpy(&Ble_telemetry.beacons[i], &ble_stg.beacons[i], sizeof(ble_beacon_t));
		}
		Ble_telemetry.beacon_count = ble_stg.beacon_count;
		Ble_telemetry.beacon_ms    = ble_stg.beacons_ms;
		TX_RESTORE;
		ble_stg.beacons_dirty = 0u;
	}

	/* --- СИЗ (PPE) ---------------------------------------------------------- */
	if (ble_stg.ppe_dirty != 0u) {
		TX_DISABLE;
		for (uint8_t i = 0; i < ble_stg.ppe_count; i++) {
			Ble_telemetry.ppe[i] = ble_stg.ppe[i];
		}
		Ble_telemetry.ppe_count = ble_stg.ppe_count;
		Ble_telemetry.ppe_ms    = ble_stg.ppe_ms;
		TX_RESTORE;
		ble_stg.ppe_dirty = 0u;
	}

	/* --- СОСТОЯНИЯ: ошибка обмена -> Config_common.StateErr ----------------
	   RMW общего поля StateErr из нескольких потоков - под блокировкой
	   конфига (Sys_Cfg_Lock, п.65/66).                                      */
	Sys_Cfg_Lock();
	if (get_state_err_ble() != 0u) {
		SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_BLE);
	} else {
		CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_BLE);
	}
	Sys_Cfg_Unlock();
}

/**
 * @brief  Отправить AT-команду модулю и разобрать ответ (порт Ble_TransmitReceive).
 * @param  command  команда (BLE_CMD_t); data — параметр числовых команд /
 *                  серийный номер для имени.
 * @retval 1 успех (OK или разобраны данные), 0 неудача (все попытки).
 * Контекст: BLE-поток. Блокирующая: до BLE_TRANSMIT_REPEAT*(35+5) мс.
 * Логика Avis: 4 попытки; после каждой — Ble_Receive(); OK -> счётчик ошибок
 * в 0, успех; ERROR/нет ответа -> counter_err++, после 3 серий -> flag_err
 * (Ble_Run поднимет ST_COMMON_BIT_ERR_BLE).
 */
static uint8_t Ble_TransmitReceive(BLE_CMD_t command, uint16_t data)
{
	uint8_t status = 0;
	uint8_t repeat = 0;
	uint8_t cmd_len = 0;
	uint8_t res = 1;

	ble_status.cnt_tx++;
	ble_status.cmd_last = (uint8_t)command;
	tasks_status.mod[TASK_MOD_BLE].cnt_tx_frames++;
	tasks_status.mod[TASK_MOD_BLE].last_run_ms = GetTick();

	// Сборка строки команды (как в Ble_mk01.c). Имя модуля: BLE_InitName()
	// уже приготовил Ble_string до вызова - указатель через uint16_t data НЕ
	// передаём (в Avis это (uint16_t)&BLE_Name - обрезание 32-битного адреса).
	switch (command) {
	case CMD_ADVBLENAMESET:
		cmd_len = (uint8_t)strlen(Ble_string);
		break;
	case CMD_ADVSTART:
	case CMD_ADVPERIODMS:
	case CMD_ADVPOWER:
		cmd_len = (uint8_t)sprintf(Ble_string, "%s%d", str_BLE_CMD[command], data);
		break;
	case CMD_AT:
	case CMD_MODINFO:
	case CMD_SLEEP:
	case CMD_SYSTEMOFF:
	case CMD_DFU:
	case CMD_BLEADDR:
	case CMD_ADVMSDDATAGET:
	default:
		cmd_len = (uint8_t)sprintf(Ble_string, "%s", str_BLE_CMD[command]);
		break;
	}

	do {
		repeat++;
		// Отправка команды + терминатор 0x0D в USART3
		for (uint8_t i = 0; i < cmd_len; i++) {
			while ((USART_BLE.Instance->ISR & USART_ISR_TXE_TXFNF) == 0u) { }
			USART_BLE.Instance->TDR = (uint8_t)Ble_string[i];
		}
		while ((USART_BLE.Instance->ISR & USART_ISR_TXE_TXFNF) == 0u) { }
		USART_BLE.Instance->TDR = 0x0Du;
		Delay(10);									// пауза после команды (Avis)

		// Ответ модуля (OK / ERROR / данные)
		res = Ble_ReadAnswer(command);
		if (res == 0) {
			Ble_counter_err = 0;
			status = 1;
		} else {
			Ble_counter_err++;
			if (Ble_counter_err >= BLE_ERROR_DATA_REPEAT) {
				Ble_flag_err = 1;
			}
			if (command == CMD_BLEADDR && Ble_counter_err >= BLE_ERROR_DATA_REPEAT) {
				Ble_init_flag = 0;					// без MAC инициализация неполна (Avis)
			}
		}
	} while ((status != 1) && (repeat <= BLE_TRANSMIT_REPEAT));

	ble_status.init_flag  = Ble_init_flag;
	ble_status.flag_err   = Ble_flag_err;
	ble_status.counter_err = Ble_counter_err;
	return status;
}

/**
 * @brief  Дождаться и разобрать ОДИН ответ модуля (порт static Ble_Receive
 *         из Ble_mk01.c; переименован в Ble_ReadAnswer - имя Ble_Receive
 *         занято публичной функцией vector_tasks.h).
 * @param  command  команда, на которую ждём ответ.
 * @retval 0 успех (OK / разобраны данные), 1 неудача (ERROR / нет ответа / мусор).
 * Контекст: BLE-поток. Читаем ring InputBuffer[TYPE_BLE] (USART3 ISR кладёт
 * туда байты модуля); ожидание до BLE_WAIT_UART_TIMEOUT мс по GetTick() +
 * BLE_WAIT_UART_DELAY мс на хвост — тайминги Avis.
 */
static uint8_t Ble_ReadAnswer(BLE_CMD_t command)
{
	buffer_uart_ble.check = 0;
	buffer_uart_ble.len = 0;

	uint32_t time_data_start = GetTick();
	uint16_t len = 0;	/* uint16 - буфер приёма 520 (uint8 обрезал бы границу) */
	while ((check_buffer(&InputBuffer[TYPE_BLE]) == 0u) &&
		   ((GetTick() - time_data_start) <= BLE_WAIT_UART_TIMEOUT)) {
		// ждём первый байт ответа (ISR USART3 кладёт байты в InputBuffer[TYPE_BLE])
	}
	Delay(BLE_WAIT_UART_DELAY);						// дотекание хвоста ответа

	// Сливаем ring в локальный буфер (единственный читатель TYPE_BLE — здесь;
	// API SPSC-кольца buffer.h: check_buffer/receive_buffer, метки begin/end)
	uint8_t byte = 0;
	while ((check_buffer(&InputBuffer[TYPE_BLE]) != 0u) &&
		   (len < (uint16_t)(sizeof(buffer_uart_ble.receive) - 1u))) {
		if (receive_buffer(&InputBuffer[TYPE_BLE], &byte) == 0u) {
			break;
		}
		buffer_uart_ble.receive[len++] = byte;
	}
	buffer_uart_ble.receive[len] = '\0';
	buffer_uart_ble.len = len;
	tasks_status.mod[TASK_MOD_BLE].cnt_rx_bytes += len;
	tasks_status.mod[TASK_MOD_BLE].last_receive_ms = GetTick();

	ble_status.last_rx_len = (uint8_t)((len > sizeof(ble_status.last_rx)) ?
									   sizeof(ble_status.last_rx) : len);
	memcpy(ble_status.last_rx, buffer_uart_ble.receive, ble_status.last_rx_len);

	if (len < 2u) {
		ble_status.cnt_noanswer++;
		LOG_W(VLOG_M_COM, "BLE: no answer cmd=%d", (int)command);
		return 1;
	}

	switch (command) {
	case CMD_MODINFO:								// информация о модуле
		if ((strstr((char*)buffer_uart_ble.receive, "BLE") != NULL) && (len > 18u)) {
			memcpy(ModInfo, buffer_uart_ble.receive, (len > 38u) ? 38u : len);
		}
		break;
	case CMD_BLEADDR:								// MAC-адрес модуля (hex-парами)
		if (len > 13u) {
			int m[6] = {0};
			sscanf((char*)&buffer_uart_ble.receive[2],
				   "%02x%02x%02x%02x%02x%02x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
			for (uint8_t i = 0; i < 6u; i++) {
				MACAddrBle[i] = (uint8_t)m[i];
			}
		}
		break;
	case CMD_ADVMSDDATAGET: {						// пакет протокола v1 (hex)
		/* Общий hex-разбор (vector_macros.h) вместо прежнего Ble_HexToBin на
		   strtol: толерантный, стоп на хвосте ответа "OK"/CR; буфер
		   '\0'-терминирован (buffer_uart_ble.receive[len] = 0), длина не нужна. */
		uint16_t n = Vector_HexStringToBytes((const char *)&buffer_uart_ble.receive[2],
											 ble_rx_raw, sizeof(ble_rx_raw));
		ble_rx_raw_len = n;
		if (n > 0u) {
			Ble_ParsePacket(ble_rx_raw, n);			// legacy Avis / BEACONS / PPE / SUBDEV / AGG
		}
		break;
	}
	default:
		break;
	}

	if (strstr((char*)buffer_uart_ble.receive, "OK") != NULL) {
		ble_status.cnt_ok++;
		tasks_status.mod[TASK_MOD_BLE].cnt_rx_frames++;
		return 0;
	} else if (strstr((char*)buffer_uart_ble.receive, "ERROR") != NULL) {
		ble_status.cnt_error++;
		tasks_status.mod[TASK_MOD_BLE].cnt_errors++;
		LOG_W(VLOG_M_COM, "BLE: ERROR cmd=%d", (int)command);
		return 1;
	} else {
		ble_status.cnt_unknown++;
		tasks_status.mod[TASK_MOD_BLE].cnt_errors++;
		LOG_W(VLOG_M_COM, "BLE: unknown answer cmd=%d len=%u", (int)command, (unsigned)len);
		return 1;
	}
}

/**
 * @brief  Имя модуля: метка сборки + серийный номер, задать ADVBLENAMESET.
 * @param  serial  (SerialHi<<16)|SerialLo.
 * @retval Нет. Контекст: BLE-поток (из Ble_Init).
 * Формат Avis (Device2_1): label + "%04d%04d" (Hi 0..9999, Lo 0..9999).
 * Метка VECTOR предварительно "VEC"/"22." — TODO автор (имя в сети).
 */
void BLE_InitName(uint32_t serial)
{
	const char *BLE_Label[2] = { "VEC", "22." };		// [0]=MIRAX, [1]=BACS
	/* Метка сборки - из входного моста (ble_in.label читает current_build_type,
	   п.66: код модуля внешние структуры напрямую не читает). Метка VECTOR
	   предварительно "VEC" - TODO автор.                                   */
	uint8_t label = ble_in.label;

	/* Формат Avis (format_type=0): метка + "%04d%04d" от serial/10000 и
	   serial%10000 (serial = (SerialHi<<16)|SerialLo целиком).              */
	sprintf(Ble_string, "%s%04d%04d", BLE_Label[label],
			(int)(serial / 10000u), (int)(serial % 10000u));
	(void)Ble_TransmitReceive(CMD_ADVBLENAMESET, 0);
}

/**
 * @brief  Инициализация BLE-модуля (порт Ble_Init из Avis - один в один).
 * @param  serial  (SerialHi<<16)|SerialLo — для имени модуля.
 * @retval 1 последовательность выполнена, 0 уже инициализирован.
 * Контекст: BLE-поток (Ble_Run по INIT-таймеру). Последовательность Avis:
 * flag=1 ЗАРАНЕЕ -> AT -> AT -> имя -> BLEADDR? -> период 300 мс ->
 * мощность 8 -> старт рекламы. Команды идут БЕЗ && (каждая выполняется);
 * если модуль не отвечает на BLEADDR? 3 серии подряд, Ble_TransmitReceive
 * сам снимает init_flag - Ble_Run увидит get_state_init_flag_ble()==0 и
 * повторит инициализацию через INIT-таймер.
 */
uint8_t Ble_Init(uint32_t serial)
{
	if (get_state_init_flag_ble() != 0u) {
		return 0;
	}
	Ble_init_flag = 1;
	ble_status.init_flag = 1;
	(void)Ble_TransmitReceive(CMD_AT, 0);
	(void)Ble_TransmitReceive(CMD_AT, 0);
	BLE_InitName(serial);
	(void)Ble_TransmitReceive(CMD_BLEADDR, 0);
	Ble_AdvPeriodms(300);
	Ble_AdvPower(8);
	Ble_AdvStart(1);
	return 1;
}

/**
 * @brief  Деинициализация модуля (порт Ble_DeInit из Avis - один в один).
 * @param  init  1 — принудительно взвести init_flag, чтобы Ble_SystemOff()
 *               реально выполнил выключение (сценарий Ble_Run: модуль мог
 *               не успеть инициализироваться); 0 — только выключить, если
 *               модуль и так инициализирован.
 * @retval Нет. Контекст: BLE-поток (Ble_Run: выкл. TURN_ON_BLE / INIT-ветка).
 * Avis: Ble_SystemDFU(); if(init) flag=1; Ble_SystemOff() (SYSTEMOFF + импульс
 * RESET_BLE 50 мс + переинит UART + flag=0); counter_err=0; flag_err=0.
 */
void Ble_DeInit(uint8_t init)
{
	Ble_SystemDFU();
	if (init != 0u) {
		Ble_init_flag = 1;
		ble_status.init_flag = 1;
	}
	Ble_SystemOff();
	Ble_counter_err = 0;
	Ble_flag_err = 0;
	ble_status.counter_err = 0;
	ble_status.flag_err = 0;
}

/**
 * @brief Периодический ОПРОС принятых данных (замена Ble_MsdDataSet).
 * @param  Нет. @retval Нет.
 * Контекст: BLE-поток (Ble_Run по SET-таймеру, TIME_RTC_BLE_DATA_SET=60 с).
 * AT+ADVMSDDATAGET -> ответ hex -> ble_stg (публикация в Connected_device -
 * Ble_Control_States_Update в конце этого же прохода Ble_Run, п.66).
 */
void Ble_MsdDataGet(void)
{
	if (Ble_init_flag == 1u && Ble_flag_err == 0u && BleFlagDFU == 0u) {
		(void)Ble_TransmitReceive(CMD_AT, 0);
		(void)Ble_TransmitReceive(CMD_ADVMSDDATAGET, 0);
	}
}

/**
 * @brief  Усыпить BLE-модуль (порт Ble_MsdDataSleep).
 * @param  Нет. @retval Нет. Контекст: BLE-поток (SLEEP-таймер, 30 с).
 */
void Ble_MsdDataSleep(void)
{
	if (Ble_init_flag == 1u && Ble_flag_err == 0u && BleFlagDFU == 0u) {
		(void)Ble_TransmitReceive(CMD_SLEEP, 0);
	}
}

/**
 * @brief  Включить/выключить рекламу (порт Ble_AdvStart).
 * @param  data 1 вкл / 0 выкл. @retval Нет. Контекст: BLE-поток.
 */
void Ble_AdvStart(uint8_t data)
{
	(void)Ble_TransmitReceive(CMD_ADVSTART, data);
}

/**
 * @brief  Задать период рекламы, мс (порт Ble_AdvPeriodms).
 * @param  data период. @retval Нет. Контекст: BLE-поток.
 */
void Ble_AdvPeriodms(uint16_t data)
{
	(void)Ble_TransmitReceive(CMD_ADVPERIODMS, data);
}

/**
 * @brief  Задать мощность рекламы (порт Ble_AdvPower).
 * @param  data уровень. @retval Нет. Контекст: BLE-поток.
 */
void Ble_AdvPower(uint8_t data)
{
	(void)Ble_TransmitReceive(CMD_ADVPOWER, data);
}

/**
 * @brief  Выключить BLE-модуль (порт Ble_SystemOff из Avis - один в один).
 * @param  Нет. @retval Нет. Контекст: BLE-поток.
 * Avis: только если init_flag==1: снять флаг, импульс RESET_BLE 50 мс
 * (SET_OFF -> Delay(50) -> SET_ON) и переинициализировать UART.
 * AT+SYSTEMOFF в Avis не отправляется - модуль гасится пином RESET.
 */
void Ble_SystemOff(void)
{
	if (get_state_init_flag_ble() == 1u) {
		Ble_init_flag = 0;
		ble_status.init_flag = 0;

		SET_OFF(BLE_RESET);
		Delay(50);
		SET_ON(BLE_RESET);
		Uart_SetBaudrate(&USART_BLE, 115200);		// переинит USART3 (+IT)
	}
}

/**
 * @brief  Флаг режима прошивки модуля (порт SetBleFlagDFU).
 * @param  data 1 — DFU. @retval Нет. Контекст: любой поток (флаг).
 */
void SetBleFlagDFU(uint8_t data)
{
	BleFlagDFU = data;
}

/**
 * @brief  Перевести модуль в режим прошивки DFU (порт Ble_SystemDFU).
 * @param  Нет. @retval Нет. Контекст: BLE-поток.
 */
void Ble_SystemDFU(void)
{
	if (Ble_init_flag == 1u && Ble_flag_err == 0u && BleFlagDFU == 0u) {
		(void)Ble_TransmitReceive(CMD_DFU, 0);
	}
}

/**
 * @brief  Состояние ошибки обмена с модулем (порт get_state_err_ble).
 * @retval 0 нет, 1 ошибка (Ble_Run транслирует в ST_COMMON_BIT_ERR_BLE).
 */
uint8_t get_state_err_ble(void)
{
	return Ble_flag_err;
}

/**
 * @brief  Флаг инициализации модуля (порт get_state_init_flag_ble).
 * @retval 0 не инициализирован, 1 инициализирован.
 */
uint8_t get_state_init_flag_ble(void)
{
	return Ble_init_flag;
}

/**
 * @brief Публичный Ble_Receive() — заглушка (зовётся из BLE Task, п.65).
 * @param  Нет. @retval Нет.
 * В Avis Receiver Task BLE не обслуживает: обмен с модулем синхронный внутри
 * Ble_TransmitReceive (BLE Task), он и есть ЕДИНСТВЕННЫЙ читатель ring
 * InputBuffer[TYPE_BLE]. Пустое сильное определение замещает weak-заглушку
 * Vector_main.c (которая дренировала бы ring и съедала ответы модуля).
 */
void Ble_Receive(void)
{
	// Намеренно пусто — см. комментарий функции.
}

/**
 * @brief  Периодика BLE (порт Ble_Run из Avis_main.c) — BLE Task, каждый
 *         проход (VECTOR_TASKS_BLE_DELAY_MS = 5 мс, п.65); переопределяет
 *         weak-заглушку Vector_main.c.
 * @param  Нет. @retval Нет.
 * Скелет Avis: ST_COMMON_BIT_TURN_ON_BLE -> INIT-таймер (DeInit(1)+2 с ->
 * Ble_Init(serial)) -> SET-таймер (60 с): Ble_MsdDataGet() (вместо DataSet)
 * + перезапуск SET/SLEEP -> SLEEP-таймер (30 с): Ble_MsdDataSleep();
 * get_state_err_ble() -> SET/CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_BLE).
 * bit TURN_ON_BLE взводится по AT-команде верхнего ПО (будет в command_message).
 *
 * МОСТЫ (п.66): каждый проход начинается ВХОДНЫМ мостом
 * Ble_UpdateInputData() (основные структуры -> ble_in: серийник, метка
 * сборки, бит TURN_ON_BLE) и заканчивается ВЫХОДНЫМ
 * Ble_Control_States_Update() (ble_stg -> Connected_device/Ble_telemetry под
 * TX_DISABLE + ERR_BLE -> StateErr под Sys_Cfg_Lock); прямых обращений к
 * основным структурам в остальном коде модуля не осталось.
 */
void Ble_Run(void)
{
	if (device_turn != DEVICE_TURNED_OFF) {
		Ble_UpdateInputData();					/* входной мост (п.66) */
		if (ble_in.turn_on != 0u) {
			if ((get_state_init_flag_ble() == 0u) &&
				(!TEST_TIMER_RUN_RTC(TIMER_RTC_BLE_DATA_INIT)) &&
				(!TEST_TIMER_RTC(TIMER_RTC_BLE_DATA_INIT))) {
				Ble_DeInit(1);
				START_TIMER_RTC(TIMER_RTC_BLE_DATA_INIT, TIME_RTC_BLE_DATA_INIT);
			}
			if (TEST_TIMER_RTC(TIMER_RTC_BLE_DATA_INIT)) {
				RESET_TIMER_RTC(TIMER_RTC_BLE_DATA_INIT);
				if (Ble_Init(ble_in.serial)) {
					START_TIMER_RTC(TIMER_RTC_BLE_DATA_SLEEP, TIME_RTC_BLE_DATA_SLEEP);
					START_TIMER_RTC(TIMER_RTC_BLE_DATA_SET, TIME_RTC_BLE_DATA_SET);
				}
			}
			if (TEST_TIMER_RTC(TIMER_RTC_BLE_DATA_SET)) {
				RESET_TIMER_RTC(TIMER_RTC_BLE_DATA_SET);
				Ble_MsdDataGet();					// периодический опрос
				START_TIMER_RTC(TIMER_RTC_BLE_DATA_SLEEP, TIME_RTC_BLE_DATA_SLEEP);
				START_TIMER_RTC(TIMER_RTC_BLE_DATA_SET, TIME_RTC_BLE_DATA_SET);
			}
			if (TEST_TIMER_RTC(TIMER_RTC_BLE_DATA_SLEEP)) {
				RESET_TIMER_RTC(TIMER_RTC_BLE_DATA_SLEEP);
				Ble_MsdDataSleep();
			}
		} else {
			Ble_DeInit(0);
		}
		Ble_Control_States_Update();			/* выходной мост (п.66): данные + ERR_BLE */
	}
}

#endif /* CONFIG_BLE */
