#pragma once

#include <cstddef>
#include <cstdint>

enum class Color : std::uint8_t
{
  Black,
  White,
};

// A 1-bit-per-pixel drawing surface over caller-owned memory (no heap).
//
// Layout matches the SSD1681's RAM, so the buffer can be sent to the display
// as-is: rows top to bottom, each row padded to whole bytes, most significant
// bit leftmost, and 1 = white, 0 = black.
//
// Coordinates are signed so shapes can extend past an edge; anything outside
// the surface is clipped.
class MonoFramebuffer
{
public:
  static constexpr std::size_t BufferSize(std::uint16_t width, std::uint16_t height)
  {
    return ((width + 7u) / 8u) * height;
  }

  // `buffer` must hold BufferSize(width, height) bytes and outlive this object.
  MonoFramebuffer(std::uint8_t *buffer, std::uint16_t width, std::uint16_t height);

  std::uint16_t Width() const
  {
    return width_;
  }
  std::uint16_t Height() const
  {
    return height_;
  }
  const std::uint8_t *Data() const
  {
    return buffer_;
  }
  std::size_t SizeBytes() const
  {
    return BufferSize(width_, height_);
  }

  void Fill(Color color);
  void SetPixel(int x, int y, Color color);
  // Pixels outside the surface read as white (the blank-paper colour).
  Color GetPixel(int x, int y) const;

  void HorizontalLine(int x, int y, int length, Color color);
  void VerticalLine(int x, int y, int length, Color color);
  // Any direction; both end points are drawn.
  void Line(int x0, int y0, int x1, int y1, Color color);
  // Outline only.
  void Rect(int x, int y, int width, int height, Color color);
  void FillRect(int x, int y, int width, int height, Color color);

private:
  bool Contains(int x, int y) const
  {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
  }

  std::uint8_t *buffer_;
  std::uint16_t width_;
  std::uint16_t height_;
  std::uint16_t bytesPerRow_;
};
