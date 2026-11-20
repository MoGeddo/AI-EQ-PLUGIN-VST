#pragma once
// The control shapes, as arithmetic - the parts taken from the reference set
// (dribbble.com/AudioUi) that 1.2.58-1.2.63 had not taken yet: the **rocker switch** of VST
// kit 01 (a red lamp beside a slanted paddle), the **round push button** of kit 03, the
// **orange pill** that marks the chosen side of a two-way switch, and the **card header
// rule** of kit 03's Grain / Transients / Time / Effects cards.
//
// Same discipline as KnobSpec.h: a picture cannot be asserted from a unit test, a relation
// between numbers can.  Everything here is a fraction of the box the caller passes, so the
// test can walk several sizes, and check that a drawn part is inside its own seat, that a
// paddle's travel is symmetrical, and that a rule clears the type above and below it.
namespace aeq::ui {

struct RockerMetrics
{
    float plate;      // the machined plate the rocker sits in
    float pad;        // air between the plate's edge and its parts
    float paddleH;    // the paddle's height - a part of its own, not "the plate minus the lamp"
    float tilt;       // how far the paddle travels, as a fraction of the box
    float lampD;      // the lamp's diameter
    float lampGap;    // air between the paddle and the lamp
};

// 1.2.66.  The first version derived the paddle's height as `plate - 2 * pad - lampD -
// lampGap`, and at a 34 px header key that is 7.5 px: the switch shipped as a small "=" in a
// box.  Every part is now its own fraction of the box, and the plate is 86 % of it - the box
// a caller passes here is the *target*, and the part fills it.
constexpr RockerMetrics rockerMetrics (float box) noexcept
{
    RockerMetrics m {};
    m.plate   = box * 0.86f;
    m.pad     = box * 0.055f;
    m.paddleH = box * 0.40f;
    m.tilt    = box * 0.045f;
    m.lampD   = box * 0.095f;
    m.lampGap = box * 0.055f;
    return m;
}

// The paddle is the plate less the padding; `tilt` is how far it travels.  On and off are the
// same distance in opposite directions: a switch that leans further one way than the other is
// a switch that lies about its state.
constexpr float rockerTiltFor (float box, bool on) noexcept
{
    const auto m = rockerMetrics (box);
    return on ? m.tilt : -m.tilt;
}

// The whole control, lamp included, measured from the plate's centre: the caller uses this
// to keep the group inside the header and off the panel's corner.
constexpr float rockerExtent (float box) noexcept
{
    const auto m = rockerMetrics (box);
    return m.plate * 0.5f + m.pad;
}

struct CardMetrics
{
    float padX;        // left/right padding of the card's content
    float headerH;     // the title row's height
    float ruleInset;   // how far the header rule stops from the card's edge
    float ruleGap;     // air between the rule and the first line of content below it
};

constexpr CardMetrics cardMetrics (float box) noexcept
{
    CardMetrics c {};
    c.padX      = box * 0.028f;      // >= 6.7 px at the narrowest chip
    c.headerH   = box * 0.062f;      // >= 17 px at every chip width: 15 px type + air
    c.ruleInset = box * 0.030f;      // stops 9 px inside a 300 px card, never on its border
    c.ruleGap   = box * 0.014f;
    return c;
}

// The orange pill that marks the chosen side of a two-way switch, as used on kit 03's
// "Last".  It is inset inside the key, so the key's border and the pill never merge into one
// outline - which is what the 1.2.53 shot showed.
struct PillMetrics
{
    float insetX;
    float insetY;
    float radius;
};

constexpr PillMetrics pillMetrics (float keyW, float keyH) noexcept
{
    PillMetrics p {};
    p.insetX = keyW * 0.055f;
    p.insetY = keyH * 0.115f;
    p.radius = (keyH - 2.0f * p.insetY) * 0.5f;
    return p;
}

} // namespace aeq::ui
