#pragma once

#include <cstdint>
#include <optional>

#include "i2c_device.hpp"

// Driver for the Vishay VEML7700 ambient light sensor over I2C.
//
// The sensor is kept in shutdown (~0.5uA) between samples and only powered
// for the duration of Read() - left running it draws ~45uA continuously.
class Veml7700
{
public:
  // VEML7700 has a fixed 7-bit I2C address.
  static constexpr std::uint8_t kI2cAddress7Bit = 0x10;

  struct Reading
  {
    // Hundredths of a lux (e.g. 2148 = 21.48 lx) - fixed-point, this MCU has
    // no hardware FPU.
    std::int32_t luxCenti;
    // The ADC hit full scale, so the true light level is at least luxCenti
    // (above ~4.4 klx with the gain/integration time used here).
    bool saturated;
  };

  explicit Veml7700(I2cDevice &device);

  // Configures gain/integration time and leaves the sensor shut down.
  [[nodiscard]] bool Init();

  // Powers the sensor up, waits for one full integration period (~115ms),
  // reads it, and shuts it down again.
  [[nodiscard]] std::optional<Reading> Read();

private:
  static constexpr std::uint8_t kRegAlsConf = 0x00;
  static constexpr std::uint8_t kRegAls = 0x04;

  // ALS_CONF_0: gain x1 (bits 12:11 = 00), integration time 100ms (bits 9:6
  // = 0000), persistence 1, interrupt disabled. Bit 0 is ALS_SD (shutdown).
  static constexpr std::uint16_t kConfig = 0x0000;
  static constexpr std::uint16_t kShutdown = 1u << 0;

  // Vishay application note 84323 (rev. 06-Mar-2025): wait 2.5ms after
  // clearing ALS_SD, and at least the integration time before reading a
  // result. The margin covers the sensor's internal oscillator tolerance.
  static constexpr std::uint32_t kPowerUpMs = 3;
  static constexpr std::uint32_t kIntegrationTimeMs = 100;
  static constexpr std::uint32_t kResultWaitMs = kPowerUpMs + kIntegrationTimeMs + kIntegrationTimeMs / 10;

  // Same application note, "Resolution and maximum detection range" table:
  // gain x1 at 100ms is 0.0672 lux/count, i.e. 6.72 centi-lux/count - kept as
  // an integer ratio to avoid float math. (The original datasheet's 0.0576,
  // still used by some libraries, reads ~14% low.)
  static constexpr std::int32_t kCentiLuxPerCountNum = 672;
  static constexpr std::int32_t kCentiLuxPerCountDen = 100;

  static constexpr std::uint16_t kFullScaleCount = 0xFFFF;

  [[nodiscard]] bool WriteConfig(std::uint16_t value);

  I2cDevice &device_;
};
