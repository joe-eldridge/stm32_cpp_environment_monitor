#include "ds3231.hpp"

#include "bcd.hpp"

Ds3231::Ds3231(I2cDevice &device) : device_(device)
{
}

bool Ds3231::Init()
{
  // INTCN=1 routes Alarm 1/2 flags to SQW/INT instead of a square wave;
  // A1IE=1 lets Alarm 1 actually assert it.
  return device_.WriteRegister(kRegControl, kControlIntcn | kControlA1Ie);
}

std::optional<Ds3231::DateTime> Ds3231::ReadDateTime()
{
  std::uint8_t raw[7];
  if (!device_.ReadRegisters(kRegSeconds, raw, sizeof(raw)))
  {
    return std::nullopt;
  }

  DateTime out{};
  out.second = bcd::ToBinary(raw[0] & 0x7F);
  out.minute = bcd::ToBinary(raw[1] & 0x7F);
  out.hour = bcd::ToBinary(raw[2] & 0x3F); // 24-hour mode: bits [5:0]
  // raw[3] is day-of-week - not tracked, unused by this design
  out.date = bcd::ToBinary(raw[4] & 0x3F);
  out.month = bcd::ToBinary(raw[5] & 0x1F); // bit 7 is century, assumed 0
  out.year = static_cast<std::uint16_t>(2000 + bcd::ToBinary(raw[6]));
  return out;
}

bool Ds3231::SetDateTime(const DateTime &dt)
{
  if (dt.year < 2000 || dt.year > 2099)
  {
    return false;
  }

  // One burst across the contiguous seconds..year registers (0x00-0x06).
  const std::uint8_t payload[7] = {
      bcd::ToBcd(dt.second),
      bcd::ToBcd(dt.minute),
      bcd::ToBcd(dt.hour), // bit 6 = 0 selects 24-hour mode
      bcd::ToBcd(1),       // day-of-week isn't used by this design; DS3231 just needs 1-7 here
      bcd::ToBcd(dt.date),
      bcd::ToBcd(dt.month), // bit 7 = 0 selects 20xx century
      bcd::ToBcd(static_cast<std::uint8_t>(dt.year - 2000)),
  };
  return device_.WriteRegisters(kRegSeconds, payload, sizeof(payload));
}

bool Ds3231::ScheduleNextAlarm(std::uint8_t intervalMinutes)
{
  if (!IsValidWakeInterval(intervalMinutes))
  {
    return false;
  }

  const std::optional<DateTime> now = ReadDateTime();
  if (!now)
  {
    return false;
  }

  // Next multiple of the interval, before wrapping at 60 (so 59 -> 60 is
  // still recognised as "the very next minute").
  unsigned nextMinute = ((now->minute / intervalMinutes) + 1u) * intervalMinutes;
  const bool nextIsImminent = (nextMinute == now->minute + 1u) && (now->second >= 60u - kAlarmWriteMarginSeconds);
  if (nextIsImminent)
  {
    nextMinute += intervalMinutes;
  }
  nextMinute %= 60u;

  // One burst across the contiguous Alarm 1 registers (0x07-0x0A).
  // A1M1=0, A1M2=0: match seconds and minutes. A1M3=1, A1M4=1: ignore hours/date.
  const std::uint8_t payload[4] = {
      bcd::ToBcd(0),
      bcd::ToBcd(static_cast<std::uint8_t>(nextMinute)),
      kAlarmIgnoreField,
      kAlarmIgnoreField,
  };
  return device_.WriteRegisters(kRegAlarm1Sec, payload, sizeof(payload));
}

bool Ds3231::ClearAlarmFlag()
{
  return ClearStatusBit(kStatusA1F);
}

bool Ds3231::IsOscillatorStopped()
{
  std::uint8_t status;
  if (!device_.ReadRegisters(kRegStatus, &status, 1))
  {
    return true; // fail safe: an unreadable status register means "don't trust the time"
  }
  return (status & kStatusOsf) != 0;
}

bool Ds3231::ClearOscillatorStopFlag()
{
  return ClearStatusBit(kStatusOsf);
}

bool Ds3231::ClearStatusBit(std::uint8_t bit)
{
  std::uint8_t status;
  if (!device_.ReadRegisters(kRegStatus, &status, 1))
  {
    return false;
  }
  return device_.WriteRegister(kRegStatus, static_cast<std::uint8_t>(status & ~bit));
}
