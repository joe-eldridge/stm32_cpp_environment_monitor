#pragma once

namespace low_power
{

// MCU settings for the lowest Stop-mode current. Call once at startup,
// before the first Stop-mode entry.
void ConfigureStopMode();

} // namespace low_power
