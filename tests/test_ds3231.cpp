#include <gtest/gtest.h>

#include "bcd.hpp"
#include "ds3231.hpp"
#include "fake_hal.hpp"
#include "fake_i2c_device.hpp"

namespace
{

constexpr std::uint8_t kRegSeconds = 0x00;
constexpr std::uint8_t kRegAlarm1Sec = 0x07;
constexpr std::uint8_t kRegControl = 0x0E;
constexpr std::uint8_t kRegStatus = 0x0F;

class Ds3231Test : public ::testing::Test
{
protected:
  void SetClock(std::uint8_t hour, std::uint8_t minute, std::uint8_t second)
  {
    device.registers[0x00] = bcd::ToBcd(second);
    device.registers[0x01] = bcd::ToBcd(minute);
    device.registers[0x02] = bcd::ToBcd(hour);
    device.registers[0x03] = 0x01;
    device.registers[0x04] = 0x16;
    device.registers[0x05] = 0x09;
    device.registers[0x06] = 0x26;
  }

  FakeI2cDevice device;
  Ds3231 rtc{device};
};

} // namespace

TEST_F(Ds3231Test, InitRoutesAlarm1ToInterruptPin)
{
  ASSERT_TRUE(rtc.Init());
  EXPECT_EQ(device.registers[kRegControl], 0x05); // INTCN | A1IE, oscillator enabled
}

TEST_F(Ds3231Test, ReadDateTimeDecodesBcdFields)
{
  device.registers[0x00] = 0x30; // 30 s
  device.registers[0x01] = 0x45; // 45 min
  device.registers[0x02] = 0x23; // 23 h, 24-hour mode
  device.registers[0x04] = 0x31; // 31st
  device.registers[0x05] = 0x92; // December, century bit set - must be masked
  device.registers[0x06] = 0x99; // 2099

  const auto dt = rtc.ReadDateTime();
  ASSERT_TRUE(dt.has_value());
  EXPECT_EQ(dt->year, 2099);
  EXPECT_EQ(dt->month, 12);
  EXPECT_EQ(dt->date, 31);
  EXPECT_EQ(dt->hour, 23);
  EXPECT_EQ(dt->minute, 45);
  EXPECT_EQ(dt->second, 30);
}

TEST_F(Ds3231Test, ReadDateTimeFailsOnBusError)
{
  device.failReads = true;
  EXPECT_FALSE(rtc.ReadDateTime().has_value());
}

TEST_F(Ds3231Test, SetDateTimeWritesOneBurst)
{
  ASSERT_TRUE(rtc.SetDateTime({2026, 9, 16, 19, 45, 31}));
  ASSERT_EQ(device.writes.size(), 1u);
  EXPECT_EQ(device.writes[0].reg, kRegSeconds);
  const std::vector<std::uint8_t> expected{0x31, 0x45, 0x19, 0x01, 0x16, 0x09, 0x26};
  EXPECT_EQ(device.writes[0].data, expected);
}

TEST_F(Ds3231Test, SetDateTimeRejectsYearsOutsideItsCentury)
{
  EXPECT_FALSE(rtc.SetDateTime({1999, 1, 1, 0, 0, 0}));
  EXPECT_FALSE(rtc.SetDateTime({2100, 1, 1, 0, 0, 0}));
  EXPECT_TRUE(device.writes.empty());
}

TEST_F(Ds3231Test, ValidWakeIntervalsDivideAnHour)
{
  static_assert(Ds3231::IsValidWakeInterval(1));
  static_assert(Ds3231::IsValidWakeInterval(5));
  static_assert(Ds3231::IsValidWakeInterval(60));
  static_assert(!Ds3231::IsValidWakeInterval(0));
  static_assert(!Ds3231::IsValidWakeInterval(7));
  static_assert(!Ds3231::IsValidWakeInterval(61));
  SUCCEED();
}

TEST_F(Ds3231Test, ScheduleNextAlarmRejectsInvalidIntervalWithoutTouchingDevice)
{
  SetClock(12, 4, 0);
  EXPECT_FALSE(rtc.ScheduleNextAlarm(0));
  EXPECT_FALSE(rtc.ScheduleNextAlarm(7));
  EXPECT_TRUE(device.writes.empty());
}

TEST_F(Ds3231Test, ScheduleNextAlarmMatchesMinutesAndSecondsOnly)
{
  SetClock(12, 4, 0);
  ASSERT_TRUE(rtc.ScheduleNextAlarm(5));
  const auto writes = device.WritesTo(kRegAlarm1Sec);
  ASSERT_EQ(writes.size(), 1u);
  // seconds = 00, minutes = 05, hours and date "don't care" (A1M3/A1M4 set).
  const std::vector<std::uint8_t> expected{0x00, 0x05, 0x80, 0x80};
  EXPECT_EQ(writes[0].data, expected);
}

struct AlarmCase
{
  std::uint8_t minute;
  std::uint8_t second;
  std::uint8_t interval;
  std::uint8_t expectedAlarmMinute;
};

class Ds3231AlarmTest : public Ds3231Test, public ::testing::WithParamInterface<AlarmCase>
{
};

TEST_P(Ds3231AlarmTest, PicksNextIntervalBoundary)
{
  const AlarmCase &c = GetParam();
  SetClock(12, c.minute, c.second);
  ASSERT_TRUE(rtc.ScheduleNextAlarm(c.interval));
  EXPECT_EQ(bcd::ToBinary(device.registers[kRegAlarm1Sec + 1]), c.expectedAlarmMinute);
}

INSTANTIATE_TEST_SUITE_P(
    Boundaries, Ds3231AlarmTest,
    ::testing::Values(
        // Normal wakes land just after the boundary.
        AlarmCase{4, 0, 1, 5}, AlarmCase{5, 1, 5, 10}, AlarmCase{58, 30, 5, 0}, AlarmCase{59, 0, 1, 0},
        // Up to 2 s before the next boundary the write is still safely early...
        AlarmCase{4, 57, 1, 5},
        // ...within 2 s it might land late, so one interval is skipped.
        AlarmCase{4, 58, 1, 6}, AlarmCase{4, 59, 5, 10}, AlarmCase{59, 59, 1, 1}, AlarmCase{59, 58, 5, 5},
        // Late in a minute is only a risk if the boundary is the very next minute.
        AlarmCase{2, 59, 5, 5}));

TEST_F(Ds3231Test, ClearAlarmFlagLeavesOtherStatusBitsAlone)
{
  device.registers[kRegStatus] = 0x8B; // OSF | EN32kHz | A2F | A1F
  ASSERT_TRUE(rtc.ClearAlarmFlag());
  EXPECT_EQ(device.registers[kRegStatus], 0x8A);
}

TEST_F(Ds3231Test, OscillatorStopFlagIsReported)
{
  device.registers[kRegStatus] = 0x80;
  EXPECT_TRUE(rtc.IsOscillatorStopped());
  device.registers[kRegStatus] = 0x00;
  EXPECT_FALSE(rtc.IsOscillatorStopped());
}

TEST_F(Ds3231Test, OscillatorStopFailsSafeOnBusError)
{
  device.failReads = true;
  EXPECT_TRUE(rtc.IsOscillatorStopped());
}
