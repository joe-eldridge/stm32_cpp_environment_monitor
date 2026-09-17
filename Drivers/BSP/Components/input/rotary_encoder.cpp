#include "rotary_encoder.hpp"

namespace
{

// One step per valid transition of the two-bit Gray code, indexed by the
// previous pair followed by the current one. A transition that changes both
// bits is impossible on a real rotation (it means samples were missed or the
// contacts bounced), and scores nothing.
constexpr std::int8_t kTransitions[16] = {
    0, -1, 1, 0,  //
    1, 0,  0, -1, //
    -1, 0, 0, 1,  //
    0, 1,  -1, 0, //
};

std::uint8_t Encode(bool clk, bool dt)
{
  return static_cast<std::uint8_t>((clk ? 2u : 0u) | (dt ? 1u : 0u));
}

} // namespace

RotaryEncoder::RotaryEncoder(std::uint8_t stepsPerDetent)
    : stepsPerDetent_(stepsPerDetent == 0 ? 1 : stepsPerDetent)
{
}

void RotaryEncoder::Reset(bool clk, bool dt)
{
  previous_ = Encode(clk, dt);
  started_ = true;
  steps_ = 0;
}

int RotaryEncoder::Update(bool clk, bool dt)
{
  const std::uint8_t current = Encode(clk, dt);
  if (!started_)
  {
    Reset(clk, dt);
    return 0;
  }
  if (current == previous_)
  {
    return 0;
  }

  const std::int8_t step = kTransitions[(previous_ << 2) | current];
  previous_ = current;
  if (step == 0)
  {
    return 0;
  }

  steps_ = static_cast<std::int16_t>(steps_ + step);

  // Whole detents only. A part-turn stays in the accumulator, so a rotation
  // that pauses between detents is picked up when it continues. One call
  // moves one step, so it can complete at most one detent.
  if (steps_ >= stepsPerDetent_)
  {
    steps_ = 0;
    return 1;
  }
  if (steps_ <= -stepsPerDetent_)
  {
    steps_ = 0;
    return -1;
  }
  return 0;
}
