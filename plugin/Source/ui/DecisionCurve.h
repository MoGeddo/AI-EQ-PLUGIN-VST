#pragma once

#include <cmath>
#include <vector>

#include "dsp/Biquad.h"

// What the "decision" line on the graph is: every static section that is in the signal
// path, summed, at one frequency.
//
// It lives here, in plain C++, for the same reason the panel's sentences live in
// PanelCopy.h: the drawing claimed something about the audio and nothing could test it.
// Until 1.2.49 this line was the sum of the *bands* only - it left the low cut out - so the
// window drew a decision that was flat from 40 Hz to the lowest band while a 4th-order high
// pass at 95 Hz was cutting 30 dB down there.  The user looked at the graph, saw the take
// (cyan) run to the left edge and the reference (white) start at 100 Hz, and asked the only
// question the picture allowed: "why does the line start at 100 and not from the start?"
// The honest answer was "the section that acts below 100 Hz is not drawn", and a picture
// that leaves a section out is a picture that is wrong.
//
// What is in the sum:
//   * every static band in the path (peaks and shelves, plus the user's trim on each)
//   * the low cut, as its own magnitude response, because it is the section that lives
//     below the lowest band
// What is deliberately not in it: the moving layers.  The stabiliser, the de-esser and the
// pitch-tracked bands change every block, so a single drawn line cannot be their sum -
// their live total is the number on the stop row (STATIC n . DYNAMIC n . x dB).

namespace aeq::ui
{

// 4th-order Butterworth (two cascaded biquads), the same pair HighPass24 runs in the path.
inline double lowCutMagDb (double f, double cornerHz, double fs)
{
    if (cornerHz <= 0.0 || f <= 0.0) return 0.0;
    const double c = std::min (std::max (cornerHz, 10.0), fs * 0.49);
    Biquad a = Biquad::highPass (c, 0.54119610, fs);
    Biquad b = Biquad::highPass (c, 1.30656296, fs);
    return a.magDb (f, fs) + b.magDb (f, fs);
}

// One band's contribution at f, i.e. the drawn curve's own arithmetic.
inline double bandDb (const BandSpec& band, double trimDb, double f, double fs)
{
    BandSpec b = band;
    b.gainDb = std::min (18.0, std::max (-18.0, band.gainDb + trimDb));
    return bandMagDb (b, f, fs);
}

// The decision line at f.  `trims` is indexed like `bands`; a short or empty vector means
// "no trim has been made", not "no bands".
inline double decisionDb (const std::vector<BandSpec>& bands, const std::vector<double>& trims,
                          double hpfHz, double f, double fs)
{
    double db = lowCutMagDb (f, hpfHz, fs);
    for (size_t i = 0; i < bands.size(); ++i)
        db += bandDb (bands[i], i < trims.size() ? trims[i] : 0.0, f, fs);
    return db;
}

} // namespace aeq::ui
