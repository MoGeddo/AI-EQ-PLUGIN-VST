// Turns measurements into named problems with a severity, an explanation and a
// concrete action.  This is the "understanding" layer: every line the user sees
// in the report is backed by a number that can be re-measured.
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "Analyzer.h"
#include "../model/TargetCurve.h"
#include "../model/WarnThresholds.h"
#include "../model/EnvTargetCurve.h"
#include "../model/EnvThresholds.h"

namespace aeq {

enum class Action {
    None, RaiseHpf, CutBroad, CutNarrow, DeEss, AddAir, WarnOnly
};

struct Problem {
    std::string id;        // stable machine id
    std::string labelAr;   // short name shown in the UI
    std::string detailAr;  // the measured evidence, formatted
    double severity = 0.0; // 0..1
    double freq = 0.0;     // centre frequency of the action (Hz)
    double q = 1.0;
    double gainDb = 0.0;   // suggested gain for CutBroad / AddAir
    Action action = Action::None;
};

struct Measurements {
    std::vector<double> bandDb;    // absolute levels on the 1/12-oct grid
    std::vector<double> bandSdDb;
    std::vector<double> excessMean;
    std::vector<double> excessSd;
    // the same levels measured on the harmonic envelope: the shape of the vocal
    // tract with the harmonic ladder removed (Analyzer::envelopeLevelsDb)
    std::vector<double> envDb;
    std::vector<double> envSdDb;
    std::vector<int>    envFrames;  // frames in which that band was measurable
    std::vector<double> freqs;
    double sibMed = -60, sibP90 = -60, sibP99 = -60;
    double medianF0 = 0, dynamicRange = 0, peakDbfs = 0;
    double dcOffset = 0, clipRate = 0, activeFraction = 0, clipSamples = 0;
    double noiseFloorDb = -120, maxRmsDb = -120;
    int    nActive = 0;
};

inline double levelBetween(const Measurements& m, double lo, double hi) {
    double p = 0.0; int n = 0;
    for (size_t i = 0; i < m.freqs.size(); ++i)
        if (m.freqs[i] >= lo && m.freqs[i] < hi) { p += std::pow(10.0, m.bandDb[i] / 10.0); ++n; }
    return n ? 10.0 * std::log10(std::max(p / n, 1e-20)) : -120.0;
}

// how far a region sits above a *balanced* vocal (the smoothed corpus target),
// normalised at 1 kHz.  This is what "your voice is 4 dB too boomy" means.
// A monophonic voice carries almost no energy below its fundamental, so a fixed
// band that reaches under 1.5*F0 measures the singer's register, not the mix.
// Measured on the 15 mixes that *define* the target: the 110-260 Hz "boom" test
// fires on 6 of them, and the number splits 16 dB between the songs whose
// fundamental sits inside the band (+3.5 dB) and those above it (-12.4 dB),
// r = -0.76 against F0.  None of that is a mixing fault, so the window is
// clamped to the measurable part of the spectrum and a band with less than a
// third of an octave left is not scored at all.
inline double excessVsTarget(const Measurements& m, double lo, double hi) {
    if (m.medianF0 > 0.0) lo = std::max(lo, 1.5 * m.medianF0);
    double s = 0.0; int n = 0;
    const double ref = levelBetween(m, 900.0, 1150.0);
    for (size_t i = 0; i < m.freqs.size(); ++i) {
        const double f = m.freqs[i];
        if (f < lo || f >= hi) continue;
        if (!TargetCurve::inMeasuredRange(f)) continue;
        s += (m.bandDb[i] - ref) - TargetCurve::atSmoothed(f, 0.5);
        ++n;
    }
    return n >= 4 ? s / n : -50.0;   // -50 = "not measurable", never a problem
}

// -------------------------------------------------------------------------
// The envelope domain.
//
// `bandDb` is what the analyser measures on the raw spectrum: a comb.  A 1/12-
// octave band around 300 Hz is ~18 Hz wide and the harmonics of a 250 Hz voice
// are 250 Hz apart, so *where the teeth happen to fall* moves that band by up to
// 32 dB (measured, docs/14).  `envDb` is the same band on the cepstral envelope -
// the vocal-tract curve with the ladder removed - which moves 3 dB for the same
// pitch change.
//
// It is not a free swap: an envelope level is a different coordinate from a raw
// level (about 14 dB lower at 200 Hz, 0.5 dB lower at 1.5 kHz on our own corpus),
// so it is compared with EnvTargetCurve - a target built from the same 15 mixes
// *in this domain* - and never with TargetCurve.
// -------------------------------------------------------------------------
inline bool envUsable(const Measurements& m, size_t i) {
    // -120 is the analyser's explicit "this band was never measurable" marker
    // (a monophonic voice has no energy below its fundamental).  It is NOT a
    // level threshold: the envelope lives on its own absolute scale - around
    // -74 dB on a real take whose raw bands read -30 - because it averages ln
    // power per bin rather than summing bin power.  Only the shape is comparable,
    // which is why every use below is re a 1 kHz reference.
    if (i >= m.envDb.size() || m.envDb[i] <= -119.0) return false;
    if (i < m.envFrames.size() && m.nActive > 0 && m.envFrames[i] < m.nActive / 2) return false;
    if (!EnvTargetCurve::definedAt(m.freqs[i])) return false;
    return true;
}

// the envelope is already a log curve, so the band value is the mean of the log
// values (the C++ analyser and the Python reference both do exactly this)
inline double envLevelBetween(const Measurements& m, double lo, double hi) {
    double s = 0.0; int n = 0;
    for (size_t i = 0; i < m.freqs.size(); ++i) {
        const double f = m.freqs[i];
        if (f < lo || f >= hi || !envUsable(m, i)) continue;
        s += m.envDb[i]; ++n;
    }
    return n ? s / n : -120.0;
}

// Same arithmetic as excessVsTarget, in the envelope domain: level re 1 kHz minus
// the corpus envelope target.  -50 means "not measurable", which is never a fault.
inline double excessVsTargetEnv(const Measurements& m, double lo, double hi) {
    if (m.medianF0 > 0.0) lo = std::max(lo, 0.9 * m.medianF0);
    const double ref = envLevelBetween(m, 900.0, 1150.0);
    if (ref <= -119.0) return -50.0;
    double s = 0.0; int n = 0;
    for (size_t i = 0; i < m.freqs.size(); ++i) {
        const double f = m.freqs[i];
        if (f < lo || f >= hi || !envUsable(m, i)) continue;
        s += (m.envDb[i] - ref) - EnvTargetCurve::at(f);
        ++n;
    }
    return n >= 4 ? s / n : -50.0;
}

// Mean of (envelope level re 1 kHz, minus the corpus target) over a window.  The
// envelope is usable down to 0.9*F0, which is the whole reason this can exist.
inline double envRelMean(const Measurements& m, double lo, double hi, int* used = nullptr) {
    const double ref = envLevelBetween(m, 900.0, 1150.0);
    double s = 0.0; int n = 0;
    if (ref > -119.0) {
        for (size_t i = 0; i < m.freqs.size(); ++i) {
            const double f = m.freqs[i];
            if (f < lo || f >= hi || !envUsable(m, i)) continue;
            s += (m.envDb[i] - ref) - EnvTargetCurve::at(f);
            ++n;
        }
    }
    if (used) *used = n;
    return n ? s / n : -50.0;
}

// -------------------------------------------------------------------------
// "Boom" cannot be a fixed band.
//
// The shipped detector scored 110-260 Hz after clamping the region to 1.5*F0.
// On the 15 reference mixes the median F0 is 197-350 Hz, so the clamp removed
// the *entire* region every time: the detector has never fired once, on anything,
// and it looked like an absence of boomy voices rather than a blind measurement
// (0/15 measurable - audited in analysis/scripts/audit_decisions.py).
//
// The fix is to stop naming frequencies, because a monophonic voice has no energy
// below its own fundamental.  Instead: how heavy is the bottom of THIS voice
// compared with its own body?
//
//     tilt = mean(deviation over 0.9-2.5 F0) - mean(deviation over 3-8 F0)
//
// both windows are multiples of the singer's fundamental, so the statistic cannot
// move when the singer changes register, and each window is compared with the
// corpus target *at its own frequencies*.  This is the same idea as the
// pitch-tracked bands in the Antares EQ, used the other way round: they move an EQ
// band with the voice, this moves a *measurement* window with it.
// -------------------------------------------------------------------------
inline double weightTilt(const Measurements& m, int* lowBands = nullptr, int* highBands = nullptr) {
    const double f0 = m.medianF0;
    if (!(f0 > 55.0 && f0 < 700.0)) { if (lowBands) *lowBands = 0; if (highBands) *highBands = 0; return -50.0; }
    int nl = 0, nh = 0;
    const double low = envRelMean(m, 0.9 * f0, 2.5 * f0, &nl);
    const double high = envRelMean(m, 3.0 * f0, 8.0 * f0, &nh);
    if (lowBands) *lowBands = nl;
    if (highBands) *highBands = nh;
    if (low <= -49.0 || high <= -49.0) return -50.0;
    return low - high;
}

// -------------------------------------------------------------------------
// Per-band z, smoothed the way a real fault is shaped.
//
// z(f) = (envelope level - target) / corpus spread, band by band.  On its own a
// per-band test is close to useless: the corpus spread is ~1 dB, a +3 dB fault
// gives z ~= 3, and with 71 bands x 15 takes a pure threshold of 3 fires by
// chance (multiple comparisons).  But an EQ fault is *smooth in frequency* - a
// bell or a shelf lifts several neighbouring bands by the same amount - while the
// corpus noise is per-band and independent.  Averaging z over a window matched to
// the width of a real fault divides the noise by sqrt(n) and leaves the fault
// standing: 3 dB against 1 dB/sqrt(7) ~= 0.38 dB is z ~= 8.
//
// So the statistic is a matched filter, not a threshold on one band.
// -------------------------------------------------------------------------
struct ZScan {
    std::vector<double> z;        // raw per-band z (0 where not measurable)
    std::vector<double> smoothed; // matched-filtered
    std::vector<bool>   usable;
    int peakBand = -1;
    double peakZ = 0.0;
};

inline ZScan scanZ(const Measurements& m, int halfBands = 3) {
    ZScan s;
    const size_t n = m.freqs.size();
    s.z.assign(n, 0.0);
    s.usable.assign(n, false);
    const double ref = envLevelBetween(m, 900.0, 1150.0);
    for (size_t i = 0; i < n; ++i) {
        if (ref <= -119.0 || !envUsable(m, i)) continue;
        const double sd = EnvTargetCurve::sdAt(m.freqs[i]);
        if (!(sd > 0.15) || sd > 50.0) continue;       // no usable spread, no z
        s.z[i] = ((m.envDb[i] - ref) - EnvTargetCurve::at(m.freqs[i])) / sd;
        s.usable[i] = true;
    }
    s.smoothed.assign(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        if (!s.usable[i]) continue;
        double acc = 0.0, wsum = 0.0;
        for (int k = -halfBands; k <= halfBands; ++k) {
            const long j = long(i) + k;
            if (j < 0 || j >= long(n) || !s.usable[j]) continue;
            // triangle kernel: weights 1, 2/3, 1/3 - a smooth fault keeps its shape,
            // a single stray band is diluted instead of being amplified
            const double w = 1.0 - std::abs(double(k)) / (halfBands + 1.0);
            acc += w * s.z[j]; wsum += w;
        }
        s.smoothed[i] = acc / std::max(wsum, 1e-9);
    }
    for (size_t i = 1; i + 1 < n; ++i) {
        if (!s.usable[i]) continue;
        if (s.smoothed[i] <= s.smoothed[i - 1] || s.smoothed[i] <= s.smoothed[i + 1]) continue;
        if (s.smoothed[i] > s.peakZ) { s.peakZ = s.smoothed[i]; s.peakBand = int(i); }
    }
    return s;
}

inline double excessBetween(const Measurements& m, double lo, double hi) {
    double s = 0.0; int n = 0;
    for (size_t i = 0; i < m.freqs.size(); ++i)
        if (m.freqs[i] >= lo && m.freqs[i] < hi) { s += m.excessMean[i]; ++n; }
    return n ? s / n : 0.0;
}

class ProblemDetector {
public:
    std::vector<Problem> detect(const Measurements& m) const {
        std::vector<Problem> out;
        if (m.nActive < 40) return out;

        // ---- 1. rumble / sub energy -------------------------------------
        // The bar is measured, not chosen: WarnThresholds::rumble is the midpoint between
        // the worst 30-70 Hz excess in his own 15 mixes (-31.3 dB) and the best a 40 Hz
        // hum does to them (-13.8 dB), so his material cannot trip it and a hum cannot
        // hide under it.
        const double rumble = levelBetween(m, 30, 70) - levelBetween(m, 300, 700);
        if (rumble > WarnThresholds::rumble) {
            out.push_back({"rumble", "ترددات تحت صوت الغناء",
                           fmt("طاقة 30–70 Hz أعلى من الطبيعي بـ %.0f dB", {rumble - WarnThresholds::rumble}),
                           sev(rumble, WarnThresholds::rumble, WarnThresholds::rumble + 18.0),
                           0, 1, 0, Action::RaiseHpf});
        }
        if (std::abs(m.dcOffset) > 0.002) {
            out.push_back({"dc", "إزاحة DC",
                           fmt("متوسط الإشارة %.3f (المفروض أصفار)", {m.dcOffset}),
                           sev(std::abs(m.dcOffset), 0.002, 0.02), 0, 1, 0, Action::RaiseHpf});
        }

        // ---- 2. broadband excess regions --------------------------------
        // `env` = score this region on the harmonic envelope instead of the raw
        // spectrum.  Only two regions qualify, and both were measured, not assumed
        // (analysis/out/amir15/env_target.json):
        //   520-850 Hz: the envelope's own spread over the 15 mixes is 1.35 dB
        //               against 4.35 dB raw, so it can see a 1.5 dB problem the raw
        //               measurement is structurally blind to.
        //   260-520 Hz: 2.02 dB against 4.74 dB.
        // Everywhere else the raw spectrum stays: below 260 Hz the corpus cannot
        // measure the envelope at all (no harmonic energies down there for these
        // 15 voices), 850-1300 Hz holds the 1 kHz reference so a fault there
        // cancels in both domains, and above 2 kHz the raw spectrum is the more
        // stable of the two (docs/14 section 5.2).
        enum class Mode { Raw, Env, Tilt };
        struct Region { const char* id; const char* label; double lo, hi, thr; Mode mode; };
        // Thresholds are the engineer's own natural variation, not taste.  Each
        // one is a measured standard deviation of that region across the 15
        // mixes that define the target (analysis/out/amir15/tolerance.json), so
        // a region has to leave his own spread before it can be called a fault.
        // The old fixed 2.2 dB sat *below* that spread (3.04 dB median per band)
        // and flagged the target material itself 6 times out of 15.
        // Only the regions whose bar is *measured* live here.  `harsh` (2-4.2 kHz) and
        // `nasal` (850-1300 Hz) used to sit in this table with a typed 2.2 / 2.5 dB, and
        // on his 15 dry stems the harsh one fired on a released Samira Said mix: the 2-4k
        // balance of his own material spans 8 dB, and a +8 dB bell - an obviously harsh
        // voice - only reaches the same place from the other end.  The two are still
        // measured and printed (the report's `EXCESS` lines and `STAT harshslope` /
        // `STAT nasalslope`); what they no longer do is call a record wrong.
        const Region regions[] = {
            {"boom",  "تخنة (Boom)",           0,   0, EnvThresholds::boom_tilt, Mode::Tilt},  // pitch-relative
            {"mud",   "عكارة (Mud)",         260, 520, EnvThresholds::mud, Mode::Env},   // corpus max +4.88
            {"box",   "صندوقية (Box)",       520, 850, EnvThresholds::box, Mode::Env},   // corpus max +2.54
        };
        for (const auto& r : regions) {
            double ex = 0.0, fc = 0.0, q = 1.0;
            std::string detail;
            switch (r.mode) {
                case Mode::Raw:
                    ex = excessVsTarget(m, r.lo, r.hi);
                    detail = fmt("زيادة %.1f dB في %.0f–%.0f Hz", {ex, r.lo, r.hi});
                    break;
            case Mode::Env:
                    ex = excessVsTargetEnv(m, r.lo, r.hi);
                    detail = fmt("زيادة %.1f dB في %.0f–%.0f Hz (على مظروف الصوت)", {ex, r.lo, r.hi});
                    break;
                case Mode::Tilt: {
                    int nl = 0, nh = 0;
                    ex = weightTilt(m, &nl, &nh);
                    fc = m.medianF0 > 0 ? 1.5 * m.medianF0 : 150.0;
                    q  = 0.8;
                    // the action is a broad shelf under the first formant, not a bell:
                    // the fault is that the whole bottom is heavy, not one band
                    detail = fmt("القاع أثقل من الجسم بـ %.1f dB (نسبي لطبقة %.0f Hz: تحت %.0f Hz أول %.0f هارمونيك)",
                                 {ex, m.medianF0, 2.5 * m.medianF0, 2.5});
                    break;
                }
            }
            if (r.mode == Mode::Raw || r.mode == Mode::Env) {
                fc = std::sqrt(r.lo * r.hi);
                q  = std::max(fc / (r.hi - r.lo), 0.5);
            }
            if (ex > r.thr) {
                Problem p{r.id, r.label, detail,
                          sev(ex, r.thr, r.thr + 5.0), fc, q,
                          -std::min(ex * 0.75, 6.0), Action::CutBroad};
                if (r.mode == Mode::Tilt) p.gainDb = -std::min(ex * 0.6, 4.0);
                out.push_back(p);
            } else if (r.mode != Mode::Raw && ex > -49.0 && ex > r.thr * 0.7) {
                // near-miss on the envelope: reported as evidence, not as an action.
                // The point of a tighter measurement is that we can *say* "this is
                // 0.2 dB under the bar" instead of showing nothing at all.
                out.push_back({std::string(r.id) + "_near", std::string(r.label) + " (قريب من الحد)",
                               fmt("انحراف %.1f dB من أصل %.1f مسموح", {ex, r.thr}),
                               sev(ex, r.thr * 0.7, r.thr), fc, 1.0, 0.0, Action::None});
            }
        }

        // ---- 3. static narrow resonances -------------------------------
        // A room/mic resonance is a *fixed* frequency: high mean excess with low
        // frame-to-frame variation, isolated from its neighbours.
        //
        // The bar is 4.5 dB, not 3.0: the per-band spread across the 15 mixes
        // that define the target is 3.04 dB (median), so a 3 dB "peak" is just
        // the engineer's own variation - and at 3.0 dB this loop produced 16
        // notches over 15 target songs, with 6089 and 6451 Hz each recurring in
        // 4 different songs (different singers, so not a room; it is a fixed
        // artefact of the source separation).
        //
        // And the bar was applied to a *comb*: a sung note is not a smooth spectrum,
        // it is a picket fence of harmonics, and on the 1/12-octave grid each tooth
        // of that fence is an isolated 4.5 dB peak with stable statistics.  The
        // detector produced 20 such "resonances" in one verse of the test take -
        // 202, 214, 359, 381, 403, 427, 570, 604, 761, 806 Hz ... - and every one of
        // them was the singer.  Below 1.1 kHz (six harmonics of a 190 Hz voice) the
        // teeth are wider than the analysis band, so nothing there can be told apart
        // from a partial; above it, a candidate that sits on the harmonic grid is
        // skipped as well.  What survives is a peak that is *not* where the notes put
        // their energy - which is what a room resonance is.
        std::vector<Problem> resoFound;
        for (int i = 3; i + 3 < static_cast<int>(m.freqs.size()); ++i) {
            const double f = m.freqs[i];
            if (f < std::max(1100.0, 6.0 * m.medianF0) || f > 12000.0) continue;
            const double ex = m.excessMean[i];
            if (ex < 4.5) continue;
            if (m.medianF0 > 2.0) {                       // on a partial, not a resonance
                const double h = std::round(f / m.medianF0);
                if (h >= 1.0 && std::abs(f / m.medianF0 - h) / h < 0.03) continue;
            }
            // A room / mic resonance is *narrow* and *fixed*: it must stand above
            // its own 1/4-octave shoulders (sharpness) and stay put over time
            // (the shoulders follow the formants of the take, the spike does not).
            const double shoulder = 0.5 * (m.excessMean[i - 3] + m.excessMean[i + 3]);
            const bool sharp = (ex - shoulder) > 2.0;
            const bool stable = m.excessSd[i] < std::max(3.0, 0.9 * ex);
            if (sharp && stable) {
                resoFound.push_back({"reso", "رنين ثابت (غرفة/مايك)",
                                     fmt("%.0f Hz ثابت على مدى الأداء (متوسط +%.1f dB، تغيّر %.1f dB)", {f, ex, m.excessSd[i]}),
                                     sev(ex, 4.5, 9.0), f, 8.0, -std::min(ex * 0.8, 7.0), Action::CutNarrow});
            }
        }
        // Only the three strongest are worth saying out loud.  A list of twenty
        // "problems" on a clean take is not information, it is noise - and the user
        // reads it as the plugin not knowing what it is doing, which was not wrong.
        std::sort(resoFound.begin(), resoFound.end(),
                  [](const Problem& a, const Problem& b) { return a.severity > b.severity; });
        for (size_t i = 0; i < resoFound.size() && i < 3; ++i) out.push_back(resoFound[i]);

        // ---- 4. sibilance (dynamic, not static) -------------------------
        // Sibilance is measured three ways (median, p90, p99 - see `sibilance()` in
        // Analyzer.h) and none of them is a verdict: his own 15 mixes span 26 dB of the
        // excess, and 200 injected bursts at +20 dB do not reach the top of that range.
        // The de-esser decides from the take itself in RestoreCore
        // (WarnThresholds::deEssAuto), which is the only place where being wrong costs a
        // decibel rather than a wrong claim.

        // ---- 5. dull / missing air -------------------------------------
        // "Dull" is measured inside the take.  The distance from the target curve was the
        // old statistic and his own mixes span 6.9 dB of it, while a 7 kHz low-pass moves
        // it 7.3 dB - the two ranges overlap (a known fault clears it by 0.2 dB), so no
        // bar exists.  The slope from 8-12k down to 2-4k is the same property measured
        // against the take instead of against a curve: his own range is 7.8 dB wide and
        // the same fault moves it 10.6 dB, which is daylight.
        const double airSlope = levelBetween(m, 8000, 12000) - levelBetween(m, 2000, 4000);
        if (airSlope < WarnThresholds::airSlope) {
            // only if the content above 10 kHz still exists (not codec-limited)
            const double top = levelBetween(m, 12000, 16000) - levelBetween(m, 7000, 10000);
            if (top > -18.0) {
                const double miss = WarnThresholds::airSlope - airSlope;
                out.push_back({"dull", "نقص في الأعلى",
                               fmt("المنطقة 8–12k أقل من 2–4k بـ %.1f dB عن الحد (%.1f)",
                                   {miss, WarnThresholds::airSlope}),
                               sev(-airSlope, -WarnThresholds::airSlope,
                                   -WarnThresholds::airSlope + 8.0), 9000.0, 0.7,
                               std::min(miss * 0.5, 4.0), Action::AddAir});
            }
        }

        // ---- 6. things EQ must not pretend to fix ----------------------
        if (m.clipRate > 1e-6) {
            out.push_back({"clip", "قصّ (Clipping)",
                           fmt("%.0f عيّنة عند الحد الأقصى - قلّل الدخل قبل أي EQ", {(double) m.clipSamples}),
                           sev(m.clipRate, 1e-6, 1e-4), 0, 1, 0, Action::WarnOnly});
        }
        if (m.noiseFloorDb > WarnThresholds::noiseFloor
            && m.maxRmsDb > WarnThresholds::loudEnough) {
            out.push_back({"noise", "أرضية تشويش عالية",
                           fmt("أرضية الصمت %.0f dBFS — الـEQ مش هيشيلها (بوابة/تنظيف مطلوب)", {m.noiseFloorDb}),
                           sev(m.noiseFloorDb, WarnThresholds::noiseFloor,
                               WarnThresholds::noiseFloor + 17.0), 0, 1, 0,
                           Action::WarnOnly});
        }
        const double hfCut = levelBetween(m, 14000, 16000) - levelBetween(m, 9000, 11000);
        if (hfCut < -20.0) {
            out.push_back({"codec", "الصوت مقطوع في الأعلى",
                           fmt("تراجع %.0f dB فوق 14 kHz — مفيش محتوى، متزوّدش air", {hfCut}),
                           sev(-hfCut, 20, 40), 0, 1, 0, Action::WarnOnly});
        }
        return out;
    }

private:
    static double sev(double v, double lo, double hi) {
        return std::clamp((v - lo) / std::max(hi - lo, 1e-9), 0.0, 1.0);
    }
    // printf-style, with the argument count taken from the format itself.
    //
    // Two bugs lived here and this is the fix for both:
    //  (a) "%%" is an escaped percent, not a placeholder.  The old loop skipped
    //      the first '%' and counted the second, so the de-esser message - which
    //      contains "10%%" - asked for 2 arguments while being given 1, and fmt
    //      read past the end of the initializer list.  g++ printed garbage; MSVC
    //      crashed the gate binary before a single line of output was flushed, so
    //      CI reported "the DSP gates did not pass" with no evidence at all.
    //      Found with -fsanitize=address.
    //  (b) even with correct counting, a future mismatch would do the same thing.
    //      The count is now clamped to the arguments actually supplied.
    static int placeholders(const char* f) {
        int n = 0;
        for (const char* p = f; *p; ++p) {
            if (*p != '%') continue;
            if (p[1] == '%') { ++p; continue; }   // skip both characters of %%
            ++n;
        }
        return n;
    }
    // The report text is Arabic with numbers in it, so it is a printf format plus
    // an argument list.  Getting that wrong is not cosmetic: this used to have a
    // switch with cases 1..3 only, so a string with four placeholders fell through
    // to "%s" and the window printed
    //     القاع أثقل من الجسم بـ % .1f dB (نسبي لطبقة % .0f Hz ...)
    // verbatim - the literal format string, straight into the report cards.
    //
    // Passing extra arguments to snprintf is harmless (they are unused); passing
    // too few is undefined.  So the argument list is always padded to four and the
    // count is asserted rather than trusted.
    static std::string fmt(const char* f, std::initializer_list<double> a) {
        char buf[512];
        double v[4] = { 0.0, 0.0, 0.0, 0.0 };
        int i = 0;
        for (double d : a) { if (i < 4) v[i] = d; ++i; }
        const int needed = placeholders(f);
        // (no jassert here: this header is compiled without JUCE, by the gates)
        if (needed > 4 || needed > i) {
            std::snprintf (buf, sizeof (buf), "%s", f);   // rather than undefined behaviour
            return std::string (buf);
        }
        std::snprintf (buf, sizeof (buf), f, v[0], v[1], v[2], v[3]);
        return std::string (buf);
    }
};

} // namespace aeq
