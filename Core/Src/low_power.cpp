#include "low_power.hpp"

#include <cstdint>

#include "build_config.hpp"
#include "stm32l0xx_hal.h"

void low_power::ConfigureStopMode()
{
  // VREFINT is only needed by the ADC and comparators, neither of which this
  // design uses. ULP switches it off in Stop mode; FWU lets wake-up carry on
  // without waiting for it to restart.
  HAL_PWREx_EnableUltraLowPower();
  HAL_PWREx_EnableFastWakeUp();

  if (!kDebugBuild)
  {
    // An attached debugger sets DBG_STOP, which keeps clocks running in
    // Stop mode. It survives a reset (only a power cycle clears it), so a
    // Release build clears it itself. Debug builds leave it alone so the
    // debugger stays connected while the MCU sleeps.
    __HAL_RCC_DBGMCU_CLK_ENABLE();
    HAL_DBGMCU_DisableDBGStopMode();
    __HAL_RCC_DBGMCU_CLK_DISABLE();
  }
}

// Replaces HAL's weak HAL_Delay(), which busy-waits with the CPU running.
// This one sleeps between SysTick interrupts (1 ms apart) instead. Any other
// interrupt also wakes it, so the loop re-checks the elapsed time.
void HAL_Delay(std::uint32_t Delay)
{
  const std::uint32_t start = HAL_GetTick();
  std::uint32_t wait = Delay;

  // Same one-tick margin as HAL's version, guaranteeing the minimum wait.
  if (wait < HAL_MAX_DELAY)
  {
    wait += static_cast<std::uint32_t>(uwTickFreq);
  }

  while ((HAL_GetTick() - start) < wait)
  {
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
  }
}
