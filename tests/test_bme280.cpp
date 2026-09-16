#include <gtest/gtest.h>

#include <cmath>

#include "bme280.hpp"
#include "fake_hal.hpp"
#include "fake_i2c_device.hpp"

namespace
{

constexpr std::uint8_t kRegChipId = 0xD0;
constexpr std::uint8_t kRegReset = 0xE0;
constexpr std::uint8_t kRegCtrlHum = 0xF2;
constexpr std::uint8_t kRegStatus = 0xF3;
constexpr std::uint8_t kRegCtrlMeas = 0xF4;
constexpr std::uint8_t kRegData = 0xF7;

constexpr std::uint8_t kCtrlMeasSleepX1 = 0x24;  // osrs_t x1, osrs_p x1, sleep
constexpr std::uint8_t kCtrlMeasForcedX1 = 0x25; // osrs_t x1, osrs_p x1, forced

struct Calibration
{
  int t1, t2, t3;
  int p1, p2, p3, p4, p5, p6, p7, p8, p9;
  int h1, h2, h3, h4, h5, h6;
};

// Temperature/pressure coefficients from Bosch's published worked example
// (BMP280 datasheet, same algorithm): adc_T=519888, adc_P=415148 ->
// 25.08 degC, 100653.27 Pa. Humidity coefficients are typical values.
constexpr Calibration kExample{27504, 26435, -1000, 36477, -10685, 3024, 2855, 140, -7, 15500, -14600, 6000,
                               75,    362,   0,     313,   50,     30};
constexpr std::int32_t kExampleAdcT = 519888;
constexpr std::int32_t kExampleAdcP = 415148;

struct Reference
{
  double temperatureC;
  double pressurePa;
  double humidityPct;
};

// Bosch's double-precision compensation formulas (datasheet section 8.1) -
// an independent check on the driver's integer versions.
Reference Compensate(const Calibration &c, std::int32_t adcT, std::int32_t adcP, std::int32_t adcH)
{
  double v1 = (adcT / 16384.0 - c.t1 / 1024.0) * c.t2;
  double v2 = (adcT / 131072.0 - c.t1 / 8192.0);
  v2 = v2 * v2 * c.t3;
  const double tFine = v1 + v2;

  v1 = tFine / 2.0 - 64000.0;
  v2 = v1 * v1 * c.p6 / 32768.0;
  v2 = v2 + v1 * c.p5 * 2.0;
  v2 = v2 / 4.0 + c.p4 * 65536.0;
  v1 = (c.p3 * v1 * v1 / 524288.0 + c.p2 * v1) / 524288.0;
  v1 = (1.0 + v1 / 32768.0) * c.p1;
  double p = 1048576.0 - adcP;
  p = (p - v2 / 4096.0) * 6250.0 / v1;
  v1 = c.p9 * p * p / 2147483648.0;
  v2 = p * c.p8 / 32768.0;
  p = p + (v1 + v2 + c.p7) / 16.0;

  double h = tFine - 76800.0;
  h = (adcH - (c.h4 * 64.0 + c.h5 / 16384.0 * h)) *
      (c.h2 / 65536.0 * (1.0 + c.h6 / 67108864.0 * h * (1.0 + c.h3 / 67108864.0 * h)));
  h = h * (1.0 - c.h1 * h / 524288.0);
  h = std::fmin(std::fmax(h, 0.0), 100.0);

  return {tFine / 5120.0, p, h};
}

// Simulates the sensor: register map, soft reset, and status bits that take
// a few reads to clear.
class Bme280Test : public ::testing::Test
{
protected:
  void SetUp() override
  {
    fake_hal::SetTick(0);
    device.registers[kRegChipId] = 0x60;
    LoadCalibration(kExample);
    LoadAdc(kExampleAdcT, kExampleAdcP, 30000);

    device.onWrite = [this](std::uint8_t reg, const std::vector<std::uint8_t> &data) {
      if (reg == kRegReset && data[0] == 0xB6)
      {
        device.registers[kRegReset] = 0x00; // reset register reads back 0
        imUpdateReadsRemaining = nvmCopyReads;
      }
      if (reg == kRegCtrlMeas && (data[0] & 0x03) == 0x01)
      {
        forcedWriteTick = fake_hal::Tick();
        measuringReadsRemaining = measuringReads;
      }
    };
    device.onRead = [this](std::uint8_t reg, std::uint8_t) {
      if (reg == kRegStatus)
      {
        // A non-zero count (including kForever) means the bit is still set.
        device.registers[kRegStatus] = static_cast<std::uint8_t>((measuringReadsRemaining != 0 ? 0x08 : 0x00) |
                                                                 (imUpdateReadsRemaining != 0 ? 0x01 : 0x00));
        if (measuringReadsRemaining > 0 && measuringReadsRemaining != kForever)
        {
          --measuringReadsRemaining;
        }
        if (imUpdateReadsRemaining > 0 && imUpdateReadsRemaining != kForever)
        {
          --imUpdateReadsRemaining;
        }
      }
      if (reg == kRegData)
      {
        dataReadTick = fake_hal::Tick();
      }
    };
  }

  void LoadCalibration(const Calibration &c)
  {
    const int words[] = {c.t1, c.t2, c.t3, c.p1, c.p2, c.p3, c.p4, c.p5, c.p6, c.p7, c.p8, c.p9};
    for (int i = 0; i < 12; ++i)
    {
      device.SetLe16(static_cast<std::uint8_t>(0x88 + 2 * i), static_cast<std::uint16_t>(words[i]));
    }
    device.registers[0xA1] = static_cast<std::uint8_t>(c.h1);
    device.SetLe16(0xE1, static_cast<std::uint16_t>(c.h2));
    device.registers[0xE3] = static_cast<std::uint8_t>(c.h3);
    // dig_H4 = E4[7:0] E5[3:0], dig_H5 = E6[7:0] E5[7:4] - both signed 12-bit.
    const unsigned h4 = static_cast<unsigned>(c.h4) & 0xFFF;
    const unsigned h5 = static_cast<unsigned>(c.h5) & 0xFFF;
    device.registers[0xE4] = static_cast<std::uint8_t>(h4 >> 4);
    device.registers[0xE5] = static_cast<std::uint8_t>((h4 & 0x0F) | ((h5 & 0x0F) << 4));
    device.registers[0xE6] = static_cast<std::uint8_t>(h5 >> 4);
    device.registers[0xE7] = static_cast<std::uint8_t>(c.h6);
  }

  void LoadAdc(std::int32_t adcT, std::int32_t adcP, std::int32_t adcH)
  {
    device.registers[kRegData + 0] = static_cast<std::uint8_t>(adcP >> 12);
    device.registers[kRegData + 1] = static_cast<std::uint8_t>(adcP >> 4);
    device.registers[kRegData + 2] = static_cast<std::uint8_t>(adcP << 4);
    device.registers[kRegData + 3] = static_cast<std::uint8_t>(adcT >> 12);
    device.registers[kRegData + 4] = static_cast<std::uint8_t>(adcT >> 4);
    device.registers[kRegData + 5] = static_cast<std::uint8_t>(adcT << 4);
    device.registers[kRegData + 6] = static_cast<std::uint8_t>(adcH >> 8);
    device.registers[kRegData + 7] = static_cast<std::uint8_t>(adcH);
  }

  static constexpr int kForever = -1;

  FakeI2cDevice device;
  Bme280 bme280{device};

  int nvmCopyReads = 2;
  int measuringReads = 2;
  int imUpdateReadsRemaining = 0;
  int measuringReadsRemaining = 0;
  std::uint32_t forcedWriteTick = 0;
  std::uint32_t dataReadTick = 0;
};

} // namespace

TEST_F(Bme280Test, InitSoftResetsFirst)
{
  ASSERT_TRUE(bme280.Init());
  ASSERT_FALSE(device.writes.empty());
  EXPECT_EQ(device.writes.front().reg, kRegReset);
  EXPECT_EQ(device.writes.front().data[0], 0xB6);
}

TEST_F(Bme280Test, InitLeavesSensorAsleep)
{
  ASSERT_TRUE(bme280.Init());
  EXPECT_EQ(device.registers[kRegCtrlMeas], kCtrlMeasSleepX1);
}

TEST_F(Bme280Test, InitWritesCtrlHumBeforeCtrlMeas)
{
  // ctrl_hum only takes effect on the next ctrl_meas write.
  ASSERT_TRUE(bme280.Init());
  std::size_t humIndex = device.writes.size();
  std::size_t measIndex = device.writes.size();
  for (std::size_t i = 0; i < device.writes.size(); ++i)
  {
    if (device.writes[i].reg == kRegCtrlHum)
    {
      humIndex = i;
    }
    if (device.writes[i].reg == kRegCtrlMeas)
    {
      measIndex = i;
    }
  }
  ASSERT_LT(humIndex, device.writes.size());
  EXPECT_LT(humIndex, measIndex);
  EXPECT_EQ(device.registers[kRegCtrlHum], 0x01);
}

TEST_F(Bme280Test, InitWaitsForCalibrationCopy)
{
  nvmCopyReads = 5;
  EXPECT_TRUE(bme280.Init());
}

TEST_F(Bme280Test, InitFailsIfCalibrationCopyNeverFinishes)
{
  nvmCopyReads = kForever;
  EXPECT_FALSE(bme280.Init());
}

TEST_F(Bme280Test, InitRejectsWrongChipId)
{
  device.registers[kRegChipId] = 0x58; // BMP280
  EXPECT_FALSE(bme280.Init());
}

TEST_F(Bme280Test, InitFailsOnBusError)
{
  device.failWrites = true;
  EXPECT_FALSE(bme280.Init());
}

TEST_F(Bme280Test, ReadTriggersForcedMeasurement)
{
  ASSERT_TRUE(bme280.Init());
  device.writes.clear();
  ASSERT_TRUE(bme280.Read().has_value());
  ASSERT_FALSE(device.writes.empty());
  EXPECT_EQ(device.writes.front().reg, kRegCtrlMeas);
  EXPECT_EQ(device.writes.front().data[0], kCtrlMeasForcedX1);
}

TEST_F(Bme280Test, ReadWaitsOutWorstCaseConversionTime)
{
  ASSERT_TRUE(bme280.Init());
  ASSERT_TRUE(bme280.Read().has_value());
  EXPECT_GE(dataReadTick - forcedWriteTick, 10u); // datasheet max 9.3 ms at x1 oversampling
}

TEST_F(Bme280Test, ReadMatchesBoschWorkedExample)
{
  ASSERT_TRUE(bme280.Init());
  const auto m = bme280.Read();
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->temperatureCenti, 2508);  // 25.08 degC
  EXPECT_EQ(m->pressureCentiHpa, 100653); // 100653.27 Pa = 1006.53 hPa
}

TEST_F(Bme280Test, ReadFailsIfMeasurementNeverCompletes)
{
  ASSERT_TRUE(bme280.Init());
  measuringReads = kForever;
  EXPECT_FALSE(bme280.Read().has_value());
}

TEST_F(Bme280Test, ReadRejectsSkippedChannels)
{
  ASSERT_TRUE(bme280.Init());
  LoadAdc(kExampleAdcT, kExampleAdcP, 0x8000); // humidity not measured
  EXPECT_FALSE(bme280.Read().has_value());
  LoadAdc(0x80000, kExampleAdcP, 30000); // temperature not measured
  EXPECT_FALSE(bme280.Read().has_value());
  LoadAdc(kExampleAdcT, 0x80000, 30000); // pressure not measured
  EXPECT_FALSE(bme280.Read().has_value());
}

TEST_F(Bme280Test, ReadFailsOnBusError)
{
  ASSERT_TRUE(bme280.Init());
  device.failReads = true;
  EXPECT_FALSE(bme280.Read().has_value());
}

// Humidity across a range of calibrations, including the negative dig_H4 /
// dig_H5 values the original driver decoded as large positive numbers.
struct HumidityCase
{
  int h4;
  int h5;
  std::int32_t adcH;
};

class Bme280HumidityTest : public Bme280Test, public ::testing::WithParamInterface<HumidityCase>
{
};

TEST_P(Bme280HumidityTest, MatchesFloatingPointReference)
{
  const HumidityCase &hc = GetParam();
  Calibration c = kExample;
  c.h4 = hc.h4;
  c.h5 = hc.h5;
  LoadCalibration(c);
  LoadAdc(kExampleAdcT, kExampleAdcP, hc.adcH);

  ASSERT_TRUE(bme280.Init());
  const auto m = bme280.Read();
  ASSERT_TRUE(m.has_value());

  const Reference ref = Compensate(c, kExampleAdcT, kExampleAdcP, hc.adcH);
  EXPECT_NEAR(m->temperatureCenti, ref.temperatureC * 100.0, 1.0);
  EXPECT_NEAR(m->pressureCentiHpa, ref.pressurePa, 1.0);
  EXPECT_NEAR(m->humidityCentiPct, ref.humidityPct * 100.0, 2.0);
  // Every case is chosen to sit mid-range, so a clamp would hide a bug.
  EXPECT_GT(ref.humidityPct, 5.0);
  EXPECT_LT(ref.humidityPct, 95.0);
}

INSTANTIATE_TEST_SUITE_P(Calibrations, Bme280HumidityTest,
                         ::testing::Values(HumidityCase{313, 50, 30000}, HumidityCase{313, 50, 33000},
                                           HumidityCase{313, 50, 24000},
                                           HumidityCase{-100, -30, 3568}, HumidityCase{-50, -200, 6768},
                                           HumidityCase{0, 0, 10000}));
