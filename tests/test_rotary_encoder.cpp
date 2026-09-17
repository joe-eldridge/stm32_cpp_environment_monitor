#include <gtest/gtest.h>

#include <vector>

#include "rotary_encoder.hpp"

namespace
{

// The Gray code an encoder walks through, one entry per quadrature step, in
// the clockwise direction. Detents are at 3 (both lines high, the resting
// position of a KY-040).
struct Levels
{
  bool clk;
  bool dt;
};
constexpr Levels kClockwise[4] = {{true, true}, {false, true}, {false, false}, {true, false}};

Levels StepAt(int step)
{
  const int index = ((step % 4) + 4) % 4;
  return kClockwise[index];
}

// Turns the encoder through `steps` quadrature steps (negative for
// anticlockwise) from `startStep`, returning the detents reported.
int Turn(RotaryEncoder &encoder, int startStep, int steps)
{
  int detents = 0;
  const int direction = steps >= 0 ? 1 : -1;
  for (int i = 1; i <= (steps >= 0 ? steps : -steps); ++i)
  {
    const Levels levels = StepAt(startStep + i * direction);
    detents += encoder.Update(levels.clk, levels.dt);
  }
  return detents;
}

} // namespace

TEST(RotaryEncoder, FirstSampleIsTakenAsTheRestingPosition)
{
  RotaryEncoder encoder;
  // Whatever levels the knob happens to be resting at, the first sample is
  // the starting point rather than a step away from some assumed one.
  EXPECT_EQ(encoder.Update(true, false), 0);
  EXPECT_EQ(encoder.Update(true, false), 0);
  // So a full detent from there still takes four steps, not three.
  EXPECT_EQ(Turn(encoder, 3, 3), 0);
  EXPECT_EQ(Turn(encoder, 6, 1), 1);
}

TEST(RotaryEncoder, OneDetentClockwise)
{
  RotaryEncoder encoder;
  encoder.Reset(true, true);
  EXPECT_EQ(Turn(encoder, 0, 4), 1);
}

TEST(RotaryEncoder, OneDetentAnticlockwise)
{
  RotaryEncoder encoder;
  encoder.Reset(true, true);
  EXPECT_EQ(Turn(encoder, 0, -4), -1);
}

TEST(RotaryEncoder, PartOfADetentReportsNothingYet)
{
  RotaryEncoder encoder;
  encoder.Reset(true, true);
  EXPECT_EQ(Turn(encoder, 0, 3), 0);
  // The remaining step completes it.
  EXPECT_EQ(Turn(encoder, 3, 1), 1);
}

TEST(RotaryEncoder, SeveralDetentsInOneDirection)
{
  RotaryEncoder encoder;
  encoder.Reset(true, true);
  int detents = 0;
  for (int turn = 0; turn < 5; ++turn)
  {
    detents += Turn(encoder, turn * 4, 4);
  }
  EXPECT_EQ(detents, 5);
}

TEST(RotaryEncoder, ReversingBeforeADetentCancelsOut)
{
  RotaryEncoder encoder;
  encoder.Reset(true, true);
  EXPECT_EQ(Turn(encoder, 0, 2), 0);
  EXPECT_EQ(Turn(encoder, 2, -2), 0);
  // And the next full detent still reports exactly one.
  EXPECT_EQ(Turn(encoder, 0, 4), 1);
}

TEST(RotaryEncoder, ContactBounceOnOneLineIsIgnored)
{
  RotaryEncoder encoder;
  encoder.Reset(true, true);
  // The first line rattles while the second stays put: the steps either side
  // of the bounce cancel, so no detent appears from standing still.
  int detents = 0;
  for (int i = 0; i < 6; ++i)
  {
    detents += encoder.Update(i % 2 == 0, true);
  }
  EXPECT_EQ(detents, 0);
}

TEST(RotaryEncoder, BothLinesChangingAtOnceIsDiscarded)
{
  RotaryEncoder encoder;
  encoder.Reset(true, true);
  // Impossible on a real rotation - it means samples were missed. Scoring it
  // as a step would leave the accumulator a step out of phase, so the next
  // three steps would finish a detent early.
  EXPECT_EQ(encoder.Update(false, false), 0);
  EXPECT_EQ(Turn(encoder, 2, 3), 0);
  EXPECT_EQ(Turn(encoder, 5, 1), 1);
}

TEST(RotaryEncoder, SamplingTooSlowlyLosesMovementRatherThanInventingIt)
{
  RotaryEncoder encoder;
  encoder.Reset(true, true);
  // Every other step is missed, so each transition changes both lines and is
  // discarded: nothing is reported, rather than a wrong direction.
  int detents = 0;
  for (int step = 2; step <= 8; step += 2)
  {
    const Levels levels = StepAt(step);
    detents += encoder.Update(levels.clk, levels.dt);
  }
  EXPECT_EQ(detents, 0);
}

TEST(RotaryEncoder, ResetDiscardsPartialMovement)
{
  RotaryEncoder encoder;
  encoder.Reset(true, true);
  EXPECT_EQ(Turn(encoder, 0, 3), 0);

  const Levels here = StepAt(3);
  encoder.Reset(here.clk, here.dt);
  // The step that would have completed the detent no longer does.
  EXPECT_EQ(Turn(encoder, 3, 1), 0);
}

TEST(RotaryEncoder, StepsPerDetentIsConfigurable)
{
  RotaryEncoder twoStep(2);
  twoStep.Reset(true, true);
  EXPECT_EQ(Turn(twoStep, 0, 2), 1);
  EXPECT_EQ(Turn(twoStep, 2, 2), 1);

  // A part that reports every step still works, and zero is treated as one
  // rather than dividing by it.
  RotaryEncoder everyStep(0);
  everyStep.Reset(true, true);
  EXPECT_EQ(Turn(everyStep, 0, 1), 1);
}

TEST(RotaryEncoder, LeftoverStepsCarryIntoTheNextDetent)
{
  RotaryEncoder encoder;
  encoder.Reset(true, true);
  // Five steps is one detent with one step over; three more complete the
  // second detent, which needs that leftover step to have been kept.
  EXPECT_EQ(Turn(encoder, 0, 5), 1);
  EXPECT_EQ(Turn(encoder, 5, 2), 0);
  EXPECT_EQ(Turn(encoder, 7, 1), 1);
}

TEST(RotaryEncoder, BothLinesChangingAtOnceIsDiscardedInEitherDirection)
{
  RotaryEncoder encoder;
  // From the other resting position, so the discarded transition is a
  // different entry in the decoding table.
  encoder.Reset(false, false);
  EXPECT_EQ(encoder.Update(true, true), 0);
  EXPECT_EQ(Turn(encoder, 0, 3), 0);
  EXPECT_EQ(Turn(encoder, 3, 1), 1);
}
