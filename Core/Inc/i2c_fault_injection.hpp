#pragma once

#include <cstdint>

#include "i2c_bus.hpp"

// Test hook for I2cBus::Recover(). Bit-bangs the start of a register read
// from a slave, then stops clocking just after the slave has started driving
// its first data bit. If that bit is 0, the slave holds SDA low indefinitely -
// the same state an MCU reset mid-transaction leaves behind.
//
// Pick a register whose value starts with a 0 bit (e.g. BME280 chip ID 0x60)
// so the fault is deterministic. Disables the I2C peripheral while running
// and restores the pins' alternate function afterwards (the bus stays wedged).
//
// Returns true if the slave acknowledged and SDA ended up held low.
bool WedgeI2cBus(I2C_TypeDef *i2c, I2cBus::Pin scl, I2cBus::Pin sda, std::uint8_t address7Bit,
                 std::uint8_t reg);
