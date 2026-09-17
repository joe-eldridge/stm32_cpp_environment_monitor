#pragma once

#include "mono_framebuffer.hpp"
#include "ssd1681.hpp"

// Owns the update cycle for an SSD1681 panel and the framebuffer drawn on it.
//
// The framebuffer always holds what's on the panel. A partial refresh needs
// that previous image uploaded as the reference, so Update() sends it before
// the new drawing replaces it - one 5 KB buffer instead of two, which matters
// with 20 KB of RAM.
//
// The panel is in deep sleep between updates, and its RAM isn't trusted
// across that: both images are re-sent every time.
class EpaperDisplay
{
public:
  EpaperDisplay(Ssd1681 &panel, MonoFramebuffer &canvas) : panel_(panel), canvas_(canvas)
  {
  }

  // Wakes the panel, lets `draw(MonoFramebuffer&)` change the canvas,
  // refreshes, and puts the panel back into deep sleep.
  //
  // A partial refresh is upgraded to full when the panel's contents aren't
  // known to match the canvas: the first update after boot, or after any
  // failed update.
  template <typename DrawFn>
  bool Update(Ssd1681::RefreshMode mode, DrawFn &&draw)
  {
    if (!panelMatchesCanvas_)
    {
      mode = Ssd1681::RefreshMode::Full;
    }
    // Assume the worst until this update completes.
    panelMatchesCanvas_ = false;

    if (!panel_.Wake())
    {
      // The reset may still have woken the controller, so try to put it
      // back to sleep rather than leave it drawing current.
      static_cast<void>(panel_.Sleep());
      return false;
    }

    bool ok = true;
    if (mode == Ssd1681::RefreshMode::Partial)
    {
      ok = panel_.WritePreviousImage(canvas_.Data());
    }

    if (ok)
    {
      draw(canvas_);
      ok = panel_.WriteImage(canvas_.Data());
    }

    if (ok && mode == Ssd1681::RefreshMode::Full)
    {
      // Keep the reference RAM consistent with what a full refresh shows.
      ok = panel_.WritePreviousImage(canvas_.Data());
    }

    ok = ok && panel_.Refresh(mode);
    const bool slept = panel_.Sleep();

    panelMatchesCanvas_ = ok;
    return ok && slept;
  }

private:
  Ssd1681 &panel_;
  MonoFramebuffer &canvas_;
  bool panelMatchesCanvas_ = false;
};
