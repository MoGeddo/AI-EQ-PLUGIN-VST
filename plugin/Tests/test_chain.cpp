// test_chain.cpp - the decision line the graph draws is the chain that is in the path.
//
// The window draws one pink line and calls it "decision".  Until 1.2.49 that line was the
// sum of the static bands and nothing else: the low cut was in the audio path and absent
// from the drawing, so the region below the lowest band read as "the plugin does nothing
// here" while a 4th-order high pass at 95 Hz was cutting 30 dB in it.  The user saw a take
// that runs to the left edge, a reference that starts at 100 Hz, and a decision that is
// flat at 0 dB under both - and asked why the drawing starts in the middle.
//
// These checks are about the arithmetic of that line, not about the pixels: the same
// function (Source/ui/DecisionCurve.h) is called by CurveView::drawCorrection.
//
//   0. the band sum is what it was (one band, +3 dB, is +3 dB at its own centre)
//   1. with no low cut the line is exactly the band sum
//   2. a 95 Hz 4th-order cut really is a cut: -12 dB or lower by 40 Hz
//   3. ... and it is inaudible at 1 kHz (where the bands do their work)
//   4. the cut's own corner is -3 dB, i.e. the section is a Butterworth pair and not a shelf
//   5. moving the corner moves the line: 95 Hz cuts 70 Hz harder than 60 Hz does
//   6. below the corner the line rises monotonically with frequency (no ripple)
//   7. the user's trim is in the line, and it is the same trim the handle shows
//   8. a band at +/-18 dB stays inside +/-18 dB (the drawn ceiling)
//   9. nothing in 20 Hz .. 20 kHz is not-a-number, at 44.1k and at 48k
//  10. a short trims vector means "no trim", not "no bands"
//  11. the reference curve has no value below 100 Hz - which is what the panel now says
//  12. ... and it has one at 100 Hz, so the marker is drawn where the data starts
#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp/Biquad.h"
#include "model/EnvTargetCurve.h"
#include "ui/DecisionCurve.h"

namespace {

int passed = 0, failed = 0;

void check (bool ok, const char* what) {
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) ++passed; else ++failed;
}

bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }

aeq::BandSpec band (aeq::BandType t, double f, double q, double g) {
    aeq::BandSpec b;
    b.type = t; b.f0 = f; b.q = q; b.gainDb = g;
    return b;
}

} // namespace

int main() {
    const double fs = 48000.0;
    std::printf ("\n===== the decision line is the chain in the path =====\n");

    const std::vector<aeq::BandSpec> one { band (aeq::BandType::Peak, 1000.0, 1.0, 3.0) };
    const std::vector<aeq::BandSpec> none;

    // 0. the band sum is unchanged by any of this
    check (near (aeq::ui::decisionDb (one, {}, 0.0, 1000.0, fs), 3.0, 0.05),
           "0. one peak at +3 dB reads +3.0 dB at its own centre");

    // 1. no low cut, no change
    check (near (aeq::ui::decisionDb (one, {}, 0.0, 40.0, fs),
                 aeq::ui::bandDb (one[0], 0.0, 40.0, fs), 1e-9),
           "1. with no low cut the line is exactly the band sum");

    // 2. the low cut is a cut, and this is the whole point of the change
    const double at40 = aeq::ui::decisionDb (none, {}, 95.0, 40.0, fs);
    check (at40 <= -12.0,
           "2. a 95 Hz low cut pulls the decision to -12 dB or lower at 40 Hz "
           "(that is the section the drawing used to leave out)");

    // 3. and it does not touch the midrange
    const double at1k = aeq::ui::lowCutMagDb (1000.0, 95.0, fs);
    check (near (at1k, 0.0, 0.1), "3. the same cut is flat at 1 kHz (within 0.1 dB)");

    // 4. a Butterworth pair, not a shelf: -3 dB at its own corner
    const double atCorner = aeq::ui::lowCutMagDb (95.0, 95.0, fs);
    check (near (atCorner, -3.0, 0.5),
           "4. the corner reads -3 dB, so it is the 4th-order Butterworth pair the path runs");

    // 5. the corner is what the line shows
    const double at70_95 = aeq::ui::decisionDb (none, {}, 95.0, 70.0, fs);
    const double at70_60 = aeq::ui::decisionDb (none, {}, 60.0, 70.0, fs);
    check (at70_95 < at70_60 - 3.0,
           "5. moving the corner 60 -> 95 Hz cuts 70 Hz by more than 3 dB more");

    // 6. no ripple below the corner: dB rises with frequency
    bool monotone = true;
    double prev = -200.0;
    for (double f = 20.0; f <= 95.0; f *= 1.02) {
        const double db = aeq::ui::lowCutMagDb (f, 95.0, fs);
        if (db < prev - 1e-9) monotone = false;
        prev = db;
    }
    check (monotone, "6. below the corner the line only rises (no ripple in the drawn cut)");

    // 7. the user's trim is in the line
    const double trimmed = aeq::ui::decisionDb (one, { -4.0 }, 0.0, 1000.0, fs);
    check (near (trimmed, -1.0, 0.05),
           "7. a -4 dB trim on a +3 dB band reads -1.0 dB: the handle and the line agree");

    // 8. the drawn ceiling holds
    const std::vector<aeq::BandSpec> hot { band (aeq::BandType::Peak, 500.0, 1.0, 15.0) };
    check (aeq::ui::decisionDb (hot, { 9.0 }, 0.0, 500.0, fs) <= 18.0 + 1e-6,
           "8. a band trimmed past +18 dB stays bounded by the drawn ceiling");

    // 9. finite everywhere the plot can ask
    bool finite = true;
    for (double rate : { 44100.0, 48000.0 })
        for (double f = 20.0; f <= 20000.0; f *= 1.07) {
            const double db = aeq::ui::decisionDb (one, { 2.0 }, 95.0, f, rate);
            if (! std::isfinite (db)) finite = false;
        }
    check (finite, "9. the line is finite from 20 Hz to 20 kHz at 44.1k and at 48k");

    // 10. a short trims vector is "no trim"
    check (near (aeq::ui::decisionDb (one, {}, 0.0, 1000.0, fs), 3.0, 0.05),
           "10. a short trims vector means no trim, not no bands");

    // 11-12. where the reference curve exists - the white line's start
    check (! aeq::EnvTargetCurve::definedAt (60.0),
           "11. the reference curve has no value at 60 Hz (a voice has nothing below its "
           "fundamental, so the corpus cannot define a target there)");
    check (aeq::EnvTargetCurve::definedAt (100.0),
           "12. ... and it has one at 100 Hz, which is where the panel now says it starts");

    std::printf ("\n===== %d passed, %d failed =====\n\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
