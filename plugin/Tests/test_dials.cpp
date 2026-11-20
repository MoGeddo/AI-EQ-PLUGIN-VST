// test_dials.cpp - the dial's geometry, as arithmetic.
//
// The knob on this panel is drawn from four concentric parts and every one of them has to
// stay inside the square the slider gives it.  That is not a matter of taste, so it is not
// checked by looking at a screenshot: in 1.2.53 the scale was placed at `body * 1.34`
// without being compared to the box, and the shipped shot showed two things at once - tick
// marks running past their own box into the neighbouring label, and tick marks crossing the
// value ring so they read as nubs stuck to it.  Both are relations between numbers, and the
// numbers now live in one place (Source/ui/KnobSpec.h) that this file can include without
// JUCE.
//
//   0. the parts are ordered: body < seat < groove < scale
//   1. nothing painted leaves the box it was given (every box size the panel uses)
//   2. the scale never touches the value ring: there is air between them
//   3. the ticks are long enough to read at the size they are drawn
//   4. the body is not so small that the dial loses its weight
//   5. the air between the body and its seat, and between the seat and the ring
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
    // (it is `54 * sy`), plus the press state's 0.96 scale, plus a margin either side so a
    // future resize cannot quietly walk out of the test
    const float boxes[] = { 44.0f, 46.0f, 51.84f, 54.0f, 58.0f, 62.0f };

    // ---- 0. ordered -----------------------------------------------------------
    {
        bool ordered = true;
        for (const float box : boxes) {
            const auto m = knobMetrics (box);
            ordered = ordered && m.body < m.seat && m.seat < m.groove
                      && m.groove < m.majorIn && m.majorIn < m.tickIn && m.tickIn < m.tickOut;
        }
        check (ordered, "body < seat < value ring < major tick < minor tick < scale edge");
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
        check (inside, "the scale's outer edge stays inside the dial's own square");
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

    // ---- 3. a tick you can see -------------------------------------------------
    {
        bool readable = true;
        for (const float box : boxes) {
            const auto m = knobMetrics (box);
            readable = readable && (m.tickOut - m.tickIn) >= box * 0.04f
                       && (m.tickOut - m.majorIn) >= box * 0.05f;
        }
        check (readable, "a minor tick is >= 4 % of the box long, a major >= 5 %");
    }

    // ---- 4/5. the body, and the air around it ---------------------------------
    {
        bool weight = true, seated = true;
        for (const float box : boxes) {
            const auto m = knobMetrics (box);
            weight = weight && m.body >= box * 0.32f;
            seated = seated && m.seat >= m.body + 0.6f
                     && m.groove >= m.seat + 0.6f;
        }
        check (weight, "the body is at least 32 % of the box, so the dial keeps its weight");
        check (seated, "the body sits in its seat, and the seat inside the value ring");
    }

    std::printf ("dials: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
