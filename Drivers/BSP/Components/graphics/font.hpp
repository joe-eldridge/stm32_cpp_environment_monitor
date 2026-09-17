#pragma once

#include <cstdint>

// A fixed-width bitmap font stored in flash. Glyphs are `width` bytes each,
// one per column, left to right; bit 0 of each byte is the top row.
struct Font
{
  std::uint8_t width;
  std::uint8_t height;
  char first;
  char last;
  const std::uint8_t *glyphs;

  // nullptr for characters the font doesn't contain.
  const std::uint8_t *Glyph(char c) const
  {
    if (c < first || c > last)
    {
      return nullptr;
    }
    return glyphs + static_cast<unsigned>(c - first) * width;
  }
};
