#pragma once

#include <cstddef>
#include <cstdint>

#include "button.hpp"
#include "ds3231.hpp"

// What the menu is asking the application to do. The menu itself touches no
// hardware: it turns encoder movement and button events into these requests,
// and the application carries them out and reports back, which keeps the
// whole interaction testable on a host.
enum class MenuAction : std::uint8_t
{
  None,
  SetTime,    // write `time` to the RTC
  EjectCard,  // flush and unmount, so the card can be pulled
  EraseLogs,  // delete the log file
  Close,      // leave the menu and go back to sleep
};

struct MenuRequest
{
  MenuAction action = MenuAction::None;
  Ds3231::DateTime time{}; // valid when action is SetTime
};

// What the menu currently looks like. The screen draws this; the menu never
// draws anything itself.
struct MenuView
{
  enum class Mode : std::uint8_t
  {
    List,     // a list of items, one selected
    TimeEdit, // a date and time, one field being changed
    Confirm,  // a question with No/Yes
    Message,  // the result of an action, dismissed with a click
  };

  Mode mode = Mode::List;
  const char *title = "";
  const char *const *items = nullptr;
  std::size_t itemCount = 0;
  std::size_t selected = 0;
  Ds3231::DateTime time{};
  std::uint8_t field = 0; // which part of `time` is being changed
  const char *message = "";
};

// The button menu: turn to move, click to choose, hold to go back.
//
// Actions are requests rather than calls, so the menu has no opinion about
// how the card is unmounted or the clock is set. The application performs
// the request and calls Complete() with the outcome, which the menu shows
// as a message.
class Menu
{
public:
  // Fields of the date and time, in the order they are edited.
  enum class TimeField : std::uint8_t
  {
    Year,
    Month,
    Day,
    Hour,
    Minute,
    Count,
  };

  // Opens at the top level. `now` seeds the clock editor, so setting the
  // time starts from the current one rather than from an arbitrary date.
  void Open(const Ds3231::DateTime &now);

  bool IsOpen() const
  {
    return open_;
  }

  // Feeds one poll's worth of input: `detents` from the encoder (positive is
  // clockwise) and whatever the button reported. Returns what the
  // application should do, if anything.
  MenuRequest Update(int detents, Button::Event buttonEvent);

  // Reports the outcome of the last request as a line to show until the user
  // clicks. The caller words it for either outcome ("Card ejected", "Erase
  // failed"), so the menu needs no view on what went wrong. `message` must
  // outlive the menu, which in practice means a literal.
  void Complete(const char *message);

  MenuView View() const;

  // Days in a month, accounting for leap years, so the day being edited
  // can't be pushed past the end of a short month.
  static std::uint8_t DaysInMonth(std::uint16_t year, std::uint8_t month);

private:
  enum class Screen : std::uint8_t
  {
    Root,
    TimeEdit,
    ConfirmErase,
    Result,
  };

  void GoTo(Screen screen);
  MenuRequest ActivateRootItem();
  MenuRequest AdvanceTimeField();
  void AdjustTimeField(int detents);
  void ClampDay();

  bool open_ = false;
  Screen screen_ = Screen::Root;
  std::size_t selected_ = 0;
  std::uint8_t field_ = 0;
  bool confirmYes_ = false; // the Confirm screen starts on "No"
  const char *message_ = "";
  Ds3231::DateTime time_{};       // the time being edited
  Ds3231::DateTime clockTime_{};  // what the clock reads, so an abandoned edit is discarded
};
