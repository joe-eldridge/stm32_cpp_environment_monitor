#include <gtest/gtest.h>

#include "button.hpp"

namespace
{

constexpr std::uint16_t kDebounceMs = 5;
constexpr std::uint16_t kLongPressMs = 800;

// Holds `pressed` for `durationMs`, sampling every millisecond, and returns
// the last event that wasn't None.
Button::Event Hold(Button &button, bool pressed, std::uint32_t &nowMs, std::uint32_t durationMs)
{
  Button::Event last = Button::Event::None;
  for (std::uint32_t i = 0; i < durationMs; ++i)
  {
    const Button::Event event = button.Update(pressed, nowMs++);
    if (event != Button::Event::None)
    {
      last = event;
    }
  }
  return last;
}

} // namespace

TEST(ButtonTest, ReportsAClickOnRelease)
{
  Button button(kDebounceMs, kLongPressMs);
  std::uint32_t now = 1000;

  // Nothing is reported while the button is still down.
  EXPECT_EQ(Hold(button, true, now, 50), Button::Event::None);
  EXPECT_TRUE(button.IsPressed());
  EXPECT_EQ(Hold(button, false, now, 50), Button::Event::Click);
  EXPECT_FALSE(button.IsPressed());
}

TEST(ButtonTest, ReportsALongPressWhileStillHeld)
{
  Button button(kDebounceMs, kLongPressMs);
  std::uint32_t now = 0;

  EXPECT_EQ(Hold(button, true, now, kLongPressMs - 10), Button::Event::None);
  EXPECT_EQ(Hold(button, true, now, 20), Button::Event::LongPress);
  // Only once, however long it's held for.
  EXPECT_EQ(Hold(button, true, now, 1000), Button::Event::None);
  // And releasing after a long press is not also a click.
  EXPECT_EQ(Hold(button, false, now, 50), Button::Event::None);
}

TEST(ButtonTest, BounceOnPressDoesNotProduceExtraClicks)
{
  Button button(kDebounceMs, kLongPressMs);
  std::uint32_t now = 0;
  int clicks = 0;

  // Contacts rattle for a few milliseconds, then settle closed.
  for (int i = 0; i < 8; ++i)
  {
    if (button.Update(i % 2 == 0, now++) == Button::Event::Click)
    {
      ++clicks;
    }
  }
  Hold(button, true, now, 50);
  if (Hold(button, false, now, 50) == Button::Event::Click)
  {
    ++clicks;
  }
  EXPECT_EQ(clicks, 1);
}

TEST(ButtonTest, BounceOnReleaseDoesNotProduceExtraClicks)
{
  Button button(kDebounceMs, kLongPressMs);
  std::uint32_t now = 0;
  Hold(button, true, now, 50);

  int clicks = 0;
  for (int i = 0; i < 8; ++i)
  {
    if (button.Update(i % 2 != 0, now++) == Button::Event::Click)
    {
      ++clicks;
    }
  }
  if (Hold(button, false, now, 50) == Button::Event::Click)
  {
    ++clicks;
  }
  EXPECT_EQ(clicks, 1);
}

TEST(ButtonTest, GlitchShorterThanTheDebounceWindowIsIgnored)
{
  Button button(kDebounceMs, kLongPressMs);
  std::uint32_t now = 0;

  EXPECT_EQ(Hold(button, true, now, kDebounceMs - 2), Button::Event::None);
  EXPECT_EQ(Hold(button, false, now, 50), Button::Event::None);
  EXPECT_FALSE(button.IsPressed());
}

TEST(ButtonTest, LongPressIsTimedFromTheDebouncedPressNotTheGlitch)
{
  Button button(kDebounceMs, kLongPressMs);
  std::uint32_t now = 0;

  // A glitch well before the real press must not count towards the hold.
  EXPECT_EQ(Hold(button, true, now, kDebounceMs - 2), Button::Event::None);
  Hold(button, false, now, 100);
  EXPECT_EQ(Hold(button, true, now, kLongPressMs - 10), Button::Event::None);
  EXPECT_EQ(Hold(button, true, now, 20), Button::Event::LongPress);
}

TEST(ButtonTest, WorksAcrossTickWraparound)
{
  Button button(kDebounceMs, kLongPressMs);
  // The 32-bit millisecond tick wraps about every 49 days.
  std::uint32_t now = 0xFFFFFFFFu - 20;

  EXPECT_EQ(Hold(button, true, now, 50), Button::Event::None);
  EXPECT_TRUE(button.IsPressed());
  EXPECT_EQ(Hold(button, true, now, kLongPressMs), Button::Event::LongPress);
  EXPECT_EQ(Hold(button, false, now, 50), Button::Event::None);

  EXPECT_EQ(Hold(button, true, now, 50), Button::Event::None);
  EXPECT_EQ(Hold(button, false, now, 50), Button::Event::Click);
}
