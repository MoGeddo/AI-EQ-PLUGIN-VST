// test_dials.cpp - the dial's geometry, as arithmetic.
//
// The knob on this panel is drawn from concentric parts and every one of them has to stay
// inside the square the slider gives it.  That is not a matter of taste, so it is not checked
// by looking at a screenshot: in 1.2.53 the scale was placed at `body * 1.34` without being
// compared to the box, and the shipped shot showed two things at once - tick marks running
// past their own box into the neighbouring label, and tick marks crossing the value ring so
// they read as nubs stuck to it.  Both are relations between numbers, and the numbers now
// live in one place (Source/ui/KnobSpec.h) that this file can include without JUCE.
//
// 1.2.59 rebuilt the dial to the reference's anatomy (dribbble.com/AudioUi): cap, knurled
// skirt, seat, a thick value ring and - only on the large dials - a scale.  The reference's
// small knobs (VST kit 03, its Grain and Effect knobs) have no ticks at all, and the
// arithmetic says why: at the sizes this panel's footer dials are drawn, a minor tick is
// under 2 px and reads as noise.  That threshold is now a number in KnobSpec and a check
// here, so "should this dial have a scale" is not re-argued at every call site.
//
//   0. the parts are ordered: cap < knurl < seat < ring < scale
//   1. nothing painted leaves the box it was given (every box size the panel uses)
//   2. the scale never touches the value ring: there is air between them
//   3. when ticks are drawn they are long enough to read - in *pixels*, not in fractions
//   4. below that size no ticks are drawn at all
//   5. the cap keeps its weight, and the ring is thick enough to be the number
#include <cstdio>

#include "ui/KnobSpec.h"

namespace {

int passed = 0, failed = 0;

void check (bool ok, const char* what) {
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) ++passed; else ++failed;
}

} // namespace

int main() {
    using namespace aeq::ui;

    // the sizes this panel actually draws: 54 design px in the footer at every window size
    // (it is `54 * sy`), the press state's 0.96 scale, a margin either side, and the scale's
    // own sizes above the tick threshold so the tick checks have something to check
    const float boxes[] = { 44.0f, 46.0f, 51.84f, 54.0f, 58.0f, 62.0f, 66.0f, 70.0f, 84.0f };

    // ---- 0. ordered -----------------------------------------------------------
    {
        bool ordered = true;
        for (const float box : boxes) {
            const auto m = knobMetrics (box);
            ordered = ordered && m.body < m.knurlOut && m.knurlOut <= m.seat
                      && m.seat < m.groove && m.groove + m.grooveStroke * 0.5f < m.majorIn
                      && m.majorIn < m.tickIn && m.tickIn < m.tickOut;
        }
        check (ordered, "cap < knurled skirt < seat < value ring < major tick < scale edge");
    }

    // ---- 1. nothing painted leaves the box ------------------------------------
    {
        bool inside = true;
        for (const float box : boxes)
            inside = inside && knobPaintRadius (box) <= box * 0.5f;

        // ... and it is not just "inside", it is not *on* the edge either: a stroke drawn on
        // the boundary is clipped by half its width on a panel that clips to the block
        bool notOnEdge = true;
        for (const float box : boxes)
            notOnEdge = notOnEdge && knobPaintRadius (box) <= box * 0.5f - 0.25f;
        check (inside, "everything the dial paints stays inside the dial's own square");
        check (notOnEdge, "... and off the square's edge, so no stroke is clipped by half");
    }

    // ---- 2. the scale never touches the value ring ----------------------------
    {
        bool air = true;
        for (const float box : boxes) {
            const auto m = knobMetrics (box);
            air = air && (m.majorIn - (m.groove + m.grooveStroke * 0.5f)) >= 0.8f;
        }
        check (air, "the value ring and the scale do not touch (>= 0.8 px of air)");
    }

    // ---- 3/4. ticks, in pixels, and only where they can be read ---------------
    {
        bool readable = true;
        for (const float box : boxes)
            if (knobHasTicks (box)) {
                const auto m = knobMetrics (box);
                readable = readable && (m.tickOut - m.tickIn) >= 2.0f     // minor, px
                           && (m.tickOut - m.majorIn) >= 2.8f;           // major, px
            }
        check (readable, "every tick drawn is at least 2 px long, a major 2.8 px");

        bool none = true;
        for (const float box : boxes)
            if (box < kKnobTicksMinBox) none = none && ! knobHasTicks (box);
        bool some = false;
        for (const float box : boxes)
            if (box >= kKnobTicksMinBox) some = some || knobHasTicks (box);
        check (none, "the footer's dials carry no scale: a tick there is under 2 px");
        check (some, "... and the threshold is somewhere the panel's dials can reach");
    }

    // ---- 5. the cap's weight, and the ring's -----------------------------------
    {
        bool weight = true, seated = true, thick = true;
        for (const float box : boxes) {
            const auto m = knobMetrics (box);
            weight = weight && m.body >= box * 0.30f;
            // the skirt is inside the seat, and the ring sits *outside* the skirt so the
            // knurling is never painted over - on the reference the arc clears the ridges
            seated = seated && m.seat >= m.knurlOut + 0.3f
                     && m.groove - m.grooveStroke * 0.5f >= m.knurlOut
                     && m.knurlOut - m.knurlIn >= 1.8f;      // the skirt is a visible band
            // the ring is the number on this dial: below ~2 px it stops being one
            thick = thick && m.grooveStroke >= 2.0f;
        }
        check (weight, "the cap is at least 30 % of the box, so the dial keeps its weight");
        check (seated, "the skirt sits inside the seat, and the ring clears the skirt");
        check (thick, "the value ring is at least 2 px thick, i.e. readable as a value");
    }

    std::printf ("dials: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
