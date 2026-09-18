#include "ssd1681.hpp"

#include "delay.hpp"
#include "timeout.hpp"

namespace
{
constexpr std::uint16_t kLastRow = Ssd1681::kHeight - 1;
constexpr std::uint8_t kLastColumnByte = static_cast<std::uint8_t>((Ssd1681::kWidth + 7u) / 8u - 1u);
} // namespace

Ssd1681::Ssd1681(SpiDevice &spi, OutputPin &dataCommand, OutputPin &reset, InputPin &busy)
    : spi_(spi), dataCommand_(dataCommand), reset_(reset), busy_(busy)
{
}

bool Ssd1681::Command(std::uint8_t command)
{
  dataCommand_.Write(false);
  return spi_.Write(&command, 1);
}

bool Ssd1681::Command(std::uint8_t command, const std::uint8_t *data, std::size_t length)
{
  if (!Command(command))
  {
    return false;
  }
  dataCommand_.Write(true);
  return spi_.Write(data, length);
}

bool Ssd1681::WaitUntilIdle(std::uint32_t timeoutMs)
{
  const Timeout timeout(timeoutMs);
  while (busy_.IsHigh())
  {
    if (timeout.Expired())
    {
      return false;
    }
    DelayMs(kBusyPollMs);
  }
  return true;
}

bool Ssd1681::Wake()
{
  // Every update starts here, so this is where the last update's refresh
  // timing stops being true.
  lastRefreshMs_ = 0;

  reset_.Write(false);
  DelayMs(kResetStepMs);
  reset_.Write(true);
  DelayMs(kResetStepMs);

  if (!Command(kCmdSoftwareReset))
  {
    return false;
  }
  DelayMs(kResetStepMs);
  if (!WaitUntilIdle(kResetTimeoutMs))
  {
    return false;
  }

  // Gate lines: rows 0..199, default scan direction.
  const std::uint8_t driverOutput[] = {static_cast<std::uint8_t>(kLastRow & 0xFF),
                                       static_cast<std::uint8_t>(kLastRow >> 8), 0x00};
  // Address counters increment in X then Y, i.e. plain raster order.
  const std::uint8_t dataEntry[] = {0x03};
  // RAM window: X is in bytes (0..24), Y in rows (0..199).
  const std::uint8_t xWindow[] = {0x00, kLastColumnByte};
  const std::uint8_t yWindow[] = {0x00, 0x00, static_cast<std::uint8_t>(kLastRow & 0xFF),
                                  static_cast<std::uint8_t>(kLastRow >> 8)};
  const std::uint8_t border[] = {0x05};
  const std::uint8_t internalTemperatureSensor[] = {0x80};

  return Command(kCmdDriverOutputControl, driverOutput, sizeof(driverOutput)) &&
         Command(kCmdDataEntryMode, dataEntry, sizeof(dataEntry)) &&
         Command(kCmdRamXWindow, xWindow, sizeof(xWindow)) &&
         Command(kCmdRamYWindow, yWindow, sizeof(yWindow)) &&
         Command(kCmdBorderWaveform, border, sizeof(border)) &&
         Command(kCmdTemperatureSensor, internalTemperatureSensor, sizeof(internalTemperatureSensor));
}

bool Ssd1681::WriteRam(std::uint8_t ramCommand, const std::uint8_t *image, RowRange rows)
{
  // The Y window bounds the RAM write. X stays the full width, as set in
  // Wake().
  const std::uint8_t yWindow[] = {static_cast<std::uint8_t>(rows.first & 0xFF),
                                  static_cast<std::uint8_t>(rows.first >> 8),
                                  static_cast<std::uint8_t>(rows.last & 0xFF),
                                  static_cast<std::uint8_t>(rows.last >> 8)};
  const std::uint8_t xStart[] = {0x00};
  const std::uint8_t yStart[] = {static_cast<std::uint8_t>(rows.first & 0xFF),
                                 static_cast<std::uint8_t>(rows.first >> 8)};
  return Command(kCmdRamYWindow, yWindow, sizeof(yWindow)) && Command(kCmdRamXCounter, xStart, sizeof(xStart)) &&
         Command(kCmdRamYCounter, yStart, sizeof(yStart)) &&
         Command(ramCommand, image + rows.first * kBytesPerRow, rows.Bytes());
}

bool Ssd1681::WriteImage(const std::uint8_t *image, RowRange rows)
{
  return WriteRam(kCmdWriteBlackWhiteRam, image, rows);
}

bool Ssd1681::WritePreviousImage(const std::uint8_t *image, RowRange rows)
{
  return WriteRam(kCmdWriteRedRam, image, rows);
}

bool Ssd1681::Refresh(RefreshMode mode)
{
  const std::uint8_t sequence[] = {mode == RefreshMode::Full ? kUpdateFull : kUpdatePartial};
  if (!Command(kCmdDisplayUpdateControl2, sequence, sizeof(sequence)) || !Command(kCmdMasterActivation))
  {
    return false;
  }
  const std::uint32_t startedMs = HAL_GetTick();
  const bool ok = WaitUntilIdle(kRefreshTimeoutMs);
  lastRefreshEndedAt_ = HAL_GetTick();
  lastRefreshMs_ = lastRefreshEndedAt_ - startedMs;
  return ok;
}

bool Ssd1681::Sleep()
{
  // No busy wait afterwards: the datasheet says BUSY stays high for as long
  // as the controller is in deep sleep.
  const std::uint8_t mode[] = {kDeepSleepMode1};
  return Command(kCmdDeepSleepMode, mode, sizeof(mode));
}
