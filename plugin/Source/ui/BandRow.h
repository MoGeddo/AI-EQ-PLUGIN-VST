#pragma once

#include <vector>

// Which band each drawn row refers to.
//
// The row under the graph shows the bands that are actually doing something.  The core's
// list is longer: it also carries the bands the corpus study puts in the chain at rest, the
// resonances, and the two taste axes - all of which sit at 0 dB and are skipped by the
// cascade.  So "the third chip" and "band 3" are not the same thing, and in 1.2.16 the
// editor used one for the other: a drag on the third visible band wrote the trim of
// whichever band happened to be third in the array.  Nothing on screen said so; the band
// moved by the wrong amount, or did not move at all.
//
// The mapping is a decision, so it lives here - plain C++, no JUCE - where the gate that
// builds without a GUI can hold it to an answer.
//
// `src` semantics:
//   >= 0  an index into the core's band list, and a band a trim can address
//   -1    a resonance or a taste-axis band: it is in the path, but it has no trim slot
//   -2    a dynamic layer (tracked): it has no fixed frequency at all

namespace aeq::bands
{

enum : int { kTracked = -2, kNoTrim = -1 };

struct Row
{
    int index = -1;         // where it is in the core's list (what activeBands() returned)
    int src = kNoTrim;      // ... and whether a trim can address it (see the enum above)
    double gainDb = 0.0;
};

// The bands worth drawing: everything that is not sitting at 0 dB.  Bands are ordered the
// way the core has them - corrective first, then resonances - so a row's `src` is its
// index in that list whenever it can be trimmed.
inline std::vector<Row> visible (const double* gainDb, int count, int correctiveCount,
                                 double threshold = 0.05)
{
    std::vector<Row> rows;
    if (gainDb == nullptr || count <= 0) return rows;
    rows.reserve (size_t (count));
    for (int i = 0; i < count; ++i)
    {
        const double g = gainDb[i];
        if (! (g > threshold || g < -threshold)) continue;     // also drops NaN
        rows.push_back ({ i, i < correctiveCount ? i : kNoTrim, g });
    }
    return rows;
}

} // namespace aeq::bands
