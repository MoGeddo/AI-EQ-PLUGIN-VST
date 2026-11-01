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
inline double excessVsTarget(const Measurements& m, double lo, double hi) {
    double s = 0.0; int n = 0;
    const double ref = levelBetween(m, 900.0, 1150.0);
    for (size_t i = 0; i < m.freqs.size(); ++i) {
        const double f = m.freqs[i];
        if (f < lo || f >= hi) continue;
        if (!TargetCurve::inMeasuredRange(f)) continue;
        s += (m.bandDb[i] - ref) - TargetCurve::atSmoothed(f, 0.5);
        ++n;
    }
    return n ? s / n : 0.0;
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
        struct Region { const char* id; const char* label; double lo, hi, thr; };
        const Region regions[] = {
            {"boom",  "تخنة (Boom)",        110, 260, 2.5},
            {"mud",   "عكارة (Mud)",         260, 520, 2.2},
            {"box",   "صندوقية (Box)",       520, 850, 2.2},
            {"nasal", "خنّة (Nasal)",        850, 1300, 2.5},
            {"harsh", "حدّة (Harsh)",       2000, 4200, 2.2},
        };
        for (const auto& r : regions) {
            const double ex = excessVsTarget(m, r.lo, r.hi);
            if (ex > r.thr) {
                const double fc = std::sqrt(r.lo * r.hi);
                const double q  = fc / (r.hi - r.lo);
                out.push_back({r.id, r.label,
                               fmt("زيادة %.1f dB في %.0f–%.0f Hz", {ex, r.lo, r.hi}),
                               sev(ex, r.thr, r.thr + 5.0), fc, std::max(q, 0.5),
                               -std::min(ex * 0.75, 6.0), Action::CutBroad});
            }
        }

        // ---- 3. static narrow resonances -------------------------------
        // A room/mic resonance is a *fixed* frequency: high mean excess with low
        // frame-to-frame variation, isolated from its neighbours.
        for (int i = 3; i + 3 < static_cast<int>(m.freqs.size()); ++i) {
            const double f = m.freqs[i];
            if (f < 200.0 || f > 12000.0) continue;
            const double ex = m.excessMean[i];
            if (ex < 3.0) continue;
            // A room / mic resonance is *narrow* and *fixed*: it must stand above
            // its own 1/4-octave shoulders (sharpness) and stay put over time
            // (the shoulders follow the formants of the take, the spike does not).
            const double shoulder = 0.5 * (m.excessMean[i - 3] + m.excessMean[i + 3]);
            const bool sharp = (ex - shoulder) > 1.5;
            const bool stable = m.excessSd[i] < std::max(3.0, 0.9 * ex);
            if (sharp && stable) {
                out.push_back({"reso", "رنين ثابت (غرفة/مايك)",
                               fmt("%.0f Hz ثابت على مدى الأداء (متوسط +%.1f dB، تغيّر %.1f dB)", {f, ex, m.excessSd[i]}),
                               sev(ex, 3.0, 8.0), f, 8.0, -std::min(ex * 0.8, 7.0), Action::CutNarrow});
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
