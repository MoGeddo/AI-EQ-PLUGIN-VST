#pragma once
// The dial's radial budget, as pure arithmetic.
//
// The knob is drawn from concentric parts - the machined body, the knurled skirt, the recess
// it sits in, the value ring, and (on the big dials) the scale - and every one of them has to
// fit inside the square the slider gives it.  In 1.2.53 the scale was placed at `body * 1.34`
// without checking that against the box, so on the shipped 1240x800 shot the tick marks (a)
// ran past the box they were given, into the label column of the dial next to them, and (b)
// crossed the value ring, which read as small nubs stuck on the ring rather than as a scale.
//
// 1.2.59 rebuilt the dial to the reference's anatomy (dribbble.com/AudioUi, VST kit 01 and
// kit 03): a knurled skirt under the cap, a *thick* coloured arc ring, and a scale **only on
// the large dials** - kit 03's small knobs have no ticks at all, because at 30-54 px a tick is
// a pixel and a half and reads as noise.  That rule is a number here (`kKnobTicksMinBox`), not
// a judgement made at each call site.
//
// A picture cannot be asserted from a unit test, but this arithmetic can: the numbers below
// are the whole geometry, `drawKnob()` multiplies them by the box, and test_dials.cpp checks
// the relations (order, air, containment, and the size at which ticks appear) for every box
// the panel uses.
//
// Units: design pixels, for a dial drawn in a `box` x `box` square.
namespace aeq::ui {

// Below this, a minor tick is shorter than 2 px and the scale stops being a scale.
constexpr float kKnobTicksMinBox = 66.0f;

struct KnobMetrics
{
    float body;      // the anodised cap
    float knurlIn;   // the knurled skirt: the ridges between the cap and its recess
    float knurlOut;
    int   knurlCount;
    float knurlStroke;
    float seat;      // the recess the body sits in
    float groove;    // the value ring: the travel, and the lit part of it
    float grooveStroke;
    float tickIn;    // where a minor tick starts
    float tickOut;   // where every tick ends (the scale's outer edge)
    float majorIn;   // where a major tick starts (0 / 50 / 100 %)
    float majorStroke;
    float minorStroke;
};

// Every length here is a fraction of the box, strokes included.  A stroke that is a fixed
// number of pixels is a stroke that is 3.6 % of a 44 px dial and 2.6 % of a 62 px one, and
// the 1.6 px major tick was already wide enough to be clipped by the box on the smaller
// size - which is why the test walks a range of boxes and not just the one on screen.
constexpr KnobMetrics knobMetrics (float box) noexcept
{
    KnobMetrics m {};
    m.body         = box * 0.312f;
    m.knurlIn      = m.body * 1.00f;
    m.knurlOut     = box * 0.354f;     // a 4 px band at 54 px: the ridges have to be *seen*
    m.knurlCount   = 44;
    m.knurlStroke  = box * 0.011f;
    m.seat         = box * 0.371f;
    m.groove       = box * 0.390f;
    m.grooveStroke = box * 0.048f;    // the reference's ring is thick - it is the value read
    m.majorIn      = box * 0.434f;
    m.tickIn       = box * 0.442f;
    m.tickOut      = box * 0.478f;
    m.majorStroke  = box * 0.028f;
    m.minorStroke  = box * 0.016f;
    return m;
}

// Is the scale drawn at this size?  The reference draws it on the large dials and not on the
// small ones, and the arithmetic agrees: at 54 px a minor tick is 1.7 px of hairline.
constexpr bool knobHasTicks (float box) noexcept { return box >= kKnobTicksMinBox; }

// The outer edge of everything that is painted, measured from the dial's centre.
constexpr float knobPaintRadius (float box) noexcept
{
    const auto m = knobMetrics (box);
    return knobHasTicks (box) ? m.tickOut + m.majorStroke * 0.5f
                              : m.groove + m.grooveStroke * 0.5f;
}

} // namespace aeq::ui
