#include "text.hpp"

#include <cstring>

namespace
{

void DrawMissingGlyph(MonoFramebuffer &canvas, const Font &font, int x, int y, Color color, int scale)
{
  // Leave the last column blank as the spacing, like real glyphs.
  canvas.Rect(x, y, (font.width - 1) * scale, font.height * scale, color);
}

} // namespace

int DrawText(MonoFramebuffer &canvas, const Font &font, int x, int y, const char *text, Color color, int scale)
{
  for (const char *c = text; *c != '\0'; ++c)
  {
    const std::uint8_t *glyph = font.Glyph(*c);
    if (glyph == nullptr)
    {
      DrawMissingGlyph(canvas, font, x, y, color, scale);
    }
    else
    {
      // Each column of the glyph is drawn as runs of lit pixels, one
      // rectangle per run rather than one per pixel - most strokes in a
      // bitmap font are vertical runs, so this is several times fewer calls.
      for (int column = 0; column < font.width; ++column)
      {
        const unsigned bits = glyph[column];
        int row = 0;
        while (row < font.height)
        {
          if (((bits >> row) & 1u) == 0)
          {
            ++row;
            continue;
          }
          const int runStart = row;
          while (row < font.height && ((bits >> row) & 1u) != 0)
          {
            ++row;
          }
          canvas.FillRect(x + column * scale, y + runStart * scale, scale, (row - runStart) * scale, color);
        }
      }
    }
    x += font.width * scale;
  }
  return x;
}

int TextWidth(const Font &font, const char *text, int scale)
{
  return static_cast<int>(std::strlen(text)) * font.width * scale;
}
