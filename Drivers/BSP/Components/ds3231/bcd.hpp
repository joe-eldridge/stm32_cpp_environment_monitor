#pragma once

#include <cstdint>

// DS3231 (and most RTC chips) store time/date fields as BCD, not binary -
// two 4-bit decimal digits packed per byte. Centralized here so no call site
// has to re-derive the packing by hand.
namespace bcd
{

constexpr std::uint8_t ToBinary(std::uint8_t bcd)
{
  return static_cast<std::uint8_t>(((bcd >> 4) * 10) + (bcd & 0x0F));
}

constexpr std::uint8_t ToBcd(std::uint8_t binary)
{
  return static_cast<std::uint8_t>(((binary / 10) << 4) | (binary % 10));
}

} // namespace bcd
