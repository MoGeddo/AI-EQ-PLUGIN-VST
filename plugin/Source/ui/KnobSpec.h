#pragma once
// The dial's radial budget, as pure arithmetic.
//
// The knob is drawn as four concentric parts - the machined body, the recess it sits in,
// the value ring, and the scale - and every one of them has to fit inside the square the
// slider gives it.  In 1.2.53 the scale was placed at `body * 1.34` without checking that
// against the box, so on the shipped 1240x800 shot the tick marks (a) ran past the box they
// were given, into the label column of the dial next to them, and (b) crossed the value
// ring, which read as small nubs stuck on the ring rather than as a scale.
//
// A picture cannot be asserted from a unit test, but this arithmetic can: the numbers below
// are the whole geometry, `drawKnob()` multiplies them by the box, and test_dials.cpp checks
// the relations (order, air, and that nothing leaves the box) for every box size the panel
// uses.  Change one number here and the test tells you what it broke.
//
// Units: design pixels, for a dial drawn in a `box` x `box` square.
namespace aeq::ui {

struct KnobMetrics
{
    float body;      // the anodised body
    float seat;      // the recess the body sits in
    float groove;    // the value ring: the travel, and the lit part of it
    float tickIn;    // where a minor tick starts
    float tickOut;   // where every tick ends (the scale's outer edge)
    float majorIn;   // where a major tick starts (0 / 50 / 100 %)
    float majorStroke;
    float minorStroke;
    float grooveStroke;
};

// Every length here is a fraction of the box, strokes included.  A stroke that is a fixed
// number of pixels is a stroke that is 3.6 % of a 44 px dial and 2.6 % of a 62 px one, and
// the 1.6 px major tick was already wide enough to be clipped by the box on the smaller
// size - which is why the test walks a range of boxes and not just the one on screen.
constexpr KnobMetrics knobMetrics (float box) noexcept
{
    KnobMetrics m {};
    m.body         = box * 0.335f;
    m.seat         = m.body * 1.08f;
    m.groove       = box * 0.380f;
    m.majorIn      = box * 0.4185f;
    m.tickIn       = box * 0.435f;
    m.tickOut      = box * 0.478f;
    m.majorStroke  = box * 0.030f;
    m.minorStroke  = box * 0.0185f;
    m.grooveStroke = box * 0.033f;
    return m;
}

// The outer edge of everything that is painted, measured from the dial's centre.
constexpr float knobPaintRadius (float box) noexcept
{
    const auto m = knobMetrics (box);
    return m.tickOut + m.majorStroke * 0.5f;
}

} // namespace aeq::ui
