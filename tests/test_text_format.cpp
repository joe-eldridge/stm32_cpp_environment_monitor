#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "text_format.hpp"

namespace
{

std::string Centi(std::int32_t value, int decimals, std::size_t size = 16)
{
  char buffer[16] = "unchanged";
  const bool ok = text_format::Centi(buffer, size, value, decimals);
  return ok ? std::string(buffer) : "FAIL:" + std::string(buffer);
}

} // namespace

TEST(TextFormatCenti, OneDecimalPlace)
{
  EXPECT_EQ(Centi(2384, 1), "23.8");
  EXPECT_EQ(Centi(2385, 1), "23.9"); // half rounds away from zero
  EXPECT_EQ(Centi(100893, 1), "1008.9");
  EXPECT_EQ(Centi(0, 1), "0.0");
  EXPECT_EQ(Centi(4, 1), "0.0");
  EXPECT_EQ(Centi(5, 1), "0.1");
  EXPECT_EQ(Centi(99, 1), "1.0");
}

TEST(TextFormatCenti, NegativeValues)
{
  EXPECT_EQ(Centi(-520, 1), "-5.2");
  EXPECT_EQ(Centi(-525, 1), "-5.3");
  EXPECT_EQ(Centi(-5, 1), "-0.1");
  EXPECT_EQ(Centi(-4, 1), "0.0"); // never "-0.0"
  EXPECT_EQ(Centi(-49, 0), "0");
  EXPECT_EQ(Centi(INT32_MIN, 0), "-21474836");
}

TEST(TextFormatCenti, NoDecimalPlaces)
{
  EXPECT_EQ(Centi(115302, 0), "1153");
  EXPECT_EQ(Centi(115350, 0), "1154");
  EXPECT_EQ(Centi(49, 0), "0");
  EXPECT_EQ(Centi(50, 0), "1");
  EXPECT_EQ(Centi(440395, 0), "4404");
}

TEST(TextFormatCenti, RejectsWhatDoesNotFit)
{
  EXPECT_EQ(Centi(2384, 1, 5), "23.8");     // exactly fits with the terminator
  EXPECT_EQ(Centi(2384, 1, 4), "FAIL:");    // one short: left empty
  EXPECT_EQ(Centi(-520, 1, 4), "FAIL:");
  EXPECT_EQ(Centi(100, 2), "FAIL:");        // unsupported precision
  char buffer[1] = {'x'};
  EXPECT_FALSE(text_format::Centi(buffer, 0, 1, 0));
  EXPECT_EQ(buffer[0], 'x'); // nothing written into a zero-size buffer
}

TEST(TextFormat, TwoDigits)
{
  char buffer[3];
  text_format::TwoDigits(buffer, 7);
  EXPECT_STREQ(buffer, "07");
  text_format::TwoDigits(buffer, 59);
  EXPECT_STREQ(buffer, "59");
  text_format::TwoDigits(buffer, 0);
  EXPECT_STREQ(buffer, "00");
}

TEST(TextFormat, DayOfWeek)
{
  EXPECT_EQ(text_format::DayOfWeek(2026, 9, 17), 4);  // Thursday
  EXPECT_EQ(text_format::DayOfWeek(2000, 1, 1), 6);   // Saturday
  EXPECT_EQ(text_format::DayOfWeek(2024, 2, 29), 4);  // leap day
  EXPECT_EQ(text_format::DayOfWeek(2000, 3, 1), 3);   // just after a century leap day
  EXPECT_EQ(text_format::DayOfWeek(2099, 12, 31), 4); // last date the RTC supports
  EXPECT_EQ(text_format::DayOfWeek(2026, 0, 1), -1);
  EXPECT_EQ(text_format::DayOfWeek(2026, 13, 1), -1);
}

TEST(TextFormatCopy, CopiesAndTerminates)
{
  char out[8] = "xxxxxxx";
  text_format::Copy(out, sizeof(out), "abc");
  EXPECT_STREQ(out, "abc");
}

TEST(TextFormatCopy, TruncatesRatherThanOverrunning)
{
  char out[4];
  text_format::Copy(out, sizeof(out), "abcdef");
  EXPECT_STREQ(out, "abc");
}

TEST(TextFormatAppend, AppendsToWhatIsAlreadyThere)
{
  char out[8];
  text_format::Copy(out, sizeof(out), "ab");
  text_format::Append(out, sizeof(out), "cd");
  EXPECT_STREQ(out, "abcd");
}

TEST(TextFormatAppend, StopsAtTheEndOfTheBuffer)
{
  char out[5];
  text_format::Copy(out, sizeof(out), "abc");
  text_format::Append(out, sizeof(out), "defgh");
  EXPECT_STREQ(out, "abcd");
  // A full buffer takes nothing more.
  text_format::Append(out, sizeof(out), "i");
  EXPECT_STREQ(out, "abcd");
}
