// Profile.h - a measured spectrum you can keep, weight, and compare against.
//
// WHAT THIS IS AND WHY IT EXISTS
// ------------------------------
// Three of the four reference products scan the same way, and it is not the way this
// plugin scanned until now:
//
//   iZotope Match EQ     two profiles - *source* (your vocal) and *reference* (a record
//                        you want to sound like) - both measured by the same analyser,
//                        both normalised to their own level, and the EQ is the difference
//                        between them.  The reference can be learned once and reused.
//   sonible smart:EQ 4   one to three references per group, *weighted*, blended into one
//                        target group profile, then matched by tonal balance.
//   Waves Curves AQ      the learned curve is an object that is kept, not a decision that
//                        is thrown away when the pass ends.
//
// What this plugin did instead: it measured the take with the in-plugin analyser (1/12
// octave, 42.7 ms windows, gated frames) and compared it against a corpus table that was
// produced by a *different* program (1/3-octave, register-corrected, smoothed to 1/6,
// resampled).  Two different instruments, two different statistics - the same class of
// error that made the stabiliser cut 6 dB on a quiet take (see GATES G71), only at the
// corpus boundary where nobody could see it.
//
// A Profile is the fix: a spectrum measured by *this* analyser, in *this* frame, that can
// be saved, reloaded, weighted and subtracted.  When one is loaded, Match mode stops
// chasing the corpus average and chases the thing the user actually asked for.
//
// THE THREE OPERATIONS THAT MATTER
//   normalised()  each profile is expressed relative to its own 1 kHz, so the overall
//                 level of the record it came from cannot bias the shape.  This is what
//                 makes "sound like this record" a question about tone, not loudness.
//   blend()       references combine in the power domain with weights (smart:EQ's group
//                 idea): 3 parts of one record and 1 part of another is a well-defined
//                 target, and 100/0 is exactly the first profile, not approximately it.
//   delta()       reference - source, smoothed and limited.  This is the EQ curve Match EQ
//                 draws: computed in one frame, so it is exact and checkable.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>        // std::atof / std::atol, which the parser uses on the header lines
#include <string>
#include <vector>

namespace aeq {

struct Profile {
    std::vector<double> freqs;   // the analyser grid, Hz, rising
    std::vector<double> db;      // band level, dBFS RMS
    double loudDb = -30.0;       // the take's loudness, for the record
    long   frames = 0;           // how many analyser frames it was averaged over
    std::string name;            // what to call it on the panel

    bool valid() const {
        return freqs.size() >= 8 && freqs.size() == db.size();
    }

    // linear in log2(f), flat outside the measured range - the same convention the
    // solver and the target curve already use, so nothing has to be resampled.
    double at(double f) const {
        if (! valid()) return 0.0;
        if (f <= freqs.front()) return db.front();
        if (f >= freqs.back())  return db.back();
        for (size_t i = 1; i < freqs.size(); ++i)
            if (freqs[i] >= f) {
                const double t = std::log2(f / freqs[i - 1]) / std::log2(freqs[i] / freqs[i - 1]);
                return db[i - 1] * (1.0 - t) + db[i] * t;
            }
        return db.back();
    }

    // The same curve with its own level divided out: every value is dB relative to this
    // profile's 1 kHz.  A profile that is 6 dB louder is the same shape.
    Profile normalised() const {
        Profile p = *this;
        if (! valid()) return p;
        const double ref = at(1000.0);
        for (auto& v : p.db) v -= ref;
        p.loudDb = 0.0;
        return p;
    }

    // dB RMS difference of two shapes, over the reliable range only.
    double distanceTo(const Profile& other) const {
        if (! valid() || ! other.valid()) return 0.0;
        double acc = 0.0; int n = 0;
        for (size_t i = 0; i < freqs.size(); ++i) {
            const double f = freqs[i];
            if (f < 120.0 || f > 13000.0) continue;
            const double a = db[i] - at(1000.0);
            const double b = other.at(f) - other.at(1000.0);
            acc += (a - b) * (a - b); ++n;
        }
        return n ? std::sqrt(acc / n) : 0.0;
    }
};

// ---- weighting (smart:EQ's group profile) -----------------------------------
// References combine in the power domain, not in dB.  A band that is +6 dB in one
// reference and 0 dB in the other averages to +3.96 dB, not +3: the blend is half of 4x
// the power plus half of 1x, and 10*log10(2.49) is 3.96.  Averaging the dB values instead
// would quietly claim a target that is quieter than either record in that band.
inline Profile blend(const std::vector<const Profile*>& refs, const std::vector<double>& weights,
                     const char* name = "blend") {
    Profile out;
    double wsum = 0.0;
    for (size_t i = 0; i < refs.size() && i < weights.size(); ++i)
        if (refs[i] && refs[i]->valid() && weights[i] > 0.0) wsum += weights[i];
    if (refs.empty() || wsum <= 0.0) return out;
    const Profile* first = nullptr;
    for (auto* r : refs) if (r && r->valid()) { first = r; break; }
    if (! first) return out;

    out.freqs = first->freqs;
    out.db.assign(out.freqs.size(), 0.0);
    std::vector<double> p(out.freqs.size(), 0.0);
    for (size_t k = 0; k < refs.size() && k < weights.size(); ++k) {
        if (! refs[k] || ! refs[k]->valid() || weights[k] <= 0.0) continue;
        const double w = weights[k] / wsum;
        const Profile n = refs[k]->normalised();
        for (size_t i = 0; i < out.freqs.size(); ++i)
            p[i] += w * std::pow(10.0, n.at(out.freqs[i]) / 10.0);
    }
    for (size_t i = 0; i < out.freqs.size(); ++i)
        out.db[i] = 10.0 * std::log10(std::max(p[i], 1e-20));
    // re-normalise: the blend is a shape relative to 1 kHz like every other profile
    const double ref = out.at(1000.0);
    for (auto& v : out.db) v -= ref;
    out.name = (refs.size() == 1) ? refs[0]->name : std::string(name);
    out.loudDb = 0.0;
    out.frames = 0;
    for (auto* r : refs) if (r) out.frames += r->frames;
    return out;
}

inline void smooth(std::vector<double>& y, double octaves) {
    if (y.size() < 3) return;
    const double sigma = std::max(0.5, octaves * 12.0);   // 12 points per octave
    const int r = static_cast<int>(std::ceil(3.0 * sigma));
    std::vector<double> tmp(y.size(), 0.0);
    for (int i = 0; i < static_cast<int>(y.size()); ++i) {
        double acc = 0.0, wsum = 0.0;
        for (int k = -r; k <= r; ++k) {
            const int j = std::clamp(i + k, 0, static_cast<int>(y.size()) - 1);
            const double w = std::exp(-0.5 * (k / sigma) * (k / sigma));
            acc += y[j] * w; wsum += w;
        }
        tmp[size_t(i)] = acc / wsum;
    }
    y.swap(tmp);
}

// ---- the delta: reference - source, on the source's own grid -----------------
// `smoothingOct` is the width of the gaussian in log-frequency.  1/6 octave is the width
// Match EQ calls its default: narrow enough to keep a resonance, wide enough that the
// answer is not 200 wiggles the solver cannot implement with 6 bands.
inline std::vector<double> deltaCurve(const Profile& source, const Profile& reference,
                                      double smoothingOct = 1.0 / 6.0,
                                      double limitDb = 12.0) {
    std::vector<double> d;
    if (! source.valid() || ! reference.valid()) return d;
    const Profile s = source.normalised();
    const double rAt1k = reference.at(1000.0);
    d.resize(source.freqs.size());
    for (size_t i = 0; i < source.freqs.size(); ++i) {
        const double f = source.freqs[i];
        d[i] = (reference.at(f) - rAt1k) - s.db[i];
    }
    smooth(d, smoothingOct);
    for (auto& v : d) v = std::clamp(v, -limitDb, limitDb);
    return d;
}

// ---- a profile is a file, not a state of mind ---------------------------------
// Plain text with 17 significant digits, so a saved and reloaded profile is bit-for-bit
// the same curve.  A profile is something you learn once from a record you like and keep.
inline std::string toText(const Profile& p) {
    std::string s = "AEQPROFILE 1\n";
    s += "name " + p.name + "\n";
    char buf[64];
    std::snprintf(buf, sizeof buf, "loud %.17g\nframes %ld\n", p.loudDb, p.frames);
    s += buf;
    std::snprintf(buf, sizeof buf, "points %d\n", int(p.freqs.size()));
    s += buf;
    for (size_t i = 0; i < p.freqs.size(); ++i) {
        std::snprintf(buf, sizeof buf, "%.17g %.17g\n", p.freqs[i], p.db[i]);
        s += buf;
    }
    return s;
}

// A profile file is user input.  It is parsed by the plugin, inside a DAW, on the message
// thread, and its numbers become the *target* of an EQ that is then applied to audio.
// So the parser is a trust boundary and it is written like one: bounded size, bounded
// point count, and every value has to be a finite number in a plausible range before it
// is allowed to become a curve.  A NaN in here used to be able to reach a biquad.
inline constexpr size_t kMaxProfileBytes = 4u * 1024u * 1024u;
inline constexpr size_t kMaxProfilePoints = 4096u;

inline Profile fromText(const std::string& text) {
    Profile p;
    if (text.size() > kMaxProfileBytes) return Profile{};
    size_t pos = 0;
    auto line = [&]() -> std::string {
        const size_t e = text.find('\n', pos);
        const std::string l = text.substr(pos, (e == std::string::npos ? text.size() : e) - pos);
        pos = (e == std::string::npos) ? text.size() : e + 1;
        return l;
    };
    if (line().rfind("AEQPROFILE", 0) != 0) return p;
    std::string head;
    while (pos < text.size()) {
        head = line();
        if (head.rfind("name ", 0) == 0)   p.name = head.substr(5);
        else if (head.rfind("loud ", 0) == 0)   p.loudDb = std::atof(head.c_str() + 5);
        else if (head.rfind("frames ", 0) == 0) p.frames = std::atol(head.c_str() + 7);
        else if (head.rfind("points ", 0) == 0) break;
        else return Profile{};
    }
    while (pos < text.size()) {
        const std::string l = line();
        if (l.empty()) continue;
        if (p.freqs.size() >= kMaxProfilePoints) return Profile{};
        double f = 0.0, d = 0.0;
        if (std::sscanf(l.c_str(), "%lf %lf", &f, &d) != 2) break;
        // a frequency has to be a positive, rising, audible number; a level has to be a
        // finite dB value.  Anything else makes the curve meaningless, so the whole file
        // is refused rather than half-loaded.
        if (! std::isfinite(f) || ! std::isfinite(d)) return Profile{};
        if (f < 10.0 || f > 48000.0) return Profile{};
        if (! p.freqs.empty() && f <= p.freqs.back()) return Profile{};
        if (std::abs(d) > 200.0) return Profile{};
        p.freqs.push_back(f);
        p.db.push_back(d);
    }
    if (! p.name.empty() && p.name.size() > 128) p.name = p.name.substr(0, 128);
    if (! p.valid()) return Profile{};
    return p;
}

} // namespace aeq
