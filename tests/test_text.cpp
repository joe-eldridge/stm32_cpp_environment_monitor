#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "font_5x7.hpp"
#include "text.hpp"

namespace
{

class TextTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    canvas.Fill(Color::White);
  }

  // Renders a region as rows of '#' and '.'.
  std::vector<std::string> Region(int x, int y, int width, int height) const
  {
    std::vector<std::string> rows;
    for (int row = 0; row < height; ++row)
    {
      std::string line;
      for (int column = 0; column < width; ++column)
      {
        line += canvas.GetPixel(x + column, y + row) == Color::Black ? '#' : '.';
      }
      rows.push_back(line);
    }
    return rows;
  }

  int CountBlack() const
  {
    int count = 0;
    for (const auto &row : Region(0, 0, 64, 32))
    {
      for (char c : row)
      {
        count += c == '#' ? 1 : 0;
      }
    }
    return count;
  }

  std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(MonoFramebuffer::BufferSize(64, 32));
  MonoFramebuffer canvas{memory.data(), 64, 32};
};

} // namespace

TEST(Font5x7, CoversPrintableAsciiAndDegreeSign)
{
  EXPECT_EQ(kFont5x7.width, 5);
  EXPECT_EQ(kFont5x7.height, 7);
  EXPECT_NE(kFont5x7.Glyph(' '), nullptr);
  EXPECT_NE(kFont5x7.Glyph('~'), nullptr);
  EXPECT_NE(kFont5x7.Glyph(kDegreeSign), nullptr);
  EXPECT_EQ(kFont5x7.Glyph('\x1F'), nullptr);
  EXPECT_EQ(kFont5x7.Glyph('\x80'), nullptr);
}

TEST(Font5x7, SpaceIsBlankAndGlyphsKeepSpacingColumn)
{
  const std::uint8_t *space = kFont5x7.Glyph(' ');
  for (int column = 0; column < 5; ++column)
  {
    EXPECT_EQ(space[column], 0);
  }
  // Only these three use the full cell in the source font; they'd touch a
  // neighbouring character, so avoid them on screens.
  const std::string fullWidth = "#$+";
  for (char c = '!'; c <= '~'; ++c)
  {
    const bool touches = kFont5x7.Glyph(c)[4] != 0;
    EXPECT_EQ(touches, fullWidth.find(c) != std::string::npos) << "glyph '" << c << "'";
  }
}

TEST_F(TextTest, DrawsGlyphPixels)
{
  DrawText(canvas, kFont5x7, 1, 1, "H", Color::Black);
  const std::vector<std::string> expected{
      "#..#.", "#..#.", "####.", "#..#.", "#..#.", "#..#.", ".....",
  };
  EXPECT_EQ(Region(1, 1, 5, 7), expected);
}

TEST_F(TextTest, DrawsDegreeSign)
{
  const char text[] = {kDegreeSign, '\0'};
  DrawText(canvas, kFont5x7, 0, 0, text, Color::Black);
  const std::vector<std::string> expected{".#.", "#.#", ".#."};
  EXPECT_EQ(Region(1, 0, 3, 3), expected);
}

TEST_F(TextTest, ScaleEnlargesEachPixel)
{
  DrawText(canvas, kFont5x7, 0, 0, "H", Color::Black, 2);
  EXPECT_EQ(CountBlack(), 14 * 4); // 'H' has 14 pixels
  EXPECT_EQ(Region(0, 0, 4, 2), (std::vector<std::string>{"##..", "##.."}));
}

TEST_F(TextTest, ReturnsNextXAndAdvancesPerCharacter)
{
  EXPECT_EQ(DrawText(canvas, kFont5x7, 3, 0, "AB", Color::Black), 13);
  EXPECT_EQ(DrawText(canvas, kFont5x7, 0, 10, "AB", Color::Black, 3), 30);
  EXPECT_EQ(TextWidth(kFont5x7, "AB"), 10);
  EXPECT_EQ(TextWidth(kFont5x7, "AB", 3), 30);
  EXPECT_EQ(TextWidth(kFont5x7, ""), 0);
}

TEST_F(TextTest, BackgroundIsNotPainted)
{
  canvas.Fill(Color::Black);
  DrawText(canvas, kFont5x7, 0, 0, "H", Color::Black);
  EXPECT_EQ(canvas.GetPixel(1, 0), Color::Black); // gap inside 'H' left alone
}

TEST_F(TextTest, UnknownCharactersDrawABox)
{
  DrawText(canvas, kFont5x7, 0, 0, "\x01", Color::Black);
  EXPECT_EQ(Region(0, 0, 5, 3), (std::vector<std::string>{"####.", "#..#.", "#..#."}));
}

TEST_F(TextTest, ClipsAtCanvasEdge)
{
  EXPECT_EQ(DrawText(canvas, kFont5x7, 60, 28, "HH", Color::Black), 70);
  EXPECT_GT(CountBlack(), 0);
}

// Glyphs are drawn as vertical runs of lit pixels rather than pixel by
// pixel. Checked against the pixel-by-pixel version for every character in
// the font, at every scale in use and at every alignment within a byte.
TEST(TextRendering, MatchesPixelByPixelForEveryGlyphScaleAndAlignment)
{
  constexpr std::uint16_t kWidth = 64;
  constexpr std::uint16_t kHeight = 32;
  constexpr std::size_t kBytes = MonoFramebuffer::BufferSize(kWidth, kHeight);

  for (int scale = 1; scale <= 3; ++scale)
  {
    for (int x = 0; x < 8; ++x)
    {
      for (int code = kFont5x7.first; code <= kFont5x7.last; ++code)
      {
        std::vector<std::uint8_t> fastMemory(kBytes, 0xFF);
        std::vector<std::uint8_t> slowMemory(kBytes, 0xFF);
        MonoFramebuffer fast(fastMemory.data(), kWidth, kHeight);
        MonoFramebuffer slow(slowMemory.data(), kWidth, kHeight);

        const char text[] = {static_cast<char>(code), '\0'};
        DrawText(fast, kFont5x7, x, 3, text, Color::Black, scale);

        const std::uint8_t *glyph = kFont5x7.Glyph(static_cast<char>(code));
        ASSERT_NE(glyph, nullptr);
        for (int column = 0; column < kFont5x7.width; ++column)
        {
          for (int row = 0; row < kFont5x7.height; ++row)
          {
            if ((glyph[column] >> row) & 1u)
            {
              for (int dy = 0; dy < scale; ++dy)
              {
                for (int dx = 0; dx < scale; ++dx)
                {
                  slow.SetPixel(x + column * scale + dx, 3 + row * scale + dy, Color::Black);
                }
              }
            }
          }
        }

        ASSERT_EQ(fastMemory, slowMemory) << "character " << code << ", scale " << scale << ", x " << x;
      }
    }
  }
}
