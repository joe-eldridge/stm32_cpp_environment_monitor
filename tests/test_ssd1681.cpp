#include <gtest/gtest.h>

#include <vector>

#include "fake_hal.hpp"
#include "fake_spi_display.hpp"
#include "ssd1681.hpp"

namespace
{

using Bytes = std::vector<std::uint8_t>;

// Simulates the controller's BUSY line: high for a number of polls after
// a software reset or a refresh starts.
class Ssd1681Test : public ::testing::Test
{
protected:
  void SetUp() override
  {
    fake_hal::SetTick(0);
    busy.isHigh = [this] {
      if (busyPollsRemaining == kForever)
      {
        return true;
      }
      if (busyPollsRemaining > 0)
      {
        --busyPollsRemaining;
        return true;
      }
      return false;
    };
    spi.onCommand = [this](std::uint8_t code) {
      if (code == 0x12 || code == 0x20)
      {
        busyPollsRemaining = busyPollsPerOperation;
      }
      if (code == 0x01)
      {
        configTick = fake_hal::Tick();
      }
    };
  }

  static constexpr int kForever = -1;

  FakeOutputPin dataCommand;
  FakeOutputPin reset;
  FakeInputPin busy;
  RecordingSpiDevice spi{dataCommand};
  Ssd1681 panel{spi, dataCommand, reset, busy};

  int busyPollsPerOperation = 3;
  int busyPollsRemaining = 0;
  std::uint32_t configTick = 0;
};

Bytes Image(std::uint8_t fill)
{
  return Bytes(Ssd1681::kImageBytes, fill);
}

} // namespace

TEST_F(Ssd1681Test, WakePulsesResetLow)
{
  ASSERT_TRUE(panel.Wake());
  EXPECT_EQ(reset.history, (std::vector<bool>{false, true}));
}

TEST_F(Ssd1681Test, WakeSoftwareResetsThenConfiguresPanel)
{
  ASSERT_TRUE(panel.Wake());
  EXPECT_EQ(spi.Codes(), (Bytes{0x12, 0x01, 0x11, 0x44, 0x45, 0x3C, 0x18}));
  EXPECT_TRUE(spi.LastDataFor(0x12).empty());
  EXPECT_EQ(spi.LastDataFor(0x01), (Bytes{0xC7, 0x00, 0x00}));             // 200 gate lines
  EXPECT_EQ(spi.LastDataFor(0x11), (Bytes{0x03}));                         // X then Y increment
  EXPECT_EQ(spi.LastDataFor(0x44), (Bytes{0x00, 0x18}));                   // bytes 0..24
  EXPECT_EQ(spi.LastDataFor(0x45), (Bytes{0x00, 0x00, 0xC7, 0x00}));       // rows 0..199
  EXPECT_EQ(spi.LastDataFor(0x3C), (Bytes{0x05}));
  EXPECT_EQ(spi.LastDataFor(0x18), (Bytes{0x80}));                         // internal sensor
  EXPECT_FALSE(spi.malformed);
}

TEST_F(Ssd1681Test, WakeWaitsForSoftwareResetToFinish)
{
  busyPollsPerOperation = 5;
  ASSERT_TRUE(panel.Wake());
  // 20 ms of reset pulse + 10 ms settle + 5 busy polls at 10 ms.
  EXPECT_GE(configTick, 80u);
}

TEST_F(Ssd1681Test, WakeFailsIfBusyNeverClears)
{
  busyPollsPerOperation = kForever;
  EXPECT_FALSE(panel.Wake());
  EXPECT_EQ(spi.Codes(), (Bytes{0x12})); // configuration never sent
}

TEST_F(Ssd1681Test, CommandsUseDcLowAndParametersDcHigh)
{
  ASSERT_TRUE(panel.Wake());
  // Every command was a single DC-low byte and every parameter followed
  // one - checked by the recorder as it parsed the stream.
  EXPECT_FALSE(spi.malformed);
  EXPECT_FALSE(spi.commands.empty());
}

TEST_F(Ssd1681Test, WriteImageStartsAtOriginInBlackWhiteRam)
{
  const Bytes image = Image(0x5A);
  ASSERT_TRUE(panel.WriteImage(image.data()));
  EXPECT_EQ(spi.Codes(), (Bytes{0x4E, 0x4F, 0x24}));
  EXPECT_EQ(spi.LastDataFor(0x4E), (Bytes{0x00}));
  EXPECT_EQ(spi.LastDataFor(0x4F), (Bytes{0x00, 0x00}));
  EXPECT_EQ(spi.LastDataFor(0x24), image);
}

TEST_F(Ssd1681Test, WritePreviousImageUsesRedRam)
{
  const Bytes image = Image(0xA5);
  ASSERT_TRUE(panel.WritePreviousImage(image.data()));
  EXPECT_EQ(spi.Codes(), (Bytes{0x4E, 0x4F, 0x26}));
  EXPECT_EQ(spi.LastDataFor(0x26), image);
}

TEST_F(Ssd1681Test, FullRefreshUsesDisplayMode1)
{
  ASSERT_TRUE(panel.Refresh(Ssd1681::RefreshMode::Full));
  EXPECT_EQ(spi.Codes(), (Bytes{0x22, 0x20}));
  EXPECT_EQ(spi.LastDataFor(0x22), (Bytes{0xF7}));
  EXPECT_EQ(busyPollsRemaining, 0); // waited for the refresh to finish
}

TEST_F(Ssd1681Test, PartialRefreshUsesDisplayMode2)
{
  ASSERT_TRUE(panel.Refresh(Ssd1681::RefreshMode::Partial));
  EXPECT_EQ(spi.LastDataFor(0x22), (Bytes{0xFF}));
}

TEST_F(Ssd1681Test, RefreshTimesOutOnStuckBusy)
{
  busyPollsPerOperation = kForever;
  EXPECT_FALSE(panel.Refresh(Ssd1681::RefreshMode::Full));
  EXPECT_GE(fake_hal::Tick(), 10000u);
}

TEST_F(Ssd1681Test, SleepEntersDeepSleepMode1WithoutWaitingOnBusy)
{
  // BUSY stays high throughout deep sleep, so waiting on it would hang.
  busyPollsRemaining = kForever;
  ASSERT_TRUE(panel.Sleep());
  EXPECT_EQ(spi.Codes(), (Bytes{0x10}));
  EXPECT_EQ(spi.LastDataFor(0x10), (Bytes{0x01}));
  EXPECT_EQ(busy.reads, 0);
}

TEST_F(Ssd1681Test, SpiFailuresArePropagated)
{
  spi.failWrites = true;
  const Bytes image = Image(0x00);
  EXPECT_FALSE(panel.Wake());
  EXPECT_FALSE(panel.WriteImage(image.data()));
  EXPECT_FALSE(panel.Refresh(Ssd1681::RefreshMode::Full));
  EXPECT_FALSE(panel.Sleep());
}
