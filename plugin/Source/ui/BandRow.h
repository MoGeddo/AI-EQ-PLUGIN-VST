#pragma once

#include <cmath>
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

// The bands this decision actually uses, in the order the core has them - corrective first,
// then resonances - so a row's `src` is its index in that list whenever it can be trimmed.
//
// **Every band the decision made, not every band that happens to be above 0.05 dB right
// now.**  This is the difference between a row that means something and a row that flickers,
// and the flicker was the user's own report: "عدد الاستاتيك بيزيد و يقل يعني مرة 5 مرة 4 مرة
// 6 مرة 7" - five, four, six, seven, on a decision that had not changed.
//
// What made it move: the solver refines four times a second and re-chooses its band set from
// the take's current spectrum, so a band whose correction is 0.04 dB at one solve and 0.06 dB
// at the next crossed this threshold twice a second.  The count on the panel is a *statement
// about the decision*, and a decision is a fixed number of bands - so the threshold belongs
// to the solver (which bands earn a place in the chain), not to the display (which bands
// exist).  A band in the chain at 0.02 dB is still a band in the chain: it is drawn at its
// own gain, which is what makes the picture honest, and it is counted, which is what makes
// the number honest.
//
// `visible()` survives for the one caller that genuinely wants "which bands are doing
// something audible": the gate that counts hot bands in a test.
inline std::vector<Row> decision (const double* gainDb, int count, int correctiveCount)
{
    std::vector<Row> rows;
    if (gainDb == nullptr || count <= 0) return rows;
    rows.reserve (size_t (count));
    for (int i = 0; i < count; ++i)
    {
        const double g = gainDb[i];
        if (std::isnan (g)) continue;              // a band with no answer is not a band
        rows.push_back ({ i, i < correctiveCount ? i : kNoTrim, g });
    }
    return rows;
}

// The bands whose correction is loud enough to see moving.  Not what the row draws any more
// (see `decision()` above), but still the right question for a gate that wants to know
// whether the decision is doing anything at a given moment.
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
