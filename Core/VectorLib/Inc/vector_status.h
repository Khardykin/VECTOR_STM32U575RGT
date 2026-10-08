/**
  ******************************************************************************
  * @file    vector_status.h
  * @brief   Макросы доступа к битам статусов прибора (Sns_Cfg_struct)
  *
  *          В shared_types.h заданы перечисления битов:
  *            ST_COMMON      -> Config_common.State    (режимы/включения)
  *            ST_COMMON_ERR  -> Config_common.StateErr (ошибки модулей)
  *            ST_GA_ERR      -> побитово на канал (Sensor[].StateErr)
  *          Макросов доступа в проекте не было - в коде встречались только
  *          закомментированные вызовы (stm32u5xx_it.c). Здесь они есть, имена
  *          совпадают с другими приборами автора, чтобы переносимый код
  *          (Ble_Run/Lora_Run/Gps_Run из Avis_main.c) собирался без правок.
  *
  *          КОНТЕКСТ: любой. Операция - одно чтение/запись 32-битного слова
  *          (|=, &=): на Cortex-M33 это НЕ атомарно (read-modify-write),
  *          поэтому правило прежнее - статусы правит ОДИН поток (Measure Task),
  *          из ISR в них не писать. Если понадобится из ISR - заводить
  *          отдельный флаг и выставлять бит уже из потока.
  *
  *          Бит ST_COMMON_BIT_TURN_OFF = 31: прибор выключен (журнал не ведётся).
  ******************************************************************************
  */
#ifndef VECTOR_STATUS_H
#define VECTOR_STATUS_H

#include <stdint.h>
#include "shared_types.h"    /* SNS_CFG, ST_COMMON, ST_COMMON_ERR, DEVICE_TURNED */

#ifdef __cplusplus
extern "C" {
#endif
/* --- статусы канала (ST_GA_ERR) --------------------------------------------
 * НЕ ОПРЕДЕЛЕНЫ намеренно: в SNS_CFG этого проекта нет массива Sensor[]
 * (только Config_common + application_language + CRC_CONFIG), а COUNT_CHAN не
 * задан. Понадобятся каналы датчиков - добавьте в shared_types.h
 * `Sensor[COUNT_CHAN]` и сюда макросы SET/CLEAR/TEST_STATUS_ERR_BIT(bit, ch). */

/* Прибор включён (не в режиме TURN_OFF) - частая проверка в модулях обмена. */
#define VECTOR_DEVICE_IS_ON()             (device_turn != DEVICE_TURNED_OFF)

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_STATUS_H */
