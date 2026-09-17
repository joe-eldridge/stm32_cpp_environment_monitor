#include "text_format.hpp"

namespace text_format
{

bool Centi(char *out, std::size_t size, std::int32_t centi, int decimals)
{
  if (size == 0)
  {
    return false;
  }
  out[0] = '\0';
  if (decimals < 0 || decimals > 1)
  {
    return false;
  }

  // Work in unsigned magnitude so INT32_MIN doesn't overflow.
  const bool negative = centi < 0;
  const std::uint32_t magnitude = negative ? 0u - static_cast<std::uint32_t>(centi) : static_cast<std::uint32_t>(centi);
  const std::uint32_t divisor = decimals == 1 ? 10u : 100u;
  const std::uint32_t scaled = magnitude / divisor + ((magnitude % divisor) * 2u >= divisor ? 1u : 0u);

  // Digits in reverse, then copied out forwards.
  char digits[12];
  std::size_t count = 0;
  std::uint32_t remaining = scaled;
  do
  {
    digits[count++] = static_cast<char>('0' + remaining % 10u);
    remaining /= 10u;
    if (decimals == 1 && count == 1)
    {
      digits[count++] = '.';
    }
  } while (remaining != 0 || (decimals == 1 && count < 3));

  const bool showSign = negative && scaled != 0;
  const std::size_t length = count + (showSign ? 1u : 0u);
  if (length + 1 > size)
  {
    return false;
  }

  std::size_t pos = 0;
  if (showSign)
  {
    out[pos++] = '-';
  }
  while (count > 0)
  {
    out[pos++] = digits[--count];
  }
  out[pos] = '\0';
  return true;
}

void TwoDigits(char *out, std::uint8_t value)
{
  out[0] = static_cast<char>('0' + (value / 10u) % 10u);
  out[1] = static_cast<char>('0' + value % 10u);
  out[2] = '\0';
}

int DayOfWeek(int year, int month, int day)
{
  if (month < 1 || month > 12)
  {
    return -1;
  }
  // Sakamoto's method.
  constexpr int kMonthOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 3)
  {
    year -= 1;
  }
  return (year + year / 4 - year / 100 + year / 400 + kMonthOffsets[month - 1] + day) % 7;
}

} // namespace text_format
