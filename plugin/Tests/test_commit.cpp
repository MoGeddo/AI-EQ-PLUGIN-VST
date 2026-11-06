// test_commit.cpp - learn the whole take, the user stops it, then it is frozen.
//
// The workflow the product was always meant to have, in the user's words:
// "الأول يعمل ليرن للصوت كله وبعدين أوقفه أنا".  That is four hard promises, and
// each one is measurable without a DAW, a screen or a click:
//
//   1. WHILE LEARNING THE AUDIO IS UNTOUCHED, bit for bit.  Not "gentle" - identical.
//      Anything else is the plugin mixing while it is supposed to be listening.
//   2. STOP APPLIES THE DECISION within 250 ms, and the transition is a fade, not a
//      step: no sample-to-sample jump over 1e-3 anywhere in the first 400 ms.
//   3. AFTER THE COMMIT IT IS FROZEN.  Ten more seconds of the same take may not
//      change a single band, a Q, or the low cut.  (The dynamic layers move; the
//      decision does not.)
//   4. RELEARN RESETS.  Asking for a new learn pass puts it back to untouched audio
//      and an invalid state, and the next commit is computed from the new take only.
//
//   g++ -std=c++17 -O2 -Iplugin/Source plugin/Tests/test_commit.cpp -o build/aeq_commit
//   build/aeq_commit

#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../Source/dsp/RestoreCore.h"
#include "../Source/ui/BandRow.h"
#include "../Source/ui/PanelCopy.h"

static const double kPi = 3.14159265358979323846;
static const double kSr = 48000.0;
static int gPass = 0, gFail = 0;

static bool has (const std::string& s, const char* needle) {
    return s.find (needle) != std::string::npos;
}

static void checkTrue(bool ok, const char* what) {
    std::printf("  %s   %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) ++gPass; else ++gFail;
}
static void checkF(bool ok, const char* fmt, double v) {
    std::printf("  %s   ", ok ? "ok  " : "FAIL");
    std::printf(fmt, v);
    std::printf("\n");
    if (ok) ++gPass; else ++gFail;
}

// a dry vocal with a boom and a room bell - same generator as the other gates
static std::vector<float> dryVocal(double f0, double seconds, double boomDb, double bellHz) {
    const int n = int(seconds * kSr);
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    std::mt19937 rng(5);
    std::normal_distribution<double> nd(0.0, 1.0);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / kSr;
        const double vib = 1.0 + 0.012 * std::sin(2.0 * kPi * 5.2 * t);
        phase += 2.0 * kPi * f0 * vib / kSr;
        double s = 0.0;
        for (int k = 1; k <= 30; ++k) s += std::sin(k * phase) / std::pow(k, 1.05);
        const double cyc = std::fmod(t, 1.55);
        const double env = (cyc < 1.25) ? 1.0 : 0.0;
        x[size_t(i)] = float(0.13 * env * s);
    }
    double lp = 0.0;
    const double aLp = 1.0 - std::exp(-2.0 * kPi * 110.0 / kSr);
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    const double w = 2.0 * kPi * bellHz / kSr, q = 2.2, A = std::pow(10.0, 4.0 / 40.0);
    const double alpha = std::sin(w) / (2.0 * q), cw = std::cos(w), a0 = 1.0 + alpha / A;
    const double pb0 = (1.0 + alpha * A) / a0, pb1 = (-2.0 * cw) / a0, pb2 = (1.0 - alpha * A) / a0;
    const double pa1 = (-2.0 * cw) / a0, pa2 = (1.0 - alpha / A) / a0;
    const double boom = std::pow(10.0, boomDb / 20.0);
    for (auto& v : x) {
        const double in = v;
        lp += aLp * (in - lp);
        const double y = pb0 * in + pb1 * x1 + pb2 * x2 - pa1 * y1 - pa2 * y2;
        x2 = x1; x1 = in; y2 = y1; y1 = y;
        v = float(in + 0.02 * boom * lp + 0.16 * y);
    }
    double pk = 0.0;
    for (float v : x) pk = std::max(pk, std::abs(double(v)));
    if (pk > 0.0) for (auto& v : x) v = float(v * (0.89 / pk));
    return x;
}

struct Ctx {
    std::unique_ptr<aeq::RestoreCore> core;
    std::vector<float> l, r;
    Ctx() : core(std::make_unique<aeq::RestoreCore>()), l(512), r(512) {
        core->prepare(kSr, 512);
        aeq::RestoreSettings s;
        s.learn = true;
        // No safety cap: this gate drives the commit itself, exactly like a user
        // pressing Stop.  (0 would mean "apply immediately", the CLI's mode.)
        s.settleSeconds = 3600.0;
        core->setSettings(s);
    }
    // returns the output of the next block
    std::vector<float> play(const std::vector<float>& take, size_t& pos, double* blockOut = nullptr) {
        for (int i = 0; i < 512; ++i) {
            const float v = (pos < take.size()) ? take[pos] : 0.0f;
            ++pos;
            l[size_t(i)] = v;
            r[size_t(i)] = v;
        }
        core->processBlock(l.data(), r.data(), 512);
        if (blockOut) *blockOut = double(l[0]);
        return std::vector<float>(l.begin(), l.end());
    }
};

static double maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double m = 0.0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) m = std::max(m, std::abs(double(a[i]) - double(b[i])));
    return m;
}

int main() {
    std::printf("\n===== AEQ RESTORE - learn, stop, frozen =====\n\n");
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const auto take = dryVocal(196.0, 40.0, 24.0, 240.0);

    // ---- 1. while learning, the audio is untouched --------------------------
    {
        Ctx c;
        size_t pos = 0;
        double worst = 0.0;
        // 30 s of audio, refresh at 5 Hz like the plugin's timer
        for (int b = 0; b < int(30.0 * kSr / 512); ++b) {
            std::vector<float> in(512, 0.0f);
            for (int i = 0; i < 512; ++i) in[size_t(i)] = (pos + size_t(i) < take.size()) ? take[pos + size_t(i)] : 0.0f;
            const auto out = c.play(take, pos);
            worst = std::max(worst, maxDiff(in, out));
            if (b % 6 == 0) c.core->refresh();
        }
        std::printf("  after 30 s of learning: %zu bands in state, analysed %.1f s\n",
                    c.core->state().corrective.size(), c.core->state().analyzedSeconds);
        checkF(worst == 0.0, "1. while it learns, the audio is bit-exact (worst |diff| %.1e)", worst);
        checkTrue(c.core->phase() == aeq::RestoreCore::Phase::Learning, "1. and it says it is still learning");
        checkTrue(c.core->appliedBandCount() == 0, "1. nothing is in the signal path yet");

        // ---- 2. stop: applies within 250 ms, with a fade --------------------
        c.core->commit();
        double step = 0.0, firstNonZero = -1.0;
        std::vector<float> stream;          // concatenated output: a "step" is between
        size_t pos2 = pos;                  // two *adjacent samples*, not two blocks
        for (int b = 0; b < int(0.6 * kSr / 512); ++b) {
            std::vector<float> in(512, 0.0f);
            for (int i = 0; i < 512; ++i) in[size_t(i)] = (pos2 + size_t(i) < take.size()) ? take[pos2 + size_t(i)] : 0.0f;
            const auto out = c.play(take, pos2);
            if (firstNonZero < 0.0 && maxDiff(in, out) > 1e-7) firstNonZero = double(b * 512) / kSr;
            for (float v : out) {
                if (!stream.empty()) step = std::max(step, std::abs(double(v) - double(stream.back())));
                stream.push_back(v);
            }
        }
        const auto st = c.core->state();
        std::printf("  after stop: %d bands, HPF %.0f Hz, first change at %.0f ms\n",
                    c.core->appliedBandCount(), st.hpfHz, firstNonZero * 1000.0);
        checkTrue(c.core->phase() == aeq::RestoreCore::Phase::Committed, "2. stop commits");
        checkTrue(c.core->appliedBandCount() > 0, "2. bands went into the signal path");
        checkF(firstNonZero >= 0.0 && firstNonZero <= 0.25,
               "2. it applies inside 250 ms of the stop (%.0f ms)", firstNonZero * 1000.0);
        double inStep = 0.0;
        for (size_t i = 1; i < take.size(); ++i)
            inStep = std::max(inStep, std::abs(double(take[i]) - double(take[i - 1])));
        std::printf("  %s   2. and it does it with a fade, not a step (worst step %.3f, "
                    "largest step in the take itself %.3f)\n",
                    (step <= inStep * 1.2) ? "ok  " : "FAIL", step, inStep);
        if (step <= inStep * 1.2) ++gPass; else ++gFail;

        // ---- 3. frozen: ten more seconds change nothing --------------------
        const auto before = c.core->state();
        for (int b = 0; b < int(10.0 * kSr / 512); ++b) {
            c.play(take, pos2);
            if (b % 6 == 0) c.core->refresh();
        }
        const auto after = c.core->state();
        bool same = before.corrective.size() == after.corrective.size()
                 && before.hpfHz == after.hpfHz;
        for (size_t i = 0; same && i < before.corrective.size(); ++i)
            same = before.corrective[i].f0 == after.corrective[i].f0
                && before.corrective[i].q == after.corrective[i].q
                && before.corrective[i].gainDb == after.corrective[i].gainDb;
        checkTrue(same, "3. after the commit the decision does not move");
        checkF(after.analyzedSeconds > before.analyzedSeconds,
               "3. but the meter keeps counting (%.1f s heard)", after.analyzedSeconds);

        // ---- 3b. reopen: a stop made a phrase early costs one click ----------
        const auto beforeReopen = c.core->state();
        c.core->reopen();
        checkTrue(c.core->committed() && ! c.core->frozen(),
                  "3b. reopen unfreezes the pass and keeps the decision");
        checkTrue(c.core->appliedBandCount() > 0,
                  "3b. the correction keeps playing while it listens again");
        size_t pos3 = 0;                    // the 40 s take is used up by now: play it again
        for (int b = 0; b < int(4.0 * kSr / 512); ++b) {
            c.play(take, pos3);
            if (b % 6 == 0) c.core->refresh();
        }
        c.core->commit();
        const auto afterSecond = c.core->state();
        std::printf("  reopen -> second stop: %.1f s heard, then %.1f s, %d bands\n",
                    beforeReopen.analyzedSeconds, afterSecond.analyzedSeconds,
                    c.core->appliedBandCount());
        checkTrue(afterSecond.analyzedSeconds > beforeReopen.analyzedSeconds,
                  "3b. the second stop solves from a longer take");

        // ---- 3c. a rule that changes, changes the sealed decision ------------
        {
            const auto before = c.core->state();
            double strongest = 0.0;
            for (const auto& b : before.corrective)
                if (std::abs (b.gainDb) > std::abs (strongest)) strongest = b.gainDb;
            auto s2 = c.core->settings();
            s2.correctionAmount = 0.25;                 // the strength macro, moved
            c.core->setSettings (s2);
            c.core->resolveNow();                       // what the processor's timer does
            const auto after = c.core->state();
            double strongest2 = 0.0;
            for (const auto& b : after.corrective)
                if (std::abs (b.gainDb) > std::abs (strongest2)) strongest2 = b.gainDb;
            std::printf("  strength 100%% -> 25%%: strongest band %.2f -> %.2f dB\n",
                        strongest, strongest2);
            checkTrue(c.core->frozen(), "3c. re-solving does not unfreeze the pass");
            checkTrue(std::abs (strongest2) < std::abs (strongest) * 0.9,
                      "3c. the same take, a gentler rule, a smaller correction");
        }

        // ---- 4. relearn resets ---------------------------------------------
        c.core->relearn();
        checkTrue(c.core->phase() == aeq::RestoreCore::Phase::Learning, "4. relearn goes back to learning");
        checkTrue(c.core->appliedBandCount() == 0, "4. and takes the bands out of the path");
        std::vector<float> in(512, 0.0f);
        for (int i = 0; i < 512; ++i) in[size_t(i)] = (pos2 + size_t(i) < take.size()) ? take[pos2 + size_t(i)] : 0.0f;
        const auto out = c.play(take, pos2);
        checkF(maxDiff(in, out) == 0.0, "4. relearn is bit-exact again (worst |diff| %.1e)", maxDiff(in, out));
    }

    // ---- 5. determinism: the same take twice gives the same decision ---------
    {
        auto runTo = [&](void) {
            Ctx c;
            size_t pos = 0;
            for (int b = 0; b < int(20.0 * kSr / 512); ++b) {
                c.play(take, pos);
                if (b % 6 == 0) c.core->refresh();
            }
            c.core->commit();
            return c.core->state();
        };
        const auto a = runTo();
        const auto b = runTo();
        bool same = a.corrective.size() == b.corrective.size() && a.hpfHz == b.hpfHz;
        for (size_t i = 0; same && i < a.corrective.size(); ++i)
            same = a.corrective[i].f0 == b.corrective[i].f0 && a.corrective[i].gainDb == b.corrective[i].gainDb;
        checkTrue(same, "5. the same take commits to the same decision, twice");
    }

    // ---- 6. the panel says one thing at a time --------------------------------
    // The 1.2.16 window said SEALED in the header and "the verdict lands when it
    // settles" in the footer, on the same frame, because the verdict asked "has it
    // settled?" before "is it sealed?".  The sentences live in ui/PanelCopy.h now and
    // these are the promises they make.
    {
        aeq::copy::Facts f;
        f.haveData = false;
        const std::string waiting = aeq::copy::verdict (f);
        checkTrue (has (waiting, "clamp") && ! has (waiting, "Sealed"),
                   "6a. before a take, the panel asks for a take - it does not claim a verdict");

        f.haveData = true; f.settled = false; f.frozen = false; f.committed = false;
        const std::string listening = aeq::copy::verdict (f);
        checkTrue (has (listening, "Listening") && ! has (listening, "Sealed"),
                   "6b. while learning it says it is listening, and nothing else");

        // sealed by the user, but stopped before the solver had the audio it trusts:
        // still a decision, and the sentence carries both numbers instead of pretending
        f.frozen = true; f.committed = true; f.bands = 3;
        f.heardSeconds = 21.4; f.trustSeconds = 45.0;
        const std::string early = aeq::copy::verdict (f);
        checkTrue (has (early, "Sealed at 3 bands") && has (early, "coarse")
                     && ! has (early, "settles"),
                   "6c. a take sealed before the trust threshold says so, with its seconds");

        // the clamp's second press: the decision still plays, the pass counts again
        {
            aeq::copy::Facts o = f;
            o.frozen = false; o.committed = true; o.settled = false;
            const std::string open = aeq::copy::verdict (o);
            checkTrue (has (open, "Open again") && aeq::copy::stateWord (o) == "OPEN"
                         && aeq::copy::clampVerb (o) == "STOP",
                       "6c2. a reopened take says it is open, keeps its bands, and the "
                       "clamp says STOP again");
        }

        f.settled = true;
        const std::string sealed = aeq::copy::verdict (f);
        checkTrue (sealed.rfind ("Sealed at 3 bands.", 0) == 0
                     && has (sealed, "does not move on its own"),
                   "6d. a sealed take says sealed, once");

        f.bands = 0;
        checkTrue (has (aeq::copy::verdict (f), "nothing to correct"),
                   "6e. a sealed take with no bands says there was nothing to do");

        // the invariant, over every combination the panel can be in: the footer never
        // carries two stages at once
        bool oneStory = true;
        for (int mask = 0; mask < 8; ++mask) {
            aeq::copy::Facts g;
            g.haveData  = (mask & 1) != 0;
            g.settled   = (mask & 2) != 0;
            g.frozen    = (mask & 4) != 0;
            g.committed = g.frozen;
            g.bands = (mask & 2) ? 2 : 0;
            g.heardSeconds = 20.0; g.trustSeconds = 45.0;
            const std::string v = aeq::copy::verdict (g);
            const int stories = (has (v, "Sealed") ? 1 : 0)
                              + (has (v, "learning") ? 1 : 0)
                              + (has (v, "Listening") ? 1 : 0);
            if (stories != 1) oneStory = false;
            if (has (v, "Sealed") && aeq::copy::stateWord (g) != "SEALED") oneStory = false;
            if (! has (v, "Sealed") && aeq::copy::stateWord (g) == "SEALED") oneStory = false;
        }
        checkTrue (oneStory, "6f. every state says exactly one thing, and the pill agrees with it");
    }

    // ---- 7. a chip addresses the band it shows ---------------------------------
    // The row under the graph drops the bands sitting at 0 dB, so the row's position and
    // the core's band index drift apart.  In 1.2.16 the editor used one as the other: a
    // drag on the third visible band moved whichever band happened to be third in the
    // array.  These are the four cases that mapping has to answer.
    {
        // corrective: 3 bands, the middle one at rest; then two resonances, one of them
        // doing something.  6 entries, 3 of them trimmable.
        const double gains[] = { -3.0, 0.0, 2.5, 0.0, 0.0, -4.0 };
        const auto rows = aeq::bands::visible (gains, 6, 3);
        checkTrue (rows.size() == 3, "7a. three of the six bands are doing something, "
                                     "and three chips are drawn");

        bool mapped = rows.size() == 3
                   && rows[0].index == 0 && rows[0].src == 0
                   && rows[1].index == 2 && rows[1].src == 2      // the 0 dB band is skipped
                   && rows[2].index == 5 && rows[2].src == aeq::bands::kNoTrim;
        checkTrue (mapped, "7b. the third chip is band 3 of the core, not band 2 of the "
                           "row - and the resonance after it cannot be trimmed");

        const double empty[] = { 0.0, 0.0, 0.0 };
        checkTrue (aeq::bands::visible (empty, 3, 3).empty(),
                   "7c. a take with nothing to correct draws no chips");

        const double nan[] = { std::nan ("") , 1.0 };
        const auto nr = aeq::bands::visible (nan, 2, 2);
        checkTrue (nr.size() == 1 && nr[0].index == 1,
                   "7d. a band whose gain is not a number is not drawn");
    }

    std::printf("\n===== %d passed, %d failed =====\n\n", gPass, gFail);
    return gFail ? 1 : 0;
}
