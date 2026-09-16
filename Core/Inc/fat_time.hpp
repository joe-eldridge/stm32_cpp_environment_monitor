#pragma once

#include <cstdint>

#include "ds3231.hpp"

// Packs an RTC time into FatFs's get_fattime() format: year-1980 (7 bits),
// month (4), day (5), hour (5), minute (6), seconds/2 (5). The DS3231 driver
// only produces years 2000-2099, which all fit.
constexpr std::uint32_t PackFatTime(const Ds3231::DateTime &dt)
{
  return (static_cast<std::uint32_t>(dt.year - 1980) << 25) | (static_cast<std::uint32_t>(dt.month) << 21) |
         (static_cast<std::uint32_t>(dt.date) << 16) | (static_cast<std::uint32_t>(dt.hour) << 11) |
         (static_cast<std::uint32_t>(dt.minute) << 5) | (dt.second / 2u);
}
