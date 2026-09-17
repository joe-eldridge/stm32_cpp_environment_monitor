#pragma once

#include <cstdint>

#include "gpio_pin.hpp"
#include "stm32l0xx_hal.h"

// Target implementations of OutputPin/InputPin, taking the port/pin pairs
// CubeMX generates in main.h. The pin must already be configured by
// MX_GPIO_Init().
class HalOutputPin final : public OutputPin
{
public:
  HalOutputPin(GPIO_TypeDef *port, std::uint16_t pin) : port_(port), pin_(pin)
  {
  }

  void Write(bool high) override
  {
    HAL_GPIO_WritePin(port_, pin_, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }

private:
  GPIO_TypeDef *port_;
  std::uint16_t pin_;
};

class HalInputPin final : public InputPin
{
public:
  HalInputPin(GPIO_TypeDef *port, std::uint16_t pin) : port_(port), pin_(pin)
  {
  }

  bool IsHigh() override
  {
    return HAL_GPIO_ReadPin(port_, pin_) == GPIO_PIN_SET;
  }

private:
  GPIO_TypeDef *port_;
  std::uint16_t pin_;
};
