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
#include "../Source/dsp/Profile.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include "../Source/Platform.h"
#include "../Source/model/TargetCurve.h"

// ---- minimal WAV I/O (PCM 16/24/32 and float32) ----
struct Wav {
    std::vector<float> L, R;
    int sr = 48000;
    int channels = 1;
};

static uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// The path is a std::filesystem::path, not a `const char*`: on Windows a path is UTF-16,
// so `path::c_str()` is `const wchar_t*` and `fopen` cannot see an Arabic file name.
// aeq::openRead (Platform.h) is the only fopen in the project.
static bool loadWav(const std::filesystem::path& path, Wav& w) {
    FILE* f = aeq::openRead(path);
    if (!f) { std::printf("cannot open %s\n", aeq::pathUtf8(path).c_str()); return false; }
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

static bool saveWav(const std::filesystem::path& path, const Wav& w) {
    FILE* f = aeq::openWrite(path);
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
    std::printf("sibilance median : %.1f dB   p90 %.1f   p99 %.1f\n",
                m.sibMed, m.sibP90, m.sibP99);
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
            {"mud", 260, 520}, {"box", 520, 850},
            {"nasal", 850, 1300}, {"harsh", 2000, 4200}};
        for (const auto& r : regs)
            std::printf("EXCESS %s raw %+.2f env %+.2f\n", r.id,
                        aeq::excessVsTarget(m, r.lo, r.hi),
                        aeq::excessVsTargetEnv(m, r.lo, r.hi));
        // "boom" is pitch-relative now, so it has no raw counterpart: the raw
        // spectrum below 1.5*F0 measured nothing at all on this corpus (0/15).
        std::printf("EXCESS boomtilt raw %+.2f env %+.2f\n",
                    -50.0, aeq::weightTilt(m));
        // The evidence behind the detectors that are NOT scored by a region: each of these
        // numbers is the one an `if` in ProblemDetector compares against a constant.  They
        // are printed so the constant can be derived from the material that defines
        // "right" instead of being typed, and so a reader can check the bar against the
        // distribution it was set from (`analysis/scripts/calibrate_warn.py`).
        {
            const double rumble = aeq::levelBetween(m, 30, 70) - aeq::levelBetween(m, 300, 700);
            const double airMiss = aeq::TargetCurve::at(9000.0)
                                 - (aeq::levelBetween(m, 7000, 12000) - aeq::levelBetween(m, 1000, 2000));
            const double airTop = aeq::levelBetween(m, 12000, 16000) - aeq::levelBetween(m, 7000, 10000);
            const double hfCut = aeq::levelBetween(m, 14000, 16000) - aeq::levelBetween(m, 9000, 11000);
            std::printf("STAT rumble %+.3f\n", rumble);
            std::printf("STAT sibp90 %+.3f\n", m.sibP90);
            std::printf("STAT sibmed %+.3f\n", m.sibMed);
            std::printf("STAT sibp99 %+.3f\n", m.sibP99);
            // two take-relative candidates for the same decisions: how far the top stands
            // above the body *of this take*, which is what a singer's brightness is,
            // rather than an absolute level that depends on how the mix was mastered
            std::printf("STAT sibexc %+.3f\n", m.sibP90 - m.sibMed);
            std::printf("STAT sibexc99 %+.3f\n", m.sibP99 - m.sibMed);
            std::printf("STAT airslope %+.3f\n",
                        aeq::levelBetween(m, 8000, 12000) - aeq::levelBetween(m, 2000, 4000));
            // the same idea for the two "raw" regions: measured inside the take, so the
            // arrangement's own balance cancels and only the shape of the voice is left
            std::printf("STAT nasalslope %+.3f\n",
                        aeq::levelBetween(m, 850, 1300) - aeq::levelBetween(m, 2000, 4000));
            std::printf("STAT harshslope %+.3f\n",
                        aeq::levelBetween(m, 2000, 4200) - aeq::levelBetween(m, 6000, 9000));
            std::printf("STAT airmiss %+.3f\n", airMiss);
            std::printf("STAT airtop %+.3f\n", airTop);
            std::printf("STAT hfcut %+.3f\n", hfCut);
            std::printf("STAT noisefloor %.3f\n", m.noiseFloorDb);
            std::printf("STAT maxrms %.3f\n", m.maxRmsDb);
            std::printf("STAT clip %.3e\n", m.clipRate);
            std::printf("STAT dc %+.5f\n", m.dcOffset);
        }
        // every measurable band of the envelope, re the 900-1150 Hz reference, in
        // dB.  analysis/scripts/env_target.py averages exactly these numbers over the
        // 15 takes to build the target - the target must come from the same binary
        // that measures, or the z-scores compare two different statistics.
        // where this take sits against the 15 that define the standard, in sigma,
        // after matching the filter to the width of a real EQ fault.  Evidence, not
        // an action: a 3 dB broad lift is inside the engineer's own range (his 15
        // mixes span ~1 dB per band on the envelope), so the plugin reports the
        // position and leaves the decision to the engineer.
        if (const auto zs = aeq::scanZ(m, 3); zs.peakBand >= 0)
            for (size_t i = 1; i + 1 < zs.smoothed.size(); ++i) {
                const double f = m.freqs[i];
                if (f < 240.0 || f > 2500.0 || !zs.usable[i]) continue;
                if (zs.smoothed[i] <= zs.smoothed[i - 1] || zs.smoothed[i] <= zs.smoothed[i + 1]) continue;
                if (zs.smoothed[i] < 1.5) continue;
                std::printf("ZPEAK %.1f %+.2f\n", f, zs.smoothed[i]);
            }
        const double eref = aeq::envLevelBetween(m, 900.0, 1150.0);
        if (eref > -119.0)
            for (size_t i = 0; i < m.freqs.size(); ++i)
                if (aeq::envUsable(m, i))
                    std::printf("ENVLINE %.1f %+.3f\n", m.freqs[i], m.envDb[i] - eref);
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
    // The honest number the panel shows, printed by the same code path that produced it:
    // how far the take was from the target, and how far the curve took it.  Before this,
    // the offline tool could describe the curve but not whether it helped, so a claim like
    // "this sounds like Amir now" had no number behind it outside the plugin window.
    if (st.gapValid)
        std::printf("match to target  : %s, %.2f -> %.2f dB RMS  (%.0f%% closer)\n",
                    core.targetName().empty() ? "unnamed" : core.targetName().c_str(),
                    st.gapBeforeDb, st.gapAfterDb,
                    100.0 * (1.0 - st.gapAfterDb / std::max(st.gapBeforeDb, 1e-6)));
    std::printf("=====================================================\n");
}


// ---- a folder of stems -> one reference profile --------------------------------
// Each file is measured by the plugin's own analyser (learn pass, gated frames - exactly
// what the plugin will do to the take), then the profiles are blended in the power domain
// with equal weights.  Files that are too short to gate, or that are silent, are skipped
// and *counted*, because silently averaging in a dead file is how a reference ends up
// describing the room instead of the voice.
struct DirResult { aeq::Profile prof; int used = 0, skipped = 0; };

static aeq::Profile measureFile(const std::filesystem::path& path, const std::string& name, bool* ok) {
    *ok = false;
    Wav w;
    if (!loadWav(path, w)) return {};
    aeq::RestoreCore core;
    core.prepare(w.sr, 512);
    aeq::RestoreSettings s;
    s.learn = true;
    s.settleSeconds = 0.0;
    core.setSettings(s);
    std::vector<float> L(512), R(512);
    int blocks = 0;
    for (size_t off = 0; off < w.L.size(); off += 512) {
        const int n = (int)std::min<size_t>(512, w.L.size() - off);
        for (int i = 0; i < n; ++i) {
            L[i] = w.L[off + i];
            R[i] = w.R.empty() ? w.L[off + i] : w.R[off + i];
        }
        core.processBlock(L.data(), R.data(), n);
        if (++blocks % 40 == 0) core.refresh();
    }
    core.refresh();
    auto p = core.captureProfile(name);
    *ok = p.valid();
    return p;
}

static std::string refNameOf(int argc, char** argv) {
    for (int i = 3; i < argc - 1; ++i)
        if (std::string(argv[i]) == "--name") return argv[i + 1];
    return "reference";
}

static int profileFromDir(const std::filesystem::path& dir, const std::filesystem::path& out,
                          const std::string& name) {
    std::vector<aeq::Profile> profiles;
    int skipped = 0;
    // Sorted: a reference blended from 15 stems must be the same number whichever order
    // the file system hands the names over in.
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".wav") continue;
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        bool ok = false;
        auto p = measureFile(path, aeq::pathUtf8(path.stem()), &ok);
        if (!ok) { ++skipped; std::printf("  skip %s (nothing measurable)\n",
                                          aeq::pathUtf8(path.filename()).c_str()); continue; }
        profiles.push_back(p);
        std::printf("  %-40s %zu bands, %ld frames\n",
                    aeq::pathUtf8(path.filename()).c_str(), p.db.size(), p.frames);
    }
    if (profiles.empty()) { std::printf("no measurable wav files in %s\n",
                                        aeq::pathUtf8(dir).c_str()); return 4; }
    std::vector<const aeq::Profile*> ptr;
    for (const auto& p : profiles) ptr.push_back(&p);
    const std::vector<double> weights(profiles.size(), 1.0);
    const auto blended = aeq::blend(ptr, weights, name.c_str());
    std::ofstream f(out, std::ios::binary);
    f << aeq::toText(blended);
    std::printf("wrote %s: %zu bands blended from %zu stems (%d skipped), name \"%s\"\n",
                aeq::pathUtf8(out).c_str(), blended.db.size(), profiles.size(), skipped,
                name.c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: aeq_cli analyze in.wav [--ref profile.aeqprof]\n"
                    "       aeq_cli process in.wav out.wav [--amount 1.0] [--maxgain 6] [--bands 8]\n"
                    "                        [--ref profile.aeqprof] [--mode match]\n"
                    "       aeq_cli profile in.wav out.aeqprof [--name \"who it sounds like\"]\n"
                    "       aeq_cli profile --from-dir stems/ out.aeqprof [--name \"Amir 15\"]\n"
                    "       aeq_cli profile --rewrite in.aeqprof out.aeqprof\n"
                    "       (--width <octaves> sets the Match comparison width, default 1/6)\n");
        return 1;
    }
    const std::string mode = argv[1];
    // `profile --from-dir <dir> <out>`: a *set* of stems becomes one reference, measured by
    // this plugin's own analyser (the same instrument as the take) and blended in the power
    // domain.  This is how a record - or Amir's 15 dry stems - becomes a target without
    // anyone re-implementing the analyser in Python and hoping the two agree.
    if (mode == "profile" && argc >= 6 && std::string(argv[2]) == "--from-dir")
        return profileFromDir(aeq::pathFromUtf8(argv[3]), aeq::pathFromUtf8(argv[4]),
                              refNameOf(argc, argv));
    // `profile --rewrite in out`: read a profile, write it back.  The file is user input
    // that becomes the target of an EQ, so the parser and the writer have to agree with
    // each other bit for bit - otherwise a profile saved by the plugin and reloaded by the
    // plugin is a slightly different curve, which is exactly the kind of drift a session
    // that reopens "sounding different" is made of.
    if (mode == "profile" && argc >= 5 && std::string(argv[2]) == "--rewrite") {
        std::ifstream f(aeq::pathFromUtf8(argv[3]));
        std::stringstream ss; ss << f.rdbuf();
        const auto prof = aeq::fromText(ss.str());
        if (! prof.valid()) { std::printf("cannot read profile %s\n", argv[3]); return 3; }
        std::ofstream o(aeq::pathFromUtf8(argv[4]), std::ios::binary);
        o << aeq::toText(prof);
        std::printf("rewrote %s: %zu bands, name \"%s\"\n", argv[4], prof.db.size(),
                    prof.name.c_str());
        return 0;
    }

    Wav w;
    if (!loadWav(aeq::pathFromUtf8(argv[2]), w)) return 2;

    aeq::RestoreSettings s;
    s.learn = true;
    std::string refPath, refName = "reference";
    for (int i = 3; i < argc - 1; ++i) {
        const std::string a = argv[i];
        if (a == "--amount")  s.correctionAmount = std::atof(argv[i + 1]);
        if (a == "--maxgain") s.maxGainDb = std::atof(argv[i + 1]);
        if (a == "--bands")   s.maxBands = std::atoi(argv[i + 1]);
        if (a == "--ref")     refPath = argv[i + 1];
        if (a == "--name")    refName = argv[i + 1];
        if (a == "--mode")    s.mode = (std::string(argv[i + 1]) == "match") ? 1 : 0;
        if (a == "--width")   s.compareWidthOct = std::atof(argv[i + 1]);
    }

    aeq::RestoreCore core;
    core.prepare(w.sr, 512);

    // A reference profile, if one was named: read from the text file the `profile`
    // command writes.  Match then aims at *this* record instead of the corpus average,
    // measured on the same grid as the take.
    //
    // Order matters, and it was wrong: the settings were pushed to the core *before* this
    // block, so `--ref` set `s.mode = 1` on a local copy the core had already ignored -
    // the run printed "reference: ..." and then solved for the corpus average.  A tool that
    // ignores the flag it reports reading is worse than one that rejects it, so the
    // settings are pushed after the reference is known.
    if (! refPath.empty()) {
        std::ifstream f(aeq::pathFromUtf8(refPath.c_str()));
        std::stringstream ss; ss << f.rdbuf();
        const auto prof = aeq::fromText(ss.str());
        if (! prof.valid()) { std::printf("cannot read profile %s\n", refPath.c_str()); return 3; }
        core.setReferences({prof}, {1.0});
        s.mode = 1;
        std::printf("reference: %s (%zu bands, %ld frames)\n",
                    prof.name.c_str(), prof.db.size(), prof.frames);
    }
    core.setSettings(s);

    // pass 1: learn over the whole take
    const int blk = 512;
    std::vector<float> L(blk), R(blk);
    int blocks = 0;
    for (size_t off = 0; off < w.L.size(); off += blk) {
        const int n = (int)std::min<size_t>(blk, w.L.size() - off);
        for (int i = 0; i < n; ++i) {
            L[i] = w.L[off + i];
            R[i] = w.R.empty() ? w.L[off + i] : w.R[off + i];
        }
        core.processBlock(L.data(), R.data(), n);   // audio thread: queue only
        // analysis runs on the caller's thread now, so the offline reader pumps it
        if (++blocks % 40 == 0) core.refresh();
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
        saveWav(aeq::pathFromUtf8(argv[3]), o);
        std::printf("wrote %s\n", argv[3]);
    }

    if (mode == "profile") {
        if (argc < 4) { std::printf("need an output path\n"); return 1; }
        // Everything the analyser heard, kept as a curve that can be reused: this is the
        // "learn a reference once" half of Match EQ, in one command.
        const auto prof = core.captureProfile(refName);
        if (! prof.valid()) { std::printf("nothing measured - was the file silent?\n"); return 4; }
        std::ofstream f(aeq::pathFromUtf8(argv[3]), std::ios::binary);
        f << aeq::toText(prof);
        std::printf("wrote %s: %zu bands, %ld frames, %s\n",
                    argv[3], prof.db.size(), prof.frames, prof.name.c_str());
    }
    return 0;
}
