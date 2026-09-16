#include "i2c_bus.hpp"

#include "delay.hpp"

I2cBus::I2cBus(I2C_TypeDef *i2c, Pin scl, Pin sda) : i2c_(i2c), scl_(scl), sda_(sda)
{
}

template <typename Predicate>
I2cBus::WaitResult I2cBus::WaitUntil(Predicate ready, const Timeout &timeout)
{
  while (!ready())
  {
    if (LL_I2C_IsActiveFlag_NACK(i2c_))
    {
      return WaitResult::Nack;
    }
    if (timeout.Expired())
    {
      return WaitResult::TimedOut;
    }
  }
  return WaitResult::Ready;
}

bool I2cBus::WaitForIdleBus()
{
  const Timeout timeout(kTimeoutMs);
  while (LL_I2C_IsActiveFlag_BUSY(i2c_))
  {
    if (timeout.Expired())
    {
      // BUSY stuck set means either our own peripheral is mid-transaction
      // from an earlier failure, or a slave is holding a line low.
      return Recover();
    }
  }
  return true;
}

void I2cBus::StartTransfer(std::uint8_t address7Bit, std::uint8_t count, std::uint32_t endMode,
                           std::uint32_t request)
{
  // A STOPF/NACKF left latched by an earlier transaction would otherwise
  // satisfy this transaction's wait for them before its own STOP happens.
  LL_I2C_ClearFlag_STOP(i2c_);
  LL_I2C_ClearFlag_NACK(i2c_);
  LL_I2C_HandleTransfer(i2c_, static_cast<std::uint32_t>(address7Bit << 1), LL_I2C_ADDRSLAVE_7BIT, count, endMode,
                        request);
}

bool I2cBus::Fail(WaitResult result)
{
  if (result == WaitResult::Nack)
  {
    // An absent or busy device - a normal, recoverable condition.
    FinishAfterNack();
  }
  else
  {
    static_cast<void>(Recover());
  }
  return false;
}

void I2cBus::FinishAfterNack()
{
  // In AUTOEND mode the peripheral sends STOP itself after a NACK; in
  // SOFTEND mode (the register-pointer phase of a read) it's left to us,
  // and skipping it would leave the bus held by this master.
  if (!LL_I2C_IsEnabledAutoEndMode(i2c_))
  {
    LL_I2C_GenerateStopCondition(i2c_);
  }

  const Timeout timeout(kTimeoutMs);
  while (!LL_I2C_IsActiveFlag_STOP(i2c_))
  {
    if (timeout.Expired())
    {
      static_cast<void>(Recover());
      return;
    }
  }
  LL_I2C_ClearFlag_STOP(i2c_);
  LL_I2C_ClearFlag_NACK(i2c_);
}

bool I2cBus::WriteRegisters(std::uint8_t address7Bit, std::uint8_t startReg, const std::uint8_t *data,
                            std::uint8_t count)
{
  if (count == 0 || count > kMaxWriteBurst)
  {
    return false;
  }

  std::uint8_t payload[kMaxWriteBurst + 1];
  payload[0] = startReg;
  for (std::uint8_t i = 0; i < count; ++i)
  {
    payload[1 + i] = data[i];
  }
  const auto payloadSize = static_cast<std::uint8_t>(count + 1);

  // Own timeout for the transfer itself, started only once the bus is idle,
  // so time spent recovering a stuck bus doesn't eat into it.
  if (!WaitForIdleBus())
  {
    return false;
  }
  const Timeout timeout(kTimeoutMs);

  StartTransfer(address7Bit, payloadSize, LL_I2C_MODE_AUTOEND, LL_I2C_GENERATE_START_WRITE);

  for (std::uint8_t i = 0; i < payloadSize; ++i)
  {
    const WaitResult result = WaitUntil([this] { return LL_I2C_IsActiveFlag_TXIS(i2c_) != 0; }, timeout);
    if (result != WaitResult::Ready)
    {
      return Fail(result);
    }
    LL_I2C_TransmitData8(i2c_, payload[i]);
  }

  const WaitResult result = WaitUntil([this] { return LL_I2C_IsActiveFlag_STOP(i2c_) != 0; }, timeout);
  if (result != WaitResult::Ready)
  {
    return Fail(result);
  }
  LL_I2C_ClearFlag_STOP(i2c_);
  return true;
}

bool I2cBus::ReadRegisters(std::uint8_t address7Bit, std::uint8_t startReg, std::uint8_t *buffer,
                           std::uint8_t count)
{
  if (count == 0)
  {
    return false;
  }

  // Own timeout for the transfer itself, started only once the bus is idle,
  // so time spent recovering a stuck bus doesn't eat into it.
  if (!WaitForIdleBus())
  {
    return false;
  }
  const Timeout timeout(kTimeoutMs);

  // Phase 1: write the register pointer, software-END so a repeated start
  // (not a stop) follows once the byte is sent.
  StartTransfer(address7Bit, 1, LL_I2C_MODE_SOFTEND, LL_I2C_GENERATE_START_WRITE);

  WaitResult result = WaitUntil([this] { return LL_I2C_IsActiveFlag_TXIS(i2c_) != 0; }, timeout);
  if (result != WaitResult::Ready)
  {
    return Fail(result);
  }
  LL_I2C_TransmitData8(i2c_, startReg);

  result = WaitUntil([this] { return LL_I2C_IsActiveFlag_TC(i2c_) != 0; }, timeout);
  if (result != WaitResult::Ready)
  {
    return Fail(result);
  }

  // Phase 2: repeated start, read `count` bytes, auto-STOP once done. Not via
  // StartTransfer() - clearing flags isn't needed mid-transaction.
  LL_I2C_HandleTransfer(i2c_, static_cast<std::uint32_t>(address7Bit << 1), LL_I2C_ADDRSLAVE_7BIT, count,
                        LL_I2C_MODE_AUTOEND, LL_I2C_GENERATE_START_READ);

  for (std::uint8_t i = 0; i < count; ++i)
  {
    result = WaitUntil([this] { return LL_I2C_IsActiveFlag_RXNE(i2c_) != 0; }, timeout);
    if (result != WaitResult::Ready)
    {
      return Fail(result);
    }
    buffer[i] = LL_I2C_ReceiveData8(i2c_);
  }

  result = WaitUntil([this] { return LL_I2C_IsActiveFlag_STOP(i2c_) != 0; }, timeout);
  if (result != WaitResult::Ready)
  {
    return Fail(result);
  }
  LL_I2C_ClearFlag_STOP(i2c_);
  return true;
}

bool I2cBus::Recover()
{
  // PE=0 is the peripheral's software reset: it aborts any transfer and
  // clears the state machine and flags. The reference manual requires PE to
  // stay low for at least 3 APB clock cycles - the register reads provide that.
  LL_I2C_Disable(i2c_);
  for (int i = 0; i < 3; ++i)
  {
    static_cast<void>(LL_I2C_IsEnabled(i2c_));
  }

  if (!LL_GPIO_IsInputPinSet(sda_.port, sda_.pinMask))
  {
    ClockOutStuckSlave();
  }

  LL_I2C_Enable(i2c_);

  return LL_GPIO_IsInputPinSet(scl_.port, scl_.pinMask) && LL_GPIO_IsInputPinSet(sda_.port, sda_.pinMask);
}

void I2cBus::ClockOutStuckSlave()
{
  // Bit-bang both lines as open-drain GPIOs. Set the output latch high
  // (released) before switching mode so taking the pins over doesn't glitch.
  LL_GPIO_SetOutputPin(scl_.port, scl_.pinMask);
  LL_GPIO_SetOutputPin(sda_.port, sda_.pinMask);
  LL_GPIO_SetPinMode(scl_.port, scl_.pinMask, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetPinMode(sda_.port, sda_.pinMask, LL_GPIO_MODE_OUTPUT);

  // ~1ms half-periods: far slower than needed, but this path is rare and
  // HAL_Delay is the only timebase available without extra peripherals.
  for (std::uint8_t i = 0; i < kRecoveryClockPulses && !LL_GPIO_IsInputPinSet(sda_.port, sda_.pinMask); ++i)
  {
    LL_GPIO_ResetOutputPin(scl_.port, scl_.pinMask);
    DelayMs(1);
    LL_GPIO_SetOutputPin(scl_.port, scl_.pinMask);
    DelayMs(1);
  }

  // STOP condition (SDA rising while SCL is high) so every slave resets its
  // own transaction state.
  LL_GPIO_ResetOutputPin(scl_.port, scl_.pinMask);
  DelayMs(1);
  LL_GPIO_ResetOutputPin(sda_.port, sda_.pinMask);
  DelayMs(1);
  LL_GPIO_SetOutputPin(scl_.port, scl_.pinMask);
  DelayMs(1);
  LL_GPIO_SetOutputPin(sda_.port, sda_.pinMask);
  DelayMs(1);

  LL_GPIO_SetPinMode(scl_.port, scl_.pinMask, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinMode(sda_.port, sda_.pinMask, LL_GPIO_MODE_ALTERNATE);
}
