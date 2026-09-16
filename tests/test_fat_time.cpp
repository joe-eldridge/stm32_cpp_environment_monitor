#include <gtest/gtest.h>

#include "fat_time.hpp"

namespace
{

Ds3231::DateTime Unpack(std::uint32_t packed)
{
  Ds3231::DateTime dt{};
  dt.year = static_cast<std::uint16_t>(((packed >> 25) & 0x7F) + 1980);
  dt.month = static_cast<std::uint8_t>((packed >> 21) & 0x0F);
  dt.date = static_cast<std::uint8_t>((packed >> 16) & 0x1F);
  dt.hour = static_cast<std::uint8_t>((packed >> 11) & 0x1F);
  dt.minute = static_cast<std::uint8_t>((packed >> 5) & 0x3F);
  dt.second = static_cast<std::uint8_t>((packed & 0x1F) * 2);
  return dt;
}

} // namespace

TEST(FatTime, MatchesFatFsReferenceTimestamp)
{
  // FatFs's own fallback timestamp when there's no RTC: 2016-01-01 00:00:00.
  static_assert(PackFatTime({2016, 1, 1, 0, 0, 0}) == 0x48210000u);
  SUCCEED();
}

TEST(FatTime, RoundTripsFields)
{
  const Ds3231::DateTime dt{2026, 9, 16, 19, 45, 30};
  const Ds3231::DateTime back = Unpack(PackFatTime(dt));
  EXPECT_EQ(back.year, 2026);
  EXPECT_EQ(back.month, 9);
  EXPECT_EQ(back.date, 16);
  EXPECT_EQ(back.hour, 19);
  EXPECT_EQ(back.minute, 45);
  EXPECT_EQ(back.second, 30);
}

TEST(FatTime, SecondsHaveTwoSecondResolution)
{
  EXPECT_EQ(Unpack(PackFatTime({2026, 1, 1, 0, 0, 59})).second, 58);
}

TEST(FatTime, LatestRtcDateFits)
{
  const Ds3231::DateTime back = Unpack(PackFatTime({2099, 12, 31, 23, 59, 58}));
  EXPECT_EQ(back.year, 2099);
  EXPECT_EQ(back.month, 12);
  EXPECT_EQ(back.date, 31);
  EXPECT_EQ(back.hour, 23);
  EXPECT_EQ(back.minute, 59);
  EXPECT_EQ(back.second, 58);
}
