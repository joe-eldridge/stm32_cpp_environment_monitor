#include "i2c_fault_injection.hpp"

#include "delay.hpp"

namespace
{

// Open-drain lines: "high" means released to the pull-up.
void Release(const I2cBus::Pin &pin)
{
  LL_GPIO_SetOutputPin(pin.port, pin.pinMask);
}

void Drive(const I2cBus::Pin &pin)
{
  LL_GPIO_ResetOutputPin(pin.port, pin.pinMask);
}

bool IsHigh(const I2cBus::Pin &pin)
{
  return LL_GPIO_IsInputPinSet(pin.port, pin.pinMask) != 0;
}

// ~1ms half-periods: slow, but every slave on the bus accepts it and it
// needs no timebase beyond HAL_Delay.
void HalfPeriod()
{
  DelayMs(1);
}

class BitBanger
{
public:
  BitBanger(I2cBus::Pin scl, I2cBus::Pin sda) : scl_(scl), sda_(sda)
  {
  }

  // SDA falls while SCL is high. Also works as a repeated start when called
  // with SCL low mid-transaction.
  void Start()
  {
    Release(sda_);
    HalfPeriod();
    Release(scl_);
    HalfPeriod();
    Drive(sda_);
    HalfPeriod();
    Drive(scl_);
    HalfPeriod();
  }

  // Sends one byte MSB first, then clocks the ACK bit. Returns true if the
  // slave pulled SDA low (ACK). Leaves SCL low.
  bool WriteByte(std::uint8_t value)
  {
    for (int bit = 7; bit >= 0; --bit)
    {
      if ((value >> bit) & 1u)
      {
        Release(sda_);
      }
      else
      {
        Drive(sda_);
      }
      HalfPeriod();
      Release(scl_);
      HalfPeriod();
      Drive(scl_);
    }

    Release(sda_);
    HalfPeriod();
    Release(scl_);
    HalfPeriod();
    const bool acked = !IsHigh(sda_);
    Drive(scl_);
    HalfPeriod();
    return acked;
  }

private:
  I2cBus::Pin scl_;
  I2cBus::Pin sda_;
};

} // namespace

bool WedgeI2cBus(I2C_TypeDef *i2c, I2cBus::Pin scl, I2cBus::Pin sda, std::uint8_t address7Bit,
                 std::uint8_t reg)
{
  LL_I2C_Disable(i2c);

  Release(scl);
  Release(sda);
  LL_GPIO_SetPinMode(scl.port, scl.pinMask, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetPinMode(sda.port, sda.pinMask, LL_GPIO_MODE_OUTPUT);

  BitBanger bus(scl, sda);
  const auto writeAddress = static_cast<std::uint8_t>(address7Bit << 1);
  const auto readAddress = static_cast<std::uint8_t>(writeAddress | 1u);

  bus.Start();
  bool acked = bus.WriteByte(writeAddress) && bus.WriteByte(reg);
  if (acked)
  {
    bus.Start(); // repeated start
    // After this ACK clock, SCL is left low: the slave now drives the first
    // bit of `reg`'s value onto SDA.
    acked = bus.WriteByte(readAddress);
  }

  // Release SCL - as an MCU reset would. The slave only changes SDA on a
  // falling SCL edge, so it keeps driving its data bit.
  HalfPeriod();
  Release(scl);
  HalfPeriod();
  const bool sdaHeldLow = !IsHigh(sda);

  LL_GPIO_SetPinMode(scl.port, scl.pinMask, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinMode(sda.port, sda.pinMask, LL_GPIO_MODE_ALTERNATE);
  LL_I2C_Enable(i2c);

  return acked && sdaHeldLow;
}
