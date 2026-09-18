#include "button.hpp"

Button::Button(std::uint16_t debounceMs, std::uint16_t longPressMs)
    : debounceMs_(debounceMs), longPressMs_(longPressMs)
{
}

void Button::Reset(bool pressed, std::uint32_t nowMs)
{
  accepted_ = pressed;
  candidate_ = pressed;
  timing_ = false;
  // Already-held counts as reported: a press adopted this way has no click
  // or hold of its own to give.
  longPressSent_ = true;
  changedAtMs_ = nowMs;
  pressedAtMs_ = nowMs;
}

Button::Event Button::Update(bool pressed, std::uint32_t nowMs)
{
  if (pressed != accepted_)
  {
    if (!timing_ || pressed != candidate_)
    {
      // A new change, or the level moved again while being timed: start the
      // debounce window from here.
      timing_ = true;
      candidate_ = pressed;
      changedAtMs_ = nowMs;
    }
    else if (nowMs - changedAtMs_ >= debounceMs_)
    {
      timing_ = false;
      accepted_ = pressed;
      if (accepted_)
      {
        pressedAtMs_ = nowMs;
        longPressSent_ = false;
      }
      else if (!longPressSent_)
      {
        return Event::Click;
      }
    }
  }
  else
  {
    timing_ = false; // the level came back before the window elapsed
  }

  if (accepted_ && !longPressSent_ && nowMs - pressedAtMs_ >= longPressMs_)
  {
    longPressSent_ = true;
    return Event::LongPress;
  }
  return Event::None;
}
