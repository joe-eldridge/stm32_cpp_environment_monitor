#include <gtest/gtest.h>

#include "bcd.hpp"

TEST(Bcd, KnownValues)
{
  EXPECT_EQ(bcd::ToBinary(0x00), 0);
  EXPECT_EQ(bcd::ToBinary(0x59), 59);
  EXPECT_EQ(bcd::ToBinary(0x99), 99);
  EXPECT_EQ(bcd::ToBcd(0), 0x00);
  EXPECT_EQ(bcd::ToBcd(59), 0x59);
  EXPECT_EQ(bcd::ToBcd(99), 0x99);
}

TEST(Bcd, RoundTripsEveryTwoDigitValue)
{
  for (std::uint8_t value = 0; value <= 99; ++value)
  {
    EXPECT_EQ(bcd::ToBinary(bcd::ToBcd(value)), value);
  }
}
