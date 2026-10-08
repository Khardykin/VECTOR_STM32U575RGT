#ifndef LIS3DH_H
#define LIS3DH_H

#include "config_device.h"   /* CONFIG_LIS3DH */

#ifdef __cplusplus
extern "C" {
#endif
#if (CONFIG_LIS3DH)
#include "lis3dh_reg.h"
#include <stdbool.h>
#include <stdint.h>

// НАСТРОЙКИ ПОЛЬЗОВАТЕЛЯ
#define LIS3DH_I2C_HANDLE     (&hi2c1)
#define LIS3DH_I2C_ADDR       (0x19 << 1)  // 8-бит адрес: 0x32 (SA0=1) или 0x30 (SA0=0)
#define LIS3DH_TIMEOUT_MS     50

// Автоматическая настройка 6D-ориентации
// Порог 55° + задержка 300 мс = надёжная защита от шагов/тряски
#define LIS3DH_ENABLE_6D_INIT       1
#define LIS3DH_CACHE_ACCEL_ENABLE   1
#define LIS3DH_6D_THRESHOLD_DEG     25   // ≈768мг (~50°). Должно быть < 31 для ±4g!
#define LIS3DH_6D_DURATION_MS       200  // Оптимально: 250..400

// Ориентация устройства (6D detection)
typedef enum {
    LIS3DH_ORIENT_UNKNOWN = 0,
    LIS3DH_ORIENT_Z_UP,
    LIS3DH_ORIENT_Z_DOWN,
    LIS3DH_ORIENT_X_UP,
    LIS3DH_ORIENT_X_DOWN,
    LIS3DH_ORIENT_Y_UP,
    LIS3DH_ORIENT_Y_DOWN,
} lis3dh_orientation_t;

// Публичный API
int32_t lis3dh_init(void);

// Основная функция обновления: акселерометр + ориентация (вызывать из ОДНОГО потока)
int32_t lis3dh_update_all(void);

// API для ориентации (безопасный для многопоточки)
int32_t lis3dh_enable_6d_orientation(uint8_t threshold_deg, uint16_t duration_ms);
void    lis3dh_irq_handler(void);  // Вызывать из вашего GPIO_EXTI_Callback (только флаг!)
bool    lis3dh_orientation_changed(void);
lis3dh_orientation_t lis3dh_get_orientation(void);
uint8_t lis3dh_get_rotation_state(void);  // Возвращает 0 или 2 для TFT_Rotation()

// Опционально: если нужен только акселерометр
bool    lis3dh_get_cached_accel_g(float *x, float *y, float *z);  // Получить данные из кэша
bool    lis3dh_is_accel_cached(void);                              // Флаг: данные в кэше свежие

#ifdef __cplusplus
}
#endif
#endif
#endif // LIS3DH_H
