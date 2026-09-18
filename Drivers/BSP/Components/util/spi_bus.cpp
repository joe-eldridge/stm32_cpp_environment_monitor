#include "spi_bus.hpp"

#include "timeout.hpp"

SpiBus::SpiBus(SPI_TypeDef *spi, GPIO_TypeDef *csPort, std::uint16_t csPin)
    : spi_(spi), csPort_(csPort), csPin_(csPin)
{
}

void SpiBus::Select()
{
  // MX_SPI1_Init() configures SPI1 but leaves it disabled (SPE=0), and a
  // disabled SPI never clocks anything out. Enable it here rather than rely
  // on some other device's code (the SD card's speed change) running first.
  if (!LL_SPI_IsEnabled(spi_))
  {
    LL_SPI_Enable(spi_);
  }
  HAL_GPIO_WritePin(csPort_, csPin_, GPIO_PIN_RESET);
}

void SpiBus::Deselect()
{
  HAL_GPIO_WritePin(csPort_, csPin_, GPIO_PIN_SET);
}

void SpiBus::WaitUntilTransferComplete()
{
  // The reference manual's procedure for disabling SPI: the last frame must
  // have finished shifting out (TXE set, BSY clear) first. Clearing SPE part
  // way through a frame stops the clock mid-byte, which leaves the peripheral
  // and whatever it was talking to out of step - the card's next command is
  // misread, and the display misses the commands after it.
  //
  // Every transfer here ends by reading the byte back, so RXNE is already
  // clear, which is the other half of that procedure.
  const Timeout timeout(kByteTimeoutMs);
  while (!LL_SPI_IsActiveFlag_TXE(spi_) || LL_SPI_IsActiveFlag_BSY(spi_))
  {
    if (timeout.Expired())
    {
      return; // a peripheral this stuck won't be fixed by waiting longer
    }
  }
}

void SpiBus::SetPrescaler(std::uint32_t prescaler)
{
  // BR bits are only writable while SPE=0, per the reference manual.
  WaitUntilTransferComplete();
  LL_SPI_Disable(spi_);
  LL_SPI_SetBaudRatePrescaler(spi_, prescaler);
  LL_SPI_Enable(spi_);
}

void SpiBus::SetSlow()
{
  // The SD spec gives identification mode a window, not a ceiling: 100kHz to
  // 400kHz. From a ~2.1MHz kernel clock, /8 lands at ~262kHz, in the middle
  // of it. /256 (~8kHz) is not "even safer" - it is below the minimum, and
  // cards differ in whether they tolerate that.
  SetPrescaler(LL_SPI_BAUDRATEPRESCALER_DIV8);
}

void SpiBus::SetFast()
{
  // Matches the DIV2 prescaler MX_SPI1_Init() already configures for
  // eInk/SRAM transfers (~1.05MHz), well within the SD card's 25MHz cap.
  SetPrescaler(LL_SPI_BAUDRATEPRESCALER_DIV2);
}

bool SpiBus::TransferByte(std::uint8_t txByte, std::uint8_t &rxByte)
{
  const Timeout timeout(kByteTimeoutMs);
  return TransferByteWithin(timeout, txByte, rxByte);
}

bool SpiBus::TransferByteWithin(const Timeout &timeout, std::uint8_t txByte, std::uint8_t &rxByte)
{
  while (!LL_SPI_IsActiveFlag_TXE(spi_))
  {
    if (timeout.Expired())
    {
      return false;
    }
  }
  LL_SPI_TransmitData8(spi_, txByte);

  while (!LL_SPI_IsActiveFlag_RXNE(spi_))
  {
    if (timeout.Expired())
    {
      return false;
    }
  }
  rxByte = LL_SPI_ReceiveData8(spi_);
  return true;
}

bool SpiBus::Write(const std::uint8_t *data, std::size_t length)
{
  Select();
  bool ok = true;
  std::uint8_t discarded;
  // One Timeout for the whole block: built per byte, its HAL_GetTick() calls
  // cost more than the transfer they are guarding when 5KB goes out at a time.
  const Timeout timeout(kBlockTimeoutMs);
  for (std::size_t i = 0; i < length && ok; ++i)
  {
    // Full duplex: reading each byte back keeps RXNE clear, so no overrun
    // is left behind for the next device on the bus (the SD card reads).
    ok = TransferByteWithin(timeout, data[i], discarded);
  }
  Deselect();
  return ok;
}
