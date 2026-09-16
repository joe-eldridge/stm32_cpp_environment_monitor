#pragma once

#include <cstdint>

// What a register-addressed I2C sensor driver needs from the bus, and nothing
// more. Drivers depend on this interface rather than on I2C_TypeDef, so they
// can be unit-tested on a host PC against a fake device.
//
// A virtual call costs nanoseconds next to a multi-millisecond I2C transfer,
// so runtime polymorphism is the clearer choice here over templating every
// driver on its bus type.
//
// Methods are non-const, reads included: every call drives the bus, and for
// some devices a read also changes device state (e.g. clear-on-read flags).
class I2cDevice
{
public:
  [[nodiscard]] virtual bool WriteRegisters(std::uint8_t startReg, const std::uint8_t *data, std::uint8_t count) = 0;
  [[nodiscard]] virtual bool ReadRegisters(std::uint8_t startReg, std::uint8_t *buffer, std::uint8_t count) = 0;

  [[nodiscard]] bool WriteRegister(std::uint8_t reg, std::uint8_t value)
  {
    return WriteRegisters(reg, &value, 1);
  }

protected:
  // Never owned or deleted through this interface, so no virtual destructor.
  ~I2cDevice() = default;
};
