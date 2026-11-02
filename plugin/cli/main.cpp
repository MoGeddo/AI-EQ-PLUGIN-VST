// aeq_cli - offline harness for the exact DSP that the VST3 runs.
//
//   aeq_cli analyze  in.wav
//   aeq_cli process  in.wav  out.wav [--amount 1.0] [--maxgain 6] [--bands 8]
//
// It exists for two reasons: the VST3 wrapper stays thin (all the risk lives in
// code that is tested here), and the same numbers can be reproduced in Python.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

#include "../Source/dsp/RestoreCore.h"
#include "../Source/model/TargetCurve.h"

// ---- minimal WAV I/O (PCM 16/24/32 and float32) ----
struct Wav {
    std::vector<float> L, R;
    int sr = 48000;
    int channels = 1;
};

static uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static bool loadWav(const char* path, Wav& w) {
    FILE* f = fopen(path, "rb");
    if (!f) { std::printf("cannot open %s\n", path); return false; }
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) {
        std::printf("not a WAV file\n"); fclose(f); return false;
    }
    int bits = 16, fmt = 1;
    std::vector<unsigned char> data;
    while (true) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        const uint32_t sz = rd32(ch + 4);
        if (!std::memcmp(ch, "fmt ", 4)) {
            std::vector<unsigned char> b(sz);
            if (fread(b.data(), 1, sz, f) != sz) break;
            fmt = rd16(b.data());
            w.channels = rd16(b.data() + 2);
            w.sr = (int)rd32(b.data() + 4);
            bits = rd16(b.data() + 14);
        } else if (!std::memcmp(ch, "data", 4)) {
            data.resize(sz);
            if (fread(data.data(), 1, sz, f) != sz) data.resize(0);
            break;
        } else {
            fseek(f, sz, SEEK_CUR);
        }
    }
    fclose(f);
    const int bytes = bits / 8;
    if (bytes <= 0 || data.empty()) return false;
    const size_t frames = data.size() / (bytes * w.channels);
    w.L.resize(frames); if (w.channels > 1) w.R.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < w.channels && c < 2; ++c) {
            const unsigned char* p = &data[(i * w.channels + c) * bytes];
            float v = 0.f;
            if (fmt == 3 && bits == 32) { float t; std::memcpy(&t, p, 4); v = t; }
            else if (bits == 16) v = (int16_t)rd16(p) / 32768.f;
            else if (bits == 24) { int32_t s = (p[0] | (p[1] << 8) | (p[2] << 16)); if (s & 0x800000) s |= ~0xFFFFFF; v = s / 8388608.f; }
            else if (bits == 32) { int32_t s; std::memcpy(&s, p, 4); v = s / 2147483648.f; }
            (c == 0 ? w.L : w.R)[i] = v;
        }
    }
    return true;
}

static void wr32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void wr16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

static bool saveWav(const char* path, const Wav& w) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const int ch = 2;
    const size_t n = w.L.size();
    fwrite("RIFF", 1, 4, f); wr32(f, (uint32_t)(36 + n * ch * 4));
    fwrite("WAVEfmt ", 1, 8, f); wr32(f, 16); wr16(f, 3); wr16(f, ch);
    wr32(f, w.sr); wr32(f, w.sr * ch * 4); wr16(f, ch * 4); wr16(f, 32);
    fwrite("data", 1, 4, f); wr32(f, (uint32_t)(n * ch * 4));
    for (size_t i = 0; i < n; ++i) {
        fwrite(&w.L[i], 4, 1, f);
        const float r = w.R.empty() ? w.L[i] : w.R[i];
        fwrite(&r, 4, 1, f);
    }
    fclose(f);
    return true;
}

// ---- report ----
static void printReport(aeq::RestoreCore& core) {
    const auto st = core.state();
    const auto m = core.measurements();
    if (!st.valid) { std::printf("not enough audio to analyse\n"); return; }

    std::printf("\n================ AEQ RESTORE - REPORT ================\n");
    std::printf("median F0        : %.0f Hz\n", st.measuredF0);
    std::printf("peak             : %.1f dBFS   dynamic range %.1f dB\n", m.peakDbfs, m.dynamicRange);
    std::printf("sibilance median : %.1f dB   p90 %.1f dB\n", m.sibMed, m.sibP90);
    // The envelope is printed, not just used: any claim about it can then be
    // rechecked on the user's own material with this binary.
    if (!m.envDb.empty()) {
        const double refRaw = levelBetween(m, 900.0, 1150.0);
        double p = 0.0; int n = 0;
        for (size_t i = 0; i < m.freqs.size(); ++i)
            if (m.freqs[i] >= 900.0 && m.freqs[i] < 1150.0) { p += std::pow(10.0, m.envDb[i] / 10.0); ++n; }
        const double refEnv = n ? 10.0 * std::log10(p / n) : -120.0;
        std::printf("\n--- harmonic envelope vs raw, dB re their own 1 kHz ---\n");
        for (size_t i = 0; i < m.freqs.size(); ++i) {
            const double f = m.freqs[i];
            if (f < 195.0 || f > 1650.0) continue;
            std::printf("  ENV  %6.0f Hz   envelope %+7.2f   raw %+7.2f   gap %+6.2f\n",
                        f, m.envDb[i] - refEnv, m.bandDb[i] - refRaw,
                        (m.envDb[i] - refEnv) - (m.bandDb[i] - refRaw));
        }
    }

    std::printf("\n--- problems found (%zu) ---\n", st.problems.size());
    // machine-readable line, one per problem, so a whole-corpus run can be counted
    // without parsing Arabic.  `id` is stable, `sev` is 0..1, `gain` is what the
    // plugin would apply.
    for (const auto& p : st.problems)
        std::printf("PROBLEM %s %.2f %.0f %.1f\n", p.id.c_str(), p.severity, p.freq, p.gainDb);
    // the raw evidence behind every region decision, in both domains.  This is what
    // analysis/scripts/calibrate_env.py reads to derive the thresholds, so the
    // constants can never drift away from the measurement they came from.
    {
        const struct { const char* id; double lo, hi; } regs[] = {
            {"boom", 110, 260}, {"mud", 260, 520}, {"box", 520, 850},
            {"nasal", 850, 1300}, {"harsh", 2000, 4200}};
        for (const auto& r : regs)
            std::printf("EXCESS %s raw %+.2f env %+.2f\n", r.id,
                        aeq::excessVsTarget(m, r.lo, r.hi),
                        aeq::excessVsTargetEnv(m, r.lo, r.hi));
    }
    for (const auto& p : st.problems) {
        const char* act = "warn";
        switch (p.action) {
            case aeq::Action::RaiseHpf: act = "HPF";   break;
            case aeq::Action::CutBroad: act = "EQ cut"; break;
            case aeq::Action::CutNarrow: act = "notch"; break;
            case aeq::Action::DeEss:    act = "de-ess"; break;
            case aeq::Action::AddAir:   act = "air";    break;
            default: break;
        }
        std::printf("  [%5.2f] %-22s %s\n", p.severity, p.labelAr.c_str(), p.detailAr.c_str());
        std::printf("          -> %-7s %6.0f Hz  Q %.1f  %+.1f dB\n", act, p.freq, p.q, p.gainDb);
    }
    std::printf("\n--- corrective EQ (%zu bands) ---\n", st.corrective.size());
    std::printf("  %-10s %8s %6s %8s\n", "type", "freq", "Q", "gain");
    for (const auto& b : st.corrective) {
        const char* t = b.type == aeq::BandType::Peak ? "peak" :
                        (b.type == aeq::BandType::LowShelf ? "lowshelf" : "highshelf");
        std::printf("  %-10s %8.0f %6.2f %+8.2f\n", t, b.f0, b.q, b.gainDb);
    }
    std::printf("\n--- surgical notches (%zu) ---\n", st.resonance.size());
    for (const auto& b : st.resonance) std::printf("  peak %8.0f Hz Q %4.1f %+6.2f dB\n", b.f0, b.q, b.gainDb);
    std::printf("\nHPF              : %.0f Hz (24 dB/oct)\n", st.hpfHz);
    std::printf("de-esser         : thr %.0f dBFS, max %.1f dB\n", st.deEssThresholdDb, st.deEssMaxGrDb);
    std::printf("problems fixed   : %.2f dB RMS -> %.2f dB RMS  (%.0f%% of the excess removed)\n",
                st.fixBeforeDb, st.fixAfterDb,
                100.0 * (1.0 - st.fixAfterDb / std::max(st.fixBeforeDb, 1e-6)));
    std::printf("distance to family target: %.2f -> %.2f dB RMS (informational; restore\n"
                "  mode deliberately refuses to chase natural variation)\n",
                st.distBeforeDb, st.distAfterDb);
    std::printf("=====================================================\n");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: aeq_cli analyze in.wav\n"
                    "       aeq_cli process in.wav out.wav [--amount 1.0] [--maxgain 6] [--bands 8]\n");
        return 1;
    }
    const std::string mode = argv[1];
    Wav w;
    if (!loadWav(argv[2], w)) return 2;

    aeq::RestoreSettings s;
    s.learn = true;
    for (int i = 3; i < argc - 1; ++i) {
        const std::string a = argv[i];
        if (a == "--amount")  s.correctionAmount = std::atof(argv[i + 1]);
        if (a == "--maxgain") s.maxGainDb = std::atof(argv[i + 1]);
        if (a == "--bands")   s.maxBands = std::atoi(argv[i + 1]);
    }

    aeq::RestoreCore core;
    core.prepare(w.sr, 512);
    core.setSettings(s);

    // pass 1: learn over the whole take
    const int blk = 512;
    std::vector<float> L(blk), R(blk);
    for (size_t off = 0; off < w.L.size(); off += blk) {
        const int n = (int)std::min<size_t>(blk, w.L.size() - off);
        for (int i = 0; i < n; ++i) {
            L[i] = w.L[off + i];
            R[i] = w.R.empty() ? w.L[off + i] : w.R[off + i];
        }
        core.processBlock(L.data(), R.data(), n);   // analysis only happens here
    }
    core.refresh();
    printReport(core);

    if (mode == "process") {
        if (argc < 4) { std::printf("need an output path\n"); return 1; }
        core.reset();
        core.setSettings(s);
        // pass 2: apply (learn disabled so the analysis is frozen)
        auto s2 = s; s2.learn = false;
        core.setSettings(s2);
        Wav o = w;
        for (size_t off = 0; off < w.L.size(); off += blk) {
            const int n = (int)std::min<size_t>(blk, w.L.size() - off);
            for (int i = 0; i < n; ++i) {
                L[i] = w.L[off + i];
                R[i] = w.R.empty() ? w.L[off + i] : w.R[off + i];
            }
            core.processBlock(L.data(), R.data(), n);
            for (int i = 0; i < n; ++i) { o.L[off + i] = L[i]; if (!o.R.empty()) o.R[off + i] = R[i]; }
        }
        saveWav(argv[3], o);
        std::printf("wrote %s\n", argv[3]);
    }
    return 0;
}
