#include <gtest/gtest.h>

#include "fake_hal.hpp"
#include "fake_i2c_device.hpp"
#include "veml7700.hpp"

namespace
{

constexpr std::uint8_t kRegAlsConf = 0x00;
constexpr std::uint8_t kRegAls = 0x04;

const std::vector<std::uint8_t> kPoweredOn{0x00, 0x00};
const std::vector<std::uint8_t> kShutDown{0x01, 0x00}; // ALS_SD set, little-endian

class Veml7700Test : public ::testing::Test
{
protected:
  void SetUp() override
  {
    fake_hal::SetTick(0);
    device.onWrite = [this](std::uint8_t reg, const std::vector<std::uint8_t> &data) {
      if (reg == kRegAlsConf && data == kPoweredOn)
      {
        powerOnTick = fake_hal::Tick();
      }
    };
    device.onRead = [this](std::uint8_t reg, std::uint8_t) {
      if (reg == kRegAls)
      {
        alsReadTick = fake_hal::Tick();
      }
    };
  }

  std::optional<Veml7700::Reading> ReadCount(std::uint16_t count)
  {
    device.SetLe16(kRegAls, count);
    return veml.Read();
  }

  FakeI2cDevice device;
  Veml7700 veml{device};
  std::uint32_t powerOnTick = 0;
  std::uint32_t alsReadTick = 0;
};

} // namespace

TEST_F(Veml7700Test, InitLeavesSensorShutDown)
{
  ASSERT_TRUE(veml.Init());
  const auto writes = device.WritesTo(kRegAlsConf);
  ASSERT_EQ(writes.size(), 1u);
  EXPECT_EQ(writes[0].data, kShutDown);
}

TEST_F(Veml7700Test, ReadPowersUpThenShutsDown)
{
  ASSERT_TRUE(ReadCount(1000).has_value());
  const auto writes = device.WritesTo(kRegAlsConf);
  ASSERT_EQ(writes.size(), 2u);
  EXPECT_EQ(writes[0].data, kPoweredOn);
  EXPECT_EQ(writes[1].data, kShutDown);
}

TEST_F(Veml7700Test, ReadWaitsForPowerUpPlusOneIntegrationPeriod)
{
  ASSERT_TRUE(ReadCount(1000).has_value());
  // Vishay app note 84323: 2.5 ms after power-up, then >= 100 ms integration.
  EXPECT_GE(alsReadTick - powerOnTick, 103u);
}

TEST_F(Veml7700Test, ScalesCountsAtVishayResolution)
{
  // Gain x1, 100 ms: 0.0672 lx/count, rounded to the nearest centi-lux.
  EXPECT_EQ(ReadCount(0)->luxCenti, 0);
  EXPECT_EQ(ReadCount(1)->luxCenti, 7);        // 6.72
  EXPECT_EQ(ReadCount(1000)->luxCenti, 6720);  // 67.20 lx
  EXPECT_EQ(ReadCount(65534)->luxCenti, 440388); // 4403.8848 lx
}

TEST_F(Veml7700Test, FlagsSaturationOnlyAtFullScale)
{
  EXPECT_FALSE(ReadCount(65534)->saturated);
  const auto reading = ReadCount(65535);
  ASSERT_TRUE(reading.has_value());
  EXPECT_TRUE(reading->saturated);
  EXPECT_EQ(reading->luxCenti, 440395); // lower bound on the true level
}

TEST_F(Veml7700Test, ShutsDownEvenIfReadFails)
{
  device.failReads = true;
  EXPECT_FALSE(veml.Read().has_value());
  const auto writes = device.WritesTo(kRegAlsConf);
  ASSERT_FALSE(writes.empty());
  EXPECT_EQ(writes.back().data, kShutDown);
}

TEST_F(Veml7700Test, SkipsReadIfPowerUpFails)
{
  device.failWrites = true;
  EXPECT_FALSE(veml.Read().has_value());
  EXPECT_EQ(alsReadTick, 0u);
}
