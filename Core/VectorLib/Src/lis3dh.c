#include "Vector_main.h"
#include "lis3dh.h"

#if (CONFIG_LIS3DH)

#include <math.h>      /* sinf: расчёт порога 6D-ориентации */

// Внутренний контекст драйвера
static stmdev_ctx_t dev_ctx;

// Кэш ориентации
static struct {
    volatile lis3dh_orientation_t current;
    volatile bool changed;
} orient_cache = {LIS3DH_ORIENT_UNKNOWN, false};

// Флаг события от ISR
static volatile bool orientation_event_pending = false;

// Кэш данных акселерометра
#if LIS3DH_CACHE_ACCEL_ENABLE
static struct {
    float x_g;
    float y_g;
    float z_g;
    volatile bool fresh;  // флаг: новые данные записаны
} accel_cache = {0.0f, 0.0f, 0.0f, false};
#endif

// Callback записи (использует ваши макросы)
static int32_t platform_write(void *handle, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)handle;
    uint8_t tx_buf[32];
    if (len + 1 > sizeof(tx_buf)) return -1;
    tx_buf[0] = (len > 1) ? (reg | 0x80) : reg;
    for (uint16_t i = 0; i < len; i++) tx_buf[i + 1] = buf[i];

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
        LIS3DH_I2C_HANDLE,
        LIS3DH_I2C_ADDR,  // ← Уже 8-бит адрес, сдвиг не нужен
        tx_buf,
        len + 1,
        LIS3DH_TIMEOUT_MS
    );
    return (status == HAL_OK) ? 0 : -1;
}

// Callback чтения
static int32_t platform_read(void *handle, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)handle;
    uint8_t addr = (len > 1) ? (reg | 0x80) : reg;

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
        LIS3DH_I2C_HANDLE,
        LIS3DH_I2C_ADDR,  // ← Уже 8-бит адрес
        &addr,
        1,
        LIS3DH_TIMEOUT_MS
    );
    if (status != HAL_OK) return -1;

    status = HAL_I2C_Master_Receive(
        LIS3DH_I2C_HANDLE,
        LIS3DH_I2C_ADDR,
        buf,
        len,
        LIS3DH_TIMEOUT_MS
    );
    return (status == HAL_OK) ? 0 : -1;
}

// Инициализация
int32_t lis3dh_init(void)
{
    dev_ctx.write_reg = platform_write;
    dev_ctx.read_reg  = platform_read;
    dev_ctx.handle    = NULL;

    uint8_t id;
    if (lis3dh_device_id_get(&dev_ctx, &id) != 0 || id != LIS3DH_ID) return -1;

    lis3dh_operating_mode_set(&dev_ctx, LIS3DH_HR_12bit);
    lis3dh_data_rate_set(&dev_ctx, LIS3DH_ODR_100Hz);
    lis3dh_full_scale_set(&dev_ctx, LIS3DH_4g);
    lis3dh_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);
    lis3dh_aux_adc_set(&dev_ctx, LIS3DH_AUX_DISABLE);

    orient_cache.current = LIS3DH_ORIENT_UNKNOWN;
    orient_cache.changed = false;
    orientation_event_pending = false;

#if LIS3DH_ENABLE_6D_INIT
    if (lis3dh_enable_6d_orientation(LIS3DH_6D_THRESHOLD_DEG, LIS3DH_6D_DURATION_MS) != 0) {
        return -1;
    }
#endif
    return 0;
}

// Включение 6D с прерыванием (только ось Y)
int32_t lis3dh_enable_6d_orientation(uint8_t threshold_deg, uint16_t duration_ms)
{
    // Расчет порога для шкалы ±4g (1 LSB = 32 mg). 1g = 1000mg.
    // Порог = (1000 * sin(угол)) / 32
    // Для 20 градусов: (1000 * 0.342) / 32 ≈ 10.6 -> 10 LSB
    // Для 50 градусов: (1000 * 0.766) / 32 ≈ 23.9 -> 24 LSB
    float rad = (float)threshold_deg * 0.01745329f;
    uint8_t ths = (uint8_t)((1000.0f * sinf(rad)) / 32.0f);

    if (ths > 31) ths = 31;
    if (ths == 0) ths = 1; // Защита от нулевого порога

    uint8_t dur = (uint8_t)(duration_ms / 10);
    if (dur > 127) dur = 127;

    lis3dh_int1_cfg_t cfg = {0};
    cfg._6d = 1; cfg.aoi = 1;
    cfg.xlie = 1; cfg.xhie = 1;  // Игнорируем ось X
    cfg.ylie = 0; cfg.yhie = 0;  // Только ось Y (наклон к себе / от себя)
    cfg.zlie = 0; cfg.zhie = 0;  // Игнорируем ось Z

    lis3dh_int1_gen_conf_set(&dev_ctx, &cfg);
    lis3dh_int1_gen_threshold_set(&dev_ctx, ths);
    lis3dh_int1_gen_duration_set(&dev_ctx, dur);
    lis3dh_int1_pin_notification_mode_set(&dev_ctx, LIS3DH_INT1_LATCHED);
    lis3dh_int1_pin_detect_4d_set(&dev_ctx, PROPERTY_ENABLE);

    lis3dh_ctrl_reg3_t ctrl3 = {0}; ctrl3.i1_ia1 = 1;
    lis3dh_pin_int1_config_set(&dev_ctx, &ctrl3);
    return 0;
}

// [ISR] Только установка флага — НИКАКОГО I2C!
void lis3dh_irq_handler(void)
{
    orientation_event_pending = true;
}

// [ПОТОК] Обновление: акселерометр + обработка ориентации (ОДНА функция, ОДИН вызов I2C)
int32_t lis3dh_update_all(void)
{
	// 1. Обновляем акселерометр (если кэш включен)
#if LIS3DH_CACHE_ACCEL_ENABLE
	uint8_t ready;
	if (lis3dh_xl_data_ready_get(&dev_ctx, &ready) == 0 && ready) {
		// Чтение сырых данных
		int16_t raw[3];
		if (lis3dh_acceleration_raw_get(&dev_ctx, raw) == 0) {
			// Конвертация в g (±4g + HR-режим)
			float mg[3] = {
				lis3dh_from_fs4_hr_to_mg(raw[0]),
				lis3dh_from_fs4_hr_to_mg(raw[1]),
				lis3dh_from_fs4_hr_to_mg(raw[2])
			};
			// Запись в кэш + установка флага
			accel_cache.x_g = mg[0] / 1000.0f;
			accel_cache.y_g = mg[1] / 1000.0f;
			accel_cache.z_g = mg[2] / 1000.0f;
			accel_cache.fresh = true;
		}
	}
#endif

    // 2. Если есть событие ориентации — обрабатываем (чтение INT1_SRC)
    if (orientation_event_pending) {
        orientation_event_pending = false;  // Сбрасываем флаг

        uint8_t src = 0;
        if (lis3dh_read_reg(&dev_ctx, LIS3DH_INT1_SRC, &src, 1) == 0) {
            if (src & 0x40U) {  // IA = 1, событие действительно есть
                lis3dh_orientation_t new_orient = LIS3DH_ORIENT_UNKNOWN;
                if (src & 0x02U)      new_orient = LIS3DH_ORIENT_X_UP;    // XH
                else if (src & 0x01U) new_orient = LIS3DH_ORIENT_X_DOWN;  // XL
                // Оси X и Z игнорируются по конфигурации

                if (new_orient != orient_cache.current) {
                    orient_cache.current = new_orient;
                    orient_cache.changed = true;
                }
            }
        }
    }
    return 0;
}

// Получить ориентацию из кэша (без I2C)
lis3dh_orientation_t lis3dh_get_orientation(void)
{
    return orient_cache.current;
}

// Проверка: изменилась ли ориентация (авто-сброс)
bool lis3dh_orientation_changed(void)
{
    bool was = orient_cache.changed;
    orient_cache.changed = false;
    return was;
}

// Маппинг в state для TFT_Rotation() — только 0 или 2
uint8_t lis3dh_get_rotation_state(void)
{
    static uint8_t last_state = 0;
    switch (orient_cache.current) {
        case LIS3DH_ORIENT_X_UP:   last_state = 2; return 2;  // На кармане
        case LIS3DH_ORIENT_X_DOWN: last_state = 0; return 0;  // Наклонили к себе
        default: return last_state;  // Переходные состояния — не дёргаем экран
    }
}

// Получить данные акселерометра из кэша (без I2C)
bool lis3dh_get_cached_accel_g(float *x, float *y, float *z)
{
#if LIS3DH_CACHE_ACCEL_ENABLE
    // Запоминаем состояние флага ДО чтения
    bool was_fresh = accel_cache.fresh;

    // Копируем данные из кэша (всегда, даже если флаг не установлен)
    if (x) *x = accel_cache.x_g;
    if (y) *y = accel_cache.y_g;
    if (z) *z = accel_cache.z_g;

    // Если данные были свежими — сбрасываем флаг (чтобы не отдать дважды)
    if (was_fresh) {
        accel_cache.fresh = false;
    }

    return was_fresh;  // caller может решить, что делать со «старыми» данными
#else
    (void)x; (void)y; (void)z;
    return false;
#endif
}

// Проверка: есть ли свежие данные акселерометра в кэше
bool lis3dh_is_accel_cached(void)
{
#if LIS3DH_CACHE_ACCEL_ENABLE
    return accel_cache.fresh;
#else
    return false;
#endif
}
#endif
