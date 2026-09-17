#pragma once

#include "font.hpp"
#include "mono_framebuffer.hpp"

// Draws `text` with its top-left corner at (x, y). Each font pixel becomes a
// scale x scale block. Only glyph pixels are drawn, so the background shows
// through. Characters the font doesn't contain are drawn as a hollow box.
// Returns the x coordinate just past the last character.
int DrawText(MonoFramebuffer &canvas, const Font &font, int x, int y, const char *text, Color color, int scale = 1);

// Width DrawText() would advance for `text`. Glyph cells include their
// spacing column, so this is (characters x width x scale).
int TextWidth(const Font &font, const char *text, int scale = 1);
