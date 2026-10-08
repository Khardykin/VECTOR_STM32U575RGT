/**
  ******************************************************************************
  * @file    vector_sensors.h
  * @brief   Датчики прибора: BME280 (I2C1), LIS3DH (I2C1), MAX17048 (I2C1)
  *
  *          Тонкая обёртка над драйверами модулей: инициализация, периодическое
  *          чтение, ОДНА структура состояния sensors_status (как audio_status /
  *          tasks_status) и логика поворота экрана по 6D-ориентации LIS3DH.
  *
  *          ШИНА: все три микросхемы на I2C1 (PB8 SCL / PB9 SDA), адреса
  *            BME280   0x76 (7 бит) -> 0xEC (8 бит)
  *            LIS3DH   0x19 (7 бит) -> 0x32 (8 бит), SA0 = 1
  *            MAX17048 0x36 (7 бит) -> 0x6C (8 бит)
  *          Владелец шины - поток Measure Task: чтение идёт только из
  *          sensors_read(), поэтому мьютекс не нужен. Если появится второй
  *          потребитель (например, запись калибровки из другого потока) -
  *          заводить мьютекс на I2C1.
  *
  *          ВЫКЛЮЧЕНИЕ МИКРОСХЕМ: CONFIG_BME / CONFIG_LIS3DH / CONFIG_MAX17048
  *          в config_device.h. Флаг = 0 -> драйвер не компилируется вовсе
  *          (тело файла закрыто #if), а здесь остаётся только счётчик.
  *
  *          КОНТЕКСТ: sensors_init()/sensors_read()/sensors_rotation_check() -
  *          поток Measure Task (внутри блокирующий HAL_I2C_Master_*).
  ******************************************************************************
  */
#ifndef VECTOR_SENSORS_H
#define VECTOR_SENSORS_H

#include <stdint.h>
#include "vector_config.h"
#include "config_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Всё состояние датчиков - ОДНА структура: в Expressions одна строка
   `sensors_status`. Значения дублируются из Sns_Cfg_struct.Config_common,
   чтобы состояние шины и данные читались в одном месте.                      */
typedef struct
{
  /* --- присутствие модулей на шине (результат инициализации) --- */
  volatile uint8_t  bme_ok;             /* 1 = BME280 ответил и настроен     */
  volatile uint8_t  lis3dh_ok;          /* 1 = LIS3DH ответил (WHO_AM_I)     */
  volatile uint8_t  max17048_ok;        /* 1 = MAX17048 ответил (VERSION)    */

  /* --- данные --- */
  volatile float    temperature_c;      /* BME280, градусы C                 */
  volatile float    humidity_pct;       /* BME280, %                         */
  volatile float    pressure_hpa;       /* BME280, гПа (мм рт.ст. / 1.333)   */
  volatile float    accel_x_g;          /* LIS3DH, g (шкала +-4g, HR 12 бит) */
  volatile float    accel_y_g;
  volatile float    accel_z_g;
  volatile uint8_t  orientation;        /* lis3dh_orientation_t               */
  volatile uint8_t  screen_rotation;    /* 0 или 2 - аргумент TFT_Rotation()  */
  volatile uint16_t battery_percent_x10;/* MAX17048 SOC, % * 10              */
  volatile uint16_t battery_voltage_mv; /* MAX17048 VCELL, мВ                */

  /* --- счётчики (диагностика шины) --- */
  volatile uint32_t cnt_init_ok;        /* сколько модулей поднялось         */
  volatile uint32_t cnt_bme_reads;      /* успешных чтений BME280            */
  volatile uint32_t cnt_accel_reads;    /* обновлений кэша акселерометра     */
  volatile uint32_t cnt_battery_reads;  /* чтений MAX17048                   */
  volatile uint32_t cnt_rotation_events;/* событий смены ориентации          */
  volatile uint32_t cnt_read_ms;        /* длительность последнего чтения, мс*/
  volatile uint32_t last_read_ms;       /* метка последнего sensors_read()   */
} sensors_status_t;

extern volatile sensors_status_t sensors_status;

/* Инициализация всех включённых датчиков. Звать ОДИН раз из
   Vector_Run_Pre_Init() (поток Measure Task, планировщик уже работает).      */
void sensors_init(void);

/* Периодическое чтение (из Vector_Run_Measure, 1 раз в
   VECTOR_TASKS_MEASURE_PERIOD_MS). BME280 работает в normal mode и меряет сам,
   поэтому здесь только забираем готовый результат, если MEAS_DONE.           */
void sensors_read(void);

/* 1 = с прошлого вызова ориентация изменилась (флаг сбрасывается чтением).
   Новый поворот уже лежит в sensors_status.screen_rotation (0 или 2 - аргумент
   TFT_Rotation()), а вызывающий сам решает, что с ним делать: повернуть экран,
   перерисовать LVGL или ничего. Разделение намеренное: модуль датчиков не
   знает про дисплей. Флаг взводится внутри sensors_read().                   */
uint8_t sensors_rotation_changed(void);

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_SENSORS_H */
