#pragma once

#include <cmath>

// ---------------------------------------------------------------------------
// MotionSpec - the only place in this plugin where a duration or an easing curve
// is allowed to exist.
//
// Two things this file is not: it is not a theme, and it is not a helper library.
// It is the *specification* of the three motions the product has, written down
// before the code that plays them, because the one thing this panel animates is
// the moment the plugin commits to a take - and a moment that is animated by feel
// is a moment that will be animated differently in 1.3.1.
//
// The token scale is the one from transitions.dev (recorded in
// docs/21_SKILLS_APPLIED_AR.md, section 6), so a reviewer can check a number
// against an outside table instead of against my taste.  The two rules on top of
// it come from the craft floor:
//
//   * motion is opacity and transform only - never a layout property, never a
//     blur on a control that is dragged, because those are the ones that stutter
//     inside a DAW's already-busy message thread;
//   * bounce is banned from product motion (make-interfaces-feel-better, rule 7:
//     "bounce must always be 0").  The token exists below so the ban can be
//     checked mechanically: nothing in ProductMotion may reference it.
//
// Nothing here includes JUCE, so it can be unit-tested on its own and parsed by
// the gate (design/preview/check_motion.py) without a build.
// ---------------------------------------------------------------------------

namespace aeq::ui::motion
{

// ---- durations, in milliseconds -------------------------------------------------
// Named for the *job*, not the length: the number is the consequence.
inline constexpr int stagger     = 40;   // per-item offset when several things appear
inline constexpr int micro       = 80;   // a delay that filters noise (intent gate)
inline constexpr int quick       = 150;  // a state swap in place: light on, word change
inline constexpr int fast        = 250;  // a click that moves something: clamp drop
inline constexpr int medium      = 350;  // a surface leaving
inline constexpr int slow        = 400;  // a surface arriving
inline constexpr int verySlow    = 500;  // one-off emphasis (the seal badge)

// ---- easings as cubic-bezier control points --------------------------------------
// x1, y1, x2, y2 - identical to the CSS token they are named after, so the panel
// preview (design/preview/ui_v7.py) and the plugin can be compared value for value.
struct Bezier
{
    double x1, y1, x2, y2;

    // Solve y for a given x (what a CSS timing function does), by bisection on the
    // parameter.  Eight steps is under a microsecond and beyond the eye's ability to
    // resolve on a 60 Hz repaint, so there is no reason to be cleverer here.
    double at (double x) const noexcept
    {
        if (x <= 0.0) return 0.0;      // the endpoints are exact, not approached:
        if (x >= 1.0) return 1.0;      // a bisection that "converges to" 0 returns
                                       // 0.0007, and a property that starts at
                                       // 0.0007 is a property that flickers
        auto cx = [this] (double t) { return 3.0 * (1.0 - t) * (1.0 - t) * t * x1
                                           + 3.0 * (1.0 - t) * t * t * x2 + t * t * t; };
        auto cy = [this] (double t) { return 3.0 * (1.0 - t) * (1.0 - t) * t * y1
                                           + 3.0 * (1.0 - t) * t * t * y2 + t * t * t; };
        double lo = 0.0, hi = 1.0, t = x;
        for (int i = 0; i < 24; ++i)
        {
            t = 0.5 * (lo + hi);
            if (cx (t) < x) lo = t; else hi = t;
            if (hi - lo < 1e-9) break;
        }
        return cy (0.5 * (lo + hi));
    }
};

inline constexpr Bezier smoothOut   { 0.22, 1.00, 0.36, 1.00 };  // the default for anything that moves
inline constexpr Bezier inOut       { 0.42, 0.00, 0.58, 1.00 };  // swaps in place
inline constexpr Bezier out         { 0.00, 0.00, 0.58, 1.00 };  // a thing leaving
inline constexpr Bezier linear      { 0.00, 0.00, 1.00, 1.00 };  // a meter filling
inline constexpr Bezier bounce      { 0.34, 1.36, 0.64, 1.00 };  // banned for product motion
inline constexpr Bezier bounceStrong{ 0.34, 3.85, 0.64, 1.00 };  // banned for product motion

// ---- distances, in logical pixels --------------------------------------------------
inline constexpr float distMicro   = 4.0f;    // a word changing in place
inline constexpr float distBase    = 8.0f;    // a part settling into its seat
inline constexpr float distMedium  = 12.0f;   // text revealing
inline constexpr float distLarge   = 30.0f;   // a one-off badge sweep

// ---- the three motions the product has, and nothing else ---------------------------
// The product is: listen, stop, commit.  Everything visual that moves is one of
// these three, and each names its token so it cannot drift.
struct Motion
{
    const char* name;
    int durationMs;
    Bezier ease;
    float distance;
};

namespace seal
{
    // The clamp landing on the take bar.  This is the click that commits, so it is
    // the one motion allowed to take its time: 250 ms, smooth-out, and it settles
    // 8 px (a part seating, not a thing flying in).
    inline constexpr Motion clampDrop { "clamp-drop", motion::fast, smoothOut, distBase };
    // The "مختوم" word appearing where the clamp landed: a swap in place.
    inline constexpr Motion wordSwap  { "seal-word", motion::quick, inOut, distMicro };
    // The LED.  Colour only - no scale, no glow animation, because it sits next to
    // the audio meter and anything that pulses next to a meter reads as audio.
    inline constexpr Motion ledOn     { "seal-led", motion::quick, linear, 0.0f };
}

// ---- what is deliberately NOT animated ---------------------------------------------
//  * a knob drag: it tracks the pointer 1:1.  Animating a control the hand is on
//    makes it feel like it is lagging, and inside a DAW that reads as CPU trouble.
//  * the spectrum and the curve: they are data.  They move because the audio moved.
//  * the window: no open/close animation.  A plugin window that animates costs
//    seconds of a producer's day, every time, forever.
inline constexpr bool animateWindow      = false;
inline constexpr bool animateKnobDrag    = false;
inline constexpr bool animateMeters      = false;

// ---- and the ban, stated so it can be checked --------------------------------------
inline constexpr bool bounceAllowedInProductMotion = false;

} // namespace aeq::ui::motion
