#include "mono_framebuffer.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

MonoFramebuffer::MonoFramebuffer(std::uint8_t *buffer, std::uint16_t width, std::uint16_t height)
    : buffer_(buffer), width_(width), height_(height), bytesPerRow_(static_cast<std::uint16_t>((width + 7u) / 8u))
{
}

void MonoFramebuffer::Fill(Color color)
{
  std::memset(buffer_, color == Color::White ? 0xFF : 0x00, SizeBytes());
}

void MonoFramebuffer::SetPixel(int x, int y, Color color)
{
  if (!Contains(x, y))
  {
    return;
  }
  std::uint8_t &byte = buffer_[static_cast<std::size_t>(y) * bytesPerRow_ + static_cast<std::size_t>(x / 8)];
  const auto mask = static_cast<std::uint8_t>(0x80u >> (x % 8));
  if (color == Color::White)
  {
    byte = static_cast<std::uint8_t>(byte | mask);
  }
  else
  {
    byte = static_cast<std::uint8_t>(byte & ~mask);
  }
}

Color MonoFramebuffer::GetPixel(int x, int y) const
{
  if (!Contains(x, y))
  {
    return Color::White;
  }
  const std::uint8_t byte = buffer_[static_cast<std::size_t>(y) * bytesPerRow_ + static_cast<std::size_t>(x / 8)];
  return (byte & (0x80u >> (x % 8))) != 0 ? Color::White : Color::Black;
}

void MonoFramebuffer::HorizontalLine(int x, int y, int length, Color color)
{
  FillRect(x, y, length, 1, color);
}

void MonoFramebuffer::VerticalLine(int x, int y, int length, Color color)
{
  FillRect(x, y, 1, length, color);
}

void MonoFramebuffer::Line(int x0, int y0, int x1, int y1, Color color)
{
  // Bresenham, integer-only, in all octants.
  const int dx = std::abs(x1 - x0);
  const int dy = -std::abs(y1 - y0);
  const int stepX = x0 < x1 ? 1 : -1;
  const int stepY = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  for (;;)
  {
    SetPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1)
    {
      return;
    }
    const int doubled = 2 * error;
    if (doubled >= dy)
    {
      error += dy;
      x0 += stepX;
    }
    if (doubled <= dx)
    {
      error += dx;
      y0 += stepY;
    }
  }
}

void MonoFramebuffer::Rect(int x, int y, int width, int height, Color color)
{
  if (width <= 0 || height <= 0)
  {
    return;
  }
  HorizontalLine(x, y, width, color);
  HorizontalLine(x, y + height - 1, width, color);
  VerticalLine(x, y, height, color);
  VerticalLine(x + width - 1, y, height, color);
}

void MonoFramebuffer::FillRect(int x, int y, int width, int height, Color color)
{
  // Clip once up front, so the loops only touch visible pixels.
  const int left = std::max(x, 0);
  const int top = std::max(y, 0);
  const int right = std::min(x + width, static_cast<int>(width_));
  const int bottom = std::min(y + height, static_cast<int>(height_));

  for (int row = top; row < bottom; ++row)
  {
    for (int column = left; column < right; ++column)
    {
      SetPixel(column, row, color);
    }
  }
}
