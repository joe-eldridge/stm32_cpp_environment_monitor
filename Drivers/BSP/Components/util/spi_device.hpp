#pragma once

#include <cstddef>
#include <cstdint>

// What a write-only SPI peripheral driver (e.g. a display) needs from the
// bus. Like I2cDevice, it keeps drivers independent of the STM32 peripheral
// so they can be unit-tested on a host.
class SpiDevice
{
public:
  // One complete transaction: asserts chip select, sends `length` bytes,
  // releases chip select. Anything received is discarded.
  [[nodiscard]] virtual bool Write(const std::uint8_t *data, std::size_t length) = 0;

protected:
  ~SpiDevice() = default;
};
