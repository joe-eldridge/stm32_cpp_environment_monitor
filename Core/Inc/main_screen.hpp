#pragma once

#include <cstdint>
#include <optional>

#include "bme280.hpp"
#include "ds3231.hpp"
#include "mono_framebuffer.hpp"
#include "veml7700.hpp"

// Everything the main screen shows. A missing reading is drawn as dashes.
struct MainScreenData
{
  std::optional<Ds3231::DateTime> time; // when this update happened
  std::optional<Bme280::Measurements> climate;
  std::optional<Veml7700::Reading> light;
  bool storageOk = false; // the last log write succeeded
  std::uint8_t wakeIntervalMinutes = 0;
};

// The screen's text, kept separate from its layout so it can be tested
// exactly. Units are fixed and drawn by DrawMainScreen().
struct MainScreenText
{
  char date[12];        // "Thu 17 Sep"
  char time[6];         // "14:35"
  char temperature[8];  // "23.8"
  char humidity[8];     // "50.2"
  char pressure[8];     // "1008.9"
  char light[8];        // "1204", or ">4404" when the sensor saturated
  char storage[6];      // "SD OK" / "NO SD"
  char interval[10];    // "1 min"
};

MainScreenText BuildMainScreenText(const MainScreenData &data);

// Redraws the whole 200x200 screen.
void DrawMainScreen(MonoFramebuffer &canvas, const MainScreenData &data);
