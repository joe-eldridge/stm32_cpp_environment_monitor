#include "time_sync.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" UART_HandleTypeDef huart2;

namespace
{

// HAL_UART_Receive only runs its own overrun-error detection/recovery when
// its Timeout argument isn't HAL_MAX_DELAY (see UART_WaitOnFlagUntilTimeout
// in the HAL source - the whole check is gated behind `Timeout !=
// HAL_MAX_DELAY`). Passing HAL_MAX_DELAY there would mean one dropped byte
// (e.g. an overrun while we're mid-echo of the previous one) wedges RXNE
// forever, with no way to recover short of a reset. A finite timeout - even
// a generous one - re-enables that recovery; it's re-checked on every poll
// iteration, so a genuinely idle line still waits close to the full
// duration, but a stuck one recovers almost immediately.
constexpr std::uint32_t kUartRxTimeoutMs = 30000;

void Send(UART_HandleTypeDef *uart, const char *message)
{
  HAL_UART_Transmit(uart, reinterpret_cast<const uint8_t *>(message),
                     static_cast<uint16_t>(std::strlen(message)), HAL_MAX_DELAY);
}

// Blocking line read, terminated by CR or LF. Leading CR/LF (e.g. the
// trailing '\n' of a terminal's CRLF) is ignored rather than treated as an
// empty line.
//
// Deliberately does NOT echo byte-by-byte inside this loop, even though the
// underlying overrun issue turned out to be USART2's baud rate outrunning
// this project's low-power MSI clock (~2MHz) in a Debug build, fixed by
// lowering the baud rate itself. Skipping the per-byte echo here is a cheap
// extra margin against the same class of problem regardless of baud/build
// settings - the whole line is echoed back in one burst afterward instead
// (see SyncTimeFromUart).
bool ReadLine(UART_HandleTypeDef *uart, char *buffer, std::size_t bufferSize, std::size_t &outLength)
{
  outLength = 0;
  while (outLength + 1 < bufferSize)
  {
    uint8_t ch;
    if (HAL_UART_Receive(uart, &ch, 1, kUartRxTimeoutMs) != HAL_OK)
    {
      return false;
    }

    if (ch == '\r' || ch == '\n')
    {
      if (outLength == 0)
      {
        continue;
      }
      break;
    }
    buffer[outLength++] = static_cast<char>(ch);
  }
  buffer[outLength] = '\0';
  return true;
}

// Expects the fixed format "YYYY-MM-DD HH:MM:SS" (exactly 19 characters).
// Hand-parsed rather than via sscanf, so field ranges are checked explicitly
// and there's no format-string/locale machinery involved.
bool ParseDateTime(const char *line, std::size_t length, Ds3231::DateTime &out)
{
  if (length != 19)
  {
    return false;
  }

  auto digit = [](char c) -> int { return (c >= '0' && c <= '9') ? (c - '0') : -1; };
  auto twoDigits = [&](const char *p, std::uint8_t &value) -> bool {
    const int hi = digit(p[0]);
    const int lo = digit(p[1]);
    if (hi < 0 || lo < 0)
    {
      return false;
    }
    value = static_cast<std::uint8_t>(hi * 10 + lo);
    return true;
  };

  const int y3 = digit(line[0]);
  const int y2 = digit(line[1]);
  const int y1 = digit(line[2]);
  const int y0 = digit(line[3]);
  if (y3 < 0 || y2 < 0 || y1 < 0 || y0 < 0)
  {
    return false;
  }

  if (line[4] != '-' || line[7] != '-' || line[10] != ' ' || line[13] != ':' || line[16] != ':')
  {
    return false;
  }

  Ds3231::DateTime dt{};
  dt.year = static_cast<std::uint16_t>((y3 * 1000) + (y2 * 100) + (y1 * 10) + y0);
  if (!twoDigits(&line[5], dt.month) || !twoDigits(&line[8], dt.date) ||
      !twoDigits(&line[11], dt.hour) || !twoDigits(&line[14], dt.minute) ||
      !twoDigits(&line[17], dt.second))
  {
    return false;
  }

  if (dt.month < 1 || dt.month > 12 || dt.date < 1 || dt.date > 31 || dt.hour > 23 ||
      dt.minute > 59 || dt.second > 59)
  {
    return false;
  }

  out = dt;
  return true;
}

} // namespace

void SyncTimeFromUart(UART_HandleTypeDef *uart, Ds3231 &rtc)
{
  char line[32];

  for (;;)
  {
    Send(uart, "\r\nRTC time invalid - enter UTC time as YYYY-MM-DD HH:MM:SS: ");

    std::size_t length = 0;
    Ds3231::DateTime dt{};
    if (!ReadLine(uart, line, sizeof(line), length))
    {
      Send(uart, "\r\nNo line received (timed out) - try again.");
      continue;
    }

    // The whole line is echoed here, in one burst, rather than byte-by-byte
    // inside ReadLine() - see the comment there for why.
    Send(uart, "\r\n");
    Send(uart, line);

    if (!ParseDateTime(line, length, dt))
    {
      Send(uart, "\r\nCouldn't parse that - try again.");
      continue;
    }

    if (rtc.SetDateTime(dt) && rtc.ClearOscillatorStopFlag())
    {
      Send(uart, "\r\nTime set.\r\n");
      return;
    }

    Send(uart, "\r\nFailed to write RTC - check wiring and try again.");
  }
}
