#include <gtest/gtest.h>

#include <vector>

#include "epaper_display.hpp"
#include "fake_hal.hpp"
#include "fake_spi_display.hpp"

namespace
{

using Bytes = std::vector<std::uint8_t>;
using Mode = Ssd1681::RefreshMode;

class EpaperDisplayTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    fake_hal::SetTick(0);
    canvas.Fill(Color::White);
  }

  // Everything the display sent after the most recent wake.
  std::vector<RecordingSpiDevice::Command> LastUpdate() const
  {
    std::size_t start = 0;
    for (std::size_t i = 0; i < spi.commands.size(); ++i)
    {
      if (spi.commands[i].code == 0x12)
      {
        start = i;
      }
    }
    return {spi.commands.begin() + static_cast<std::ptrdiff_t>(start), spi.commands.end()};
  }

  static Bytes DataFor(const std::vector<RecordingSpiDevice::Command> &commands, std::uint8_t code)
  {
    for (const auto &c : commands)
    {
      if (c.code == code)
      {
        return c.data;
      }
    }
    return {};
  }

  FakeOutputPin dataCommand;
  FakeOutputPin reset;
  FakeInputPin busy; // never busy
  RecordingSpiDevice spi{dataCommand};
  Ssd1681 panel{spi, dataCommand, reset, busy};
  Bytes memory = Bytes(Ssd1681::kImageBytes);
  MonoFramebuffer canvas{memory.data(), Ssd1681::kWidth, Ssd1681::kHeight};
  EpaperDisplay display{panel, canvas};
};

auto DrawFilled(Color color)
{
  return [color](MonoFramebuffer &c) { c.Fill(color); };
}

} // namespace

TEST_F(EpaperDisplayTest, FirstUpdateIsAlwaysFull)
{
  ASSERT_TRUE(display.Update(Mode::Partial, DrawFilled(Color::Black)));
  EXPECT_EQ(DataFor(LastUpdate(), 0x22), (Bytes{0xF7}));
}

TEST_F(EpaperDisplayTest, FullUpdateWritesNewImageToBothRams)
{
  ASSERT_TRUE(display.Update(Mode::Full, DrawFilled(Color::Black)));
  const auto update = LastUpdate();
  EXPECT_EQ(DataFor(update, 0x24), Bytes(Ssd1681::kImageBytes, 0x00));
  EXPECT_EQ(DataFor(update, 0x26), Bytes(Ssd1681::kImageBytes, 0x00));
}

TEST_F(EpaperDisplayTest, PartialUpdateSendsPreviousFrameThenNewFrame)
{
  ASSERT_TRUE(display.Update(Mode::Full, DrawFilled(Color::Black)));
  ASSERT_TRUE(display.Update(Mode::Partial, [](MonoFramebuffer &c) { c.SetPixel(0, 0, Color::White); }));

  const auto update = LastUpdate();
  EXPECT_EQ(DataFor(update, 0x22), (Bytes{0xFF}));

  Bytes expectedNew(Ssd1681::kImageBytes, 0x00);
  expectedNew[0] = 0x80;
  EXPECT_EQ(DataFor(update, 0x26), Bytes(Ssd1681::kImageBytes, 0x00)); // what the panel showed
  EXPECT_EQ(DataFor(update, 0x24), expectedNew);                       // what it should show

  // The reference image has to be uploaded before the new one.
  std::size_t redIndex = 0;
  std::size_t bwIndex = 0;
  for (std::size_t i = 0; i < update.size(); ++i)
  {
    redIndex = update[i].code == 0x26 ? i : redIndex;
    bwIndex = update[i].code == 0x24 ? i : bwIndex;
  }
  EXPECT_LT(redIndex, bwIndex);
}

TEST_F(EpaperDisplayTest, UpdateEndsWithPanelInDeepSleep)
{
  ASSERT_TRUE(display.Update(Mode::Full, DrawFilled(Color::White)));
  EXPECT_EQ(spi.commands.back().code, 0x10);
}

TEST_F(EpaperDisplayTest, FailedRefreshStillSleepsAndForcesNextUpdateFull)
{
  ASSERT_TRUE(display.Update(Mode::Full, DrawFilled(Color::White)));

  // BUSY never clears once the refresh starts.
  bool refreshing = false;
  spi.onCommand = [&refreshing](std::uint8_t code) { refreshing = refreshing || code == 0x20; };
  busy.isHigh = [&refreshing] { return refreshing; };
  EXPECT_FALSE(display.Update(Mode::Partial, DrawFilled(Color::Black)));
  EXPECT_EQ(spi.commands.back().code, 0x10);

  spi.onCommand = nullptr;
  busy.isHigh = nullptr;
  ASSERT_TRUE(display.Update(Mode::Partial, DrawFilled(Color::White)));
  EXPECT_EQ(DataFor(LastUpdate(), 0x22), (Bytes{0xF7}));
}

TEST_F(EpaperDisplayTest, FailedWakeStillSleepsAndForcesNextUpdateFull)
{
  ASSERT_TRUE(display.Update(Mode::Full, DrawFilled(Color::White)));

  busy.isHigh = [] { return true; }; // software reset never completes
  bool drawn = false;
  EXPECT_FALSE(display.Update(Mode::Partial, [&drawn](MonoFramebuffer &) { drawn = true; }));
  EXPECT_FALSE(drawn);
  EXPECT_EQ(spi.commands.back().code, 0x10);

  busy.isHigh = nullptr;
  ASSERT_TRUE(display.Update(Mode::Partial, DrawFilled(Color::White)));
  EXPECT_EQ(DataFor(LastUpdate(), 0x22), (Bytes{0xF7}));
}

TEST_F(EpaperDisplayTest, WakeFailureSkipsDrawing)
{
  spi.failWrites = true;
  bool drawn = false;
  EXPECT_FALSE(display.Update(Mode::Full, [&drawn](MonoFramebuffer &) { drawn = true; }));
  EXPECT_FALSE(drawn);
}

TEST_F(EpaperDisplayTest, PartialAfterSuccessfulUpdatesStaysPartial)
{
  ASSERT_TRUE(display.Update(Mode::Full, DrawFilled(Color::White)));
  ASSERT_TRUE(display.Update(Mode::Partial, DrawFilled(Color::Black)));
  ASSERT_TRUE(display.Update(Mode::Partial, DrawFilled(Color::White)));
  EXPECT_EQ(DataFor(LastUpdate(), 0x22), (Bytes{0xFF}));
}
