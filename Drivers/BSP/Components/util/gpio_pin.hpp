#pragma once

// Single GPIO lines, abstracted for the same reason as I2cDevice/SpiDevice:
// drivers that toggle or sense pins stay testable on a host.
class OutputPin
{
public:
  virtual void Write(bool high) = 0;

protected:
  ~OutputPin() = default;
};

class InputPin
{
public:
  [[nodiscard]] virtual bool IsHigh() = 0;

protected:
  ~InputPin() = default;
};
