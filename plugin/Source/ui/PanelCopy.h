#pragma once

#include <cmath>
#include <string>

// The sentences the panel says about the pass.
//
// They live here, not in the editor, because a sentence is a claim and a claim can be
// tested without a window - and this one was not.  The 1.2.16 window said "SEALED" in the
// header and "the verdict lands when it settles" in the footer on one frame: two stories
// about one take.
//
// There are four states, not three, and the fourth is the one the clamp's second press
// produces:
//
//   READY      nothing heard yet
//   LISTENING  a take is being learnt; the audio is untouched
//   SEALED     the user stopped it; the decision is frozen and playing
//   OPEN       the user pressed the clamp again: the decision still plays, and the pass
//              keeps counting, so the next stop solves from the longer take
//
// `committed && !frozen` is OPEN.  Before this file existed there was no way to say that,
// so reopening a sealed take changed nothing a person could see.
//
// Plain C++ on purpose: no JUCE, no window, so the gate that builds without a GUI links it.

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

inline bool sealed (const Facts& f) { return f.frozen; }
inline bool open (const Facts& f) { return f.committed && ! f.frozen; }

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

    if (open (f))
    {
        const std::string count = f.bands <= 0
            ? std::string ("nothing")
            : (std::to_string (f.bands) + (f.bands == 1 ? " band" : " bands"));
        return "Open again: " + count + " in the path and the pass is counting. "
               "Press the clamp to seal it where you stop.";
    }

    if (! f.settled)
        return "Listening against Amir's 15 reference takes. You stop it when you have heard the take.";

    return "Still learning. You stop it when you have heard the whole take.";
}

// The header's one-word state.
inline std::string stateWord (const Facts& f)
{
    // "no take yet" outranks everything: the pill and the footer have to agree, and the
    // footer's first question is whether there is a take at all.
    if (! f.haveData) return "READY";
    if (sealed (f)) return "SEALED";
    if (open (f)) return "OPEN";
    return "LISTENING";
}

inline std::string stateSub (const Facts& f)
{
    if (! f.haveData) return "press the clamp to stop";
    if (sealed (f)) return "nothing moves on its own";
    if (open (f)) return "correction live, still learning";
    return "you stop it";
}

// The word on the clamp itself.  This is the answer to "where does it start and where do
// I stop it": the control carries its own verb, in the same place, in both states.
inline std::string clampVerb (const Facts& f)
{
    if (sealed (f)) return "OPEN";
    return "STOP";
}

inline std::string clampSub (const Facts& f)
{
    if (sealed (f)) return "TAP TO LISTEN ON";
    return "TAP WHEN THE TAKE ENDS";
}

} // namespace aeq::copy
