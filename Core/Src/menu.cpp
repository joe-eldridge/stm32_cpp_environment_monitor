#include "menu.hpp"

namespace
{

// The top-level items, in order.
enum RootItem : std::size_t
{
  kSetTime,
  kEjectCard,
  kEraseLogs,
  kClose,
  kRootItemCount,
};

constexpr const char *kRootItems[kRootItemCount] = {
    "Set time",
    "Eject card",
    "Erase logs",
    "Close",
};

constexpr const char *kConfirmItems[2] = {"No", "Yes"};

constexpr std::uint16_t kEarliestYear = 2000;
constexpr std::uint16_t kLatestYear = 2099;

// Wraps `value` into [low, high] after moving it by `delta`, so holding one
// end of a range and turning further comes round rather than sticking.
int WrapInto(int value, int delta, int low, int high)
{
  const int span = high - low + 1;
  int moved = (value + delta - low) % span;
  if (moved < 0)
  {
    moved += span;
  }
  return low + moved;
}

} // namespace

std::uint8_t Menu::DaysInMonth(std::uint16_t year, std::uint8_t month)
{
  constexpr std::uint8_t kLengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12)
  {
    return 31;
  }
  if (month == 2)
  {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  }
  return kLengths[month - 1];
}

void Menu::Open(const Ds3231::DateTime &now)
{
  open_ = true;
  screen_ = Screen::Root;
  selected_ = 0;
  field_ = 0;
  confirmYes_ = false;
  message_ = "";
  time_ = now;
  time_.second = 0; // setting the clock starts the minute cleanly
  ClampDay();
  clockTime_ = time_;
}

void Menu::GoTo(Screen screen)
{
  screen_ = screen;
  field_ = 0;
  confirmYes_ = false;
}

void Menu::ClampDay()
{
  const std::uint8_t lastDay = DaysInMonth(time_.year, time_.month);
  if (time_.date > lastDay)
  {
    time_.date = lastDay;
  }
  if (time_.date < 1)
  {
    time_.date = 1;
  }
}

void Menu::AdjustTimeField(int detents)
{
  switch (static_cast<TimeField>(field_))
  {
  case TimeField::Year:
    time_.year = static_cast<std::uint16_t>(WrapInto(time_.year, detents, kEarliestYear, kLatestYear));
    ClampDay(); // 29 February only exists in a leap year
    break;
  case TimeField::Month:
    time_.month = static_cast<std::uint8_t>(WrapInto(time_.month, detents, 1, 12));
    ClampDay(); // the 31st doesn't survive a move to November
    break;
  case TimeField::Day:
    time_.date = static_cast<std::uint8_t>(WrapInto(time_.date, detents, 1, DaysInMonth(time_.year, time_.month)));
    break;
  case TimeField::Hour:
    time_.hour = static_cast<std::uint8_t>(WrapInto(time_.hour, detents, 0, 23));
    break;
  case TimeField::Minute:
    time_.minute = static_cast<std::uint8_t>(WrapInto(time_.minute, detents, 0, 59));
    break;
  case TimeField::Count:
    break;
  }
}

MenuRequest Menu::AdvanceTimeField()
{
  ++field_;
  if (field_ < static_cast<std::uint8_t>(TimeField::Count))
  {
    return MenuRequest{};
  }

  // Past the last field: the time is complete, so ask for it to be written.
  // Once asked for, this is what the clock reads, so a later edit starts
  // from here rather than from whatever it said when the menu opened.
  clockTime_ = time_;
  MenuRequest request;
  request.action = MenuAction::SetTime;
  request.time = time_;
  return request;
}

MenuRequest Menu::ActivateRootItem()
{
  MenuRequest request;
  switch (selected_)
  {
  case kSetTime:
    // Start from the clock, so an edit that was abandoned last time isn't
    // still sitting there.
    time_ = clockTime_;
    GoTo(Screen::TimeEdit);
    break;
  case kEjectCard:
    request.action = MenuAction::EjectCard;
    break;
  case kEraseLogs:
    // This throws away every reading the device has recorded, so it asks
    // first, and starts on "No".
    GoTo(Screen::ConfirmErase);
    break;
  case kClose:
  default:
    request.action = MenuAction::Close;
    open_ = false;
    break;
  }
  return request;
}

MenuRequest Menu::Update(int detents, Button::Event buttonEvent)
{
  MenuRequest request;
  if (!open_)
  {
    return request;
  }

  // Holding the button goes back one level, and closes the menu from the
  // top, so there is always a way out without hunting for an item.
  if (buttonEvent == Button::Event::LongPress)
  {
    if (screen_ == Screen::Root)
    {
      open_ = false;
      request.action = MenuAction::Close;
    }
    else
    {
      GoTo(Screen::Root);
    }
    return request;
  }

  switch (screen_)
  {
  case Screen::Root:
    if (detents != 0)
    {
      selected_ = static_cast<std::size_t>(WrapInto(static_cast<int>(selected_), detents, 0, kRootItemCount - 1));
    }
    if (buttonEvent == Button::Event::Click)
    {
      request = ActivateRootItem();
    }
    break;

  case Screen::TimeEdit:
    if (detents != 0)
    {
      AdjustTimeField(detents);
    }
    if (buttonEvent == Button::Event::Click)
    {
      request = AdvanceTimeField();
    }
    break;

  case Screen::ConfirmErase:
    if (detents != 0)
    {
      confirmYes_ = !confirmYes_;
    }
    if (buttonEvent == Button::Event::Click)
    {
      if (confirmYes_)
      {
        request.action = MenuAction::EraseLogs;
      }
      else
      {
        GoTo(Screen::Root);
      }
    }
    break;

  case Screen::Result:
    if (buttonEvent == Button::Event::Click)
    {
      GoTo(Screen::Root);
    }
    break;
  }

  return request;
}

void Menu::Complete(const char *message)
{
  message_ = message;
  screen_ = Screen::Result;
}

MenuView Menu::View() const
{
  MenuView view;
  switch (screen_)
  {
  case Screen::Root:
    view.mode = MenuView::Mode::List;
    view.title = "MENU";
    view.items = kRootItems;
    view.itemCount = kRootItemCount;
    view.selected = selected_;
    break;

  case Screen::TimeEdit:
    view.mode = MenuView::Mode::TimeEdit;
    view.title = "SET TIME";
    view.time = time_;
    view.field = field_;
    break;

  case Screen::ConfirmErase:
    view.mode = MenuView::Mode::Confirm;
    view.title = "ERASE LOGS";
    view.message = "Erase all logs?";
    view.items = kConfirmItems;
    view.itemCount = 2;
    view.selected = confirmYes_ ? 1u : 0u;
    break;

  case Screen::Result:
    view.mode = MenuView::Mode::Message;
    view.title = "MENU";
    view.message = message_;
    break;
  }
  return view;
}
