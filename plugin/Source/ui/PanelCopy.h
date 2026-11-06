#pragma once

#include <cmath>
#include <string>

// The sentences the panel says about the pass.
//
// They live here, not in the editor, because a sentence is a claim and a claim can be
// tested without a window - and this one was not.  The 1.2.16 window said "SEALED" in the
// header and "the verdict lands when it settles" in the footer, on one frame: two stories
// about one take.  The cause was ordering (the verdict asked "has it settled?" before "is
// it sealed?"), and a paint routine is the one place an ordering bug never gets tested.
//
// Plain C++ on purpose: no JUCE, no window, so the gate that builds without a GUI links
// it.  The rule is one stage, one sentence - sealed beats measured, because the user's
// stop is the decision.

namespace aeq::copy
{

struct Facts
{
    bool haveData = false;      // anything has been heard at all
    bool frozen = false;        // the user stopped the pass
    bool committed = false;     // a decision exists and is in the signal path
    bool settled = false;       // enough audio to trust the numbers
    int  bands = 0;             // bands in that decision
    double heardSeconds = 0.0;  // how much of the take was analysed
    double trustSeconds = 0.0;  // how much the solver wants before it calls it settled
};

inline bool sealed (const Facts& f) { return f.frozen || f.committed; }

inline std::string seconds (double s) { return std::to_string ((long long) std::llround (s)); }

inline std::string verdict (const Facts& f)
{
    if (! f.haveData)
        return "Listening. Play the take, then press the clamp to stop it.";

    if (sealed (f))
    {
        if (f.bands <= 0)
            return "Sealed and nothing to correct: the take sits inside the engineer's range.";

        const std::string count = std::to_string (f.bands) + (f.bands == 1 ? " band" : " bands");

        // A take stopped before the solver has the audio it trusts is still the user's
        // decision - it says so with the two numbers instead of claiming a verdict it
        // does not have.
        if (! f.settled && f.trustSeconds > 0.0)
            return "Sealed at " + count + " on " + seconds (f.heardSeconds) + " s of a "
                 + seconds (f.trustSeconds) + " s pass - the numbers are coarse, the decision is not.";

        return "Sealed at " + count + ". The decision does not move on its own.";
    }

    if (! f.settled)
        return "Listening against the 15 reference takes. You stop it when you have heard the take.";

    return "Still learning. You stop it when you have heard the whole take.";
}

// The header's one-word state.  "SEALED" was being shown for `frozen`, and the sub-line
// under it promises "nothing moves on its own" - true only once a decision is in the path.
inline std::string stateWord (const Facts& f)
{
    // "no take yet" outranks everything: the pill and the footer have to agree, and the
    // footer's first question is whether there is a take at all.
    if (! f.haveData) return "READY";
    if (sealed (f)) return "SEALED";
    return "LISTENING";
}

inline std::string stateSub (const Facts& f)
{
    if (! f.haveData) return "press the clamp to stop";
    if (sealed (f)) return "nothing moves on its own";
    return "you stop it";
}

} // namespace aeq::copy
