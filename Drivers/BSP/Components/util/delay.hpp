#pragma once

#include <cstdint>

// Forward-declared for the same reason as HAL_GetTick() in timeout.hpp: keeps
// drivers free of HAL headers, and lets host-side unit tests supply their own
// (instant) definition at link time.
extern "C" void HAL_Delay(std::uint32_t Delay);

// Blocks for at least `ms` milliseconds (HAL_Delay adds one tick of margin).
inline void DelayMs(std::uint32_t ms)
{
  HAL_Delay(ms);
}
