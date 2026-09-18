#include <gtest/gtest.h>

#include <string>

#include "menu.hpp"

namespace
{

constexpr Button::Event kNothing = Button::Event::None;
constexpr Button::Event kClick = Button::Event::Click;
constexpr Button::Event kHold = Button::Event::LongPress;

Ds3231::DateTime Sample()
{
  return Ds3231::DateTime{2026, 9, 17, 14, 35, 42};
}

// Opens the menu and moves to the item with this label, so the tests read as
// what the user does rather than as a count of detents.
void SelectItem(Menu &menu, const std::string &label)
{
  for (int i = 0; i < 8; ++i)
  {
    const MenuView view = menu.View();
    if (view.items != nullptr && view.selected < view.itemCount && view.items[view.selected] == label)
    {
      return;
    }
    menu.Update(1, kNothing);
  }
  ADD_FAILURE() << "no item labelled " << label;
}

MenuRequest Choose(Menu &menu, const std::string &label)
{
  SelectItem(menu, label);
  return menu.Update(0, kClick);
}

} // namespace

TEST(MenuTest, StartsClosedAndIgnoresInput)
{
  Menu menu;
  EXPECT_FALSE(menu.IsOpen());
  EXPECT_EQ(menu.Update(3, kClick).action, MenuAction::None);
}

TEST(MenuTest, OpensOnTheFirstItem)
{
  Menu menu;
  menu.Open(Sample());
  ASSERT_TRUE(menu.IsOpen());

  const MenuView view = menu.View();
  EXPECT_EQ(view.mode, MenuView::Mode::List);
  EXPECT_EQ(view.selected, 0u);
  EXPECT_STREQ(view.items[0], "Set time");
}

TEST(MenuTest, TurningMovesTheSelectionAndWrapsAround)
{
  Menu menu;
  menu.Open(Sample());
  const std::size_t count = menu.View().itemCount;

  menu.Update(1, kNothing);
  EXPECT_EQ(menu.View().selected, 1u);
  menu.Update(-1, kNothing);
  EXPECT_EQ(menu.View().selected, 0u);
  // Back past the first item comes round to the last.
  menu.Update(-1, kNothing);
  EXPECT_EQ(menu.View().selected, count - 1);
  menu.Update(1, kNothing);
  EXPECT_EQ(menu.View().selected, 0u);
}

TEST(MenuTest, EjectingAsksTheApplicationStraightAway)
{
  Menu menu;
  menu.Open(Sample());
  EXPECT_EQ(Choose(menu, "Eject card").action, MenuAction::EjectCard);

  // The application reports back, and the message stays until it's dismissed.
  menu.Complete("Card ejected");
  EXPECT_EQ(menu.View().mode, MenuView::Mode::Message);
  EXPECT_STREQ(menu.View().message, "Card ejected");
  EXPECT_EQ(menu.Update(2, kNothing).action, MenuAction::None);
  EXPECT_EQ(menu.View().mode, MenuView::Mode::Message);

  menu.Update(0, kClick);
  EXPECT_EQ(menu.View().mode, MenuView::Mode::List);
}

TEST(MenuTest, ErasingAsksFirstAndStartsOnNo)
{
  Menu menu;
  menu.Open(Sample());
  EXPECT_EQ(Choose(menu, "Erase logs").action, MenuAction::None);

  const MenuView view = menu.View();
  EXPECT_EQ(view.mode, MenuView::Mode::Confirm);
  EXPECT_EQ(view.selected, 0u);
  EXPECT_STREQ(view.items[view.selected], "No");
}

TEST(MenuTest, ConfirmingEraseRequestsIt)
{
  Menu menu;
  menu.Open(Sample());
  Choose(menu, "Erase logs");
  menu.Update(1, kNothing); // to "Yes"
  ASSERT_STREQ(menu.View().items[menu.View().selected], "Yes");
  EXPECT_EQ(menu.Update(0, kClick).action, MenuAction::EraseLogs);
}

TEST(MenuTest, DecliningEraseGoesBackWithoutErasing)
{
  Menu menu;
  menu.Open(Sample());
  Choose(menu, "Erase logs");
  EXPECT_EQ(menu.Update(0, kClick).action, MenuAction::None);
  EXPECT_EQ(menu.View().mode, MenuView::Mode::List);
}

TEST(MenuTest, HoldingBacksOutOfAConfirmationWithoutErasing)
{
  Menu menu;
  menu.Open(Sample());
  Choose(menu, "Erase logs");
  menu.Update(1, kNothing); // sitting on "Yes"
  EXPECT_EQ(menu.Update(0, kHold).action, MenuAction::None);
  EXPECT_EQ(menu.View().mode, MenuView::Mode::List);
  EXPECT_TRUE(menu.IsOpen());
}

TEST(MenuTest, ConfirmationStartsOnNoEachTimeItIsOpened)
{
  Menu menu;
  menu.Open(Sample());
  Choose(menu, "Erase logs");
  menu.Update(1, kNothing); // "Yes"
  menu.Update(0, kHold);    // back out
  Choose(menu, "Erase logs");
  EXPECT_STREQ(menu.View().items[menu.View().selected], "No");
}

TEST(MenuTest, ClosingEndsTheMenu)
{
  Menu menu;
  menu.Open(Sample());
  EXPECT_EQ(Choose(menu, "Close").action, MenuAction::Close);
  EXPECT_FALSE(menu.IsOpen());
}

TEST(MenuTest, HoldingAtTheTopLevelCloses)
{
  Menu menu;
  menu.Open(Sample());
  EXPECT_EQ(menu.Update(0, kHold).action, MenuAction::Close);
  EXPECT_FALSE(menu.IsOpen());
}

TEST(MenuTimeEdit, StartsFromTheCurrentTimeWithSecondsCleared)
{
  Menu menu;
  menu.Open(Sample());
  Choose(menu, "Set time");

  const MenuView view = menu.View();
  ASSERT_EQ(view.mode, MenuView::Mode::TimeEdit);
  EXPECT_EQ(view.time.year, 2026);
  EXPECT_EQ(view.time.month, 9);
  EXPECT_EQ(view.time.date, 17);
  EXPECT_EQ(view.time.hour, 14);
  EXPECT_EQ(view.time.minute, 35);
  EXPECT_EQ(view.time.second, 0);
  EXPECT_EQ(view.field, static_cast<std::uint8_t>(Menu::TimeField::Year));
}

TEST(MenuTimeEdit, EachClickMovesToTheNextFieldAndTheLastOneSetsTheClock)
{
  Menu menu;
  menu.Open(Sample());
  Choose(menu, "Set time");

  menu.Update(1, kNothing); // 2027
  EXPECT_EQ(menu.Update(0, kClick).action, MenuAction::None);
  EXPECT_EQ(menu.View().field, static_cast<std::uint8_t>(Menu::TimeField::Month));
  menu.Update(-1, kNothing); // August
  menu.Update(0, kClick);
  menu.Update(1, kNothing); // 18th
  menu.Update(0, kClick);
  menu.Update(1, kNothing); // 15:00
  menu.Update(0, kClick);
  menu.Update(-5, kNothing); // :30

  const MenuRequest request = menu.Update(0, kClick);
  EXPECT_EQ(request.action, MenuAction::SetTime);
  EXPECT_EQ(request.time.year, 2027);
  EXPECT_EQ(request.time.month, 8);
  EXPECT_EQ(request.time.date, 18);
  EXPECT_EQ(request.time.hour, 15);
  EXPECT_EQ(request.time.minute, 30);
  EXPECT_EQ(request.time.second, 0);
}

TEST(MenuTimeEdit, FieldsWrapWithinTheirOwnRange)
{
  Menu menu;
  menu.Open(Ds3231::DateTime{2026, 1, 15, 0, 0, 0});
  Choose(menu, "Set time");

  menu.Update(0, kClick);    // to the month
  menu.Update(-1, kNothing); // back past January
  EXPECT_EQ(menu.View().time.month, 12);
  menu.Update(1, kNothing);
  EXPECT_EQ(menu.View().time.month, 1);

  menu.Update(0, kClick); // day
  menu.Update(0, kClick); // hour
  menu.Update(-1, kNothing);
  EXPECT_EQ(menu.View().time.hour, 23);
  menu.Update(0, kClick); // minute
  menu.Update(-1, kNothing);
  EXPECT_EQ(menu.View().time.minute, 59);
}

TEST(MenuTimeEdit, DayCannotBeSetPastTheEndOfTheMonth)
{
  Menu menu;
  menu.Open(Ds3231::DateTime{2026, 2, 1, 0, 0, 0});
  Choose(menu, "Set time");
  menu.Update(0, kClick); // month
  menu.Update(0, kClick); // day

  menu.Update(30, kNothing); // far past the end of February
  EXPECT_LE(menu.View().time.date, 28);
}

TEST(MenuTimeEdit, ShorteningTheMonthPullsTheDayBack)
{
  Menu menu;
  menu.Open(Ds3231::DateTime{2026, 10, 31, 9, 0, 0});
  Choose(menu, "Set time");
  menu.Update(0, kClick);   // month
  menu.Update(1, kNothing); // November, which has 30 days
  EXPECT_EQ(menu.View().time.date, 30);
}

TEST(MenuTimeEdit, LeavingALeapYearPullsTheTwentyNinthBack)
{
  Menu menu;
  menu.Open(Ds3231::DateTime{2028, 2, 29, 9, 0, 0});
  Choose(menu, "Set time");
  menu.Update(1, kNothing); // 2029 is not a leap year
  EXPECT_EQ(menu.View().time.date, 28);
}

TEST(MenuTimeEdit, HoldingAbandonsTheEditWithoutSettingTheClock)
{
  Menu menu;
  menu.Open(Sample());
  Choose(menu, "Set time");
  menu.Update(5, kNothing);
  EXPECT_EQ(menu.Update(0, kHold).action, MenuAction::None);
  EXPECT_EQ(menu.View().mode, MenuView::Mode::List);

  // Re-entering starts from the clock again, not from the abandoned edit.
  Choose(menu, "Set time");
  EXPECT_EQ(menu.View().time.year, 2026);
}

TEST(MenuDaysInMonth, KnowsMonthLengthsAndLeapYears)
{
  EXPECT_EQ(Menu::DaysInMonth(2026, 1), 31);
  EXPECT_EQ(Menu::DaysInMonth(2026, 4), 30);
  EXPECT_EQ(Menu::DaysInMonth(2026, 2), 28);
  EXPECT_EQ(Menu::DaysInMonth(2028, 2), 29); // divisible by 4
  EXPECT_EQ(Menu::DaysInMonth(2100, 2), 28); // but not by 400
  EXPECT_EQ(Menu::DaysInMonth(2000, 2), 29);
}
