#pragma once

#include <cstdint>

// Forward-declared rather than pulling in the HAL headers: HAL_Init() still
// runs SysTick at 1ms resolution regardless of which peripheral APIs (HAL or
// LL) the rest of a driver uses, so this stays a minimal, decoupled timebase.
extern "C" std::uint32_t HAL_GetTick(void);

// Bounds a busy-wait loop against a stuck peripheral flag (e.g. an I2C bus
// wedged by a glitch or a slave holding a line low). Wraparound-safe via
// signed-difference comparison, so it stays correct across a HAL_GetTick()
// rollover (~49 days), which matters for a device meant to run for months.
class Timeout
{
public:
  explicit Timeout(std::uint32_t durationMs) : deadline_(HAL_GetTick() + durationMs)
  {
  }

  bool Expired() const
  {
    return static_cast<std::int32_t>(HAL_GetTick() - deadline_) >= 0;
  }

private:
  std::uint32_t deadline_;
};
