#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    int  bands = 0;             // bands in that decision (the frozen ones)
    double heardSeconds = 0.0;  // how much of the take was analysed
    double trustSeconds = 0.0;  // how much the solver wants before it calls it settled
    int  dynamicLayers = 0;     // layers moving the signal right now (stabiliser + de-esser
                                // + tracked), 0 when nothing is committed
    double movingDb = 0.0;      // and how much they are taking off, in total, right now
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

        return "Sealed at " + count + ". The curve is frozen; the dynamic layer keeps working.";
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
    // The frozen curve cannot move; the dynamic layer keeps working - and the old sentence
    // said "nothing moves on its own", i.e. the opposite of what the plugin does.  It is
    // the sentence a user asked about directly ("اوتوماتيك ودايناميك ولا ستاتيك؟").
    //
    // It stops at "curve frozen" now because the panel draws it *beside* `autoLine`, which
    // already names the dynamic layer: "curve frozen  ·  AUTO: GAIN + DYNAMIC + STOP AT
    // 45 s" says both halves once, and a sentence that says "dynamics live" next to
    // "+ DYNAMIC" reads like two different claims about one thing.
    if (sealed (f)) return "curve frozen";
    // "correction live" and not "correction live, still learning": this sentence is drawn
    // beside `autoLine` under the seal pill, and the two together have to fit between the
    // tagline's ink and the panel's right edge at every aspect ratio the window allows.
    if (open (f)) return "correction live";
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

// ---------------------------------------------------------------------------
// The scan: what Ozone's Match EQ and smart:EQ both make visible, and this panel did not.
//
// A learn pass has an end that is not the user's finger: the solver has a trust threshold,
// and until it is reached the numbers are coarse.  The panel knew the threshold (it is the
// auto-stop dial) and it knew the seconds; it printed the seconds only, so "have I scanned
// enough, and is it scanning at all" had to be inferred from a number going up.
//
// `scanPercent` is against the threshold, not against the safety cap: 100% means "the
// solver has what it asked for", and the cap is a different thing that must not look like
// progress.  With no threshold (0 = the CLI's settle-0 mode) the percentage is honest only
// as "no target", so it reports -1 and the wording says so.
inline int scanPercent (const Facts& f)
{
    if (f.trustSeconds <= 0.0) return -1;
    const double p = 100.0 * f.heardSeconds / f.trustSeconds;
    return int (std::llround (std::min (100.0, std::max (0.0, p))));
}

// One word for the header's second line, so a glance answers "is it scanning".
inline std::string scanWord (const Facts& f)
{
    if (sealed (f)) return "SCANNED";
    if (! f.haveData) return "READY TO SCAN";
    const int p = scanPercent (f);
    return p < 0 ? std::string ("SCANNING") : ("SCANNING " + std::to_string (p) + "%");
}

// The strip inside the take bar.  While it runs it says how far and how much; once the
// user stops it, it says what was scanned - and says "COARSE" if it was stopped early,
// because that is the one thing the number must never hide.
inline std::string scanLine (const Facts& f)
{
    const std::string secs = std::to_string ((long long) std::llround (f.heardSeconds)) + " s";
    if (sealed (f))
    {
        if (f.trustSeconds > 0.0 && f.heardSeconds + 0.5 < f.trustSeconds)
            return "SCANNED " + secs + " OF " + std::to_string ((long long) std::llround (f.trustSeconds))
                 + " s  \xc2\xb7  COARSE";
        return "SCANNED " + secs;
    }
    const int p = scanPercent (f);
    if (p < 0) return "SCAN " + secs;
    return "SCAN " + std::to_string (p) + "%  \xc2\xb7  " + secs;
}

// How much of the decision is frozen and how much moves - the answer to "static or
// dynamic", as one line, with the live number at the end.
//
// `movingLayers` is counted against `bands`, but a moving layer is not one of the bands:
// the stabiliser has its own ten-band grid and the tracked layers sit on the note.  So the
// line never adds them into one total that would be a lie; it prints the frozen count and
// the moving count side by side, and the dB the moving ones are taking off right now.
inline std::string layerLine (const Facts& f)
{
    if (! f.committed)
        return std::string();

    std::string s = "STATIC " + std::to_string (f.bands);
    if (f.dynamicLayers > 0)
    {
        s += "  \xc2\xb7  DYNAMIC " + std::to_string (f.dynamicLayers);
        // the live number, one decimal, and it only appears when the layer is doing
        // something: a permanent "LIVE 0.0 dB" is a number nobody reads
        char buf[32];
        std::snprintf (buf, sizeof buf, " %.1f dB", f.movingDb);
        s += "  \xc2\xb7"; s += buf;
    }
    else
        s += "  \xc2\xb7  DYNAMIC 0  \xc2\xb7  STILL";
    return s;
}

// What runs by itself.  The dial decides whether the pass ends on its own; the gain match
// and the dynamic layers always do.  Saying it once, in the words of the question, beats
// three dials a user has to interpret.
inline std::string autoLine (const Facts& f, double autoStopSeconds)
{
    std::string s = "AUTO: GAIN";
    if (f.committed) s += " + DYNAMIC";
    s += autoStopSeconds > 0.0
        ? (" + STOP AT " + std::to_string ((long long) std::llround (autoStopSeconds)) + " s")
        : std::string (" + STOP BY YOU");
    return s;
}

} // namespace aeq::copy
