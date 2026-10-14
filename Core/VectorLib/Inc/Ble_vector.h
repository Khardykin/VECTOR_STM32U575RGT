/**
 * @file Ble_vector.h
 * @brief BLE-модуль MK01: AT-протокол (сторона УПРАВЛЕНИЯ модулем), порт
 *        Ble_mk01.c/h из Avis по решению автора (v27, п.2):
 *        "полностью копируешь весь функционал, кроме BLE_CMD_ADVMSDDATASET,
 *        которую заменяем обычным запросом BLE_CMD_ADVMSDDATAGET, и на которую
 *        нам отвечают структурой. И так периодически опрашиваем".
 *
 * Т.е. мы, как и Avis, — ХОЗЯИН своего BLE-модуля (AT/имя/период/мощность/
 * старт/сон/DFU/SYSTEMOFF/BLEADDR?/MODINFO?), но данные не ПУШим в рекламу
 * (ADVMSDDATASET), а периодически (Ble_Run, TIMER_RTC_BLE_DATA_SET = 60 с)
 * запрашиваем AT+ADVMSDDATAGET принятые модулем данные — ответ разбирается в
 * str_ble_data_set_t (структура отправителя — прибора Avis) и кладётся в
 * Connected_device (SNS_Dev_Type). Полный набор команд модуля сохранён,
 * включая ADVBLENAMESET (задать имя в сети — нужно).
 *
 * Обмен СО СВОИМ модулем — синхронный (Ble_TransmitReceive: 4 попытки,
 * контроль OK/ERROR, как в Avis). Байты USART3 складывает ISR в ring
 * InputBuffer[TYPE_BLE] (Vector_main.c); читает ring ТОЛЬКО Ble_TransmitReceive
 * — публичный Ble_Receive() (вызов из Receiver Task) оставлен пустым, чтобы
 * не выедать ответы модуля (в Avis Receiver Task BLE тоже не обслуживает).
 *
 * Управление: CONFIG_BLE (config_device.h), USART_BLE (huart3, PC4/PC5,
 * 115200), пин BLE_RESET (PC12). Запуск из Measure Task — Ble_Run()
 * (переопределяет weak-заглушку Vector_main.c), скелет — Avis Ble_Run():
 * INIT-таймер -> Ble_Init(serial); SET-таймер -> Ble_MsdDataGet() (опрос);
 * SLEEP-таймер -> Ble_MsdDataSleep(); ошибка 3 серий подряд ->
 * ST_COMMON_BIT_ERR_BLE (общая ошибка BLE).
 *
 * Имя модуля: BLE_Label[сборка] + серийный номер (SerialHi/SerialLo, формат
 * %04d%04d как у Avis Device2_1). НАБОР МЕТКИ VECTOR — за автором (TODO,
 * временно "VEC").
 *
 * Версии: v26 — первая версия (отклик на DATASET); v27 — переписан по
 * замечанию автора: полный порт Ble_mk01 (мастерская сторона своего модуля),
 * DATASET заменён на периодический DATAGET-опрос -> Connected_device.
 */
#ifndef __BLE_VECTOR_H
#define __BLE_VECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32u5xx_hal.h"
#include "config_device.h"   /* CONFIG_BLE - ОБЯЗАТЕЛЬНО до #if (v27: без него
                                макрос не определён в точке #if и весь модуль
                                молча вырезается даже при CONFIG_BLE=1)       */
#include <stdint.h>
#include <string.h>

#if CONFIG_BLE
#define BLE_TRANSMIT_REPEAT		(4)			// Количество попыток отправить пакет
#define BLE_ERROR_DATA_REPEAT	(3)			// Допустимое количество серий с ошибками

// Размер сырых данных ответа ADVMSDDATAGET. ВАЖНО: sizeof(str_ble_data_set_t)
// = 64 байта, НЕ 57: внешний struct packed, но внутренний (sensors[]) - нет,
// поэтому сенсор = 8 байт с паддингом. Это ОДИН В ОДИН сборка Avis (те же
// правила GCC) - формат провода совпадает побайтно, packed не "доделывать"!
#define BLE_DATA_SET_MAX_SIZE	(160)

#define BLE_WAIT_UART_TIMEOUT	(35u)		// мс ожидания ответа модуля (как в Avis)
#define BLE_WAIT_UART_DELAY		(5u)		// мс добавки на дотекание хвоста ответа

// Команды AT-протокола модуля MK01 (строковый индекс = BLE_CMD_t)
typedef enum {
	CMD_AT = 0,					// Проверка связи
	CMD_MODINFO,				// Запрос информации о модуле
	CMD_ADVBLENAMESET,			// Задать имя модуля в сети
	CMD_ADVMSDDATAGET,			// ЗАПРОС принятых данных (ответ — структура, v27)
	CMD_ADVSTART,				// Включить/выключить рекламу
	CMD_ADVPERIODMS,			// Период рекламы, мс
	CMD_ADVPOWER,				// Мощность рекламы
	CMD_SLEEP,					// Сон модуля
	CMD_SYSTEMOFF,				// Выключение модуля
	CMD_DFU,					// Режим прошивки модуля
	CMD_BLEADDR,				// Запрос MAC-адреса модуля
	CMD_COUNT,
	CMD_UNKNOWN
} BLE_CMD_t;

// Структура принятых по BLE данных (заполняет рекламный пакет ОТПРАВИТЕЛЬ —
// прибор Avis; у нас — только приём, поле в поле как str_ble_data_set_t в
// Ble_mk01.h, порядок байт little-endian "как на проводе")
typedef struct __attribute__((packed)) str_ble_data_set_t
{
	uint16_t protocol_version;	// Версия протокола обмена
	uint16_t dev_id;			// Идентификатор устройства-отправителя
	uint8_t hardware_version;	// Версия железа отправителя
	uint8_t software_version;	// Версия ПО отправителя
	uint8_t battery_charge;		// Заряд батареи отправителя, %
	uint8_t sensors_enabled;	// Битовая маска включенных каналов (бит0 = канал0)
	struct {
		uint8_t  Type_gas;			// Код типа газа/канала (в SNS_Dev_Type.SNS_Dev_Type)
		uint16_t Concentration;		// Концентрация (единицы отправителя; discreteness
									// его протокола нам пока неизвестна — кладём сырьём, TODO автор)
		uint16_t State;				// Статус канала отправителя
		uint16_t Setting;			// Установки канала отправителя
	} sensors[7];					// Данные по каналам (до 7)
} str_ble_data_set_t;

// Строки команд модуля MK01
extern const char *str_BLE_CMD[];
// MAC-адрес своего модуля (ответ на BLEADDR?)
extern uint8_t MACAddrBle[6];
// Информация о модуле (ответ на MODINFO?)
extern uint8_t ModInfo[40];
// Данные, принятые последним успешным опросом ADVMSDDATAGET (сырьё)
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
	uint32_t cnt_datasets;		// успешно принятых структур по ADVMSDDATAGET
	uint8_t  init_flag;			// модуль инициализирован (Ble_Init вернул 1)
	uint8_t  flag_err;			// ошибка обмена (Avis: Ble_flag_err)
	uint8_t  counter_err;		// серии ошибок подряд (>= BLE_ERROR_DATA_REPEAT -> общая ошибка)
	uint8_t  cmd_last;			// последняя команда (BLE_CMD_t)
	uint8_t  last_rx_len;		// длина последнего сырого ответа
	uint8_t  last_rx[32];		// последний сырой ответ (диагностика, ASCII)
} ble_status_t;
extern ble_status_t ble_status;

// --- API (повторяет Ble_mk01.h; DatSet заменён на DatGet) ------------------
extern uint8_t Ble_Init(uint32_t serial);
extern void Ble_DeInit(uint8_t init);
extern void Ble_MsdDataGet(void);		// периодический опрос принятых данных (v27)
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
// Ble_TransmitReceive — см. комментарий файла).
extern void Ble_Receive(void);
// Периодика из Measure Task (переопределяет weak из Vector_main.c).
extern void Ble_Run(void);

#endif /* CONFIG_BLE */

#ifdef __cplusplus
}
#endif

#endif /* __BLE_VECTOR_H */
