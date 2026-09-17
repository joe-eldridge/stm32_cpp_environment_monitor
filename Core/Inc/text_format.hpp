#pragma once

#include <cstddef>
#include <cstdint>

// Small, allocation-free number formatting for the display. No printf: it
// would pull in a large chunk of the C library for a few fixed formats.
namespace text_format
{

// Writes a value given in hundredths with `decimals` (0 or 1) decimal
// places, rounding half away from zero: 2384 -> "23.8", 115302 -> "1153"
// (decimals = 0). A value that rounds to zero is never shown as "-0".
// Returns false, leaving `out` empty, if it doesn't fit.
bool Centi(char *out, std::size_t size, std::int32_t centi, int decimals);

// Writes `value` (0-99) as two digits with a leading zero: 7 -> "07".
void TwoDigits(char *out, std::uint8_t value);

// Screen text is built in fixed-size buffers, so these truncate rather than
// overrun, and always leave `out` NUL-terminated.
void Copy(char *out, std::size_t size, const char *text);
void Append(char *out, std::size_t size, const char *text);

// Day of the week for a Gregorian date: 0 = Sunday ... 6 = Saturday.
// Returns -1 for an invalid month.
int DayOfWeek(int year, int month, int day);

} // namespace text_format
