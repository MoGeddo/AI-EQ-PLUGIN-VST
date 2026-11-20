// test_controls.cpp - the shapes taken from the reference, as relations between numbers.
//
// After the colour pass (1.2.58) and the dial pass (1.2.59-1.2.62) the user said the obvious
// thing: "في مفاتيح كتير وكنوبس انت مخدتش منها أي حاجة" - the reference is full of *switches*,
// and the panel was still drawing flat rounded rectangles with glyphs in them.
//
// The three shapes this holds are in Source/ui/Controls.h: the rocker switch (kit 01: a lamp
// beside a slanted paddle), the orange pill that marks the chosen side of a two-way switch
// (kit 03's "Last"), and the card header rule (kit 03's Grain / Transients cards).  Each is
// arithmetic, so each is checked here rather than admired in a screenshot:
//
//   0. a rocker's paddle is inside its plate, and it is symmetric between on and off
//   1. the lamp is outside the plate and inside the control's extent
//   2. the pill that marks a chosen key is inset inside the key on both axes
//   3. a card's header rule stops short of the card's edge and keeps air from the content
//   4. no size the panel uses makes any of those go negative
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "ui/Controls.h"

namespace {

int passed = 0, failed = 0;

void check (bool ok, const char* what) {
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) ++passed; else ++failed;
}

} // namespace

int main() {
    using namespace aeq::ui;

    // the header keys are 34 design px; the mode keys are 90 x 26; the chips are ~300 x 80
    const float keyBoxes[] = { 34.0f, 36.0f, 40.0f, 44.0f };
    const float chips[] = { 240.0f, 300.0f, 360.0f };

    // ---- 0. inside, and symmetric ---------------------------------------------
    {
        bool inside = true, symmetric = true;
        for (const float b : keyBoxes) {
            const auto m = rockerMetrics (b);
            const float halfPaddle = m.plate - m.pad;
            inside = inside && halfPaddle > 0.0f && m.tilt < halfPaddle;
            symmetric = symmetric && std::fabs (rockerTiltFor (b, true)
                                                + rockerTiltFor (b, false)) < 1e-6f;
        }
        check (inside, "a rocker's paddle is inside its plate, and its travel inside the paddle");
        check (symmetric, "on and off travel the same distance, in opposite directions");
    }

    // ---- 1. the lamp ----------------------------------------------------------
    {
        bool ok = true;
        for (const float b : keyBoxes) {
            const auto m = rockerMetrics (b);
            ok = ok && m.lampD > 1.5f                       // a lamp you can see
                 && rockerExtent (b) <= 0.75f * b           // and the whole thing in its box
                 && rockerExtent (b) > m.plate;             // the lamp is outside the plate
        }
        check (ok, "the lamp sits outside the plate, inside the control's box");
    }

    // ---- 2. the chosen-side pill ---------------------------------------------
    {
        bool ok = true;
        for (const float w : { 88.0f, 90.0f, 120.0f }) {
            const float h = w * 26.0f / 90.0f;
            const auto p = pillMetrics (w, h);
            ok = ok && p.insetX > 2.0f && p.insetY > 1.0f
                 && p.radius > 4.0f
                 && (w - 2.0f * p.insetX) > 20.0f;
        }
        check (ok, "the chosen-side pill is inset inside its key on both axes");
    }

    // ---- 3. the card header rule ---------------------------------------------
    {
        bool ok = true;
        for (const float c : chips) {
            const auto m = cardMetrics (c);
            ok = ok && m.ruleInset > 4.0f                  // never on the card's border
                 && m.headerH > 8.0f                       // room for 15 px type
                 && m.ruleGap > 2.0f                       // and air under it
                 && m.padX >= 6.0f;
        }
        check (ok, "a card's header rule stops inside the card and keeps air from its content");
    }

    // ---- 4. nothing degenerate --------------------------------------------------
    {
        bool ok = true;
        for (const float b : keyBoxes) {
            const auto m = rockerMetrics (b);
            ok = ok && m.plate > 0.4f * b && m.pad > 0.5f && m.lampGap > 0.5f;
        }
        check (ok, "no size the header uses makes a part of the switch vanish");
    }

    std::printf ("controls-shapes: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
