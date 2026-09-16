#include <gtest/gtest.h>

#include "fake_hal.hpp"
#include "timeout.hpp"

TEST(Timeout, NotExpiredBeforeDuration)
{
  fake_hal::SetTick(1000);
  const Timeout timeout(10);
  fake_hal::AdvanceTick(9);
  EXPECT_FALSE(timeout.Expired());
}

TEST(Timeout, ExpiredOnceDurationElapsed)
{
  fake_hal::SetTick(1000);
  const Timeout timeout(10);
  fake_hal::AdvanceTick(10);
  EXPECT_TRUE(timeout.Expired());
  fake_hal::AdvanceTick(1000);
  EXPECT_TRUE(timeout.Expired());
}

// HAL_GetTick() wraps after ~49 days - a device meant to run for months
// will see it.
TEST(Timeout, CorrectAcrossTickWraparound)
{
  fake_hal::SetTick(0xFFFFFFFAu);
  const Timeout timeout(10); // deadline lands after the wrap
  fake_hal::AdvanceTick(9);
  EXPECT_FALSE(timeout.Expired());
  fake_hal::AdvanceTick(1);
  EXPECT_TRUE(timeout.Expired());
}
