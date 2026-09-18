#include "main_screen.hpp"

#include "font_5x7.hpp"
#include "text.hpp"
#include "text_format.hpp"

namespace
{

constexpr const char *kDayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char *kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

constexpr const char kMissingDecimal[] = "--.-";
constexpr const char kMissingInteger[] = "--";

void FormatReading(char *out, std::size_t size, const std::optional<std::int32_t> &centi, int decimals)
{
  if (!centi || !text_format::Centi(out, size, *centi, decimals))
  {
    text_format::Copy(out, size, decimals == 0 ? kMissingInteger : kMissingDecimal);
  }
}

void FormatDate(char *out, std::size_t size, const Ds3231::DateTime &time)
{
  const int weekday = text_format::DayOfWeek(time.year, time.month, time.date);
  if (weekday < 0 || time.month < 1 || time.month > 12)
  {
    text_format::Copy(out, size, "--- -- ---");
    return;
  }
  char day[3];
  text_format::TwoDigits(day, time.date);
  text_format::Copy(out, size, kDayNames[weekday]);
  text_format::Append(out, size, " ");
  text_format::Append(out, size, day[0] == '0' ? day + 1 : day);
  text_format::Append(out, size, " ");
  text_format::Append(out, size, kMonthNames[time.month - 1]);
}

// Layout (200 x 200). Small text is the 5x7 font at 2x, readings at 3x.
constexpr int kSmallScale = 2;
constexpr int kLargeScale = 3;
constexpr int kMargin = 4;
constexpr int kTopTextY = 6;
constexpr int kTopRuleY = 24;
constexpr int kFirstReadingY = 34;
constexpr int kReadingPitch = 30;
constexpr int kValueRightX = 110;
constexpr int kUnitX = 118;
// Digits sit on font rows 0-5: bottom-align the smaller unit text with them.
constexpr int kUnitYOffset = 6 * (kLargeScale - kSmallScale);
constexpr int kBottomRuleY = 156;
constexpr int kBottomTextY = 166;
constexpr int kRuleThickness = 2;

void DrawReading(MonoFramebuffer &canvas, int line, const char *value, const char *unit)
{
  const int y = kFirstReadingY + line * kReadingPitch;
  const int x = kValueRightX - TextWidth(kFont5x7, value, kLargeScale);
  DrawText(canvas, kFont5x7, x, y, value, Color::Black, kLargeScale);
  DrawText(canvas, kFont5x7, kUnitX, y + kUnitYOffset, unit, Color::Black, kSmallScale);
}

void DrawSmallRightAligned(MonoFramebuffer &canvas, int y, const char *text)
{
  const int x = canvas.Width() - kMargin - TextWidth(kFont5x7, text, kSmallScale);
  DrawText(canvas, kFont5x7, x, y, text, Color::Black, kSmallScale);
}

} // namespace

MainScreenText BuildMainScreenText(const MainScreenData &data)
{
  MainScreenText text{};

  if (data.time)
  {
    FormatDate(text.date, sizeof(text.date), *data.time);
    char hours[3];
    char minutes[3];
    text_format::TwoDigits(hours, data.time->hour);
    text_format::TwoDigits(minutes, data.time->minute);
    text_format::Copy(text.time, sizeof(text.time), hours);
    text_format::Append(text.time, sizeof(text.time), ":");
    text_format::Append(text.time, sizeof(text.time), minutes);
  }
  else
  {
    text_format::Copy(text.date, sizeof(text.date), "--- -- ---");
    text_format::Copy(text.time, sizeof(text.time), "--:--");
  }

  std::optional<std::int32_t> temperature;
  std::optional<std::int32_t> humidity;
  std::optional<std::int32_t> pressure;
  if (data.climate)
  {
    temperature = data.climate->temperatureCenti;
    humidity = data.climate->humidityCentiPct;
    pressure = data.climate->pressureCentiHpa;
  }
  FormatReading(text.temperature, sizeof(text.temperature), temperature, 1);
  FormatReading(text.humidity, sizeof(text.humidity), humidity, 1);
  FormatReading(text.pressure, sizeof(text.pressure), pressure, 1);

  if (data.light)
  {
    char value[sizeof(text.light)];
    FormatReading(value, sizeof(value), data.light->luxCenti, 0);
    text_format::Copy(text.light, sizeof(text.light), data.light->saturated ? ">" : "");
    text_format::Append(text.light, sizeof(text.light), value);
  }
  else
  {
    text_format::Copy(text.light, sizeof(text.light), kMissingInteger);
  }

  text_format::Copy(text.storage, sizeof(text.storage), data.storageOk ? "SD OK" : "NO SD");

  char minutes[4] = "";
  text_format::Integer(minutes, sizeof(minutes), data.wakeIntervalMinutes);
  text_format::Copy(text.interval, sizeof(text.interval), minutes);
  text_format::Append(text.interval, sizeof(text.interval), " min");

  return text;
}

void DrawMainScreen(MonoFramebuffer &canvas, const MainScreenData &data)
{
  const MainScreenText text = BuildMainScreenText(data);
  const int width = canvas.Width();

  canvas.Fill(Color::White);

  DrawText(canvas, kFont5x7, kMargin, kTopTextY, text.date, Color::Black, kSmallScale);
  DrawSmallRightAligned(canvas, kTopTextY, text.time);
  canvas.FillRect(0, kTopRuleY, width, kRuleThickness, Color::Black);

  const char celsius[] = {kDegreeSign, 'C', '\0'};
  DrawReading(canvas, 0, text.temperature, celsius);
  DrawReading(canvas, 1, text.humidity, "%RH");
  DrawReading(canvas, 2, text.pressure, "hPa");
  DrawReading(canvas, 3, text.light, "lx");

  canvas.FillRect(0, kBottomRuleY, width, kRuleThickness, Color::Black);
  DrawText(canvas, kFont5x7, kMargin, kBottomTextY, text.storage, Color::Black, kSmallScale);
  DrawSmallRightAligned(canvas, kBottomTextY, text.interval);
}
