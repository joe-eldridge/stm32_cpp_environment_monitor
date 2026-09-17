#include <gtest/gtest.h>

#include <vector>

#include "menu_screen.hpp"
#include "ssd1681.hpp"

namespace
{

class Canvas
{
public:
  Canvas() : memory_(Ssd1681::kImageBytes), canvas_(memory_.data(), Ssd1681::kWidth, Ssd1681::kHeight)
  {
  }

  MonoFramebuffer &Get()
  {
    return canvas_;
  }

  // Counts black pixels in a band of rows, which is how these tests tell a
  // highlighted row from a plain one without pinning every glyph.
  int BlackInRows(int firstY, int lastY) const
  {
    int count = 0;
    for (int y = firstY; y <= lastY; ++y)
    {
      for (int x = 0; x < canvas_.Width(); ++x)
      {
        if (canvas_.GetPixel(x, y) == Color::Black)
        {
          ++count;
        }
      }
    }
    return count;
  }

private:
  std::vector<std::uint8_t> memory_;
  MonoFramebuffer canvas_;
};

Ds3231::DateTime Sample()
{
  return Ds3231::DateTime{2026, 9, 17, 14, 35, 0};
}

Menu OpenedMenu()
{
  Menu menu;
  menu.Open(Sample());
  return menu;
}

} // namespace

TEST(MenuScreenText, FormatsTheDateAndTime)
{
  char text[20];
  std::size_t first = 0;
  std::size_t last = 0;
  FormatEditableTime(text, sizeof(text), Sample(), 0, first, last);
  EXPECT_STREQ(text, "2026-09-17 14:35");
}

TEST(MenuScreenText, PadsEveryFieldToAFixedWidth)
{
  char text[20];
  std::size_t first = 0;
  std::size_t last = 0;
  FormatEditableTime(text, sizeof(text), Ds3231::DateTime{2026, 1, 2, 3, 4, 0}, 0, first, last);
  // Fixed positions are what lets the screen underline a field by counting
  // characters.
  EXPECT_STREQ(text, "2026-01-02 03:04");
}

TEST(MenuScreenText, MarksOutTheFieldBeingEdited)
{
  const struct
  {
    Menu::TimeField field;
    std::size_t first;
    std::size_t last;
  } cases[] = {
      {Menu::TimeField::Year, 0, 3},   {Menu::TimeField::Month, 5, 6}, {Menu::TimeField::Day, 8, 9},
      {Menu::TimeField::Hour, 11, 12}, {Menu::TimeField::Minute, 14, 15},
  };

  for (const auto &test : cases)
  {
    char text[20];
    std::size_t first = 0;
    std::size_t last = 0;
    FormatEditableTime(text, sizeof(text), Sample(), static_cast<std::uint8_t>(test.field), first, last);
    EXPECT_EQ(first, test.first) << "field " << static_cast<int>(test.field);
    EXPECT_EQ(last, test.last) << "field " << static_cast<int>(test.field);
  }
}

TEST(MenuScreenDraw, SelectedItemIsHighlightedAndTheOthersAreNot)
{
  Canvas canvas;
  Menu menu = OpenedMenu();

  // Row bands, from the layout: the first item sits at y = 38, 26 apart.
  constexpr int kFirstRowTop = 34;
  constexpr int kRowPitch = 26;

  DrawMenuScreen(canvas.Get(), menu.View());
  const int firstSelected = canvas.BlackInRows(kFirstRowTop, kFirstRowTop + 21);
  const int secondPlain = canvas.BlackInRows(kFirstRowTop + kRowPitch, kFirstRowTop + kRowPitch + 21);
  // A highlighted row is a filled bar, so it is far darker than a plain one.
  EXPECT_GT(firstSelected, secondPlain * 3);

  menu.Update(1, Button::Event::None);
  DrawMenuScreen(canvas.Get(), menu.View());
  const int firstNowPlain = canvas.BlackInRows(kFirstRowTop, kFirstRowTop + 21);
  const int secondNowSelected = canvas.BlackInRows(kFirstRowTop + kRowPitch, kFirstRowTop + kRowPitch + 21);
  EXPECT_GT(secondNowSelected, firstNowPlain * 3);
}

TEST(MenuScreenDraw, EveryModeDrawsSomethingAndFitsTheScreen)
{
  Canvas canvas;
  Menu menu = OpenedMenu();

  // Each mode in turn: list, the clock editor, the format confirmation and
  // a result message. None of them may leave the screen blank.
  const MenuView list = menu.View();
  ASSERT_EQ(list.mode, MenuView::Mode::List);
  DrawMenuScreen(canvas.Get(), list);
  EXPECT_GT(canvas.BlackInRows(0, Ssd1681::kHeight - 1), 100);

  menu.Update(0, Button::Event::Click); // Set time
  ASSERT_EQ(menu.View().mode, MenuView::Mode::TimeEdit);
  DrawMenuScreen(canvas.Get(), menu.View());
  EXPECT_GT(canvas.BlackInRows(0, Ssd1681::kHeight - 1), 100);

  menu.Update(0, Button::Event::LongPress); // back to the list
  menu.Update(2, Button::Event::None);      // Format card
  menu.Update(0, Button::Event::Click);
  ASSERT_EQ(menu.View().mode, MenuView::Mode::Confirm);
  DrawMenuScreen(canvas.Get(), menu.View());
  EXPECT_GT(canvas.BlackInRows(0, Ssd1681::kHeight - 1), 100);

  menu.Complete("Card formatted");
  ASSERT_EQ(menu.View().mode, MenuView::Mode::Message);
  DrawMenuScreen(canvas.Get(), menu.View());
  EXPECT_GT(canvas.BlackInRows(0, Ssd1681::kHeight - 1), 100);
}

TEST(MenuScreenDraw, TheUnderlineMovesWithTheFieldBeingEdited)
{
  Canvas canvas;
  Menu menu = OpenedMenu();
  menu.Update(0, Button::Event::Click); // Set time

  // The underline sits just below the text; it is the only thing drawn on
  // that row, so its position tells us which field is marked.
  constexpr int kUnderlineY = 70 + 14 + 4;
  struct Span
  {
    int first;
    int last;
  };
  const auto underlineSpan = [&canvas](int y) {
    int first = -1;
    int last = -1;
    for (int x = 0; x < Ssd1681::kWidth; ++x)
    {
      if (canvas.Get().GetPixel(x, y) == Color::Black)
      {
        first = first < 0 ? x : first;
        last = x;
      }
    }
    return Span{first, last};
  };

  DrawMenuScreen(canvas.Get(), menu.View());
  const auto year = underlineSpan(kUnderlineY);
  ASSERT_GE(year.first, 0);
  // Four digits wide, at 10 pixels a character.
  EXPECT_NEAR(year.last - year.first, 38, 2);

  menu.Update(0, Button::Event::Click); // to the month
  DrawMenuScreen(canvas.Get(), menu.View());
  const auto month = underlineSpan(kUnderlineY);
  ASSERT_GE(month.first, 0);
  EXPECT_GT(month.first, year.last);        // further right
  EXPECT_NEAR(month.last - month.first, 18, 2); // and half as wide
}
