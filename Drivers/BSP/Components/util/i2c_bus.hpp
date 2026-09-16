#pragma once

#include <cstdint>

#include "i2c_device.hpp"
#include "stm32l0xx_ll_gpio.h"
#include "stm32l0xx_ll_i2c.h"
#include "timeout.hpp"

// One I2C peripheral (and the bus wired to it), driven through ST's LL API.
// Each register read/write burst is a single START..STOP transaction, and any
// timeout triggers bus recovery so one glitch can't wedge every device on the
// bus until the next reset.
class I2cBus
{
public:
  struct Pin
  {
    GPIO_TypeDef *port;
    std::uint32_t pinMask; // LL_GPIO_PIN_x
  };

  // scl/sda must be the pins MX_I2C1_Init() (or equivalent) already put in
  // open-drain alternate-function mode - recovery temporarily takes them over.
  I2cBus(I2C_TypeDef *i2c, Pin scl, Pin sda);

  [[nodiscard]] bool WriteRegisters(std::uint8_t address7Bit, std::uint8_t startReg, const std::uint8_t *data,
                                    std::uint8_t count);
  [[nodiscard]] bool ReadRegisters(std::uint8_t address7Bit, std::uint8_t startReg, std::uint8_t *buffer,
                                   std::uint8_t count);

  // Resets the peripheral's state machine and, if a slave is still holding
  // SDA low (typically one interrupted mid-read by an MCU reset), clocks it
  // free and sends a STOP. Returns true if both lines end up released.
  bool Recover();

private:
  enum class WaitResult : std::uint8_t
  {
    Ready,
    Nack,
    TimedOut,
  };

  template <typename Predicate>
  WaitResult WaitUntil(Predicate ready, const Timeout &timeout);

  [[nodiscard]] bool WaitForIdleBus();
  void StartTransfer(std::uint8_t address7Bit, std::uint8_t count, std::uint32_t endMode, std::uint32_t request);
  [[nodiscard]] bool Fail(WaitResult result);
  void FinishAfterNack();
  void ClockOutStuckSlave();

  // Largest data burst any current caller writes (DS3231's 7-byte DateTime),
  // excluding the register-pointer byte.
  static constexpr std::uint8_t kMaxWriteBurst = 7;

  // Generous for a handful of bytes at typical I2C speeds; only meant to
  // bound a genuinely stuck bus, not to be tight against normal transaction
  // time.
  static constexpr std::uint32_t kTimeoutMs = 10;

  // A slave can be at most 8 data bits plus an ACK away from releasing SDA.
  static constexpr std::uint8_t kRecoveryClockPulses = 9;

  I2C_TypeDef *i2c_;
  Pin scl_;
  Pin sda_;
};

// Binds one slave address to a shared I2cBus, giving a driver the plain
// I2cDevice interface it needs.
class I2cBusDevice final : public I2cDevice
{
public:
  I2cBusDevice(I2cBus &bus, std::uint8_t address7Bit) : bus_(bus), address7Bit_(address7Bit)
  {
  }

  [[nodiscard]] bool WriteRegisters(std::uint8_t startReg, const std::uint8_t *data, std::uint8_t count) override
  {
    return bus_.WriteRegisters(address7Bit_, startReg, data, count);
  }

  [[nodiscard]] bool ReadRegisters(std::uint8_t startReg, std::uint8_t *buffer, std::uint8_t count) override
  {
    return bus_.ReadRegisters(address7Bit_, startReg, buffer, count);
  }

private:
  I2cBus &bus_;
  std::uint8_t address7Bit_;
};
