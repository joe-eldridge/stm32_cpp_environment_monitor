#pragma once

#include "ds3231.hpp"
#include "stm32l0xx_hal.h"

// Blocks, prompting over `uart`, until the user enters a valid
// "YYYY-MM-DD HH:MM:SS" line - then writes it to `rtc` and clears its
// oscillator-stop flag. Only meant for the rare recovery path where the
// RTC's time isn't trustworthy (dead/missing backup battery); normal
// operation never calls this.
void SyncTimeFromUart(UART_HandleTypeDef *uart, Ds3231 &rtc);
