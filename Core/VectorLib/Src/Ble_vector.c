/**
 * @file Ble_vector.c
 * @brief BLE-модуль MK01: порт Ble_mk01.c из Avis (сторона управления модулем),
 *        v27 по замечанию автора: "полностью копируешь весь функционал, кроме
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
 *    -> Connected_device (SNS_Dev_Type, включая per-канальные поля и OR ошибок);
 *  - транспорт: ring InputBuffer[TYPE_BLE] (USART3 ISR, Vector_main.c) вместо
 *    прямого UART; время/паузы — GetTick()/Delay() (vector_macros);
 *    пин RESET_BLE Avis -> BLE_RESET (PC12); BLE_UART_Init -> Uart_SetBaudrate;
 *  - публичный Ble_Receive() (вызов Receiver Task) — ПУСТОЙ: ring читает
 *    только Ble_TransmitReceive (в Avis Receiver Task BLE не обслуживает);
 *  - имя модуля: BLE_Label[сборка] + SerialHi/SerialLo %04d%04d; метка VECTOR
 *    предварительно "VEC" (TODO автор);
 *  - диагностический счётчик ble_status (поле "ble" в tasks_status).
 *
 * ДОПУЩЕНИЯ по проволоке ADVMSDDATAGET (протокол ещё дорабатывается автором):
 *  - запрос отправляется строкой "AT+ADVMSDDATAGET" БЕЗ '=' и без payload;
 *  - ответ модуля: hex-ASCII принятых данных, начиная с receive[2] (как у
 *    BLEADDR? — первые 2 байта считаем эхом/терминаторами), длина = len-2;
 *    минимум sizeof(str_ble_data_set_t)*2 = 40 hex-символов, иначе не разбираем.
 *
 * Контекст: Ble_Init/Ble_Run/Ble_MsdDataGet/Ble_TransmitReceive — Measure Task
 * (потенциально блокирующие до ~0.5 с на команду — вне "окна тишины" звук не
 * запускается, см. PLAN); Ble_Receive (публичный) — Receiver Task (пусто).
 * Возврат: см. функции. Управление: CONFIG_BLE (сейчас 0 — отладка на столе).
 *
 * Версии: v26 — первая версия; v27 — полный порт мастерской стороны Ble_mk01,
 * DATAGET-опрос вместо DATASET (замечание автора п.2).
 */
#include "config_device.h"   /* CONFIG_BLE до #if - см. Ble_vector.h (v27) */
#include "Ble_vector.h"

#if CONFIG_BLE

#include "Vector_main.h"     /* buffer.h: InputBuffer/check_buffer/receive_buffer */
#include "vector_macros.h"   /* GetTick/Delay */
#include "vector_log.h"      /* LOG_W/LOG_I + VLOG_M_COM */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Прототипы внутренних (static) функций --------------------------------
 * Ble_ApplyConnectedDevice: разобранные данные отправителя -> Connected_device.
 * Ble_TransmitReceive:      отправить команду и дождаться ответа (4 попытки).
 * Ble_ReadAnswer(command):  разобрать один ответ модуля из ring TYPE_BLE
 *                           (static; публичный Ble_Receive(void) - пустая
 *                           заглушка Receiver Task, имя занято в vector_tasks.h).
 * --------------------------------------------------------------------------*/
static void Ble_ApplyConnectedDevice(const str_ble_data_set_t *src);
static uint8_t Ble_TransmitReceive(BLE_CMD_t command, uint16_t data);
static uint8_t Ble_ReadAnswer(BLE_CMD_t command);

const char *str_BLE_CMD[] = {
		"AT",					// Проверка связи
		"AT+MODINFO?",			// Запрос информации о модуле
		"AT+ADVBLENAMESET=",	// Задать имя модуля в сети
		"AT+ADVMSDDATAGET",		// Запрос принятых данных (v27, вместо ...DATASET=)
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
static uint8_t ble_rx_raw[BLE_DATA_SET_MAX_SIZE];	// сырые байты ответа DATAGET
static uint16_t ble_rx_raw_len = 0;

// Буфер одного ответа модуля (аналог buffer_uart_ble в Ble_mk01.c).
// 192 байта: ответ DATAGET = эхо(2) + hex(2*64=128) + "\r\nOK\r"(5) = 135.
typedef struct {
	uint8_t receive[192];
	uint8_t check;
	uint16_t len;
} buffer_uart_ble_t;
static buffer_uart_ble_t buffer_uart_ble = {{0}, 0, 0};

/**
 * @brief  Разбор hex-строки ответа ADVMSDDATAGET в сырые байты.
 * @param  hex  hex-ASCII данные; len — длина.
 * @retval Количество разобранных байт.
 * Контекст: Measure Task (из Ble_Receive). Разбор ТОЛЕРАНТНЫЙ: останавливается
 * на первой не-hex паре (хвост ответа "OK"/CR/LF просто отсекается) - так же
 * ведёт себя sscanf("%02x...") у BLEADDR? в Avis.
 */
static uint16_t Ble_HexToBin(const uint8_t *hex, uint16_t len)
{
	uint16_t out = 0;
	for (uint16_t i = 0; i + 1u < len; i += 2u) {
		char c[3] = { (char)hex[i], (char)hex[i + 1u], 0 };
		char *end = NULL;
		long v = strtol(c, &end, 16);
		if (end != &c[2]) {
			break;							// хвост ответа (OK/CR) - данные кончились
		}
		if (out >= sizeof ble_rx_raw) {
			break;
		}
		ble_rx_raw[out++] = (uint8_t)v;
	}
	return out;
}

/**
 * @brief  Разобранные данные отправителя (Avis) -> Connected_device.
 * @param  src  структура из ответа ADVMSDDATAGET.
 * @retval Нет.
 * Контекст: Measure Task (Ble_TransmitReceive -> Ble_ReadAnswer). Читатель
 * Connected_device — индикация (100 мс цикл); struct-копирование на Cortex-M33
 * атомарно пословно, частичное обновление практически исключено.
 *
 * Соответствие полей str_ble_data_set_t -> SNS_Dev_Type (shared_types.h):
 *   dev_id -> dev_id; battery_charge -> battery_charge_percent;
 *   hardware_version -> HardwareVersion; software_version -> FirmwareVersion;
 *   sensors[i].Type_gas -> Sensor[i].num_formula_gas (код газа/формулы);
 *   sensors[i].Concentration -> Sensor[i].ConcentrationVal (float, сырьё -
 *     discreteness протокола отправителя неизвестна, TODO автор);
 *   sensors[i].State -> Sensor[i].StateErr; sensors[i].Setting -> Setting;
 *   State/StateErr сводные = OR по включённым каналам (маска sensors_enabled),
 *     StateErr дополнительно фильтруется ERROR_MASK_ST.
 * protocol_version/sensors_enabled в SNS_Dev_Type места нет - остаются в
 * ble_rx_data_set (сырая копия ответа, Expressions/отладка).
 */
static void Ble_ApplyConnectedDevice(const str_ble_data_set_t *src)
{
	uint32_t state_or = 0, err_or = 0;

	Connected_device.dev_id               = src->dev_id;
	Connected_device.battery_charge_percent = src->battery_charge;
	Connected_device.HardwareVersion      = src->hardware_version;
	Connected_device.FirmwareVersion      = src->software_version;

	/* У отправителя 7 слотов и 5-битная маска sensors_enabled - больше
	   min(7, COUNT_CHAN) каналов всё равно не придёт.                      */
	for (uint8_t i = 0; i < ((COUNT_CHAN < 7) ? (uint8_t)COUNT_CHAN : 7u); i++) {
		if ((src->sensors_enabled & (uint8_t)(1u << i)) == 0u) {
			continue;						// канал у отправителя выключен
		}
		Connected_device.Sensor[i].channel          = i;
		Connected_device.Sensor[i].num_formula_gas  = src->sensors[i].Type_gas;
		Connected_device.Sensor[i].ConcentrationVal = (float)src->sensors[i].Concentration;
		Connected_device.Sensor[i].StateErr         = src->sensors[i].State;
		Connected_device.Sensor[i].Setting          = (uint8_t)src->sensors[i].Setting;
		state_or |= src->sensors[i].State;
		err_or   |= ((uint32_t)src->sensors[i].State & ERROR_MASK_ST_1);
	}
	Connected_device.State    = state_or;
	Connected_device.StateErr = err_or;
	ble_status.cnt_datasets++;
}

/**
 * @brief  Отправить AT-команду модулю и разобрать ответ (порт Ble_TransmitReceive).
 * @param  command  команда (BLE_CMD_t); data — параметр числовых команд /
 *                  серийный номер для имени.
 * @retval 1 успех (OK или разобраны данные), 0 неудача (все попытки).
 * Контекст: Measure Task. Блокирующая: до BLE_TRANSMIT_REPEAT*(35+5) мс.
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
 * Контекст: Measure Task. Читаем ring InputBuffer[TYPE_BLE] (USART3 ISR кладёт
 * туда байты модуля); ожидание до BLE_WAIT_UART_TIMEOUT мс по GetTick() +
 * BLE_WAIT_UART_DELAY мс на хвост — тайминги Avis.
 */
static uint8_t Ble_ReadAnswer(BLE_CMD_t command)
{
	buffer_uart_ble.check = 0;
	buffer_uart_ble.len = 0;

	uint32_t time_data_start = GetTick();
	uint8_t len = 0;
	while ((check_buffer(&InputBuffer[TYPE_BLE]) == 0u) &&
		   ((GetTick() - time_data_start) <= BLE_WAIT_UART_TIMEOUT)) {
		// ждём первый байт ответа (ISR USART3 кладёт байты в InputBuffer[TYPE_BLE])
	}
	Delay(BLE_WAIT_UART_DELAY);						// дотекание хвоста ответа

	// Сливаем ring в локальный буфер (единственный читатель TYPE_BLE — здесь;
	// API SPSC-кольца buffer.h: check_buffer/receive_buffer, метки begin/end)
	uint8_t byte = 0;
	while ((check_buffer(&InputBuffer[TYPE_BLE]) != 0u) &&
		   (len < (uint8_t)(sizeof(buffer_uart_ble.receive) - 1u))) {
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
	case CMD_ADVMSDDATAGET: {						// v27: принятые данные (hex)
		uint16_t n = Ble_HexToBin(&buffer_uart_ble.receive[2],
								  (uint16_t)(len > 2u ? len - 2u : 0u));
		ble_rx_raw_len = n;
		if (n >= sizeof(str_ble_data_set_t)) {
			memcpy(&ble_rx_data_set, ble_rx_raw, sizeof(str_ble_data_set_t));
			Ble_ApplyConnectedDevice(&ble_rx_data_set);
		} else if (n != 0u) {
			LOG_W(VLOG_M_COM, "BLE: DATAGET short %u", (unsigned)n);
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
 * @retval Нет. Контекст: Measure Task (из Ble_Init).
 * Формат Avis (Device2_1): label + "%04d%04d" (Hi 0..9999, Lo 0..9999).
 * Метка VECTOR предварительно "VEC"/"22." — TODO автор (имя в сети).
 */
void BLE_InitName(uint32_t serial)
{
	const char *BLE_Label[2] = { "VEC", "22." };		// [0]=MIRAX, [1]=BACS
	/* current_build_type (Init_Build_Type из внутренней flash) - индекс
	   массива меток (config_device.h: "использовать как индекс массивов [2]").
	   Метка VECTOR предварительно "VEC" - TODO автор.                       */
	uint8_t label = (uint8_t)((current_build_type == BACS) ? 1u : 0u);

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
 * Контекст: Measure Task (Ble_Run по INIT-таймеру). Последовательность Avis:
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
 * @retval Нет. Контекст: Measure Task (Ble_Run: выкл. TURN_ON_BLE / INIT-ветка).
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
 * @brief  Периодический ОПРОС принятых данных (v27, замена Ble_MsdDataSet).
 * @param  Нет. @retval Нет.
 * Контекст: Measure Task (Ble_Run по SET-таймеру, TIME_RTC_BLE_DATA_SET=60 с).
 * AT+ADVMSDDATAGET -> ответ hex -> str_ble_data_set_t -> Connected_device.
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
 * @param  Нет. @retval Нет. Контекст: Measure Task (SLEEP-таймер, 30 с).
 */
void Ble_MsdDataSleep(void)
{
	if (Ble_init_flag == 1u && Ble_flag_err == 0u && BleFlagDFU == 0u) {
		(void)Ble_TransmitReceive(CMD_SLEEP, 0);
	}
}

/**
 * @brief  Включить/выключить рекламу (порт Ble_AdvStart).
 * @param  data 1 вкл / 0 выкл. @retval Нет. Контекст: Measure Task.
 */
void Ble_AdvStart(uint8_t data)
{
	(void)Ble_TransmitReceive(CMD_ADVSTART, data);
}

/**
 * @brief  Задать период рекламы, мс (порт Ble_AdvPeriodms).
 * @param  data период. @retval Нет. Контекст: Measure Task.
 */
void Ble_AdvPeriodms(uint16_t data)
{
	(void)Ble_TransmitReceive(CMD_ADVPERIODMS, data);
}

/**
 * @brief  Задать мощность рекламы (порт Ble_AdvPower).
 * @param  data уровень. @retval Нет. Контекст: Measure Task.
 */
void Ble_AdvPower(uint8_t data)
{
	(void)Ble_TransmitReceive(CMD_ADVPOWER, data);
}

/**
 * @brief  Выключить BLE-модуль (порт Ble_SystemOff из Avis - один в один).
 * @param  Нет. @retval Нет. Контекст: Measure Task.
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
 * @param  Нет. @retval Нет. Контекст: Measure Task.
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
 * @brief  Публичный Ble_Receive() — заглушка для Receiver Task (v27).
 * @param  Нет. @retval Нет.
 * В Avis Receiver Task BLE не обслуживает: обмен с модулем синхронный внутри
 * Ble_TransmitReceive (Measure Task), он и есть ЕДИНСТВЕННЫЙ читатель ring
 * InputBuffer[TYPE_BLE]. Пустое сильное определение замещает weak-заглушку
 * Vector_main.c (которая дренировала бы ring и съедала ответы модуля).
 */
void Ble_Receive(void)
{
	// Намеренно пусто — см. комментарий функции.
}

/**
 * @brief  Периодика BLE (порт Ble_Run из Avis_main.c) — Measure Task, каждый
 *         проход (50 мс); переопределяет weak-заглушку Vector_main.c.
 * @param  Нет. @retval Нет.
 * Скелет Avis: ST_COMMON_BIT_TURN_ON_BLE -> INIT-таймер (DeInit(1)+2 с ->
 * Ble_Init(serial)) -> SET-таймер (60 с): Ble_MsdDataGet() (вместо DataSet)
 * + перезапуск SET/SLEEP -> SLEEP-таймер (30 с): Ble_MsdDataSleep();
 * get_state_err_ble() -> SET/CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_BLE).
 * bit TURN_ON_BLE взводится по AT-командеверхнего ПО (будет в command_message).
 */
void Ble_Run(void)
{
	if (device_turn != DEVICE_TURNED_OFF) {
		if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_BLE)) {
			if ((get_state_init_flag_ble() == 0u) &&
				(!TEST_TIMER_RUN_RTC(TIMER_RTC_BLE_DATA_INIT)) &&
				(!TEST_TIMER_RTC(TIMER_RTC_BLE_DATA_INIT))) {
				Ble_DeInit(1);
				START_TIMER_RTC(TIMER_RTC_BLE_DATA_INIT, TIME_RTC_BLE_DATA_INIT);
			}
			if (TEST_TIMER_RTC(TIMER_RTC_BLE_DATA_INIT)) {
				RESET_TIMER_RTC(TIMER_RTC_BLE_DATA_INIT);
				if (Ble_Init((uint32_t)(((uint32_t)Sns_Cfg_struct.Config_common.SerialHi << 16) |
										(uint32_t)Sns_Cfg_struct.Config_common.SerialLo))) {
					START_TIMER_RTC(TIMER_RTC_BLE_DATA_SLEEP, TIME_RTC_BLE_DATA_SLEEP);
					START_TIMER_RTC(TIMER_RTC_BLE_DATA_SET, TIME_RTC_BLE_DATA_SET);
				}
			}
			if (TEST_TIMER_RTC(TIMER_RTC_BLE_DATA_SET)) {
				RESET_TIMER_RTC(TIMER_RTC_BLE_DATA_SET);
				Ble_MsdDataGet();					// периодический опрос (v27)
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
		if (get_state_err_ble()) {
			SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_BLE);
		} else {
			CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_BLE);
		}
	}
}

#endif /* CONFIG_BLE */
