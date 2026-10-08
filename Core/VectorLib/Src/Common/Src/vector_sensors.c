/**
  ******************************************************************************
  * @file    vector_sensors.c
  * @brief   Чтение BME280 / LIS3DH / MAX17048 (I2C1) и состояние в одной
  *          структуре sensors_status (см. vector_sensors.h)
  *
  *          ЛОГИКА ПОВОРОТА ЭКРАНА (пин ACCEL_INT, PC11):
  *            EXTI11 (RISING/FALLING, pull-down) -> stm32u5xx_it.c ->
  *              lis3dh_irq_handler()  [ТОЛЬКО флаг, никакого I2C из ISR]
  *            -> Measure Task: sensors_read() -> lis3dh_update_all() читает
  *              INT1_SRC по флагу и определяет новую ориентацию (6D, порог
  *              LIS3DH_6D_THRESHOLD_DEG)
  *            -> sensors_status.screen_rotation (0 или 2) + флаг события,
  *              который забирает Vector_Run_Measure() и зовёт TFT_Rotation().
  *          Прерывание в обработчике переключается на противоположный фронт
  *          (LL_EXTI_*RisingTrig/FallingTrig), поэтому повторных срабатываний
  *          на одном наклоне нет, а I2C-обмен всегда в потоке.
  *
  *          КОНТЕКСТ: поток Measure Task. Блокирующий HAL_I2C_Master_* - из ISR
  *          и до планировщика не звать.
  ******************************************************************************
  */
#include "vector_sensors.h"

#include "shared_types.h"    /* SNS_CFG, Sns_Cfg_struct, ST_COMMON_BIT_ERR_* */
#include "vector_status.h"   /* SET/CLEAR_STATUS_COMMON_ERR_BIT              */
#include "vector_log.h"
#include "vector_tick.h"

#if CONFIG_BME
#include "bme280_com.h"
#endif
#if CONFIG_LIS3DH
#include "lis3dh.h"
#endif
#if CONFIG_MAX17048
#include "MAX17048.h"
#endif

volatile sensors_status_t sensors_status =
{
  .cnt_init_ok  = 0u,
  .last_read_ms = 0u
};

/* Флаг "ориентация изменилась, поворот не применён". Ставится в sensors_read(),
   снимается в sensors_rotation_changed() - то есть событие не теряется, даже
   если экран решено повернуть позже.                                         */
static volatile uint8_t rotation_pending = 0u;

/* ------------------------------------------------------------------ инициализация */
/* Поднять все включённые конфигурацией датчики. Возврата нет: результат по
   каждой микросхеме - в sensors_status.*_ok и в логе.                        */
void sensors_init(void)
{
  sensors_status.cnt_init_ok = 0u;

#if CONFIG_BME
  /* BME280: I2C-адрес 0x76, normal mode, osr_t x8, фильтр 2 (настраивает
     bme280_init_com()). rslt == 0 - микросхема ответила.                     */
  if (bme280_init_com() == 0)
  {
    sensors_status.bme_ok = 1u;
    sensors_status.cnt_init_ok++;
    LOG_I(VLOG_M_SYS, "sensors: BME280 ok (I2C1 0x76)");
  }
  else
  {
    sensors_status.bme_ok = 0u;
    SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_STS4);
    LOG_E(VLOG_M_SYS, "sensors: BME280 FAIL (I2C1 0x76) - check PB8/PB9, addr");
  }
#endif

#if CONFIG_LIS3DH
  /* LIS3DH: WHO_AM_I, HR 12 бит, 100 Гц, +-4g, 6D-ориентация на INT1
     (lis3dh_init() сам настраивает прерывание - см. LIS3DH_ENABLE_6D_INIT).  */
  if (lis3dh_init() == 0)
  {
    sensors_status.lis3dh_ok = 1u;
    sensors_status.cnt_init_ok++;
    LOG_I(VLOG_M_SYS, "sensors: LIS3DH ok (I2C1 0x19), 6D int=%u deg",
          (uint32_t)LIS3DH_6D_THRESHOLD_DEG);
  }
  else
  {
    sensors_status.lis3dh_ok = 0u;
    LOG_E(VLOG_M_SYS, "sensors: LIS3DH FAIL (I2C1 0x19, WHO_AM_I != 0x33)");
  }
#endif

#if CONFIG_MAX17048
  /* MAX17048: топливный счётчик, проверка VERSION (не 0x0000/0xFFFF). */
  if (max17048_init())
  {
    sensors_status.max17048_ok = 1u;
    sensors_status.cnt_init_ok++;
    LOG_I(VLOG_M_SYS, "sensors: MAX17048 ok (I2C1 0x36)");
  }
  else
  {
    sensors_status.max17048_ok = 0u;
    LOG_E(VLOG_M_SYS, "sensors: MAX17048 FAIL (I2C1 0x36)");
  }
#endif

  LOG_I(VLOG_M_SYS, "sensors: init done, %u of 3 modules alive",
        sensors_status.cnt_init_ok);
}

/* -------------------------------------------------------------------- чтение */
/* Прочитать всё, что включено. Звать периодически из Vector_Run_Measure().
   BME280 в normal mode меряет сам, поэтому здесь только забираем результат,
   если MEAS_DONE (иначе счётчик ошибок растёт внутри bme280_measure).        */
void sensors_read(void)
{
  uint32_t t0 = VTICK_MS();

#if CONFIG_BME
  if (sensors_status.bme_ok != 0u)
  {
    /* 0 = данные обновлены, 1 = измерение ещё не готово, <0 = ошибка шины.
       Значения модуль кладёт прямо в Sns_Cfg_struct.Config_common.           */
    if (bme280_measure(&Sns_Cfg_struct) == 0)
    {
      sensors_status.temperature_c = Sns_Cfg_struct.Config_common.Temperature;
      sensors_status.humidity_pct  = Sns_Cfg_struct.Config_common.Humidity;
      sensors_status.pressure_hpa  = Sns_Cfg_struct.Config_common.Pressure;
      sensors_status.cnt_bme_reads++;
      CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_STS4);
    }
  }
#endif

#if CONFIG_LIS3DH
  if (sensors_status.lis3dh_ok != 0u)
  {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    /* один вызов = чтение акселерометра (если готов) + разбор события 6D,
       которое прилетело в ACCEL_INT (INT1_SRC читается только здесь)          */
    (void)lis3dh_update_all();

    if (lis3dh_get_cached_accel_g(&x, &y, &z))
    {
      sensors_status.accel_x_g = x;
      sensors_status.accel_y_g = y;
      sensors_status.accel_z_g = z;
      sensors_status.cnt_accel_reads++;
    }

    sensors_status.orientation = (uint8_t)lis3dh_get_orientation();

    /* Смена ориентации -> новый поворот экрана (0 или 2). Сам экран этот
       модуль не трогает: решение принимает Vector_Run_Measure().             */
    if (lis3dh_orientation_changed())
    {
      uint8_t st = lis3dh_get_rotation_state();

      if (st != sensors_status.screen_rotation)
      {
        sensors_status.screen_rotation = st;
        sensors_status.cnt_rotation_events++;
        rotation_pending = 1u;
        LOG_I(VLOG_M_SYS, "sensors: orientation=%u -> rotation=%u",
              (uint32_t)sensors_status.orientation, (uint32_t)st);
      }
    }
  }
#endif

#if CONFIG_MAX17048
  if (sensors_status.max17048_ok != 0u)
  {
    sensors_status.battery_percent_x10 = (uint16_t)(max17048_cellPercent() * 10.0f);
    sensors_status.battery_voltage_mv  = (uint16_t)(max17048_cellVoltage() * 1000.0f);
    sensors_status.cnt_battery_reads++;
  }
#endif

  sensors_status.last_read_ms = VTICK_MS();
  sensors_status.cnt_read_ms  = VTICK_ELAPSED_MS(t0);
}

/* Событие смены ориентации: 1 = надо применить новый поворот
   (sensors_status.screen_rotation), флаг сбрасывается. КОНТЕКСТ: поток.      */
uint8_t sensors_rotation_changed(void)
{
  if (rotation_pending == 0u)
  {
    return 0u;
  }
  rotation_pending = 0u;
  return 1u;
}
