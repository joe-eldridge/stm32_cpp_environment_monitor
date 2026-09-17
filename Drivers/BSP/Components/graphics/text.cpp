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
      for (int column = 0; column < font.width; ++column)
      {
        for (int row = 0; row < font.height; ++row)
        {
          if ((glyph[column] >> row) & 1u)
          {
            canvas.FillRect(x + column * scale, y + row * scale, scale, scale, color);
          }
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
