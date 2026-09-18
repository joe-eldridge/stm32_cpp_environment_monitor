#include <gtest/gtest.h>

#include <vector>

#include "mono_framebuffer.hpp"

namespace
{

class Framebuffer : public ::testing::Test
{
protected:
  static constexpr std::uint16_t kWidth = 20; // not a multiple of 8: rows are 3 bytes
  static constexpr std::uint16_t kHeight = 10;

  void SetUp() override
  {
    canvas.Fill(Color::White);
  }

  int CountBlack() const
  {
    int count = 0;
    for (int y = 0; y < kHeight; ++y)
    {
      for (int x = 0; x < kWidth; ++x)
      {
        count += canvas.GetPixel(x, y) == Color::Black ? 1 : 0;
      }
    }
    return count;
  }

  std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(MonoFramebuffer::BufferSize(kWidth, kHeight), 0xAA);
  MonoFramebuffer canvas{memory.data(), kWidth, kHeight};
};

} // namespace

TEST(FramebufferSize, RowsArePaddedToWholeBytes)
{
  EXPECT_EQ(MonoFramebuffer::BufferSize(200, 200), 5000u);
  EXPECT_EQ(MonoFramebuffer::BufferSize(20, 10), 30u);
  EXPECT_EQ(MonoFramebuffer::BufferSize(1, 1), 1u);
}

TEST_F(Framebuffer, FillSetsEveryByte)
{
  canvas.Fill(Color::Black);
  EXPECT_EQ(memory, std::vector<std::uint8_t>(memory.size(), 0x00));
  canvas.Fill(Color::White);
  EXPECT_EQ(memory, std::vector<std::uint8_t>(memory.size(), 0xFF));
}

TEST_F(Framebuffer, PixelLayoutMatchesSsd1681Ram)
{
  // Black is 0; MSB is the leftmost pixel of each byte; rows are 3 bytes.
  canvas.SetPixel(0, 0, Color::Black);
  EXPECT_EQ(memory[0], 0x7F);
  canvas.SetPixel(9, 0, Color::Black);
  EXPECT_EQ(memory[1], 0xBF);
  canvas.SetPixel(19, 0, Color::Black); // last pixel lives in the padded byte
  EXPECT_EQ(memory[2], 0xEF);
  canvas.SetPixel(0, 1, Color::Black);
  EXPECT_EQ(memory[3], 0x7F);
  canvas.SetPixel(0, 1, Color::White);
  EXPECT_EQ(memory[3], 0xFF);
}

TEST_F(Framebuffer, GetPixelReadsBack)
{
  canvas.SetPixel(7, 3, Color::Black);
  EXPECT_EQ(canvas.GetPixel(7, 3), Color::Black);
  EXPECT_EQ(canvas.GetPixel(8, 3), Color::White);
  EXPECT_EQ(CountBlack(), 1);
}

TEST_F(Framebuffer, OutOfBoundsIsClippedAndReadsWhite)
{
  const std::vector<std::uint8_t> before = memory;
  canvas.SetPixel(-1, 0, Color::Black);
  canvas.SetPixel(0, -1, Color::Black);
  canvas.SetPixel(kWidth, 0, Color::Black);
  canvas.SetPixel(0, kHeight, Color::Black);
  EXPECT_EQ(memory, before);
  EXPECT_EQ(canvas.GetPixel(-1, -1), Color::White);
  EXPECT_EQ(canvas.GetPixel(kWidth, kHeight), Color::White);
}

TEST_F(Framebuffer, FillRectIsClippedToSurface)
{
  canvas.FillRect(-5, -5, 10, 8, Color::Black); // visible part: 5 x 3
  EXPECT_EQ(CountBlack(), 15);
  EXPECT_EQ(canvas.GetPixel(4, 2), Color::Black);
  EXPECT_EQ(canvas.GetPixel(5, 2), Color::White);
  EXPECT_EQ(canvas.GetPixel(4, 3), Color::White);
}

TEST_F(Framebuffer, EmptyRectsDrawNothing)
{
  canvas.FillRect(2, 2, 0, 5, Color::Black);
  canvas.FillRect(2, 2, 5, -1, Color::Black);
  canvas.Rect(2, 2, 0, 0, Color::Black);
  EXPECT_EQ(CountBlack(), 0);
}

TEST_F(Framebuffer, RectDrawsOutlineOnly)
{
  canvas.Rect(1, 1, 5, 4, Color::Black);
  EXPECT_EQ(CountBlack(), 2 * 5 + 2 * 2); // top/bottom rows + remaining side pixels
  EXPECT_EQ(canvas.GetPixel(1, 1), Color::Black);
  EXPECT_EQ(canvas.GetPixel(5, 4), Color::Black);
  EXPECT_EQ(canvas.GetPixel(3, 2), Color::White); // inside stays clear
}

TEST_F(Framebuffer, HorizontalAndVerticalLines)
{
  canvas.HorizontalLine(2, 5, 4, Color::Black);
  canvas.VerticalLine(10, 1, 3, Color::Black);
  EXPECT_EQ(CountBlack(), 7);
  EXPECT_EQ(canvas.GetPixel(5, 5), Color::Black);
  EXPECT_EQ(canvas.GetPixel(6, 5), Color::White);
  EXPECT_EQ(canvas.GetPixel(10, 3), Color::Black);
  EXPECT_EQ(canvas.GetPixel(10, 4), Color::White);
}

struct LineCase
{
  int x0, y0, x1, y1;
  int expectedPixels;
};

class FramebufferLine : public Framebuffer, public ::testing::WithParamInterface<LineCase>
{
};

TEST_P(FramebufferLine, DrawsBothEndpointsAndOnePixelPerMajorStep)
{
  const LineCase &c = GetParam();
  canvas.Line(c.x0, c.y0, c.x1, c.y1, Color::Black);
  EXPECT_EQ(canvas.GetPixel(c.x0, c.y0), Color::Black);
  EXPECT_EQ(canvas.GetPixel(c.x1, c.y1), Color::Black);
  EXPECT_EQ(CountBlack(), c.expectedPixels);
}

INSTANTIATE_TEST_SUITE_P(Directions, FramebufferLine,
                         ::testing::Values(LineCase{0, 0, 9, 9, 10},   // diagonal
                                           LineCase{9, 9, 0, 0, 10},   // reversed
                                           LineCase{0, 9, 9, 0, 10},   // anti-diagonal
                                           LineCase{0, 0, 19, 4, 20},  // shallow: one per column
                                           LineCase{3, 0, 5, 9, 10},   // steep: one per row
                                           LineCase{4, 4, 4, 4, 1},    // single point
                                           LineCase{0, 2, 19, 2, 20})); // horizontal

TEST_F(Framebuffer, LinesAreClippedAtEdges)
{
  canvas.Line(-10, 0, 30, 0, Color::Black);
  EXPECT_EQ(CountBlack(), kWidth);
}

// FillRect writes whole bytes at a time, with masks for the partial bytes at
// each end of a row. That is exactly the sort of code that is right for the
// cases you think of and wrong at one alignment you didn't, so it is checked
// against the obvious pixel-by-pixel version at every alignment, width and
// clipping position, over a patterned background so both setting and
// clearing bits are exercised. The whole buffer is compared, padding bits
// included: they must never be touched.
TEST(FramebufferFillRect, MatchesPixelByPixelAtEveryAlignment)
{
  constexpr std::uint16_t kWidth = 37; // rows of 5 bytes, 3 of the bits padding
  constexpr std::uint16_t kHeight = 4;
  constexpr std::size_t kBytes = MonoFramebuffer::BufferSize(kWidth, kHeight);

  for (const Color color : {Color::Black, Color::White})
  {
    for (int x = -10; x < kWidth + 5; ++x)
    {
      for (int width = 0; width <= kWidth + 12; ++width)
      {
        std::vector<std::uint8_t> fastMemory(kBytes);
        std::vector<std::uint8_t> slowMemory(kBytes);
        for (std::size_t i = 0; i < kBytes; ++i)
        {
          fastMemory[i] = slowMemory[i] = static_cast<std::uint8_t>(i % 2 == 0 ? 0xA5 : 0x3C);
        }
        MonoFramebuffer fast(fastMemory.data(), kWidth, kHeight);
        MonoFramebuffer slow(slowMemory.data(), kWidth, kHeight);

        fast.FillRect(x, 1, width, 2, color);
        for (int row = 1; row < 3; ++row)
        {
          for (int column = x; column < x + width; ++column)
          {
            slow.SetPixel(column, row, color); // clips on its own
          }
        }

        ASSERT_EQ(fastMemory, slowMemory) << "x " << x << ", width " << width << ", "
                                          << (color == Color::Black ? "black" : "white");
      }
    }
  }
}

TEST(FramebufferFillRect, ClipsVerticallyToo)
{
  constexpr std::uint16_t kWidth = 16;
  constexpr std::uint16_t kHeight = 4;
  std::vector<std::uint8_t> memory(MonoFramebuffer::BufferSize(kWidth, kHeight), 0xFF);
  MonoFramebuffer canvas(memory.data(), kWidth, kHeight);

  canvas.FillRect(0, -3, 16, 5, Color::Black); // rows -3..1, only 0 and 1 visible
  EXPECT_EQ(memory, (std::vector<std::uint8_t>{0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF}));

  canvas.FillRect(0, 3, 16, 10, Color::Black); // off the bottom after row 3
  EXPECT_EQ(memory[6], 0x00);
  EXPECT_EQ(memory[7], 0x00);
  EXPECT_EQ(memory[4], 0xFF); // row 2 untouched
}
