/**
  ******************************************************************************
  * @file    shared_types.c
  * @brief   Определения функций общих типов (пара к shared_types.h)
  *
  *          Замечание автора: «не надо держать функции в .h, это не красиво» -
  *          в shared_types.h остаётся только прототип, определение здесь.
  *          Файл НАМЕРЕННО не включает main_vector.h (HAL/ThreadX): функция
  *          чистая арифметика, поэтому её можно компилировать и в хост-тестах
  *          (как log_queue.c для журнала) - достаточно shared_types.h.
  *
  *          Экземпляров общих типов (countdown_time, Sns_Cfg_struct, device_turn)
  *          здесь нет: они живут в main_vector.c - их включают/выключают
  *          CONFIG_* и переносить их в этот файл незачем.
  ******************************************************************************
  */
#include <stdint.h>
#include "shared_types.h"

//===========================================================================================================================
// Функция перевода вывода дискретности по настройкам (авторская, из Avis
// Avis_Menu_Functionality.c; сигнатура адаптирована: вместо SNS_CFG_Type* -
// слово Setting канала, вместо num1 - селектор MEASURE_SEL_*).
//   device_setting - поле Setting канала (SNS_DevChan_Type): биты DEVICE_SETTING_CHAN
//                    + маски MEASURE_*/MEASURE_DISK* (shared_types.h);
//   sel            - какую пару полей брать: MEASURE_SEL_BASIC (основная:
//                    единицы/дискретность для RF, журнала и порогов) или
//                    MEASURE_SEL_DISP (дисплейная - индикация).
// Возврат: 1/10/100/1000; 0 = степень вне диапазона (невалидные биты
// дискретности или неизвестный sel).
// КОНТЕКСТ: любой (чистая арифметика, состояния не меняет). Вызывает
// Lora_S7678S.c (контейнер Sensors, CONFIG_LORA).
//===========================================================================================================================
uint16_t calculating_the_discreteness(uint16_t device_setting, uint16_t sel)
{
	uint16_t degree;

	if (sel == (uint16_t)MEASURE_SEL_BASIC) {
		degree = (uint16_t)((device_setting & MEASURE_DISK_BASIC) >> (MEASURE_OFFSET_BASIC + OFFSET_DISK));
	}
	else if (sel == (uint16_t)MEASURE_SEL_DISP) {
		degree = (uint16_t)((device_setting & MEASURE_DISK) >> OFFSET_DISK);
	}
	else {
		return 0;
	}

	if (degree == 0u)  { return 1u; }
	if (degree == 1u)  { return 10u; }
	if (degree == 2u)  { return 100u; }
	if (degree == 3u)  { return 1000u; }
	return 0u;
}
