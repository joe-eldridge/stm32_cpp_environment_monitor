#include "menu_screen.hpp"

#include "font_5x7.hpp"
#include "text.hpp"
#include "text_format.hpp"

namespace
{

// Layout (200 x 200), sharing the other screens' rules and margins.
constexpr int kScale = 2;
constexpr int kCharWidth = 5 * kScale;
constexpr int kCharHeight = 7 * kScale;
constexpr int kMargin = 4;
constexpr int kTopTextY = 6;
constexpr int kTopRuleY = 24;
constexpr int kRuleThickness = 2;
constexpr int kFirstItemY = 38;
constexpr int kItemPitch = 26;
constexpr int kItemTextInset = 6;
constexpr int kBottomRuleY = 156;
constexpr int kBottomTextY = 166;
constexpr int kTimeY = 70;
constexpr int kUnderlineGap = 4;
constexpr int kConfirmMessageY = 50;
constexpr int kConfirmItemsY = 100;
constexpr int kMessageY = 80;

void DrawCentred(MonoFramebuffer &canvas, int y, const char *text, Color color = Color::Black)
{
  const int x = (canvas.Width() - TextWidth(kFont5x7, text, kScale)) / 2;
  DrawText(canvas, kFont5x7, x, y, text, color, kScale);
}

// One list row. The selected row is a black bar with the text knocked out of
// it, which reads clearly on e-paper without needing a second font.
void DrawItem(MonoFramebuffer &canvas, int x, int y, int width, const char *text, bool selected)
{
  if (selected)
  {
    canvas.FillRect(x, y - 4, width, kCharHeight + 8, Color::Black);
  }
  DrawText(canvas, kFont5x7, x + kItemTextInset, y, text, selected ? Color::White : Color::Black, kScale);
}

void DrawFrame(MonoFramebuffer &canvas, const MenuView &view, const char *hint)
{
  canvas.Fill(Color::White);
  DrawText(canvas, kFont5x7, kMargin, kTopTextY, view.title, Color::Black, kScale);
  canvas.FillRect(0, kTopRuleY, canvas.Width(), kRuleThickness, Color::Black);
  canvas.FillRect(0, kBottomRuleY, canvas.Width(), kRuleThickness, Color::Black);
  DrawText(canvas, kFont5x7, kMargin, kBottomTextY, hint, Color::Black, kScale);
}

} // namespace

void FormatEditableTime(char *out, std::size_t size, const Ds3231::DateTime &time, std::uint8_t field,
                        std::size_t &fieldFirst, std::size_t &fieldLast)
{
  // "2026-09-17 14:35": the fields sit at fixed positions, so marking one is
  // a matter of counting characters rather than measuring text.
  static constexpr std::size_t kFieldStart[] = {0, 5, 8, 11, 14};
  static constexpr std::size_t kFieldLength[] = {4, 2, 2, 2, 2};

  char digits[3];
  char year[6];
  text_format::TwoDigits(digits, static_cast<std::uint8_t>(time.year / 100));
  text_format::Copy(year, sizeof(year), digits);
  text_format::TwoDigits(digits, static_cast<std::uint8_t>(time.year % 100));
  text_format::Append(year, sizeof(year), digits);

  text_format::Copy(out, size, year);
  text_format::Append(out, size, "-");
  text_format::TwoDigits(digits, time.month);
  text_format::Append(out, size, digits);
  text_format::Append(out, size, "-");
  text_format::TwoDigits(digits, time.date);
  text_format::Append(out, size, digits);
  text_format::Append(out, size, " ");
  text_format::TwoDigits(digits, time.hour);
  text_format::Append(out, size, digits);
  text_format::Append(out, size, ":");
  text_format::TwoDigits(digits, time.minute);
  text_format::Append(out, size, digits);

  const std::size_t index = field < static_cast<std::uint8_t>(Menu::TimeField::Count) ? field : 0u;
  fieldFirst = kFieldStart[index];
  fieldLast = kFieldStart[index] + kFieldLength[index] - 1;
}

void DrawMenuScreen(MonoFramebuffer &canvas, const MenuView &view)
{
  switch (view.mode)
  {
  case MenuView::Mode::List:
  {
    DrawFrame(canvas, view, "Hold to close");
    for (std::size_t i = 0; i < view.itemCount; ++i)
    {
      const int y = kFirstItemY + static_cast<int>(i) * kItemPitch;
      DrawItem(canvas, kMargin, y, canvas.Width() - 2 * kMargin, view.items[i], i == view.selected);
    }
    break;
  }

  case MenuView::Mode::TimeEdit:
  {
    DrawFrame(canvas, view, "Click for next");
    char text[20];
    std::size_t first = 0;
    std::size_t last = 0;
    FormatEditableTime(text, sizeof(text), view.time, view.field, first, last);

    const int x = (canvas.Width() - TextWidth(kFont5x7, text, kScale)) / 2;
    DrawText(canvas, kFont5x7, x, kTimeY, text, Color::Black, kScale);
    // The field being changed is underlined rather than inverted: an
    // underline doesn't move the digits about as they change width.
    canvas.FillRect(x + static_cast<int>(first) * kCharWidth, kTimeY + kCharHeight + kUnderlineGap,
                    static_cast<int>(last - first + 1) * kCharWidth - kScale, kRuleThickness, Color::Black);
    break;
  }

  case MenuView::Mode::Confirm:
  {
    DrawFrame(canvas, view, "Hold to cancel");
    DrawCentred(canvas, kConfirmMessageY, view.message);
    // No on the left, Yes on the right, each in its own half.
    const int half = canvas.Width() / 2;
    for (std::size_t i = 0; i < view.itemCount; ++i)
    {
      const int x = kMargin + static_cast<int>(i) * half;
      const int width = half - 2 * kMargin;
      const bool selected = i == view.selected;
      if (selected)
      {
        canvas.FillRect(x, kConfirmItemsY - 4, width, kCharHeight + 8, Color::Black);
      }
      const int textX = x + (width - TextWidth(kFont5x7, view.items[i], kScale)) / 2;
      DrawText(canvas, kFont5x7, textX, kConfirmItemsY, view.items[i], selected ? Color::White : Color::Black,
               kScale);
    }
    break;
  }

  case MenuView::Mode::Message:
    DrawFrame(canvas, view, "Click to go back");
    DrawCentred(canvas, kMessageY, view.message);
    break;
  }
}
