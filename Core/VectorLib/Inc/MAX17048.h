/*!
 * @file 7semi_MAX17048.h
 * @brief Arduino Library for MAX17048 Fuel Gauge IC
 *
 * @details
 * This library provides an interface to the MAX17048 LiPo battery fuel gauge via I2C.
 * It includes functions to read voltage, state-of-charge, charge rate, and alert flags.
 * It also supports quick start, soft reset, voltage alert configuration, and ALRT pin handling.
 *
 * @author  7semi
 * @license MIT License
 */

#ifndef MAX17048_H_
#define MAX17048_H_

#include <stdint.h>
#include <stdbool.h>
#include "config_device.h"   /* CONFIG_MAX17048 */


#define PERIPH_I2C_MAX17048	(&hi2c1)
// I2C default address for MAX17048
#define MAX17048_TIME_ERR_I2C	(10)
#define MAX17048_I2C_ADDR  	(0x36<<1)

// Register Map
#define VCELL_REG    0x02  // Battery voltage
#define SOC_REG      0x04  // State of charge
#define MODE_REG     0x06  // Mode (quick start)
#define VERSION_REG  0x08  // IC version (includes chip ID)
#define HIBRT_REG    0x0A  // Hibernate threshold
#define CONFIG_REG   0x0C  // Configuration and ALRT latch
#define CRATE_REG    0x16  // Charge/discharge rate
#define VALRT_REG    0x14  // Voltage alert threshold (low=LSB, high=MSB)
#define COMMAND_REG  0xFE  // Reset command
#define STATUS_REG   0x1A  // Alert flags (VH, VL, POR, etc.)



// Initialize sensor
bool max17048_init(void);

// Read battery voltage (in volts)
float max17048_cellVoltage(void);

// Read battery percentage (% SoC)
float max17048_cellPercent(void);

// Read charge/discharge rate (%/hr)
float max17048_chargeRate(void);

// Issue reset command to IC
void max17048_reset(void);

// Recalibrate SoC estimation
void max17048_quickStart(void);

// Set voltage alert limits (in volts)
void max17048_setVoltageLimits(float minV, float maxV);

// Read high alert voltage threshold
float max17048_getMaxvoltage();

// Read low alert voltage threshold
float max17048_getMinvoltage();

// Check if low voltage alert is triggered
bool max17048_alertLowV();

// Check if high voltage alert is triggered
bool max17048_alertHighV();

// Clear ALRT pin latch (bit 5 in CONFIG)
void max17048_resetALRTPin();

#endif // MAX17048_H_
