#include "veml7700.hpp"

#include "delay.hpp"

Veml7700::Veml7700(I2cDevice &device) : device_(device)
{
}

bool Veml7700::WriteConfig(std::uint16_t value)
{
  // 16-bit registers are little-endian on the wire.
  const std::uint8_t payload[2] = {
      static_cast<std::uint8_t>(value & 0xFF),
      static_cast<std::uint8_t>(value >> 8),
  };
  return device_.WriteRegisters(kRegAlsConf, payload, sizeof(payload));
}

bool Veml7700::Init()
{
  return WriteConfig(kConfig | kShutdown);
}

std::optional<Veml7700::Reading> Veml7700::Read()
{
  if (!WriteConfig(kConfig))
  {
    return std::nullopt;
  }
  DelayMs(kResultWaitMs);

  std::uint8_t raw[2];
  const bool readOk = device_.ReadRegisters(kRegAls, raw, sizeof(raw));

  // Shut down whether or not the read worked. If this write fails the sensor
  // stays powered until the next Read() succeeds in shutting it down - the
  // reading itself is still valid, so it isn't discarded.
  static_cast<void>(WriteConfig(kConfig | kShutdown));

  if (!readOk)
  {
    return std::nullopt;
  }

  const auto count = static_cast<std::uint16_t>(raw[0] | (raw[1] << 8));
  Reading reading{};
  reading.luxCenti =
      (static_cast<std::int32_t>(count) * kCentiLuxPerCountNum + kCentiLuxPerCountDen / 2) / kCentiLuxPerCountDen;
  reading.saturated = (count == kFullScaleCount);
  return reading;
}
