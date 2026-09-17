#include <gtest/gtest.h>

#include "hourly_history.hpp"

namespace
{

Ds3231::DateTime At(int year, int month, int day, int hour, int minute)
{
  Ds3231::DateTime time{};
  time.year = static_cast<std::uint16_t>(year);
  time.month = static_cast<std::uint8_t>(month);
  time.date = static_cast<std::uint8_t>(day);
  time.hour = static_cast<std::uint8_t>(hour);
  time.minute = static_cast<std::uint8_t>(minute);
  time.second = 0;
  return time;
}

Bme280::Measurements Climate(std::int32_t temperature)
{
  return Bme280::Measurements{temperature, 100000, 5000};
}

// Adds a sample with a temperature only, which is enough for most cases here.
bool AddTemperature(HourlyHistory &history, const Ds3231::DateTime &time, std::int32_t temperature)
{
  return history.Add(time, Climate(temperature), std::nullopt);
}

} // namespace

TEST(HourlyHistory, StartsEmpty)
{
  const HourlyHistory history;
  EXPECT_EQ(history.Count(), 0u);
  EXPECT_EQ(history.BucketMinutes(), 60u);
}

TEST(HourlyHistory, SamplesWithinOnePeriodCompleteNothing)
{
  HourlyHistory history;
  EXPECT_FALSE(AddTemperature(history, At(2026, 9, 17, 9, 0), 2000));
  EXPECT_FALSE(AddTemperature(history, At(2026, 9, 17, 9, 30), 2100));
  EXPECT_FALSE(AddTemperature(history, At(2026, 9, 17, 9, 59), 2200));
  EXPECT_EQ(history.Count(), 0u);
}

TEST(HourlyHistory, CompletesAPeriodWhenTheNextOneStarts)
{
  HourlyHistory history;
  AddTemperature(history, At(2026, 9, 17, 9, 10), 2000);
  AddTemperature(history, At(2026, 9, 17, 9, 50), 2100);
  EXPECT_TRUE(AddTemperature(history, At(2026, 9, 17, 10, 0), 2500));

  ASSERT_EQ(history.Count(), 1u);
  const HourlyHistory::Bucket &bucket = history.At(0);
  EXPECT_EQ(bucket.hour, 9);
  EXPECT_EQ(bucket.minute, 0);
  EXPECT_EQ(bucket.Value(Metric::Temperature), std::optional<std::int32_t>(2050));
}

TEST(HourlyHistory, AveragesEachMetricSeparately)
{
  HourlyHistory history;
  history.Add(At(2026, 9, 17, 9, 0), Bme280::Measurements{2000, 100000, 4000}, Veml7700::Reading{1000, false});
  history.Add(At(2026, 9, 17, 9, 30), Bme280::Measurements{2200, 101000, 5000}, Veml7700::Reading{3000, false});
  history.Add(At(2026, 9, 17, 10, 0), Climate(0), std::nullopt);

  const HourlyHistory::Bucket &bucket = history.At(0);
  EXPECT_EQ(bucket.Value(Metric::Temperature), std::optional<std::int32_t>(2100));
  EXPECT_EQ(bucket.Value(Metric::Pressure), std::optional<std::int32_t>(100500));
  EXPECT_EQ(bucket.Value(Metric::Humidity), std::optional<std::int32_t>(4500));
  EXPECT_EQ(bucket.Value(Metric::Light), std::optional<std::int32_t>(2000));
}

TEST(HourlyHistory, RoundsAveragesHalfAwayFromZero)
{
  HourlyHistory history;
  AddTemperature(history, At(2026, 9, 17, 9, 0), 2000);
  AddTemperature(history, At(2026, 9, 17, 9, 30), 2001);
  AddTemperature(history, At(2026, 9, 17, 10, 0), 0);
  EXPECT_EQ(history.At(0).Value(Metric::Temperature), std::optional<std::int32_t>(2001));

  HourlyHistory below;
  AddTemperature(below, At(2026, 9, 17, 9, 0), -2000);
  AddTemperature(below, At(2026, 9, 17, 9, 30), -2001);
  AddTemperature(below, At(2026, 9, 17, 10, 0), 0);
  EXPECT_EQ(below.At(0).Value(Metric::Temperature), std::optional<std::int32_t>(-2001));
}

TEST(HourlyHistory, MetricWithNoReadingsIsAbsentButThePeriodIsKept)
{
  HourlyHistory history;
  // Light failed all hour; the climate readings succeeded.
  AddTemperature(history, At(2026, 9, 17, 9, 0), 2000);
  AddTemperature(history, At(2026, 9, 17, 10, 0), 2000);

  const HourlyHistory::Bucket &bucket = history.At(0);
  EXPECT_TRUE(bucket.Value(Metric::Temperature).has_value());
  EXPECT_FALSE(bucket.Value(Metric::Light).has_value());
}

TEST(HourlyHistory, PeriodWithNoSuccessfulReadsIsStillRecorded)
{
  HourlyHistory history;
  history.Add(At(2026, 9, 17, 9, 0), std::nullopt, std::nullopt);
  history.Add(At(2026, 9, 17, 10, 0), std::nullopt, std::nullopt);

  ASSERT_EQ(history.Count(), 1u);
  EXPECT_EQ(history.At(0).hour, 9);
  EXPECT_FALSE(history.At(0).Value(Metric::Temperature).has_value());
}

TEST(HourlyHistory, SaturatedLightFlagsThePeriod)
{
  HourlyHistory history;
  history.Add(At(2026, 9, 17, 9, 0), std::nullopt, Veml7700::Reading{100, false});
  history.Add(At(2026, 9, 17, 9, 30), std::nullopt, Veml7700::Reading{440300, true});
  history.Add(At(2026, 9, 17, 10, 0), std::nullopt, Veml7700::Reading{100, false});

  EXPECT_TRUE(history.At(0).lightSaturated);
  // The saturated sample still counts towards the average.
  EXPECT_EQ(history.At(0).Value(Metric::Light), std::optional<std::int32_t>(220200));
}

TEST(HourlyHistory, SleptThroughPeriodsBecomeGaps)
{
  HourlyHistory history;
  AddTemperature(history, At(2026, 9, 17, 9, 0), 2000);
  AddTemperature(history, At(2026, 9, 17, 12, 0), 2400);

  // 09:00 completed, then 10:00 and 11:00 as empty periods, keeping the
  // time axis linear.
  ASSERT_EQ(history.Count(), 3u);
  EXPECT_EQ(history.At(0).hour, 9);
  EXPECT_TRUE(history.At(0).Value(Metric::Temperature).has_value());
  EXPECT_EQ(history.At(1).hour, 10);
  EXPECT_FALSE(history.At(1).Value(Metric::Temperature).has_value());
  EXPECT_EQ(history.At(2).hour, 11);
  EXPECT_FALSE(history.At(2).Value(Metric::Temperature).has_value());
}

TEST(HourlyHistory, KeepsOnlyTheMostRecentDay)
{
  HourlyHistory history;
  for (int hour = 0; hour < 30; ++hour)
  {
    const int day = 17 + hour / 24;
    AddTemperature(history, At(2026, 9, day, hour % 24, 0), 1000 + hour);
  }

  // 30 periods started, so 29 are complete and the oldest five have gone.
  ASSERT_EQ(history.Count(), HourlyHistory::kBuckets);
  EXPECT_EQ(history.At(0).Value(Metric::Temperature), std::optional<std::int32_t>(1005));
  EXPECT_EQ(history.At(HourlyHistory::kBuckets - 1).Value(Metric::Temperature),
            std::optional<std::int32_t>(1028));
}

TEST(HourlyHistory, LongGapDoesNotPushMoreThanADayOfEmptyPeriods)
{
  HourlyHistory history;
  AddTemperature(history, At(2026, 9, 17, 9, 0), 2000);
  // Powered off for a fortnight.
  AddTemperature(history, At(2026, 10, 1, 9, 0), 2500);

  EXPECT_EQ(history.Count(), HourlyHistory::kBuckets);
  for (std::size_t i = 0; i < history.Count(); ++i)
  {
    EXPECT_FALSE(history.At(i).Value(Metric::Temperature).has_value()) << "period " << i;
  }
  // The 24 periods before the new sample, so the 09:00 average from a
  // fortnight earlier has been pushed out.
  EXPECT_EQ(history.At(0).hour, 9);
  EXPECT_EQ(history.At(HourlyHistory::kBuckets - 1).hour, 8);
}

TEST(HourlyHistory, PeriodsSpanMidnightAndMonthEnds)
{
  HourlyHistory history;
  AddTemperature(history, At(2026, 9, 30, 23, 0), 2000);
  EXPECT_TRUE(AddTemperature(history, At(2026, 10, 1, 0, 0), 2100));
  EXPECT_EQ(history.Count(), 1u);
  EXPECT_EQ(history.At(0).hour, 23);
}

TEST(HourlyHistory, ClockMovedBackwardsStartsAgain)
{
  HourlyHistory history;
  AddTemperature(history, At(2026, 9, 17, 9, 0), 2000);
  AddTemperature(history, At(2026, 9, 17, 10, 0), 2100);
  ASSERT_EQ(history.Count(), 1u);

  // The RTC was re-set to an earlier time: averages either side of the jump
  // don't share a time axis.
  EXPECT_FALSE(AddTemperature(history, At(2026, 9, 17, 8, 0), 2200));
  EXPECT_EQ(history.Count(), 0u);

  EXPECT_TRUE(AddTemperature(history, At(2026, 9, 17, 9, 0), 2300));
  ASSERT_EQ(history.Count(), 1u);
  EXPECT_EQ(history.At(0).Value(Metric::Temperature), std::optional<std::int32_t>(2200));
}

TEST(HourlyHistory, ShorterPeriodsForTesting)
{
  HourlyHistory history(5);
  EXPECT_EQ(history.BucketMinutes(), 5u);
  AddTemperature(history, At(2026, 9, 17, 9, 3), 2000);
  EXPECT_FALSE(AddTemperature(history, At(2026, 9, 17, 9, 4), 2100));
  EXPECT_TRUE(AddTemperature(history, At(2026, 9, 17, 9, 5), 2200));

  ASSERT_EQ(history.Count(), 1u);
  EXPECT_EQ(history.At(0).hour, 9);
  EXPECT_EQ(history.At(0).minute, 0);
  EXPECT_EQ(history.At(0).Value(Metric::Temperature), std::optional<std::int32_t>(2050));
}
