#include <gtest/gtest.h>

#include <vector>

#include "main_screen.hpp"

namespace
{

MainScreenData FullData()
{
  MainScreenData data;
  data.time = Ds3231::DateTime{2026, 9, 17, 14, 5, 30};
  data.climate = Bme280::Measurements{2384, 100893, 5020};
  data.light = Veml7700::Reading{120180, false};
  data.storageOk = true;
  data.wakeIntervalMinutes = 1;
  return data;
}

} // namespace

TEST(MainScreenText, FormatsAllReadings)
{
  const MainScreenText text = BuildMainScreenText(FullData());
  EXPECT_STREQ(text.date, "Thu 17 Sep");
  EXPECT_STREQ(text.time, "14:05");
  EXPECT_STREQ(text.temperature, "23.8");
  EXPECT_STREQ(text.humidity, "50.2");
  EXPECT_STREQ(text.pressure, "1008.9");
  EXPECT_STREQ(text.light, "1202");
  EXPECT_STREQ(text.storage, "SD OK");
  EXPECT_STREQ(text.interval, "1 min");
}

TEST(MainScreenText, SingleDigitDayHasNoLeadingZero)
{
  MainScreenData data = FullData();
  data.time = Ds3231::DateTime{2026, 1, 1, 0, 0, 0};
  const MainScreenText text = BuildMainScreenText(data);
  EXPECT_STREQ(text.date, "Thu 1 Jan");
  EXPECT_STREQ(text.time, "00:00");
}

TEST(MainScreenText, MissingValuesShowDashes)
{
  MainScreenData data;
  data.wakeIntervalMinutes = 5;
  const MainScreenText text = BuildMainScreenText(data);
  EXPECT_STREQ(text.date, "--- -- ---");
  EXPECT_STREQ(text.time, "--:--");
  EXPECT_STREQ(text.temperature, "--.-");
  EXPECT_STREQ(text.humidity, "--.-");
  EXPECT_STREQ(text.pressure, "--.-");
  EXPECT_STREQ(text.light, "--");
  EXPECT_STREQ(text.storage, "NO SD");
  EXPECT_STREQ(text.interval, "5 min");
}

TEST(MainScreenText, InvalidDateShowsDashesButKeepsTime)
{
  MainScreenData data = FullData();
  data.time = Ds3231::DateTime{2026, 13, 1, 12, 30, 0};
  const MainScreenText text = BuildMainScreenText(data);
  EXPECT_STREQ(text.date, "--- -- ---");
  EXPECT_STREQ(text.time, "12:30");
}

TEST(MainScreenText, NegativeTemperature)
{
  MainScreenData data = FullData();
  data.climate->temperatureCenti = -1234;
  EXPECT_STREQ(BuildMainScreenText(data).temperature, "-12.3");
}

TEST(MainScreenText, SaturatedLightIsMarked)
{
  MainScreenData data = FullData();
  data.light = Veml7700::Reading{440395, true};
  EXPECT_STREQ(BuildMainScreenText(data).light, ">4404");
}

TEST(MainScreenText, WidestValuesFit)
{
  MainScreenData data = FullData();
  data.climate = Bme280::Measurements{-4000, 110000, 10000}; // BME280 range limits
  data.wakeIntervalMinutes = 60;
  const MainScreenText text = BuildMainScreenText(data);
  EXPECT_STREQ(text.temperature, "-40.0");
  EXPECT_STREQ(text.pressure, "1100.0");
  EXPECT_STREQ(text.humidity, "100.0");
  EXPECT_STREQ(text.interval, "60 min");
}

TEST(MainScreen, DrawsOnlyInsideTheCanvas)
{
  // A canvas with guard bytes either side: drawing must never touch them.
  std::vector<std::uint8_t> memory(5000 + 16, 0xA5);
  MonoFramebuffer canvas(memory.data() + 8, 200, 200);
  DrawMainScreen(canvas, FullData());
  for (int i = 0; i < 8; ++i)
  {
    EXPECT_EQ(memory[static_cast<std::size_t>(i)], 0xA5);
    EXPECT_EQ(memory[memory.size() - 1 - static_cast<std::size_t>(i)], 0xA5);
  }

  int black = 0;
  for (int y = 0; y < 200; ++y)
  {
    for (int x = 0; x < 200; ++x)
    {
      black += canvas.GetPixel(x, y) == Color::Black ? 1 : 0;
    }
  }
  EXPECT_GT(black, 1000); // rules plus text
  EXPECT_LT(black, 20000);
}
