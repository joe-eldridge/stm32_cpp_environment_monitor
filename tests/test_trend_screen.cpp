#include <gtest/gtest.h>

#include <vector>

#include "ssd1681.hpp"
#include "trend_screen.hpp"

namespace
{

// The plot's geometry, mirrored from trend_screen.cpp so the tests pin the
// drawn result rather than restate the implementation's arithmetic.
constexpr int kPlotLeft = 4;
constexpr int kPlotTop = 52;
constexpr int kPlotRight = 195;   // kPlotX + kPlotWidth - 1
constexpr int kPlotBottom = 147;  // kPlotY + kPlotHeight - 1
constexpr int kFirstPointX = 5;
constexpr int kLastPointX = 194;
constexpr int kTopPointY = 55;
constexpr int kBottomPointY = 144;

// `hour` counts from midnight on the 17th and may run past 24, which rolls
// over into the following day.
Ds3231::DateTime At(int hour, int minute)
{
  Ds3231::DateTime time{};
  time.year = 2026;
  time.month = 9;
  time.date = static_cast<std::uint8_t>(17 + hour / 24);
  time.hour = static_cast<std::uint8_t>(hour % 24);
  time.minute = static_cast<std::uint8_t>(minute);
  return time;
}

// Where the plot puts the point for a period, counting slots from the left.
int PointXForSlot(int slot)
{
  return kFirstPointX + slot * (kLastPointX - kFirstPointX) / static_cast<int>(HourlyHistory::kBuckets - 1);
}

void AddHour(HourlyHistory &history, int hour, std::optional<std::int32_t> temperature,
             std::optional<Veml7700::Reading> light = std::nullopt)
{
  std::optional<Bme280::Measurements> climate;
  if (temperature)
  {
    climate = Bme280::Measurements{*temperature, 100000, 5000};
  }
  history.Add(At(hour, 0), climate, light);
}

// Completes one period per hour, starting at midnight. `temperatures` holds
// one value per completed period; a nullopt period gets no readings at all.
void FillHours(HourlyHistory &history, const std::vector<std::optional<std::int32_t>> &temperatures)
{
  for (std::size_t i = 0; i < temperatures.size(); ++i)
  {
    AddHour(history, static_cast<int>(i), temperatures[i]);
  }
  // One more sample starts a new period, completing the last one listed.
  AddHour(history, static_cast<int>(temperatures.size()), std::nullopt);
}

class TrendScreenCanvas
{
public:
  TrendScreenCanvas() : memory_(Ssd1681::kImageBytes), canvas_(memory_.data(), Ssd1681::kWidth, Ssd1681::kHeight)
  {
  }

  MonoFramebuffer &Get()
  {
    return canvas_;
  }

  bool IsBlack(int x, int y) const
  {
    return canvas_.GetPixel(x, y) == Color::Black;
  }

  bool RegionHasBlack(int firstX, int lastX, int firstY, int lastY) const
  {
    for (int x = firstX; x <= lastX; ++x)
    {
      if (ColumnHasBlack(x, firstY, lastY))
      {
        return true;
      }
    }
    return false;
  }

  bool ColumnHasBlack(int x, int firstY, int lastY) const
  {
    for (int y = firstY; y <= lastY; ++y)
    {
      if (IsBlack(x, y))
      {
        return true;
      }
    }
    return false;
  }

private:
  std::vector<std::uint8_t> memory_;
  MonoFramebuffer canvas_;
};

} // namespace

TEST(TrendScreenText, NoHistoryShowsDashes)
{
  TrendScreenData data;
  const TrendScreenText text = BuildTrendScreenText(data);
  EXPECT_STREQ(text.title, "TEMPERATURE");
  EXPECT_STREQ(text.time, "--:--");
  EXPECT_STREQ(text.high, "HIGH --.-");
  EXPECT_STREQ(text.low, "LOW --.-");
  EXPECT_STREQ(text.from, "--:--");
  EXPECT_STREQ(text.to, "--:--");
}

TEST(TrendScreenText, ShowsTheRangeAndSpanOfTheHistory)
{
  HourlyHistory history;
  FillHours(history, {2000, 2450, 1875});

  TrendScreenData data;
  data.history = &history;
  data.time = At(3, 5);
  const TrendScreenText text = BuildTrendScreenText(data);

  EXPECT_STREQ(text.time, "03:05");
  EXPECT_STREQ(text.high, "HIGH 24.5");
  EXPECT_STREQ(text.low, "LOW 18.8");
  EXPECT_STREQ(text.from, "00:00");
  EXPECT_STREQ(text.to, "02:00");
}

TEST(TrendScreenText, TitleAndPrecisionFollowTheMetric)
{
  HourlyHistory history;
  history.Add(At(0, 0), Bme280::Measurements{2000, 101325, 4950}, Veml7700::Reading{123456, false});
  history.Add(At(1, 0), std::nullopt, std::nullopt);

  TrendScreenData data;
  data.history = &history;

  data.metric = Metric::Humidity;
  TrendScreenText text = BuildTrendScreenText(data);
  EXPECT_STREQ(text.title, "HUMIDITY");
  EXPECT_STREQ(text.high, "HIGH 49.5");

  data.metric = Metric::Pressure;
  text = BuildTrendScreenText(data);
  EXPECT_STREQ(text.title, "PRESSURE");
  EXPECT_STREQ(text.high, "HIGH 1013.3");

  // Light is shown whole, not in hundredths of a lux.
  data.metric = Metric::Light;
  text = BuildTrendScreenText(data);
  EXPECT_STREQ(text.title, "LIGHT");
  EXPECT_STREQ(text.high, "HIGH 1235");
  EXPECT_STREQ(text.low, "LOW 1235");
}

TEST(TrendScreenText, SaturatedLightHighIsShownAsALowerBound)
{
  HourlyHistory history;
  history.Add(At(0, 0), std::nullopt, Veml7700::Reading{440395, true});
  history.Add(At(1, 0), std::nullopt, std::nullopt);

  TrendScreenData data;
  data.history = &history;
  data.metric = Metric::Light;
  const TrendScreenText text = BuildTrendScreenText(data);
  EXPECT_STREQ(text.high, "HIGH >4404");
  EXPECT_STREQ(text.low, "LOW 4404");
}

TEST(TrendScreenText, PeriodsWithoutThisMetricAreIgnoredByTheRange)
{
  HourlyHistory history;
  FillHours(history, {2000, std::nullopt, 2200});

  TrendScreenData data;
  data.history = &history;
  const TrendScreenText text = BuildTrendScreenText(data);
  EXPECT_STREQ(text.high, "HIGH 22.0");
  EXPECT_STREQ(text.low, "LOW 20.0");
  // The empty period still counts towards the span shown.
  EXPECT_STREQ(text.from, "00:00");
  EXPECT_STREQ(text.to, "02:00");
}

TEST(TrendPlotYTest, MapsTheRangeOntoThePlot)
{
  EXPECT_EQ(TrendPlotY(1000, 1000, 2000, 10, 110), 110);
  EXPECT_EQ(TrendPlotY(2000, 1000, 2000, 10, 110), 10);
  EXPECT_EQ(TrendPlotY(1500, 1000, 2000, 10, 110), 60);
  // Off centre, so that a plot drawn upside down would not still match.
  EXPECT_EQ(TrendPlotY(1250, 1000, 2000, 10, 110), 85);
  EXPECT_EQ(TrendPlotY(1750, 1000, 2000, 10, 110), 35);
}

TEST(TrendPlotYTest, FlatSeriesIsDrawnDownTheMiddle)
{
  EXPECT_EQ(TrendPlotY(2000, 2000, 2000, 10, 110), 60);
  // A reversed range would otherwise divide by a negative span.
  EXPECT_EQ(TrendPlotY(2000, 3000, 2000, 10, 110), 60);
}

TEST(TrendPlotYTest, ValuesOutsideTheRangeAreClamped)
{
  EXPECT_EQ(TrendPlotY(500, 1000, 2000, 10, 110), 110);
  EXPECT_EQ(TrendPlotY(5000, 1000, 2000, 10, 110), 10);
}

TEST(TrendPlotYTest, HandlesTheWidestLightRangeWithoutOverflowing)
{
  // Hundredths of a lux, from darkness to the sensor's full scale.
  EXPECT_EQ(TrendPlotY(440400, 0, 440400, 10, 110), 10);
  EXPECT_EQ(TrendPlotY(220200, 0, 440400, 10, 110), 60);
}

TEST(TrendScreenDraw, EmptyHistoryDrawsTheFrameAndAMessage)
{
  TrendScreenCanvas canvas;
  TrendScreenData data;
  DrawTrendScreen(canvas.Get(), data);

  EXPECT_TRUE(canvas.IsBlack(kPlotLeft, kPlotTop));
  EXPECT_TRUE(canvas.IsBlack(kPlotRight, kPlotBottom));
  // A message is drawn inside the frame, but no plot points at the edges.
  EXPECT_TRUE(canvas.RegionHasBlack(kPlotLeft + 1, kPlotRight - 1, kPlotTop + 1, kPlotBottom - 1));
  EXPECT_FALSE(canvas.ColumnHasBlack(kLastPointX, kPlotTop + 1, kPlotBottom - 1));
}

TEST(TrendScreenDraw, NewestPeriodIsAtTheRightEdge)
{
  HourlyHistory history;
  std::vector<std::optional<std::int32_t>> rising;
  for (std::size_t i = 0; i < HourlyHistory::kBuckets; ++i)
  {
    rising.push_back(static_cast<std::int32_t>(1000 + i * 10));
  }
  FillHours(history, rising);
  ASSERT_EQ(history.Count(), HourlyHistory::kBuckets);

  TrendScreenCanvas canvas;
  TrendScreenData data;
  data.history = &history;
  DrawTrendScreen(canvas.Get(), data);

  // Oldest value is the lowest, so it sits bottom left; newest is the
  // highest, top right.
  EXPECT_TRUE(canvas.IsBlack(kFirstPointX, kBottomPointY));
  EXPECT_TRUE(canvas.IsBlack(kLastPointX, kTopPointY));
  EXPECT_FALSE(canvas.IsBlack(kFirstPointX, kTopPointY));
  EXPECT_FALSE(canvas.IsBlack(kLastPointX, kBottomPointY));
}

TEST(TrendScreenDraw, PartialHistoryFillsFromTheRight)
{
  HourlyHistory history;
  FillHours(history, {2000, 2000});
  ASSERT_EQ(history.Count(), 2u);

  TrendScreenCanvas canvas;
  TrendScreenData data;
  data.history = &history;
  DrawTrendScreen(canvas.Get(), data);

  // Two flat periods: a short line at mid height against the right edge,
  // with the left of the plot still blank.
  EXPECT_TRUE(canvas.IsBlack(kLastPointX, (kTopPointY + kBottomPointY) / 2));
  EXPECT_TRUE(canvas.IsBlack(PointXForSlot(22), (kTopPointY + kBottomPointY) / 2));
  EXPECT_FALSE(canvas.ColumnHasBlack(kFirstPointX, kPlotTop + 1, kPlotBottom - 1));
}

TEST(TrendScreenDraw, APeriodWithNoReadingLeavesAGap)
{
  HourlyHistory history;
  std::vector<std::optional<std::int32_t>> temperatures(HourlyHistory::kBuckets, 2000);
  temperatures[12] = std::nullopt;
  FillHours(history, temperatures);

  TrendScreenCanvas canvas;
  TrendScreenData data;
  data.history = &history;
  DrawTrendScreen(canvas.Get(), data);

  // Nothing is drawn in the missing period's column: the line is broken
  // rather than bridged across it.
  const int gapX = PointXForSlot(12);
  EXPECT_FALSE(canvas.RegionHasBlack(PointXForSlot(11) + 2, PointXForSlot(13) - 2, kPlotTop + 1, kPlotBottom - 1));
  EXPECT_FALSE(canvas.ColumnHasBlack(gapX, kPlotTop + 1, kPlotBottom - 1));
  // Its neighbours are still plotted and still joined to their own.
  EXPECT_TRUE(canvas.ColumnHasBlack(PointXForSlot(11), kPlotTop + 1, kPlotBottom - 1));
  EXPECT_TRUE(canvas.ColumnHasBlack(PointXForSlot(13), kPlotTop + 1, kPlotBottom - 1));
}
