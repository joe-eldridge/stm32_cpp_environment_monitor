#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "bme280.hpp"
#include "ds3231.hpp"
#include "veml7700.hpp"

// The quantities this device records, in the order the trend screen cycles
// through them.
enum class Metric : std::uint8_t
{
  Temperature,
  Humidity,
  Pressure,
  Light,
};
constexpr std::size_t kMetricCount = 4;

// A day's worth of averages, held in RAM: samples are averaged over a period
// (an hour in the firmware) and the last 24 completed periods are kept in a
// ring buffer, oldest first. That is all the trend screen needs, and it costs
// well under a kilobyte, so nothing has to be read back from the SD card.
//
// The history starts empty at every reset. Rebuilding it from LOG.CSV would
// mean parsing a day of rows on the one wake after a reset, for a plot that
// fills itself again within a day.
//
// All times come from the RTC, so a period is identified by its absolute
// start time rather than by how many samples have been added. Periods with
// no samples (the device was asleep, or every read failed) are kept as gaps,
// which keeps the plot's time axis linear.
class HourlyHistory
{
public:
  static constexpr std::size_t kBuckets = 24;

  // One completed period's averages. A metric is absent if no sample in the
  // period produced a reading for it.
  struct Bucket
  {
    std::uint8_t hour = 0;   // start of the period, RTC local time
    std::uint8_t minute = 0;
    bool lightSaturated = false; // at least one light sample hit full scale
    std::int32_t values[kMetricCount] = {};
    std::uint8_t presentMask = 0;

    std::optional<std::int32_t> Value(Metric metric) const;
  };

  // `bucketMinutes` is the averaging period; it must divide a day evenly for
  // the period start times to line up with the clock. Deliberately not
  // constexpr: a static instance then lives in .bss and is zeroed at
  // start-up, rather than being held as an image in flash.
  explicit HourlyHistory(std::uint16_t bucketMinutes = 60);

  // Adds one sample, taken at `time`. Returns true if this sample fell into
  // a new period, meaning the previous one has just been completed and is
  // now in the history.
  bool Add(const Ds3231::DateTime &time, const std::optional<Bme280::Measurements> &climate,
           const std::optional<Veml7700::Reading> &light);

  // Completed periods only; the one being accumulated is not included.
  std::size_t Count() const
  {
    return count_;
  }

  // Index 0 is the oldest period held. Indices past Count() - 1 wrap, so a
  // caller that ignores Count() gets stale data rather than a bad read.
  const Bucket &At(std::size_t index) const;

  std::uint16_t BucketMinutes() const
  {
    return bucketMinutes_;
  }

  void Clear();

private:
  struct Accumulator
  {
    std::int64_t sums[kMetricCount] = {};
    std::uint16_t counts[kMetricCount] = {};
    bool saturated = false;
  };

  // Periods since 1970-01-01 00:00, so comparing two of them orders them in
  // real time even across a month or year boundary.
  std::int32_t BucketIndexFor(const Ds3231::DateTime &time) const;
  Bucket StartOf(std::int32_t index) const;
  void Push(const Bucket &bucket);
  void StartBucket(std::int32_t index);

  std::uint16_t bucketMinutes_;
  Bucket buckets_[kBuckets] = {};
  std::size_t count_ = 0;
  std::size_t head_ = 0; // index of the oldest bucket in buckets_
  std::int32_t currentIndex_ = 0;
  bool hasCurrent_ = false;
  Accumulator current_;
};
