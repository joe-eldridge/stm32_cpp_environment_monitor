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

void SpiBus::SetPrescaler(std::uint32_t prescaler)
{
  // BR bits are only writable while SPE=0, per the reference manual.
  LL_SPI_Disable(spi_);
  LL_SPI_SetBaudRatePrescaler(spi_, prescaler);
  LL_SPI_Enable(spi_);
}

void SpiBus::SetSlow()
{
  // MSI-derived SPI1 kernel clock (~2.1MHz) / 256 ~= 8kHz - comfortably
  // under the SD card's 400kHz native-mode init ceiling.
  SetPrescaler(LL_SPI_BAUDRATEPRESCALER_DIV256);
}

void SpiBus::SetFast()
{
  // Matches the DIV2 prescaler MX_SPI1_Init() already configures for
  // eInk/SRAM transfers (~1.05MHz), well within the SD card's 25MHz cap.
  SetPrescaler(LL_SPI_BAUDRATEPRESCALER_DIV2);
}

bool SpiBus::TransferByte(std::uint8_t txByte, std::uint8_t &rxByte)
{
  Timeout timeout(kByteTimeoutMs);
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
  for (std::size_t i = 0; i < length && ok; ++i)
  {
    // Full duplex: reading each byte back keeps RXNE clear, so no overrun
    // is left behind for the next device on the bus (the SD card reads).
    ok = TransferByte(data[i], discarded);
  }
  Deselect();
  return ok;
}
