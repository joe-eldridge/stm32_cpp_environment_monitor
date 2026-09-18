#include "spi_bus.hpp"

#include "stm32l0xx_ll_dma.h"
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
  // The DMA channels are SPI1's, as CubeMX mapped them, so only SPI1 uses them.
  const bool ok = (spi_ == SPI1 && length >= kDmaMinimumBytes) ? WriteDma(data, length) : WritePolled(data, length);
  Deselect();
  return ok;
}

bool SpiBus::WriteDma(const std::uint8_t *data, std::size_t length)
{
  // CubeMX maps SPI1 onto DMA1 channel 3 for transmit and channel 2 for
  // receive, and sets their direction and transfer sizes, so each transfer
  // only needs its addresses and a count.
  //
  // Receive runs too, into a single byte that is overwritten each time.
  // Transmitting alone would leave every received byte unread, ending in an
  // overrun flag and a stale byte for the next device on the bus - the SD
  // card, which does read. It also gives the right completion signal: the
  // last byte received means the last byte has fully gone out.
  //
  // Completion is polled, and the DMA interrupts stay off: the generated
  // DMA1_Channel2_3 handler is empty, so an enabled interrupt would never
  // have its flag cleared and would take the MCU with it.
  constexpr std::uint32_t kTxChannel = LL_DMA_CHANNEL_3;
  constexpr std::uint32_t kRxChannel = LL_DMA_CHANNEL_2;
  const std::uint32_t dataRegister = reinterpret_cast<std::uint32_t>(&spi_->DR);
  std::uint8_t received = 0;

  LL_DMA_DisableChannel(DMA1, kRxChannel);
  LL_DMA_DisableChannel(DMA1, kTxChannel);
  LL_DMA_ClearFlag_GI2(DMA1);
  LL_DMA_ClearFlag_GI3(DMA1);

  LL_DMA_SetMemoryIncMode(DMA1, kRxChannel, LL_DMA_MEMORY_NOINCREMENT);
  LL_DMA_ConfigAddresses(DMA1, kRxChannel, dataRegister, reinterpret_cast<std::uint32_t>(&received),
                         LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
  LL_DMA_SetDataLength(DMA1, kRxChannel, static_cast<std::uint32_t>(length));
  LL_DMA_ConfigAddresses(DMA1, kTxChannel, reinterpret_cast<std::uint32_t>(data), dataRegister,
                         LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
  LL_DMA_SetDataLength(DMA1, kTxChannel, static_cast<std::uint32_t>(length));

  // The reference manual's order: receive requests first, then both
  // channels, then transmit requests - which, with the SPI already enabled,
  // is what starts the transfer.
  LL_SPI_EnableDMAReq_RX(spi_);
  LL_DMA_EnableChannel(DMA1, kRxChannel);
  LL_DMA_EnableChannel(DMA1, kTxChannel);
  LL_SPI_EnableDMAReq_TX(spi_);

  bool ok = true;
  const Timeout timeout(kBlockTimeoutMs);
  while (!LL_DMA_IsActiveFlag_TC2(DMA1))
  {
    if (LL_DMA_IsActiveFlag_TE2(DMA1) || LL_DMA_IsActiveFlag_TE3(DMA1) || timeout.Expired())
    {
      ok = false;
      break;
    }
  }

  // Put everything back whichever way that ended, so the next user of the
  // bus finds it as polled transfers expect it.
  LL_SPI_DisableDMAReq_TX(spi_);
  LL_SPI_DisableDMAReq_RX(spi_);
  LL_DMA_DisableChannel(DMA1, kTxChannel);
  LL_DMA_DisableChannel(DMA1, kRxChannel);
  LL_DMA_ClearFlag_GI2(DMA1);
  LL_DMA_ClearFlag_GI3(DMA1);
  WaitUntilTransferComplete();
  if (!ok)
  {
    // A transfer cut short can leave a byte unread, or an overrun; reading
    // DR and then SR clears both.
    static_cast<void>(LL_SPI_ReceiveData8(spi_));
    static_cast<void>(LL_SPI_IsActiveFlag_OVR(spi_));
  }
  return ok;
}

bool SpiBus::WritePolled(const std::uint8_t *data, std::size_t length)
{
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
  return ok;
}
