#include "fake_hal.hpp"

namespace
{
std::uint32_t g_tick = 0;
} // namespace

namespace fake_hal
{

void SetTick(std::uint32_t tick)
{
  g_tick = tick;
}

void AdvanceTick(std::uint32_t ms)
{
  g_tick += ms;
}

std::uint32_t Tick()
{
  return g_tick;
}

} // namespace fake_hal

extern "C" std::uint32_t HAL_GetTick(void)
{
  return g_tick;
}

extern "C" void HAL_Delay(std::uint32_t Delay)
{
  g_tick += Delay;
}
