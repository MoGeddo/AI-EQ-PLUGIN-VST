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
    // Who ended the pass.  The user asked "does it scan all the audio or does it stop by
    // itself?" and the panel could not answer: every sealed pass read "SEALED", whether the
    // person pressed the clamp, the auto-stop dial ran out, or the five-minute cap fired.
    bool stoppedByUser = true;
    bool autoStopped = false;
    // The dials that decide it, so the panel can print the RULE and not only the outcome.
    double settleSeconds = 0.0;   // how much audio it wants before it trusts the numbers
    double autoStopSeconds = 0.0; // 0 = it never stops itself
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

    // Whichever rule is in force, said out loud, because the two are different promises:
    // with `decide after = 0` the pass ends at the end of the take, and with a number it
    // ends when that many seconds have been heard.
    if (! f.settled)
    {
        if (f.trustSeconds <= 0.0)
            return "Hearing the whole take. Play it through and it decides at the end - "
                   "press the clamp to decide now.";

        // the seconds still to go, from the two numbers the panel already has
        const double left = std::max (0.0, f.trustSeconds - f.heardSeconds);
        return "Hearing the take. " + seconds (left)
             + " s more before it decides. Press the clamp to decide now.";
    }

    // `settled` with no threshold means the transport has stopped and the whole take has
    // gone past - but the decision is still only a proposal until a hand seals it, and a
    // sentence that says "decides when the transport stops" *after* it has stopped reads as
    // "wait for something else".  So this one says what is true now and what to do.
    return "Heard the whole take and it is ready. Press the clamp to seal the decision.";
}

// The same verdict, for the rows that cannot hold the long one.
//
// The footer's sentence box is ~360 px wide at 1240x800 and ~272 px at the window's smallest
// size, and two lines are what the row has.  At full width the long sentences fit; at the
// small end they do not - "Sealed at 4 bands on 21 s of a 45 s pass - the numbers are coarse,
// the decision is not." is three lines there, which the harness caught on the shipped window.
// Dropping whole clauses silently would be dishonest, so the short form is *written* as the
// same claim with fewer words, and the panel only uses it when the long one does not fit.
// Both forms are true; neither is a truncation of the other.
inline std::string verdictCompact (const Facts& f)
{
    if (! f.haveData)
        return "Listening. Play the take, then press the clamp.";

    if (sealed (f))
    {
        if (f.bands <= 0)
            return "Sealed, nothing to correct: the take is in range.";
        const std::string count = std::to_string (f.bands) + (f.bands == 1 ? " band" : " bands");
        if (! f.settled && f.trustSeconds > 0.0)
            return "Sealed at " + count + " on " + seconds (f.heardSeconds) + " s of "
                 + seconds (f.trustSeconds) + " - coarse, not wrong.";
        return "Sealed at " + count + ". Frozen curve, live dynamics.";
    }

    if (open (f))
    {
        const std::string count = f.bands <= 0 ? std::string ("nothing") : std::to_string (f.bands)
                                  + (f.bands == 1 ? " band" : " bands");
        return "Open again: " + count + " in the path, still counting.";
    }

    if (! f.settled)
        return f.trustSeconds <= 0.0 ? "Hearing the whole take. It decides at the end."
                                     : "Hearing the take. Press the clamp to decide now.";
    return "Heard the whole take. Press the clamp to seal it.";
}

// The header's one-word state.  "SEALED" alone cannot answer the user's question - "does it
// scan all the audio, or does it stop by itself?" - because a pass that ended itself and a
// pass the person ended read the same.  So the word carries who ended it: SEALED BY YOU,
// SEALED (AUTO-STOP), SEALED (5 MIN CAP).  It is one word either way, and the difference is
// exactly the thing that was being asked about.
inline std::string stateWord (const Facts& f)
{
    if (! f.haveData) return "READY";
    if (sealed (f))
    {
        if (f.stoppedByUser) return "SEALED BY YOU";
        if (f.autoStopped)   return "SEALED (AUTO-STOP)";
        return "SEALED (5 MIN CAP)";
    }
    if (open (f)) return "OPEN";
    return "LEARNING";
}

// And the sentence under the seal keeps saying what runs without a hand.
inline std::string sealedSub (const Facts& f)
{
    if (f.stoppedByUser) return "frozen where you stopped it";
    if (f.autoStopped)   return "frozen at " + std::to_string ((long long) std::llround (f.heardSeconds))
                                + " s by the auto-stop";
    return "frozen by the 5 minute cap";
}

// The same two facts as one number for the *closed* pass: nothing on the panel may keep
// saying "SCANNING" over a decision that is frozen.  The meter is stopped by the core, so
// this is only the wording.
inline bool stillScanning (const Facts& f) { return ! sealed (f) && f.haveData; }

// The invitation before there is a take, and the state sentence while one is open.  These
// were in this file before the seal learned to name who ended it; they are kept because the
// header still draws them for the two states that have no "who" to name.
inline std::string stateSub (const Facts& f)
{
    if (! f.haveData) return "press the clamp to stop";
    if (sealed (f))   return sealedSub (f);
    return "correction live";
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
// progress.
//
// **-1 now means "there is no finish line to be a percentage of".**  With `decide after = 0`
// the plugin waits for the end of the take, so "how far to the threshold" does not exist
// and a percentage would be an invention - worse, "100%" would be a lie that reads as
// "finished" over a take it is still hearing.  The wording carries the truth instead:
// `SCANNING ALL`, and the bar says how much has been heard so far.
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
    // "SCANNING ALL", not "SCANNING": the question this pill exists to answer is whether the
    // pass covers the whole take, and a bare "SCANNING" leaves the reader to guess.  It is
    // also narrower than the "SCANNING 100%" it replaces (143.9 px against 155.6), so the
    // pill that carries it is one of the shorter ones rather than the longest.
    return p < 0 ? std::string ("SCANNING ALL")
                 : ("SCANNING " + std::to_string (p) + "%");
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
    // No threshold: the bar cannot say "how far", only "how much so far", and it says which
    // finish line it is aimed at so the number is not left hanging.
    if (p < 0) return "HEARD " + secs + "  \xc2\xb7  TO THE END";
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

// What runs by itself, and **when it stops**.  This started as "AUTO: GAIN + DYNAMIC +
// STOP BY YOU", which was half true: the plugin's default really is that the person ends
// the pass, but there is a five-minute cap under that promise (a pass nobody stops would
// listen forever and never apply anything).  The user asked directly - "does it scan all
// the audio, or does it stop by itself?" - so the line now states the rule it will actually
// follow, with the seconds that remain on the clock, and names the cap rather than
// pretending there is none.
//
//   dial set    SCANS TO 21 s, THEN STOPS ITSELF  ·  6 s LEFT
//   dial OFF    SCANS TO THE 5 MIN CAP  ·  240 s LEFT
//
// The count is against the *pass clock* (every second of input, gaps included), not the
// sung-audio meter: a stop promised in seconds has to be seconds.
inline std::string stopRuleLine (double stopAtSeconds, bool dialled, double clockSeconds)
{
    const double left = std::max (0.0, stopAtSeconds - clockSeconds);
    const std::string at   = std::to_string ((long long) std::llround (stopAtSeconds));
    const std::string rest = std::to_string ((long long) std::llround (left));
    if (dialled)
        return "SCANS TO " + at + " s, THEN STOPS ITSELF  \xc2\xb7  " + rest + " s LEFT";
    return "SCANS TO THE " + std::to_string ((long long) std::llround (stopAtSeconds / 60.0))
         + " MIN CAP  \xc2\xb7  " + rest + " s LEFT";
}

// The footer's summary of what needs no hand: the gain match and the dynamic layer always
// run; the stop is the line above.
inline std::string autoLine (const Facts& f)
{
    std::string s = "AUTO: GAIN";
    if (f.committed) s += " + DYNAMIC";
    return s;
}

} // namespace aeq::copy
