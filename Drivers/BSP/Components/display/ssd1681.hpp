#pragma once

#include <cstddef>
#include <cstdint>

#include "gpio_pin.hpp"
#include "spi_device.hpp"

// Driver for the Solomon Systech SSD1681 e-paper controller, as used on
// Adafruit's 1.54" 200x200 monochrome breakout. Command values and the
// operating sequence follow the SSD1681 datasheet (rev 0.13); the panel
// geometry settings match Adafruit's driver for this breakout.
//
// Images are 1 bit per pixel, 1 = white, MSB leftmost, rows top to bottom -
// the layout MonoFramebuffer produces.
class Ssd1681
{
public:
  static constexpr std::uint16_t kWidth = 200;
  static constexpr std::uint16_t kHeight = 200;
  static constexpr std::size_t kBytesPerRow = (kWidth + 7u) / 8u;
  static constexpr std::size_t kImageBytes = kBytesPerRow * kHeight;

  // A band of rows to write and refresh, both ends included. Sending only
  // the rows that changed is worth doing: at this bus speed a whole image
  // takes longer to send than the panel takes to show it.
  struct RowRange
  {
    std::uint16_t first;
    std::uint16_t last;

    static constexpr RowRange All()
    {
      return RowRange{0, kHeight - 1};
    }

    constexpr std::uint16_t Rows() const
    {
      return static_cast<std::uint16_t>(last - first + 1);
    }

    constexpr std::size_t Bytes() const
    {
      return Rows() * kBytesPerRow;
    }
  };

  enum class RefreshMode : std::uint8_t
  {
    // Whole-panel waveform: slow (seconds), flashes, clears ghosting.
    Full,
    // Only changed pixels are driven, using the previous image in RED RAM as
    // the reference. Fast and flicker-free, but ghosting builds up.
    Partial,
  };

  Ssd1681(SpiDevice &spi, OutputPin &dataCommand, OutputPin &reset, InputPin &busy);

  // Hardware reset (which is also the only way out of deep sleep), software
  // reset, then panel configuration. Leaves the controller awake.
  [[nodiscard]] bool Wake();

  // The image to show on the next refresh (BW RAM, command 0x24). `image`
  // always points at a whole 200x200 image; only `rows` of it are sent.
  // Sending fewer rows shortens the transfer but not the refresh: measured,
  // the panel takes the same time whatever the window.
  [[nodiscard]] bool WriteImage(const std::uint8_t *image, RowRange rows = RowRange::All());

  // The image currently on the panel (RED RAM, command 0x26). A partial
  // refresh drives only the pixels that differ between the two.
  [[nodiscard]] bool WritePreviousImage(const std::uint8_t *image, RowRange rows = RowRange::All());

  // Drives the panel and blocks until the controller reports it's done.
  [[nodiscard]] bool Refresh(RefreshMode mode);

  // How long the last Refresh() spent waiting for the panel, in milliseconds.
  // The waveform time is the panel's own and can't be shortened from here, so
  // it's worth telling apart from the time spent sending images to it.
  std::uint32_t LastRefreshMs() const
  {
    return lastRefreshMs_;
  }

  // The tick at which the last refresh finished - the moment the new image
  // was actually on the panel. Anything sent after it doesn't delay what the
  // user sees, so it's the end point for measuring responsiveness.
  std::uint32_t LastRefreshEndedAt() const
  {
    return lastRefreshEndedAt_;
  }

  // Deep sleep mode 1: about 1 uA, RAM contents kept. Only Wake() exits it.
  [[nodiscard]] bool Sleep();

private:
  [[nodiscard]] bool Command(std::uint8_t command);
  [[nodiscard]] bool Command(std::uint8_t command, const std::uint8_t *data, std::size_t length);
  [[nodiscard]] bool WriteRam(std::uint8_t ramCommand, const std::uint8_t *image, RowRange rows);
  [[nodiscard]] bool WaitUntilIdle(std::uint32_t timeoutMs);

  static constexpr std::uint8_t kCmdDriverOutputControl = 0x01;
  static constexpr std::uint8_t kCmdDeepSleepMode = 0x10;
  static constexpr std::uint8_t kCmdDataEntryMode = 0x11;
  static constexpr std::uint8_t kCmdSoftwareReset = 0x12;
  static constexpr std::uint8_t kCmdTemperatureSensor = 0x18;
  static constexpr std::uint8_t kCmdMasterActivation = 0x20;
  static constexpr std::uint8_t kCmdDisplayUpdateControl2 = 0x22;
  static constexpr std::uint8_t kCmdWriteBlackWhiteRam = 0x24;
  static constexpr std::uint8_t kCmdWriteRedRam = 0x26;
  static constexpr std::uint8_t kCmdBorderWaveform = 0x3C;
  static constexpr std::uint8_t kCmdRamXWindow = 0x44;
  static constexpr std::uint8_t kCmdRamYWindow = 0x45;
  static constexpr std::uint8_t kCmdRamXCounter = 0x4E;
  static constexpr std::uint8_t kCmdRamYCounter = 0x4F;

  // 0x22 sequences: clock + analog on, load temperature, display with
  // DISPLAY Mode 1 (full) or Mode 2 (partial), then analog + oscillator off.
  static constexpr std::uint8_t kUpdateFull = 0xF7;
  static constexpr std::uint8_t kUpdatePartial = 0xFF;
  static constexpr std::uint8_t kDeepSleepMode1 = 0x01;

  // Datasheet operation flow: 10 ms after power-on / reset steps.
  static constexpr std::uint32_t kResetStepMs = 10;
  // BUSY is sampled this often while waiting. Each wait overshoots by up to
  // one interval, so a coarse poll adds that much to every reset and every
  // refresh. The wait sleeps between samples, so polling often costs little.
  static constexpr std::uint32_t kBusyPollMs = 1;
  static constexpr std::uint32_t kResetTimeoutMs = 1000;
  // A full refresh takes a few seconds; this only bounds a stuck BUSY line.
  static constexpr std::uint32_t kRefreshTimeoutMs = 10000;

  std::uint32_t lastRefreshMs_ = 0;
  std::uint32_t lastRefreshEndedAt_ = 0;

  SpiDevice &spi_;
  OutputPin &dataCommand_;
  OutputPin &reset_;
  InputPin &busy_;
};
