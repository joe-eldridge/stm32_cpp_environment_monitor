#pragma once

#include <cstdint>
#include <optional>

#include "i2c_device.hpp"

// Driver for the DS3231 RTC (as found on the HW-084 breakout). Register-level
// detail (BCD packing, alarm match-mask bits) stays private; callers only see
// binary DateTime values.
class Ds3231
{
public:
  struct DateTime
  {
    std::uint16_t year;   // full year, e.g. 2026
    std::uint8_t month;   // 1-12
    std::uint8_t date;    // 1-31
    std::uint8_t hour;    // 0-23 (24-hour mode only)
    std::uint8_t minute;  // 0-59
    std::uint8_t second;  // 0-59
  };

  static constexpr std::uint8_t kI2cAddress7Bit = 0x68;

  explicit Ds3231(I2cDevice &device);

  // Alarm 1 matches on minutes, so only intervals that divide an hour evenly
  // give evenly spaced wakes. constexpr so callers can static_assert.
  static constexpr bool IsValidWakeInterval(std::uint8_t intervalMinutes)
  {
    return intervalMinutes != 0 && (60 % intervalMinutes) == 0;
  }

  // Enables Alarm 1's interrupt output on SQW/INT. Call once at startup.
  [[nodiscard]] bool Init();

  [[nodiscard]] std::optional<DateTime> ReadDateTime();
  [[nodiscard]] bool SetDateTime(const DateTime &dt);

  // The DS3231 has no native "every N minutes" alarm mode - only fixed
  // field-match alarms. This reprograms Alarm 1 to fire once, at the next
  // minute that's a multiple of intervalMinutes (matching minutes+seconds,
  // ignoring hours/date), so it must be called again after every wake to
  // arm the following one. Fails if intervalMinutes isn't
  // IsValidWakeInterval().
  [[nodiscard]] bool ScheduleNextAlarm(std::uint8_t intervalMinutes);

  // Must be called after each wake: the alarm flag latches until cleared,
  // and SQW/INT won't pull low again on the next match until it is.
  [[nodiscard]] bool ClearAlarmFlag();

  // True if the oscillator has stopped at some point since this flag was
  // last cleared (e.g. backup battery was dead or missing) - meaning the
  // current date/time cannot be trusted and the RTC needs re-setting.
  // Fails safe: an I2C read failure is also reported as "stopped".
  [[nodiscard]] bool IsOscillatorStopped();

  // Call after resolving a stopped-oscillator condition (i.e. after
  // SetDateTime() has re-established a trustworthy time), otherwise this
  // flag stays latched even though the oscillator is now running fine.
  [[nodiscard]] bool ClearOscillatorStopFlag();

private:
  static constexpr std::uint8_t kRegSeconds = 0x00;
  static constexpr std::uint8_t kRegMinutes = 0x01;
  static constexpr std::uint8_t kRegHours = 0x02;
  static constexpr std::uint8_t kRegDate = 0x04;
  static constexpr std::uint8_t kRegMonth = 0x05;
  static constexpr std::uint8_t kRegYear = 0x06;
  static constexpr std::uint8_t kRegAlarm1Sec = 0x07;
  static constexpr std::uint8_t kRegAlarm1Min = 0x08;
  static constexpr std::uint8_t kRegAlarm1Hour = 0x09;
  static constexpr std::uint8_t kRegAlarm1Day = 0x0A;
  static constexpr std::uint8_t kRegControl = 0x0E;
  static constexpr std::uint8_t kRegStatus = 0x0F;

  static constexpr std::uint8_t kControlIntcn = 1u << 2;
  static constexpr std::uint8_t kControlA1Ie = 1u << 0;
  static constexpr std::uint8_t kStatusA1F = 1u << 0;
  static constexpr std::uint8_t kStatusOsf = 1u << 7;

  // Set in an alarm register's top bit to make that field "don't care".
  static constexpr std::uint8_t kAlarmIgnoreField = 1u << 7;

  // If the next alarm minute starts less than this far from now, the alarm
  // write could land after that minute has begun. The match would be missed
  // and the device would sleep for an hour, so skip one extra interval.
  static constexpr std::uint8_t kAlarmWriteMarginSeconds = 2;

  [[nodiscard]] bool ClearStatusBit(std::uint8_t bit);

  I2cDevice &device_;
};
