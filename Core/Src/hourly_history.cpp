#include "hourly_history.hpp"

namespace
{

constexpr std::uint8_t MaskFor(Metric metric)
{
  return static_cast<std::uint8_t>(1u << static_cast<unsigned>(metric));
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// days_from_civil). Only the difference between two dates matters here, but
// a fixed epoch keeps period numbers comparable across resets.
std::int32_t DaysFromCivil(int year, unsigned month, unsigned day)
{
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);          // 0-399
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1; // 0-365, March-based
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<int>(dayOfEra) - 719468;
}

// Rounds half away from zero, matching the display formatter, so an average
// and a single reading of the same value are shown the same way.
std::int32_t RoundedAverage(std::int64_t sum, std::uint16_t count)
{
  const std::int64_t half = count / 2;
  const std::int64_t rounded = sum >= 0 ? (sum + half) / count : (sum - half) / count;
  return static_cast<std::int32_t>(rounded);
}

} // namespace

HourlyHistory::HourlyHistory(std::uint16_t bucketMinutes)
    : bucketMinutes_(bucketMinutes == 0 ? 1 : bucketMinutes)
{
}

std::optional<std::int32_t> HourlyHistory::Bucket::Value(Metric metric) const
{
  if ((presentMask & MaskFor(metric)) == 0)
  {
    return std::nullopt;
  }
  return values[static_cast<std::size_t>(metric)];
}

void HourlyHistory::Clear()
{
  count_ = 0;
  head_ = 0;
  hasCurrent_ = false;
  current_ = Accumulator{};
}

const HourlyHistory::Bucket &HourlyHistory::At(std::size_t index) const
{
  return buckets_[(head_ + index) % kBuckets];
}

std::int32_t HourlyHistory::BucketIndexFor(const Ds3231::DateTime &time) const
{
  const std::int32_t days = DaysFromCivil(time.year, time.month, time.date);
  const std::int32_t minutes = days * 1440 + time.hour * 60 + time.minute;
  return minutes / bucketMinutes_;
}

HourlyHistory::Bucket HourlyHistory::StartOf(std::int32_t index) const
{
  const std::int32_t minuteOfDay = (index * bucketMinutes_) % 1440;
  Bucket bucket;
  bucket.hour = static_cast<std::uint8_t>(minuteOfDay / 60);
  bucket.minute = static_cast<std::uint8_t>(minuteOfDay % 60);
  return bucket;
}

void HourlyHistory::Push(const Bucket &bucket)
{
  const std::size_t slot = (head_ + count_) % kBuckets;
  buckets_[slot] = bucket;
  if (count_ < kBuckets)
  {
    ++count_;
  }
  else
  {
    head_ = (head_ + 1) % kBuckets; // full: the oldest bucket falls off
  }
}

void HourlyHistory::StartBucket(std::int32_t index)
{
  currentIndex_ = index;
  hasCurrent_ = true;
  current_ = Accumulator{};
}

bool HourlyHistory::Add(const Ds3231::DateTime &time, const std::optional<Bme280::Measurements> &climate,
                        const std::optional<Veml7700::Reading> &light)
{
  const std::int32_t index = BucketIndexFor(time);
  bool closed = false;

  if (!hasCurrent_ || index < currentIndex_)
  {
    // Either the first sample, or the clock moved backwards (the RTC was
    // re-set). Averages either side of that jump don't belong on one time
    // axis, so start again.
    if (hasCurrent_)
    {
      Clear();
    }
    StartBucket(index);
  }
  else if (index > currentIndex_)
  {
    Bucket finished = StartOf(currentIndex_);
    finished.lightSaturated = current_.saturated;
    for (std::size_t metric = 0; metric < kMetricCount; ++metric)
    {
      if (current_.counts[metric] != 0)
      {
        finished.values[metric] = RoundedAverage(current_.sums[metric], current_.counts[metric]);
        finished.presentMask |= static_cast<std::uint8_t>(1u << metric);
      }
    }
    Push(finished);
    closed = true;

    // Periods the device slept through stay in the history as gaps. Only the
    // most recent kBuckets can be shown, so older gaps are skipped rather
    // than pushed one by one (the device could have been off for months).
    std::int32_t missing = currentIndex_ + 1;
    const std::int32_t earliestVisible = index - static_cast<std::int32_t>(kBuckets);
    if (missing < earliestVisible)
    {
      missing = earliestVisible;
    }
    for (; missing < index; ++missing)
    {
      Push(StartOf(missing));
    }

    StartBucket(index);
  }

  if (climate)
  {
    current_.sums[static_cast<std::size_t>(Metric::Temperature)] += climate->temperatureCenti;
    ++current_.counts[static_cast<std::size_t>(Metric::Temperature)];
    current_.sums[static_cast<std::size_t>(Metric::Humidity)] += climate->humidityCentiPct;
    ++current_.counts[static_cast<std::size_t>(Metric::Humidity)];
    current_.sums[static_cast<std::size_t>(Metric::Pressure)] += climate->pressureCentiHpa;
    ++current_.counts[static_cast<std::size_t>(Metric::Pressure)];
  }
  if (light)
  {
    // A saturated reading is a lower bound, not a measurement. It still goes
    // into the average - dropping it would bias a bright hour downwards -
    // but the period is flagged so the screen can show the average as a
    // lower bound too.
    current_.sums[static_cast<std::size_t>(Metric::Light)] += light->luxCenti;
    ++current_.counts[static_cast<std::size_t>(Metric::Light)];
    current_.saturated = current_.saturated || light->saturated;
  }

  return closed;
}
