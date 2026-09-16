#pragma once

#include <cstdint>
#include <optional>

#include "i2c_device.hpp"

// Driver for the Bosch BME280 temperature/pressure/humidity sensor over I2C.
//
// Uses forced mode: the sensor sleeps (~0.1uA) and takes one measurement
// only when Read() asks for it - Bosch's recommended setting for weather
// monitoring, and far cheaper than normal mode's continuous conversions at
// a once-a-minute sample rate.
//
// Fixed-point throughout, in hundredths of each unit (e.g. 2345 =
// 23.45 degC) - this MCU has no hardware FPU, so floats would pull in the
// soft-float runtime and add per-call overhead for no benefit.
class Bme280
{
public:
  struct Measurements
  {
    std::int32_t temperatureCenti;  // hundredths of a degree C
    std::int32_t pressureCentiHpa;  // hundredths of an hPa
    std::int32_t humidityCentiPct;  // hundredths of a %RH
  };

  static constexpr std::uint8_t kI2cAddressSdoLow = 0x76;
  static constexpr std::uint8_t kI2cAddressSdoHigh = 0x77;

  explicit Bme280(I2cDevice &device);

  // Soft-resets the sensor, loads its calibration and leaves it asleep.
  [[nodiscard]] bool Init();

  // Triggers one forced measurement and blocks until it's done (~10ms). The
  // sensor returns to sleep by itself afterwards.
  [[nodiscard]] std::optional<Measurements> Read();

private:
  struct Calibration
  {
    std::uint16_t digT1;
    std::int16_t digT2;
    std::int16_t digT3;
    std::uint16_t digP1;
    std::int16_t digP2;
    std::int16_t digP3;
    std::int16_t digP4;
    std::int16_t digP5;
    std::int16_t digP6;
    std::int16_t digP7;
    std::int16_t digP8;
    std::int16_t digP9;
    std::uint8_t digH1;
    std::int16_t digH2;
    std::uint8_t digH3;
    std::int16_t digH4;
    std::int16_t digH5;
    std::int8_t digH6;
  };

  static constexpr std::uint8_t kRegChipId = 0xD0;
  static constexpr std::uint8_t kRegCalib00 = 0x88;
  static constexpr std::uint8_t kRegCalib26 = 0xE1;
  static constexpr std::uint8_t kRegReset = 0xE0;
  static constexpr std::uint8_t kRegCtrlHum = 0xF2;
  static constexpr std::uint8_t kRegStatus = 0xF3;
  static constexpr std::uint8_t kRegCtrlMeas = 0xF4;
  static constexpr std::uint8_t kRegConfig = 0xF5;
  static constexpr std::uint8_t kRegData = 0xF7;
  static constexpr std::uint8_t kChipIdValue = 0x60;
  static constexpr std::uint8_t kResetCommand = 0xB6;

  static constexpr std::uint8_t kStatusImUpdate = 1u << 0; // NVM calibration still being copied
  static constexpr std::uint8_t kStatusMeasuring = 1u << 3;

  static constexpr std::uint8_t kOversamplingX1 = 0x01;
  static constexpr std::uint8_t kModeSleep = 0x00;
  static constexpr std::uint8_t kModeForced = 0x01;
  static constexpr std::uint8_t kCtrlMeasOversampling =
      static_cast<std::uint8_t>((kOversamplingX1 << 5) | (kOversamplingX1 << 2)); // osrs_t, osrs_p

  // A channel the sensor didn't measure (e.g. read before the conversion
  // finished) reads back as these reset values rather than as real data.
  static constexpr std::int32_t kAdcSkipped20Bit = 0x80000;
  static constexpr std::int32_t kAdcSkipped16Bit = 0x8000;

  // Datasheet start-up time after power-on/reset.
  static constexpr std::uint32_t kStartupTimeMs = 2;
  // Datasheet 9.1 maximum measurement time with x1 oversampling on all three
  // channels: 1.25 + 2.3 + (2.3 + 0.575) * 2 = 9.3ms.
  static constexpr std::uint32_t kMaxMeasurementTimeMs = 10;
  // Upper bound on waiting for a status bit, well beyond either figure above.
  static constexpr std::uint32_t kStatusTimeoutMs = 20;

  [[nodiscard]] bool WaitForStatusClear(std::uint8_t bit);

  // Bosch's official compensation formulas (fixed-point, from the BME280
  // datasheet). tFine is an intermediate the pressure/humidity formulas
  // depend on - threaded through explicitly as an out-param rather than
  // stored as mutable state on the object.
  std::int32_t CompensateTemperature(std::int32_t adcT, std::int32_t &tFineOut) const;
  std::uint32_t CompensatePressure(std::int32_t adcP, std::int32_t tFine) const;
  std::uint32_t CompensateHumidity(std::int32_t adcH, std::int32_t tFine) const;

  I2cDevice &device_;
  Calibration calib_{};
};
