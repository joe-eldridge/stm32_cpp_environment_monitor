#pragma once

#include <cstdint>

// Host replacements for the two HAL functions the drivers use (declared in
// timeout.hpp and delay.hpp). Time only moves when code under test calls
// HAL_Delay() or a test advances it, so timing is fully deterministic.
namespace fake_hal
{

void SetTick(std::uint32_t tick);
void AdvanceTick(std::uint32_t ms);
std::uint32_t Tick();

} // namespace fake_hal
