#include "Avis_main.h"

//===========================================================================================================================
static void writeRegister(uint8_t reg, uint16_t value);
static uint8_t readReg(uint8_t reg);
static uint16_t read2Reg(uint8_t reg);
static void write16(uint8_t reg, uint16_t value);
//===========================================================================================================================
static uint8_t count_err = 0;
//===========================================================================================================================
// Write 16-bit value to a register
static void writeRegister(uint8_t reg, uint16_t value) {	
	uint16_t error = 0;
    uint8_t buffer[3] = {0};
	buffer[0] = reg;
	buffer[1] = ((value >> 8) & 0xFF);
	buffer[2] = (value & 0xFF);
	error |= I2C_Master_Transmit(PERIPH_I2C_MAX17048, MAX17048_I2C_ADDR, &buffer[0], 3, MAX17048_TIME_ERR_I2C);
	if(error != I2C_OK){
    	count_err ++;
    }
/*	
  _wire->beginTransmission(_address);
  _wire->write(reg);
  _wire->write((value >> 8) & 0xFF);
  _wire->write(value & 0xFF);
  _wire->endTransmission();
  */
}

//===========================================================================================================================
// Write 16-bit value using little-endian format
static void write16(uint8_t reg, uint16_t value) {
	uint16_t error = 0;
    uint8_t buffer[3] = {0};
	buffer[0] = reg;
	buffer[1] = (value & 0xFF);
	buffer[2] = ((value >> 8) & 0xFF);
	error |= I2C_Master_Transmit(PERIPH_I2C_MAX17048, MAX17048_I2C_ADDR, &buffer[0], 3, MAX17048_TIME_ERR_I2C);
	
	if(error != I2C_OK){
    	count_err ++;
    }
	/*
  _wire->beginTransmission(MAX17048_ADDRESS);
  _wire->write(reg);
  _wire->write(value & 0xFF);   // LSB
  _wire->write(value >> 8);     // MSB
  _wire->endTransmission();
  */
}

//===========================================================================================================================
// Read only MSB from a 16-bit register (used for VALRT min/max)
static uint8_t readReg(uint8_t reg) {
	uint16_t error = 0;
	uint8_t value = 0;
    uint8_t buffer[2] = {0};
	
	buffer[0] = reg;
	error |= I2C_Master_Transmit(PERIPH_I2C_MAX17048, MAX17048_I2C_ADDR, &buffer[0], 1, MAX17048_TIME_ERR_I2C);
	
	error |= I2C_Master_Receive(PERIPH_I2C_MAX17048, MAX17048_I2C_ADDR, &buffer[0], 2, MAX17048_TIME_ERR_I2C);
	
	if(error != I2C_OK){
    	count_err ++;
    }
	value = buffer[0]; // MSB
	/*
  _wire->beginTransmission(_address);
  _wire->write(reg);
  _wire->endTransmission(false);

  _wire->requestFrom(_address, (uint8_t)2);
  uint8_t value = buffer[0]; // MSB
  _wire->read();                 // Discard LSB
  */
  return value;
}

//===========================================================================================================================
// Read 16-bit value from register
static uint16_t read2Reg(uint8_t reg) {
	uint16_t error = 0;
	uint16_t value = 0;
    uint8_t buffer[2] = {0};
	
	buffer[0] = reg;
	error |= I2C_Master_Transmit(PERIPH_I2C_MAX17048, MAX17048_I2C_ADDR, &buffer[0], 1, MAX17048_TIME_ERR_I2C);
	
	error |= I2C_Master_Receive(PERIPH_I2C_MAX17048, MAX17048_I2C_ADDR, &buffer[0], 2, MAX17048_TIME_ERR_I2C);

	if(error != I2C_OK){
    	count_err ++;
    }
	
	value = (buffer[0] << 8) | buffer[1]; // MSB
	/*
  _wire->beginTransmission(_address);
  _wire->write(reg);
  _wire->endTransmission(false);

  _wire->requestFrom(_address, (uint8_t)2);
  uint16_t value = (_wire->read() << 8) | _wire->read();
  */
  return value;
}

//===========================================================================================================================
// Initialize the MAX17048 sensor
bool max17048_init(void) 
{
  uint16_t version = read2Reg(VERSION_REG); // Check device version
  if (version == 0xFFFF || version == 0x0000) {
    return false; // Sensor not detected
  }

  return true;
}
//===========================================================================================================================
// Read battery voltage in mvolts
float max17048_cellVoltage(void) {
  uint16_t cell_voltage = read2Reg(VCELL_REG);
  return ((cell_voltage * 78.125) / 1000.0); // 78.125uV per LSB
}

//===========================================================================================================================
// Read battery state-of-charge (percentage)
float max17048_cellPercent(void) {
  uint16_t soc = read2Reg(SOC_REG);
  return (soc / 256.0); // 1/256 % per LSB
}

//===========================================================================================================================
// Read charge/discharge rate in %/hr
float max17048_chargeRate(void) {
  int16_t rate = (int16_t)read2Reg(CRATE_REG);
  return (rate * 0.208); // 0.208 %/hr per LSB
}

//===========================================================================================================================
// Send reset command to the IC
void max17048_reset(void) {
  writeRegister(COMMAND_REG, 0x5400);
  DelayInt(10); // Allow reset to complete
}

//===========================================================================================================================
// Trigger quick start to recalibrate SoC
void max17048_quickStart(void) {
  writeRegister(MODE_REG, 0x4000);
  DelayInt(2);
}

//===========================================================================================================================
// Set voltage alert limits in volts
void max17048_setVoltageLimits(float minV, float maxV) {
  uint8_t minReg = (uint8_t)(minV / 0.02);  // LSB = 20mV
  uint8_t maxReg = (uint8_t)(maxV / 0.02);

  uint16_t valrt = (maxReg << 8) | minReg;
  write16(VALRT_REG, valrt);
}

//===========================================================================================================================
// Get high voltage alert threshold
float max17048_getMaxvoltage() {
  return readReg(VALRT_REG + 1) * 0.02;
}

//===========================================================================================================================
// Get low voltage alert threshold
float max17048_getMinvoltage() {
  return readReg(VALRT_REG) * 0.02;
}

//===========================================================================================================================
// Check if high voltage alert is triggered
bool max17048_alertHighV() {
  uint8_t status = readReg(STATUS_REG);
  return status & 0x02; // Bit 2 = VH
}

//===========================================================================================================================
// Check if low voltage alert is triggered
bool max17048_alertLowV() {
  uint8_t status = readReg(STATUS_REG);
  return status & 0x04; // Bit 3 = VL
}

//===========================================================================================================================
// Clear ALRT latch in CONFIG register (bit 5)
void max17048_resetALRTPin() {
  uint16_t config = read2Reg(CONFIG_REG);
  config &= ~(1 << 5); // Clear ALRT bit
  write16(CONFIG_REG, config);
}
