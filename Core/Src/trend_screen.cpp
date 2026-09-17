#include "trend_screen.hpp"

#include "font_5x7.hpp"
#include "text.hpp"
#include "text_format.hpp"

namespace
{

// Layout (200 x 200), sharing the main screen's margins and rule positions
// so the two screens line up when they alternate.
constexpr int kSmallScale = 2;
constexpr int kMargin = 4;
constexpr int kTopTextY = 6;
constexpr int kTopRuleY = 24;
constexpr int kRangeTextY = 32;
constexpr int kPlotX = 4;
constexpr int kPlotY = 52;
constexpr int kPlotWidth = 192;
constexpr int kPlotHeight = 96;
constexpr int kBottomRuleY = 156;
constexpr int kBottomTextY = 166;
constexpr int kRuleThickness = 2;

// Points sit inside the frame, clear of it at every extreme.
constexpr int kFirstPointX = kPlotX + 1;
constexpr int kLastPointX = kPlotX + kPlotWidth - 2;
constexpr int kTopPointY = kPlotY + 3;
constexpr int kBottomPointY = kPlotY + kPlotHeight - 4;
constexpr int kMarkerRadius = 1; // a 3x3 block, big enough to read on e-paper

const char *MetricName(Metric metric)
{
  switch (metric)
  {
  case Metric::Temperature:
    return "TEMPERATURE";
  case Metric::Humidity:
    return "HUMIDITY";
  case Metric::Pressure:
    return "PRESSURE";
  case Metric::Light:
    break;
  }
  return "LIGHT";
}

// Light is logged in hundredths of a lux but spans four decades, so it is
// shown whole; the others keep one decimal place, as on the main screen.
int MetricDecimals(Metric metric)
{
  return metric == Metric::Light ? 0 : 1;
}

void FormatClock(char *out, std::size_t size, std::uint8_t hour, std::uint8_t minute)
{
  char digits[3];
  text_format::TwoDigits(digits, hour);
  text_format::Copy(out, size, digits);
  text_format::Append(out, size, ":");
  text_format::TwoDigits(digits, minute);
  text_format::Append(out, size, digits);
}

// The label for one end of the range: "HIGH 24.1". A saturated light average
// is a lower bound, so it is marked as one.
void FormatRange(char *out, std::size_t size, const char *label, std::optional<std::int32_t> value, int decimals,
                 bool saturated)
{
  text_format::Copy(out, size, label);
  text_format::Append(out, size, " ");
  if (!value)
  {
    text_format::Append(out, size, decimals == 0 ? "--" : "--.-");
    return;
  }
  char number[12];
  if (!text_format::Centi(number, sizeof(number), *value, decimals))
  {
    text_format::Append(out, size, decimals == 0 ? "--" : "--.-");
    return;
  }
  if (saturated)
  {
    text_format::Append(out, size, ">");
  }
  text_format::Append(out, size, number);
}

int PointX(std::size_t slot)
{
  const int span = kLastPointX - kFirstPointX;
  return kFirstPointX + static_cast<int>(slot) * span / static_cast<int>(HourlyHistory::kBuckets - 1);
}

void DrawSmallRightAligned(MonoFramebuffer &canvas, int y, const char *text)
{
  const int x = canvas.Width() - kMargin - TextWidth(kFont5x7, text, kSmallScale);
  DrawText(canvas, kFont5x7, x, y, text, Color::Black, kSmallScale);
}

void DrawCentred(MonoFramebuffer &canvas, int y, const char *text)
{
  const int x = (canvas.Width() - TextWidth(kFont5x7, text, kSmallScale)) / 2;
  DrawText(canvas, kFont5x7, x, y, text, Color::Black, kSmallScale);
}

// The values actually plotted, with their range. Periods with no reading for
// this metric leave a gap in the line.
struct Series
{
  std::optional<std::int32_t> values[HourlyHistory::kBuckets]; // by slot, newest at the right
  std::optional<std::int32_t> low;
  std::optional<std::int32_t> high;
  bool saturated = false;
  bool empty = true;
};

Series BuildSeries(const TrendScreenData &data)
{
  Series series;
  if (data.history == nullptr)
  {
    return series;
  }

  const std::size_t count = data.history->Count();
  // Fewer than a full day of periods are drawn against the right-hand edge,
  // so the plot fills from the right as time passes and "now" is always in
  // the same place.
  const std::size_t firstSlot = HourlyHistory::kBuckets - count;
  for (std::size_t i = 0; i < count; ++i)
  {
    const HourlyHistory::Bucket &bucket = data.history->At(i);
    const std::optional<std::int32_t> value = bucket.Value(data.metric);
    series.values[firstSlot + i] = value;
    if (!value)
    {
      continue;
    }
    if (series.empty)
    {
      series.low = value;
      series.high = value;
      series.empty = false;
    }
    else
    {
      series.low = *value < *series.low ? value : series.low;
      series.high = *value > *series.high ? value : series.high;
    }
    if (data.metric == Metric::Light && bucket.lightSaturated)
    {
      series.saturated = true;
    }
  }
  return series;
}

} // namespace

int TrendPlotY(std::int32_t value, std::int32_t low, std::int32_t high, int top, int bottom)
{
  if (high <= low)
  {
    return (top + bottom) / 2;
  }
  if (value <= low)
  {
    return bottom;
  }
  if (value >= high)
  {
    return top;
  }
  // 64-bit: a light range can be several million hundredths of a lux, and
  // the plot is about a hundred pixels tall.
  const std::int64_t offset = static_cast<std::int64_t>(value) - low;
  const std::int64_t range = static_cast<std::int64_t>(high) - low;
  const std::int64_t rows = static_cast<std::int64_t>(bottom - top);
  return bottom - static_cast<int>((offset * rows + range / 2) / range);
}

TrendScreenText BuildTrendScreenText(const TrendScreenData &data)
{
  TrendScreenText text{};
  const Series series = BuildSeries(data);
  const int decimals = MetricDecimals(data.metric);

  text_format::Copy(text.title, sizeof(text.title), MetricName(data.metric));

  if (data.time)
  {
    FormatClock(text.time, sizeof(text.time), data.time->hour, data.time->minute);
  }
  else
  {
    text_format::Copy(text.time, sizeof(text.time), "--:--");
  }

  // Only the high is marked as a lower bound: a saturated light average is
  // one, and saturation caps what can be measured, not what can be missed
  // below it.
  FormatRange(text.high, sizeof(text.high), "HIGH", series.high, decimals, series.saturated);
  FormatRange(text.low, sizeof(text.low), "LOW", series.low, decimals, false);

  const std::size_t count = data.history == nullptr ? 0 : data.history->Count();
  if (count == 0)
  {
    text_format::Copy(text.from, sizeof(text.from), "--:--");
    text_format::Copy(text.to, sizeof(text.to), "--:--");
  }
  else
  {
    // Labelled by period start, including periods that hold no readings:
    // the axis shows the span covered, not just where the readings are.
    const HourlyHistory::Bucket &oldest = data.history->At(0);
    const HourlyHistory::Bucket &newest = data.history->At(count - 1);
    FormatClock(text.from, sizeof(text.from), oldest.hour, oldest.minute);
    FormatClock(text.to, sizeof(text.to), newest.hour, newest.minute);
  }

  return text;
}

void DrawTrendScreen(MonoFramebuffer &canvas, const TrendScreenData &data)
{
  const TrendScreenText text = BuildTrendScreenText(data);
  const Series series = BuildSeries(data);
  const int width = canvas.Width();

  canvas.Fill(Color::White);

  DrawText(canvas, kFont5x7, kMargin, kTopTextY, text.title, Color::Black, kSmallScale);
  DrawSmallRightAligned(canvas, kTopTextY, text.time);
  canvas.FillRect(0, kTopRuleY, width, kRuleThickness, Color::Black);

  DrawText(canvas, kFont5x7, kMargin, kRangeTextY, text.high, Color::Black, kSmallScale);
  DrawSmallRightAligned(canvas, kRangeTextY, text.low);

  canvas.Rect(kPlotX, kPlotY, kPlotWidth, kPlotHeight, Color::Black);

  if (series.empty)
  {
    DrawCentred(canvas, kPlotY + kPlotHeight / 2 - 7, "NO DATA YET");
  }
  else
  {
    bool havePrevious = false;
    int previousX = 0;
    int previousY = 0;
    for (std::size_t slot = 0; slot < HourlyHistory::kBuckets; ++slot)
    {
      if (!series.values[slot])
      {
        havePrevious = false; // a gap breaks the line rather than bridging it
        continue;
      }
      const int x = PointX(slot);
      const int y = TrendPlotY(*series.values[slot], *series.low, *series.high, kTopPointY, kBottomPointY);
      if (havePrevious)
      {
        canvas.Line(previousX, previousY, x, y, Color::Black);
      }
      canvas.FillRect(x - kMarkerRadius, y - kMarkerRadius, 2 * kMarkerRadius + 1, 2 * kMarkerRadius + 1,
                      Color::Black);
      previousX = x;
      previousY = y;
      havePrevious = true;
    }
  }

  canvas.FillRect(0, kBottomRuleY, width, kRuleThickness, Color::Black);
  DrawText(canvas, kFont5x7, kMargin, kBottomTextY, text.from, Color::Black, kSmallScale);
  DrawSmallRightAligned(canvas, kBottomTextY, text.to);
}
