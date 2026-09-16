#include "bme280.hpp"

#include "delay.hpp"
#include "timeout.hpp"

Bme280::Bme280(I2cDevice &device) : device_(device)
{
}

bool Bme280::WaitForStatusClear(std::uint8_t bit)
{
  const Timeout timeout(kStatusTimeoutMs);
  for (;;)
  {
    std::uint8_t status = 0;
    if (!device_.ReadRegisters(kRegStatus, &status, 1))
    {
      return false;
    }
    if ((status & bit) == 0)
    {
      return true;
    }
    if (timeout.Expired())
    {
      return false;
    }
    DelayMs(1);
  }
}

bool Bme280::Init()
{
  // Soft reset first, so the sensor is in a known state regardless of what
  // ran before (e.g. a previous firmware left it in normal mode).
  if (!device_.WriteRegister(kRegReset, kResetCommand))
  {
    return false;
  }
  DelayMs(kStartupTimeMs);

  // Calibration read before the NVM copy finishes would be garbage.
  if (!WaitForStatusClear(kStatusImUpdate))
  {
    return false;
  }

  std::uint8_t chipId = 0;
  if (!device_.ReadRegisters(kRegChipId, &chipId, 1) || chipId != kChipIdValue)
  {
    return false;
  }

  std::uint8_t calib00[26];
  std::uint8_t calib26[7];
  if (!device_.ReadRegisters(kRegCalib00, calib00, sizeof(calib00)) ||
      !device_.ReadRegisters(kRegCalib26, calib26, sizeof(calib26)))
  {
    return false;
  }

  calib_.digT1 = static_cast<std::uint16_t>(calib00[0] | (calib00[1] << 8));
  calib_.digT2 = static_cast<std::int16_t>(calib00[2] | (calib00[3] << 8));
  calib_.digT3 = static_cast<std::int16_t>(calib00[4] | (calib00[5] << 8));
  calib_.digP1 = static_cast<std::uint16_t>(calib00[6] | (calib00[7] << 8));
  calib_.digP2 = static_cast<std::int16_t>(calib00[8] | (calib00[9] << 8));
  calib_.digP3 = static_cast<std::int16_t>(calib00[10] | (calib00[11] << 8));
  calib_.digP4 = static_cast<std::int16_t>(calib00[12] | (calib00[13] << 8));
  calib_.digP5 = static_cast<std::int16_t>(calib00[14] | (calib00[15] << 8));
  calib_.digP6 = static_cast<std::int16_t>(calib00[16] | (calib00[17] << 8));
  calib_.digP7 = static_cast<std::int16_t>(calib00[18] | (calib00[19] << 8));
  calib_.digP8 = static_cast<std::int16_t>(calib00[20] | (calib00[21] << 8));
  calib_.digP9 = static_cast<std::int16_t>(calib00[22] | (calib00[23] << 8));
  calib_.digH1 = calib00[25];

  calib_.digH2 = static_cast<std::int16_t>(calib26[0] | (calib26[1] << 8));
  calib_.digH3 = calib26[2];
  // dig_H4/dig_H5 are signed 12-bit values split across three bytes. The
  // MSB byte goes through int8_t first (as in Bosch's reference driver) so
  // the sign is kept; the low nibble is then OR'd into the zeroed low bits.
  calib_.digH4 =
      static_cast<std::int16_t>((static_cast<std::int8_t>(calib26[3]) * 16) | (calib26[4] & 0x0F));
  calib_.digH5 =
      static_cast<std::int16_t>((static_cast<std::int8_t>(calib26[5]) * 16) | (calib26[4] >> 4));
  calib_.digH6 = static_cast<std::int8_t>(calib26[6]);

  // Humidity oversampling x1. Only takes effect on the next ctrl_meas write,
  // which Read() does every time.
  if (!device_.WriteRegister(kRegCtrlHum, kOversamplingX1))
  {
    return false;
  }
  // Filter off - one sample a minute has nothing to average against. The
  // standby-time field only matters in normal mode.
  if (!device_.WriteRegister(kRegConfig, 0x00))
  {
    return false;
  }
  return device_.WriteRegister(kRegCtrlMeas, kCtrlMeasOversampling | kModeSleep);
}

std::int32_t Bme280::CompensateTemperature(std::int32_t adcT, std::int32_t &tFineOut) const
{
  const std::int32_t var1 =
      (((adcT >> 3) - (static_cast<std::int32_t>(calib_.digT1) << 1)) * static_cast<std::int32_t>(calib_.digT2)) >>
      11;
  const std::int32_t var2 = (((((adcT >> 4) - static_cast<std::int32_t>(calib_.digT1)) *
                                ((adcT >> 4) - static_cast<std::int32_t>(calib_.digT1))) >>
                               12) *
                              static_cast<std::int32_t>(calib_.digT3)) >>
                             14;
  tFineOut = var1 + var2;
  return (tFineOut * 5 + 128) >> 8; // in 0.01 degC
}

std::uint32_t Bme280::CompensatePressure(std::int32_t adcP, std::int32_t tFine) const
{
  // Bosch's reference left-shifts values that can be negative. That's
  // undefined behaviour before C++20, so those shifts are written here as
  // multiplications by the same power of two - identical results, and the
  // compiler still emits a shift.
  std::int64_t var1 = static_cast<std::int64_t>(tFine) - 128000;
  std::int64_t var2 = var1 * var1 * static_cast<std::int64_t>(calib_.digP6);
  var2 = var2 + ((var1 * static_cast<std::int64_t>(calib_.digP5)) * (INT64_C(1) << 17));
  var2 = var2 + (static_cast<std::int64_t>(calib_.digP4) * (INT64_C(1) << 35));
  var1 = ((var1 * var1 * static_cast<std::int64_t>(calib_.digP3)) >> 8) +
         ((var1 * static_cast<std::int64_t>(calib_.digP2)) * (INT64_C(1) << 12));
  var1 = ((static_cast<std::int64_t>(1) << 47) + var1) * static_cast<std::int64_t>(calib_.digP1) >> 33;
  if (var1 == 0)
  {
    return 0;
  }
  std::int64_t p = 1048576 - adcP;
  p = (((p << 31) - var2) * 3125) / var1;
  var1 = (static_cast<std::int64_t>(calib_.digP9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (static_cast<std::int64_t>(calib_.digP8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (static_cast<std::int64_t>(calib_.digP7) * 16);
  return static_cast<std::uint32_t>(p); // Q24.8 format, Pa
}

std::uint32_t Bme280::CompensateHumidity(std::int32_t adcH, std::int32_t tFine) const
{
  std::int32_t v = tFine - 76800;
  // digH4 can be negative, so it's multiplied rather than shifted (see
  // CompensatePressure).
  v = (((((adcH << 14) - (static_cast<std::int32_t>(calib_.digH4) * (1 << 20)) -
          (static_cast<std::int32_t>(calib_.digH5) * v)) +
         16384) >>
        15) *
       (((((((v * static_cast<std::int32_t>(calib_.digH6)) >> 10) *
            (((v * static_cast<std::int32_t>(calib_.digH3)) >> 11) + 32768)) >>
           10) +
          2097152) *
             static_cast<std::int32_t>(calib_.digH2) +
         8192) >>
        14));
  v = v - (((((v >> 15) * (v >> 15)) >> 7) * static_cast<std::int32_t>(calib_.digH1)) >> 4);
  v = (v < 0) ? 0 : v;
  v = (v > 419430400) ? 419430400 : v;
  return static_cast<std::uint32_t>(v >> 12); // Q22.10 format
}

std::optional<Bme280::Measurements> Bme280::Read()
{
  if (!device_.WriteRegister(kRegCtrlMeas, kCtrlMeasOversampling | kModeForced))
  {
    return std::nullopt;
  }

  // Wait out the worst-case conversion time rather than polling from the
  // start: the measuring bit isn't guaranteed to be set the instant the
  // forced-mode write lands, so an immediate poll could see it clear.
  DelayMs(kMaxMeasurementTimeMs);
  if (!WaitForStatusClear(kStatusMeasuring))
  {
    return std::nullopt;
  }

  std::uint8_t data[8];
  if (!device_.ReadRegisters(kRegData, data, sizeof(data)))
  {
    return std::nullopt;
  }

  const std::int32_t adcP = (static_cast<std::int32_t>(data[0]) << 12) | (static_cast<std::int32_t>(data[1]) << 4) |
                             (data[2] >> 4);
  const std::int32_t adcT = (static_cast<std::int32_t>(data[3]) << 12) | (static_cast<std::int32_t>(data[4]) << 4) |
                             (data[5] >> 4);
  const std::int32_t adcH = (static_cast<std::int32_t>(data[6]) << 8) | data[7];

  if (adcT == kAdcSkipped20Bit || adcP == kAdcSkipped20Bit || adcH == kAdcSkipped16Bit)
  {
    return std::nullopt;
  }

  std::int32_t tFine = 0;
  const std::int32_t t = CompensateTemperature(adcT, tFine);
  const std::uint32_t p = CompensatePressure(adcP, tFine);
  const std::uint32_t h = CompensateHumidity(adcH, tFine);

  // t is already in 0.01 degC units - Bosch's compensation formula produces that
  // directly, so no conversion needed here at all.
  Measurements out{};
  out.temperatureCenti = t;
  // p is Q24.8 Pa; hPa = Pa/100, so centi-hPa (hPa*100) = Pa = p/256, rounded.
  out.pressureCentiHpa = static_cast<std::int32_t>((p + 128) >> 8);
  // h is Q22.10 %RH; centi-%RH (%RH*100) = h*100/1024 = h*25/256, rounded. Needs a
  // 64-bit intermediate - h*25 can exceed int32_t range near its max clamp value.
  out.humidityCentiPct = static_cast<std::int32_t>((static_cast<std::int64_t>(h) * 25 + 128) / 256);

  return out;
}
