#include "sdcard.hpp"

#include "csd.hpp"

#include "timeout.hpp"

SdCard::SdCard(SpiBus &bus) : bus_(bus)
{
}

bool SdCard::SendCommand(std::uint8_t cmd, std::uint32_t arg, std::uint8_t crc, std::uint8_t &r1Out)
{
  std::uint8_t rx;
  // Eight idle clocks before the command frame. The spec requires at least
  // this much between one command's response and the next command (Nrc):
  // without it the card can still be letting go of the line as the command
  // byte arrives, and reads the frame out of step - which comes back as an
  // R1 with half the error bits set rather than as a clean failure. Cards
  // differ in how promptly they release, so this shows up on some and not
  // others.
  if (!bus_.TransferByte(0xFF, rx))
  {
    return false;
  }
  if (!bus_.TransferByte(static_cast<std::uint8_t>(0x40 | cmd), rx))
  {
    return false;
  }
  if (!bus_.TransferByte(static_cast<std::uint8_t>(arg >> 24), rx))
  {
    return false;
  }
  if (!bus_.TransferByte(static_cast<std::uint8_t>(arg >> 16), rx))
  {
    return false;
  }
  if (!bus_.TransferByte(static_cast<std::uint8_t>(arg >> 8), rx))
  {
    return false;
  }
  if (!bus_.TransferByte(static_cast<std::uint8_t>(arg), rx))
  {
    return false;
  }
  if (!bus_.TransferByte(crc, rx))
  {
    return false;
  }

  // Card may hold the line at 0xFF for a few bytes while it processes the
  // command before returning R1 - poll rather than assume the first byte back.
  Timeout timeout(kCommandResponseTimeoutMs);
  do
  {
    if (!bus_.TransferByte(0xFF, r1Out))
    {
      return false;
    }
  } while ((r1Out & 0x80) != 0 && !timeout.Expired());

  return (r1Out & 0x80) == 0;
}

bool SdCard::WaitReady(std::uint32_t timeoutMs)
{
  Timeout timeout(timeoutMs);
  std::uint8_t rx = 0;
  do
  {
    if (!bus_.TransferByte(0xFF, rx))
    {
      return false;
    }
  } while (rx != 0xFF && !timeout.Expired());
  return rx == 0xFF;
}

bool SdCard::ReadDataBlock(std::uint8_t *buffer, std::size_t length)
{
  std::uint8_t token = 0xFF;
  Timeout timeout(kDataTokenTimeoutMs);
  do
  {
    if (!bus_.TransferByte(0xFF, token))
    {
      return false;
    }
  } while (token == 0xFF && !timeout.Expired());

  if (token != kDataTokenSingle)
  {
    return false; // error token, or nothing ever arrived
  }

  for (std::size_t i = 0; i < length; ++i)
  {
    if (!bus_.TransferByte(0xFF, buffer[i]))
    {
      return false;
    }
  }

  // Trailing CRC16 - unused in SPI mode (CRC checking stays off), but the
  // card still clocks the two bytes out and expects them read.
  std::uint8_t crcByte;
  return bus_.TransferByte(0xFF, crcByte) && bus_.TransferByte(0xFF, crcByte);
}

bool SdCard::WriteDataBlock(const std::uint8_t *buffer, std::uint8_t token)
{
  std::uint8_t rx;
  if (!bus_.TransferByte(token, rx))
  {
    return false;
  }

  for (std::size_t i = 0; i < kSectorSize; ++i)
  {
    if (!bus_.TransferByte(buffer[i], rx))
    {
      return false;
    }
  }

  // Dummy CRC16 - card expects two bytes clocked regardless of CRC checking.
  if (!bus_.TransferByte(0xFF, rx) || !bus_.TransferByte(0xFF, rx))
  {
    return false;
  }

  std::uint8_t dataResponse;
  if (!bus_.TransferByte(0xFF, dataResponse))
  {
    return false;
  }
  if ((dataResponse & 0x1F) != 0x05)
  {
    return false; // data rejected (CRC or write error)
  }

  return WaitReady(kWriteTimeoutMs);
}

const char *SdCard::InitStageName(InitStage stage)
{
  switch (stage)
  {
  case InitStage::NotStarted:
    return "not started";
  case InitStage::DummyClocksFailed:
    return "power-up clocks";
  case InitStage::Cmd0Failed:
    return "CMD0 (no response)";
  case InitStage::Cmd0NotIdle:
    return "CMD0 (never idle)";
  case InitStage::Cmd8Failed:
    return "CMD8 (no response)";
  case InitStage::Cmd8NotIdle:
    return "CMD8 (bad response)";
  case InitStage::Cmd8EchoFailed:
    return "CMD8 echo (no response)";
  case InitStage::Cmd8EchoMismatch:
    return "CMD8 echo (mismatch)";
  case InitStage::AcmdFailed:
    return "ACMD41 (no response)";
  case InitStage::AcmdTimeout:
    return "ACMD41 (still busy)";
  case InitStage::OcrFailed:
    return "CMD58 (no response)";
  case InitStage::OcrReadFailed:
    return "CMD58 read";
  case InitStage::SetBlockLengthFailed:
    return "CMD16";
  case InitStage::Complete:
    return "complete";
  }
  return "unknown";
}

bool SdCard::Init()
{
  ready_ = false;
  lastInitStage_ = InitStage::NotStarted;
  const std::uint32_t startedMs = HAL_GetTick();

  // Scopes unwind in reverse: CS is released (plus its trailing clock byte)
  // at the slow rate, then the bus goes back to fast - on every exit path.
  const SpiBus::SlowClockScope slowClock(bus_);
  bus_.Deselect();

  std::uint8_t dummy;
  // >=74 clocks with CS held high: the card's power-up/native-mode
  // requirement before it will respond to any command.
  for (int i = 0; i < 10; ++i)
  {
    if (!bus_.TransferByte(0xFF, dummy))
    {
      lastInitStage_ = InitStage::DummyClocksFailed;
      return false;
    }
  }

  {
    const SelectScope select(bus_);
    const bool ok = InitSequence();
    lastInitMs_ = HAL_GetTick() - startedMs;
    if (!ok)
    {
      return false;
    }
  }

  ready_ = true;
  lastInitStage_ = InitStage::Complete;
  return true;
}

bool SdCard::InitSequence()
{
  std::uint8_t r1 = 0xFF;
  bool idle = false;
  {
    const Timeout timeout(kInitTimeoutMs);
    do
    {
      if (!SendCommand(kCmdGoIdleState, 0, 0x95, r1))
      {
        lastInitStage_ = InitStage::Cmd0Failed;
        return false;
      }
      idle = (r1 == kR1IdleState);
    } while (!idle && !timeout.Expired());
  }
  if (!idle)
  {
    lastInitStage_ = InitStage::Cmd0NotIdle;
    return false;
  }

  if (!SendCommand(kCmdSendIfCond, 0x1AA, 0x87, r1))
  {
    lastInitStage_ = InitStage::Cmd8Failed;
    return false;
  }

  // v2 cards (SDHC/SDXC) understand CMD8 and answer idle-only, echoing the
  // voltage/check pattern back. v1 cards (plain SDSC, or MMC) predate CMD8
  // and correctly answer it with the "illegal command" bit set rather than
  // failing outright - that's an expected response, not a comms error, and
  // means falling back to the v1 init path (no HCS bit, always byte-addressed).
  bool isV2 = false;
  if (r1 == kR1IdleState)
  {
    isV2 = true;
    std::uint8_t echo[4];
    for (auto &b : echo)
    {
      if (!bus_.TransferByte(0xFF, b))
      {
        lastInitStage_ = InitStage::Cmd8EchoFailed;
        return false;
      }
    }
    if (echo[2] != 0x01 || echo[3] != 0xAA)
    {
      lastInitStage_ = InitStage::Cmd8EchoMismatch; // echoed voltage/check pattern mismatch
      return false;
    }
  }
  else if ((r1 & 0x04) == 0)
  {
    // Neither a v2 ack nor a v1 "illegal command" - something else went wrong.
    lastInitStage_ = InitStage::Cmd8NotIdle;
    return false;
  }

  const std::uint32_t acmd41Arg = isV2 ? 0x40000000u : 0u; // HCS bit only meaningful to v2 cards
  {
    const Timeout timeout(kInitTimeoutMs);
    do
    {
      if (!SendCommand(kCmdAppCmd, 0, 0x01, r1) || !SendCommand(kCmdSdSendOpCond, acmd41Arg, 0x01, r1))
      {
        lastInitStage_ = InitStage::AcmdFailed;
        return false;
      }
    } while (r1 != 0x00 && !timeout.Expired());
  }
  if (r1 != 0x00)
  {
    lastInitStage_ = InitStage::AcmdTimeout;
    return false;
  }

  blockAddressed_ = false; // v1 cards are always byte-addressed
  if (isV2)
  {
    if (!SendCommand(kCmdReadOcr, 0, 0x01, r1) || r1 != 0x00)
    {
      lastInitStage_ = InitStage::OcrFailed;
      return false;
    }
    std::uint8_t ocr[4];
    for (auto &b : ocr)
    {
      if (!bus_.TransferByte(0xFF, b))
      {
        lastInitStage_ = InitStage::OcrReadFailed;
        return false;
      }
    }
    blockAddressed_ = (ocr[0] & 0x40) != 0; // CCS bit
  }

  // Byte-addressed (SDSC) cards have a settable block length; pin it to one
  // sector rather than relying on the card's default.
  if (!blockAddressed_)
  {
    if (!SendCommand(kCmdSetBlockLength, kSectorSize, 0x01, r1) || r1 != 0x00)
    {
      lastInitStage_ = InitStage::SetBlockLengthFailed;
      return false;
    }
  }

  return true;
}

bool SdCard::ReadSector(std::uint32_t sector, std::uint8_t *buffer512)
{
  if (!ready_)
  {
    return false; // never send data commands to a card that isn't initialised
  }

  const std::uint32_t address = blockAddressed_ ? sector : sector * kSectorSize;

  const SelectScope select(bus_);
  std::uint8_t r1;
  const bool ok = SendCommand(kCmdReadSingleBlock, address, 0x01, r1) && r1 == 0x00 &&
                  ReadDataBlock(buffer512, kSectorSize);
  if (!ok)
  {
    // Most likely the card was removed - force a re-init on next access.
    Deinit();
  }
  return ok;
}

bool SdCard::WriteSector(std::uint32_t sector, const std::uint8_t *buffer512)
{
  if (!ready_)
  {
    return false; // never send data commands to a card that isn't initialised
  }

  const std::uint32_t address = blockAddressed_ ? sector : sector * kSectorSize;

  const SelectScope select(bus_);
  std::uint8_t r1;
  const bool ok = SendCommand(kCmdWriteBlock, address, 0x01, r1) && r1 == 0x00 &&
                  WriteDataBlock(buffer512, kDataTokenSingle);
  if (!ok)
  {
    Deinit();
  }
  return ok;
}

std::optional<std::uint32_t> SdCard::ReadSectorCount()
{
  if (!ready_)
  {
    return std::nullopt;
  }

  std::uint8_t csd[16];
  {
    const SelectScope select(bus_);
    std::uint8_t r1;
    if (!SendCommand(kCmdSendCsd, 0, 0x01, r1) || r1 != 0x00 || !ReadDataBlock(csd, sizeof(csd)))
    {
      Deinit();
      return std::nullopt;
    }
  }

  return sdcard::SectorCountFromCsd(csd);
}
