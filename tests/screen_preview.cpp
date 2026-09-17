// Renders the display's screens to PBM image files, to check layouts
// without flashing the board:
//
//   build/tests/screen_preview [output-directory]
//
// Not a test - nothing is asserted.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "main_screen.hpp"
#include "ssd1681.hpp"
#include "trend_screen.hpp"

namespace
{

bool WritePbm(const std::string &path, const MonoFramebuffer &canvas)
{
  std::FILE *file = std::fopen(path.c_str(), "wb");
  if (file == nullptr)
  {
    return false;
  }
  std::fprintf(file, "P4\n%u %u\n", canvas.Width(), canvas.Height());
  // PBM uses 1 = black, the inverse of the framebuffer.
  for (std::size_t i = 0; i < canvas.SizeBytes(); ++i)
  {
    std::fputc(static_cast<unsigned char>(~canvas.Data()[i]), file);
  }
  return std::fclose(file) == 0;
}

} // namespace

int main(int argc, char **argv)
{
  const std::string directory = argc > 1 ? argv[1] : ".";
  std::vector<std::uint8_t> memory(Ssd1681::kImageBytes);
  MonoFramebuffer canvas(memory.data(), Ssd1681::kWidth, Ssd1681::kHeight);

  MainScreenData normal;
  normal.time = Ds3231::DateTime{2026, 9, 17, 14, 35, 0};
  normal.climate = Bme280::Measurements{2384, 100893, 5020};
  normal.light = Veml7700::Reading{120180, false};
  normal.storageOk = true;
  normal.wakeIntervalMinutes = 1;

  MainScreenData extremes = normal;
  extremes.climate = Bme280::Measurements{-4000, 110000, 10000};
  extremes.light = Veml7700::Reading{440395, true};
  extremes.wakeIntervalMinutes = 60;

  MainScreenData empty;
  empty.wakeIntervalMinutes = 5;

  const struct
  {
    const char *name;
    const MainScreenData &data;
  } mainScreens[] = {{"main_normal", normal}, {"main_extremes", extremes}, {"main_no_data", empty}};

  // A day of temperatures: an overnight low around 05:00 and an afternoon
  // peak, with one hour missing to show how a gap is drawn.
  HourlyHistory day;
  for (int hour = 0; hour <= 24; ++hour)
  {
    Ds3231::DateTime time{};
    time.year = 2026;
    time.month = 9;
    time.date = static_cast<std::uint8_t>(17 + hour / 24);
    time.hour = static_cast<std::uint8_t>(hour % 24);
    for (int minute = 0; minute < 60; minute += 10)
    {
      time.minute = static_cast<std::uint8_t>(minute);
      if (hour == 14 || hour > 23)
      {
        day.Add(time, std::nullopt, std::nullopt); // sensor failed all hour
        continue;
      }
      // A smooth day: coldest and darkest at 05:00, warmest at 17:00.
      const double timeOfDay = hour + minute / 60.0;
      const double phase = 2.0 * M_PI * (timeOfDay - 11.0) / 24.0;
      const std::int32_t temperature = static_cast<std::int32_t>(std::lround(2100 + 550 * std::sin(phase)));
      const double sunAngle = std::max(0.0, std::sin(2.0 * M_PI * (timeOfDay - 6.0) / 24.0));
      const std::int32_t lux = static_cast<std::int32_t>(std::lround(500 + 460000 * sunAngle));
      day.Add(time, Bme280::Measurements{temperature, 100893 + hour * 4, 5020 - hour * 20},
              Veml7700::Reading{std::min(lux, 440395), lux >= 440395});
    }
  }

  HourlyHistory firstHours;
  for (int hour = 0; hour <= 3; ++hour)
  {
    Ds3231::DateTime time{};
    time.year = 2026;
    time.month = 9;
    time.date = 17;
    time.hour = static_cast<std::uint8_t>(9 + hour);
    firstHours.Add(time, Bme280::Measurements{2100 + hour * 40, 100893, 5020}, std::nullopt);
  }

  const HourlyHistory noHistory;
  TrendScreenData temperatureTrend;
  temperatureTrend.history = &day;
  temperatureTrend.time = Ds3231::DateTime{2026, 9, 18, 0, 0, 0};

  TrendScreenData lightTrend = temperatureTrend;
  lightTrend.metric = Metric::Light;

  TrendScreenData partial;
  partial.history = &firstHours;
  partial.time = Ds3231::DateTime{2026, 9, 17, 12, 0, 0};

  TrendScreenData none;
  none.history = &noHistory;
  none.time = Ds3231::DateTime{2026, 9, 17, 9, 5, 0};

  const struct
  {
    const char *name;
    const TrendScreenData &data;
  } trendScreens[] = {{"trend_temperature", temperatureTrend},
                      {"trend_light", lightTrend},
                      {"trend_partial", partial},
                      {"trend_no_data", none}};

  for (const auto &screen : mainScreens)
  {
    DrawMainScreen(canvas, screen.data);
    const std::string path = directory + "/" + screen.name + ".pbm";
    if (!WritePbm(path, canvas))
    {
      std::fprintf(stderr, "failed to write %s\n", path.c_str());
      return 1;
    }
    std::printf("wrote %s\n", path.c_str());
  }

  for (const auto &screen : trendScreens)
  {
    DrawTrendScreen(canvas, screen.data);
    const std::string path = directory + "/" + screen.name + ".pbm";
    if (!WritePbm(path, canvas))
    {
      std::fprintf(stderr, "failed to write %s\n", path.c_str());
      return 1;
    }
    std::printf("wrote %s\n", path.c_str());
  }
  return 0;
}
