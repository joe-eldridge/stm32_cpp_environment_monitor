#pragma once

#include <cstdint>

#include "spi_device.hpp"
#include "timeout.hpp"
#include "stm32l0xx_hal.h"
#include "stm32l0xx_ll_spi.h"

// Thin wrapper around one SPI1 LL transaction plus this device's own CS pin.
// Unlike I2cBus, SPI has no bus-level addressing - each device on the shared
// SPI1 bus (eInk, SRAM, SD card) gets its own SpiBus instance pointed at its
// own CS pin, but all instances share the same underlying SPI1 peripheral.
class SpiBus final : public SpiDevice
{
public:
  SpiBus(SPI_TypeDef *spi, GPIO_TypeDef *csPort, std::uint16_t csPin);

  void Select();
  void Deselect();

  // Slows SPI1's clock to a rate safe for the SD card's native-mode init
  // sequence (must be <=400kHz per the SD spec) - shared across all devices
  // on the bus, so prefer SlowClockScope, which guarantees SetFast() is
  // restored on every exit path.
  void SetSlow();
  void SetFast();

  // Holds the shared bus at the slow rate for the lifetime of the scope. An
  // early return from a slow-clock sequence can't leave every other device
  // on the bus stuck at ~1/128 of its normal speed.
  class SlowClockScope
  {
  public:
    explicit SlowClockScope(SpiBus &bus) : bus_(bus)
    {
      bus_.SetSlow();
    }
    ~SlowClockScope()
    {
      bus_.SetFast();
    }
    SlowClockScope(const SlowClockScope &) = delete;
    SlowClockScope &operator=(const SlowClockScope &) = delete;

  private:
    SpiBus &bus_;
  };

  // Full-duplex byte transfer. Times out rather than spinning forever if the
  // SPI peripheral's flags never set (e.g. a clock misconfiguration).
  [[nodiscard]] bool TransferByte(std::uint8_t txByte, std::uint8_t &rxByte);

  // SpiDevice: one chip-select-framed write, e.g. a display command or image.
  [[nodiscard]] bool Write(const std::uint8_t *data, std::size_t length) override;

private:
  void SetPrescaler(std::uint32_t prescaler);
  void WaitUntilTransferComplete();
  bool TransferByteWithin(const Timeout &timeout, std::uint8_t txByte, std::uint8_t &rxByte);

  static constexpr std::uint32_t kByteTimeoutMs = 5;

  // One budget for a whole block rather than per byte. It is there to catch
  // a bus that has stopped, not to police how fast it runs, so it is loose:
  // a 5KB image takes well under a second even with the polling overhead.
  static constexpr std::uint32_t kBlockTimeoutMs = 5000;


  SPI_TypeDef *spi_;
  GPIO_TypeDef *csPort_;
  std::uint16_t csPin_;
};
