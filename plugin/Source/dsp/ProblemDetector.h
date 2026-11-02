// Turns measurements into named problems with a severity, an explanation and a
// concrete action.  This is the "understanding" layer: every line the user sees
// in the report is backed by a number that can be re-measured.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "Analyzer.h"
#include "../model/TargetCurve.h"
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
    double sibMed = -60, sibP90 = -60;
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
        const double rumble = levelBetween(m, 30, 70) - levelBetween(m, 300, 700);
        if (rumble > -34.0) {
            out.push_back({"rumble", "ترددات تحت صوت الغناء",
                           fmt("طاقة 30–70 Hz أعلى من الطبيعي بـ %.0f dB", {rumble + 34.0}),
                           sev(rumble, -34, -18), 0, 1, 0, Action::RaiseHpf});
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
        struct Region { const char* id; const char* label; double lo, hi, thr; bool env; };
        // Thresholds are the engineer's own natural variation, not taste.  Each
        // one is a measured standard deviation of that region across the 15
        // mixes that define the target (analysis/out/amir15/tolerance.json), so
        // a region has to leave his own spread before it can be called a fault.
        // The old fixed 2.2 dB sat *below* that spread (3.04 dB median per band)
        // and flagged the target material itself 6 times out of 15.
        const Region regions[] = {
            {"boom",  "تخنة (Boom)",        110, 260, 2.5, false},  // SD 11 dB, F0-driven - see clamp
            {"mud",   "عكارة (Mud)",         260, 520, EnvThresholds::mud, true},  // corpus max +4.88
            {"box",   "صندوقية (Box)",       520, 850, EnvThresholds::box, true},  // corpus max +2.54
            {"nasal", "خنّة (Nasal)",        850, 1300, 2.5, false}, // SD 1.28 (this band holds the 1 kHz reference)
            {"harsh", "حدّة (Harsh)",       2000, 4200, 2.2, false}, // SD 2.06 - keep, it is the one region that behaves
        };
        for (const auto& r : regions) {
            const double ex = r.env ? excessVsTargetEnv(m, r.lo, r.hi)
                                    : excessVsTarget(m, r.lo, r.hi);
            if (ex > r.thr) {
                const double fc = std::sqrt(r.lo * r.hi);
                const double q  = fc / (r.hi - r.lo);
                Problem p{r.id, r.label,
                          r.env ? fmt("زيادة %.1f dB في %.0f–%.0f Hz (على مظروف الصوت)",
                                      {ex, r.lo, r.hi})
                                : fmt("زيادة %.1f dB في %.0f–%.0f Hz", {ex, r.lo, r.hi}),
                          sev(ex, r.thr, r.thr + 5.0), fc, std::max(q, 0.5),
                          -std::min(ex * 0.75, 6.0), Action::CutBroad};
                out.push_back(p);
            } else if (r.env && ex > -49.0 && ex > r.thr * 0.7) {
                // near-miss on the envelope: reported as evidence, not as an action.
                // The point of a tighter measurement is that we can *say* "this is
                // 0.2 dB under the bar" instead of showing nothing at all.
                out.push_back({std::string(r.id) + "_near", std::string(r.label) + " (قريب من الحد)",
                               fmt("انحراف %.1f dB من أصل %.1f مسموح (مظروف)", {ex, r.thr}),
                               sev(ex, r.thr * 0.7, r.thr), std::sqrt(r.lo * r.hi), 1.0,
                               0.0, Action::None});
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
        for (int i = 3; i + 3 < static_cast<int>(m.freqs.size()); ++i) {
            const double f = m.freqs[i];
            if (f < 200.0 || f > 12000.0) continue;
            const double ex = m.excessMean[i];
            if (ex < 4.5) continue;
            // A room / mic resonance is *narrow* and *fixed*: it must stand above
            // its own 1/4-octave shoulders (sharpness) and stay put over time
            // (the shoulders follow the formants of the take, the spike does not).
            const double shoulder = 0.5 * (m.excessMean[i - 3] + m.excessMean[i + 3]);
            const bool sharp = (ex - shoulder) > 2.0;
            const bool stable = m.excessSd[i] < std::max(3.0, 0.9 * ex);
            if (sharp && stable) {
                out.push_back({"reso", "رنين ثابت (غرفة/مايك)",
                               fmt("%.0f Hz ثابت على مدى الأداء (متوسط +%.1f dB، تغيّر %.1f dB)", {f, ex, m.excessSd[i]}),
                               sev(ex, 4.5, 9.0), f, 8.0, -std::min(ex * 0.8, 7.0), Action::CutNarrow});
            }
        }

        // ---- 4. sibilance (dynamic, not static) -------------------------
        if (m.sibP90 > -6.0) {
            out.push_back({"sib", "حروف سِيبيلانت قوية",
                           fmt("5-10k / 1-4k عند أعلى 10%% = %.1f dB - القمم محتاجة de-esser ديناميكي", {m.sibP90}),
                           sev(m.sibP90, -6.0, 0.0), 7200.0, 1.0,
                           -std::min((m.sibP90 + 6.0) * 1.2, 6.0), Action::DeEss});
        }

        // ---- 5. dull / missing air -------------------------------------
        const double airMiss = TargetCurve::at(9000.0) - (levelBetween(m, 7000, 12000) - levelBetween(m, 1000, 2000));
        if (airMiss > 4.0) {
            // only if the content above 10 kHz still exists (not codec-limited)
            const double top = levelBetween(m, 12000, 16000) - levelBetween(m, 7000, 10000);
            if (top > -18.0) {
                out.push_back({"dull", "نقص في الأعلى",
                               fmt("المنطقة 7–12k أقل من الهدف بـ %.1f dB", {airMiss}),
                               sev(airMiss, 4.0, 12.0), 9000.0, 0.7,
                               std::min(airMiss * 0.5, 4.0), Action::AddAir});
            }
        }

        // ---- 6. things EQ must not pretend to fix ----------------------
        if (m.clipRate > 1e-6) {
            out.push_back({"clip", "قصّ (Clipping)",
                           fmt("%.0f عيّنة عند الحد الأقصى - قلّل الدخل قبل أي EQ", {(double) m.clipSamples}),
                           sev(m.clipRate, 1e-6, 1e-4), 0, 1, 0, Action::WarnOnly});
        }
        if (m.noiseFloorDb > -72 && m.maxRmsDb > -30) {
            out.push_back({"noise", "أرضية تشويش عالية",
                           fmt("أرضية الصمت %.0f dBFS — الـEQ مش هيشيلها (بوابة/تنظيف مطلوب)", {m.noiseFloorDb}),
                           sev(m.noiseFloorDb, -72, -55), 0, 1, 0, Action::WarnOnly});
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
    // printf-style, with the argument count taken from the format itself
    static int placeholders(const char* f) {
        int n = 0;
        for (const char* p = f; *p; ++p) if (*p == '%' && p[1] != '%') ++n;
        return n;
    }
    static std::string fmt(const char* f, std::initializer_list<double> a) {
        char buf[256];
        const double* v = a.begin();
        switch (placeholders(f)) {
            case 3: std::snprintf(buf, sizeof(buf), f, v[0], v[1], v[2]); break;
            case 2: std::snprintf(buf, sizeof(buf), f, v[0], v[1]); break;
            case 1: std::snprintf(buf, sizeof(buf), f, v[0]); break;
            default: std::snprintf(buf, sizeof(buf), "%s", f); break;
        }
        return std::string(buf);
    }
};

} // namespace aeq
