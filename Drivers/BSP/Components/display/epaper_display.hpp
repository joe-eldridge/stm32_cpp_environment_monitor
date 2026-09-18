#pragma once

#include <cstring>

#include "mono_framebuffer.hpp"
#include "ssd1681.hpp"

// Owns the update cycle for an SSD1681 panel and the framebuffer drawn on it.
//
// It keeps a second copy of the image: what the panel is currently showing.
// That costs 5 KB of the 20 KB of RAM and buys two things. A partial refresh
// needs the previous image as its reference, which is now to hand whatever
// order the drawing happens in; and the two images can be compared, so only
// the rows that actually changed are sent and driven. At this bus speed
// sending a whole image takes longer than the panel takes to show it, so on
// a screen where one line changes that is most of the cost gone. An update
// that changes nothing at all does nothing.
//
// The panel is in deep sleep between updates, and its RAM isn't trusted
// across that: whatever is being shown is re-sent every time.
class EpaperDisplay
{
public:
  // `panelImage` must hold Ssd1681::kImageBytes and outlive this object.
  EpaperDisplay(Ssd1681 &panel, MonoFramebuffer &canvas, std::uint8_t *panelImage)
      : panel_(panel), canvas_(canvas), panelImage_(panelImage)
  {
  }

  // Lets `draw(MonoFramebuffer&)` change the canvas, then shows the result:
  // wakes the panel, sends the rows that changed, refreshes, and puts the
  // panel back into deep sleep.
  //
  // A partial refresh is upgraded to full when the panel's contents aren't
  // known to match the stored image: the first update after boot, or after
  // any failed update.
  template <typename DrawFn>
  bool Update(Ssd1681::RefreshMode mode, DrawFn &&draw)
  {
    if (!panelMatchesImage_)
    {
      mode = Ssd1681::RefreshMode::Full;
    }

    draw(canvas_);

    Ssd1681::RowRange rows = Ssd1681::RowRange::All();
    if (mode == Ssd1681::RefreshMode::Partial && !ChangedRows(rows))
    {
      return true; // nothing to show: no wake, no waveform, no current drawn
    }

    // Assume the worst until this update completes.
    panelMatchesImage_ = false;

    if (!panel_.Wake())
    {
      // The reset may still have woken the controller, so try to put it
      // back to sleep rather than leave it drawing current.
      static_cast<void>(panel_.Sleep());
      return false;
    }

    bool ok = false;
    if (mode == Ssd1681::RefreshMode::Partial)
    {
      // The reference is what the panel shows now; the new image follows.
      ok = panel_.WritePreviousImage(panelImage_, rows) && panel_.WriteImage(canvas_.Data(), rows);
    }
    else
    {
      // A full refresh drives every pixel to the new image, so that is what
      // both RAMs must hold: the reference has to match what ends up shown.
      ok = panel_.WriteImage(canvas_.Data(), rows) && panel_.WritePreviousImage(canvas_.Data(), rows);
    }

    ok = ok && panel_.Refresh(mode);
    const bool slept = panel_.Sleep();

    if (ok)
    {
      std::memcpy(panelImage_, canvas_.Data(), canvas_.SizeBytes());
    }
    panelMatchesImage_ = ok;
    return ok && slept;
  }

private:
  // The band of rows in which the canvas differs from what the panel shows.
  // False if they are identical. Rows between two changed ones are included
  // whether they changed or not: the panel takes one contiguous window, and
  // sending a few unchanged rows costs less than a second refresh.
  bool ChangedRows(Ssd1681::RowRange &rows) const
  {
    const std::size_t bytesPerRow = Ssd1681::kBytesPerRow;
    const std::uint8_t *canvas = canvas_.Data();
    int first = -1;
    int last = -1;
    for (std::uint16_t row = 0; row < Ssd1681::kHeight; ++row)
    {
      const std::size_t offset = row * bytesPerRow;
      if (std::memcmp(canvas + offset, panelImage_ + offset, bytesPerRow) != 0)
      {
        first = first < 0 ? row : first;
        last = row;
      }
    }
    if (first < 0)
    {
      return false;
    }
    rows.first = static_cast<std::uint16_t>(first);
    rows.last = static_cast<std::uint16_t>(last);
    return true;
  }

  Ssd1681 &panel_;
  MonoFramebuffer &canvas_;
  std::uint8_t *panelImage_;
  bool panelMatchesImage_ = false;
};
