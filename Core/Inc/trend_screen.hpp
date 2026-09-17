#pragma once

#include <cstdint>
#include <optional>

#include "ds3231.hpp"
#include "hourly_history.hpp"
#include "mono_framebuffer.hpp"

// The trend screen: one metric's completed averages plotted against time,
// with the range and the span it covers. Shown in place of the main screen
// on the first wake of each averaging period.
struct TrendScreenData
{
  const HourlyHistory *history = nullptr;
  Metric metric = Metric::Temperature;
  std::optional<Ds3231::DateTime> time; // when this update happened
};

// As on the main screen, the text is built separately from the layout so it
// can be tested exactly.
struct TrendScreenText
{
  char title[12]; // "TEMPERATURE"
  char time[6];   // "14:35"
  char high[12];  // "HIGH 24.1", or ">" prefixed when a light average saturated
  char low[12];   // "LOW 18.3"
  char from[6];   // start of the oldest period plotted, "09:00"
  char to[6];     // start of the newest, "13:00"
};

TrendScreenText BuildTrendScreenText(const TrendScreenData &data);

// Where a value sits on the plot's vertical axis. `top` and `bottom` are the
// pixel rows the highest and lowest values map to (top < bottom). A flat
// series, where low == high, is drawn down the middle rather than pinned to
// an edge.
int TrendPlotY(std::int32_t value, std::int32_t low, std::int32_t high, int top, int bottom);

// Redraws the whole 200x200 screen.
void DrawTrendScreen(MonoFramebuffer &canvas, const TrendScreenData &data);
