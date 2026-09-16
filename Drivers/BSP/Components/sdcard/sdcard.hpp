#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "spi_bus.hpp"

// SD card driver, SPI mode, sector I/O for FatFs's diskio layer. Supports
// both v2 cards (SDHC/SDXC, block-addressed, CMD8-aware) and v1 cards
// (SDSC, byte-addressed, predates CMD8 and answers it with "illegal
// command" instead) - not plain MMC, which uses a different op-cond command.
class SdCard
{
public:
  // Diagnostic only - reports which init step last failed, for logging.
  enum class InitStage : std::uint8_t
  {
    NotStarted,
    DummyClocksFailed,
    Cmd0Failed,
    Cmd0NotIdle,
    Cmd8Failed,
    Cmd8NotIdle,
    Cmd8EchoFailed,
    Cmd8EchoMismatch,
    AcmdFailed,
    AcmdTimeout,
    OcrFailed,
    OcrReadFailed,
    SetBlockLengthFailed,
    Complete,
  };

  // `bus` must outlive the driver; it's this card's own SpiBus (its own CS
  // pin), though the underlying SPI peripheral may be shared.
  explicit SdCard(SpiBus &bus);

  [[nodiscard]] bool Init();

  // Marks the card as not initialised, so FatFs re-runs Init() before any
  // further I/O. Call after f_unmount() when the user ejects the card; also
  // happens automatically after any failed read/write, so a removed and
  // re-inserted card is picked up again on the next access.
  void Deinit()
  {
    ready_ = false;
  }

  [[nodiscard]] bool IsReady() const
  {
    return ready_;
  }
  InitStage GetLastInitStage() const
  {
    return lastInitStage_;
  }

  [[nodiscard]] bool ReadSector(std::uint32_t sector, std::uint8_t *buffer512);
  [[nodiscard]] bool WriteSector(std::uint32_t sector, const std::uint8_t *buffer512);

  // Card capacity in 512-byte sectors, from its CSD register (needed by
  // FatFs's f_mkfs).
  [[nodiscard]] std::optional<std::uint32_t> ReadSectorCount();

private:
  // Asserts CS for the lifetime of the scope. On exit, deasserts it and
  // clocks one extra byte: SD cards only release MISO on the clock edge
  // after CS goes high, which matters on a bus shared with other devices.
  class SelectScope
  {
  public:
    explicit SelectScope(SpiBus &bus) : bus_(bus)
    {
      bus_.Select();
    }
    ~SelectScope()
    {
      bus_.Deselect();
      std::uint8_t dummy;
      static_cast<void>(bus_.TransferByte(0xFF, dummy));
    }
    SelectScope(const SelectScope &) = delete;
    SelectScope &operator=(const SelectScope &) = delete;

  private:
    SpiBus &bus_;
  };

  [[nodiscard]] bool InitSequence();

  [[nodiscard]] bool SendCommand(std::uint8_t cmd, std::uint32_t arg, std::uint8_t crc, std::uint8_t &r1Out);
  [[nodiscard]] bool WaitReady(std::uint32_t timeoutMs);
  [[nodiscard]] bool ReadDataBlock(std::uint8_t *buffer, std::size_t length);
  [[nodiscard]] bool WriteDataBlock(const std::uint8_t *buffer, std::uint8_t token);

  static constexpr std::uint8_t kCmdGoIdleState = 0;
  static constexpr std::uint8_t kCmdSendIfCond = 8;
  static constexpr std::uint8_t kCmdSendCsd = 9;
  static constexpr std::uint8_t kCmdSetBlockLength = 16;
  static constexpr std::uint8_t kCmdReadSingleBlock = 17;
  static constexpr std::uint8_t kCmdWriteBlock = 24;
  static constexpr std::uint8_t kCmdAppCmd = 55;
  static constexpr std::uint8_t kCmdSdSendOpCond = 41;
  static constexpr std::uint8_t kCmdReadOcr = 58;

  static constexpr std::uint8_t kR1IdleState = 0x01;
  static constexpr std::uint8_t kDataTokenSingle = 0xFE;
  static constexpr std::uint32_t kSectorSize = 512;

  static constexpr std::uint32_t kInitTimeoutMs = 1000;
  static constexpr std::uint32_t kCommandResponseTimeoutMs = 100;
  static constexpr std::uint32_t kDataTokenTimeoutMs = 200;
  static constexpr std::uint32_t kWriteTimeoutMs = 500;

  SpiBus &bus_;
  bool blockAddressed_ = false; // false = SDSC (byte addresses), true = SDHC/SDXC (block addresses)
  bool ready_ = false;
  InitStage lastInitStage_ = InitStage::NotStarted;
};
