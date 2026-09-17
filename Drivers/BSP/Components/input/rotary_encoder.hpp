#pragma once

#include <cstdint>

// Quadrature decoder for a mechanical rotary encoder (KY-040 and similar).
//
// Given the current levels of the two switch outputs, it reports movement in
// detents - the clicks the user feels - rather than in raw edges. It holds no
// pins and reads no hardware: the caller samples the lines, whether from an
// interrupt or a poll, which keeps the decoding testable on a host.
//
// Contact bounce shows up as transitions that change both lines at once,
// which cannot happen on a real rotation. Those are ignored, and a bouncing
// contact that rattles back and forth cancels itself out, since the partial
// steps either side of a detent sum to zero.
class RotaryEncoder
{
public:
  // How many quadrature steps make up one detent. Four is usual for a
  // KY-040, but the part's own datasheet quotes both 20 pulses and 30
  // positions per revolution, so this is measured on the bench, not assumed.
  explicit RotaryEncoder(std::uint8_t stepsPerDetent = 4);

  // Feeds the current levels of the two outputs. Returns -1, 0 or +1: one
  // call sees at most one step, so it can complete at most one detent
  // (positive is clockwise). Sampling too slowly to see every step loses
  // movement rather than misreporting it, since a jump of two steps changes
  // both lines at once and is discarded - so the caller samples on an
  // interrupt, or often enough to keep up with a hand turning the knob.
  int Update(bool clk, bool dt);

  // Takes the current levels as the resting position, discarding any partial
  // movement. Call this when starting to watch the encoder, so the levels it
  // happens to be sitting at aren't read as a step.
  void Reset(bool clk, bool dt);

private:
  std::uint8_t stepsPerDetent_;
  std::uint8_t previous_ = 0;
  bool started_ = false;
  std::int16_t steps_ = 0; // quadrature steps since the last detent
};
