#pragma once

#include <cstdint>

// Debouncing for a momentary push button, including the encoder's own shaft
// switch. Like RotaryEncoder, it holds no pins: the caller passes the level
// it read and the time it read it, so the behaviour is testable on a host and
// the same class works whether the button is polled or drives an interrupt.
//
// A click is reported when the button is released, not when it is pressed, so
// that a press held long enough to count as a long press doesn't also act as
// a click.
class Button
{
public:
  enum class Event : std::uint8_t
  {
    None,
    Click,
    LongPress,
  };

  // `debounceMs` is how long a level must hold before it's believed, and
  // `longPressMs` how long a press must be held to count as a long one.
  Button(std::uint16_t debounceMs = 5, std::uint16_t longPressMs = 800);

  // `pressed` is the debounced-by-this-call button level (true = pressed,
  // so the caller inverts an active-low input), and `nowMs` a millisecond
  // tick. Tick wraparound is handled, as in Timeout.
  Event Update(bool pressed, std::uint32_t nowMs);

  // Adopts `pressed` as the current state without reporting anything.
  //
  // The class measures a hold from when it saw the press, so it can't tell a
  // long hold from a press it stopped watching: if the caller blocks for a
  // second or two, whatever the button did meanwhile is lost, and the next
  // poll would read the gap as a hold. Callers that block - refreshing an
  // e-paper panel, say - reset the button afterwards, and on the way in, so
  // that the press that started them isn't counted twice.
  void Reset(bool pressed, std::uint32_t nowMs);

  // True between an accepted press and its release.
  bool IsPressed() const
  {
    return accepted_;
  }

private:
  std::uint16_t debounceMs_;
  std::uint16_t longPressMs_;
  bool accepted_ = false;      // the debounced level
  bool candidate_ = false;     // the level currently being timed out
  bool timing_ = false;        // a change is being timed
  bool longPressSent_ = false; // this press has already reported a long press
  std::uint32_t changedAtMs_ = 0;
  std::uint32_t pressedAtMs_ = 0;
};
