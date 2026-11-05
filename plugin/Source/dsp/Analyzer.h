// Streaming measurement engine.
//
// Runs on a background thread over the *whole* performance (play the take from
// start to finish and it learns continuously), or over a file in the CLI.
// Everything it keeps is a running statistic: mean / variance per band, a
// sibilance histogram, clipping and floor counters.  No audio is buffered, so
// memory is constant regardless of take length.
#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>

namespace aeq {

#ifndef AEQ_PI
#define AEQ_PI
constexpr double kPi = 3.14159265358979323846;
#endif

// 1/12-octave grid, IEC base-10 centres from 40 Hz to 16 kHz
struct BandGrid {
    std::vector<double> f;      // centre frequencies
    std::vector<double> lo, hi; // edges
    static BandGrid make(double fmin = 40.0, double fmax = 16000.0, int ppOct = 12) {
        BandGrid g;
        const int n = static_cast<int>(std::ceil(std::log2(fmax / fmin) * ppOct)) + 1;
        const double r = std::pow(2.0, 1.0 / (2.0 * ppOct));
        for (int i = 0; i < n; ++i) {
            const double fc = fmin * std::pow(2.0, double(i) / ppOct);
            if (fc > fmax) break;
            g.f.push_back(fc);
            g.lo.push_back(fc / r);
            g.hi.push_back(fc * r);
        }
        return g;
    }
    int size() const { return static_cast<int>(f.size()); }
    int indexOf(double freq) const {
        int best = 0; double bd = 1e30;
        for (int i = 0; i < size(); ++i) { double d = std::abs(std::log2(f[i] / freq)); if (d < bd) { bd = d; best = i; } }
        return best;
    }
};

// --------------------------------------------------------------------------
// one measured frame
// --------------------------------------------------------------------------
struct FrameInfo {
    double rmsDb = -120.0;
    double peak = 0.0;
    bool   active = false;
    bool   noisy = false;       // fricative-like (spectral flatness high)
};

// --------------------------------------------------------------------------
class Analyzer {
public:
    static constexpr int kFft = 8192;
    static constexpr int kHop = 2048;

    void prepare(double sampleRate) {
        fs_ = sampleRate;
        grid_ = BandGrid::make(40.0, std::min(16000.0, sampleRate * 0.47), 12);
        nBands_ = grid_.size();
        binHz_ = fs_ / kFft;
        bandBinLo_.resize(nBands_);
        bandBinHi_.resize(nBands_);
        for (int i = 0; i < nBands_; ++i) {
            bandBinLo_[i] = std::max(1, static_cast<int>(std::floor(grid_.lo[i] / binHz_)));
            bandBinHi_[i] = std::min(kFft / 2 - 1, static_cast<int>(std::ceil(grid_.hi[i] / binHz_)));
        }
        fftInit();
        frameEnv_.assign(nBands_, -120.0);
        cRe_.assign(kFft, 0.0); cIm_.assign(kFft, 0.0);
        // Everything processFrame() needs is sized here, in prepare(), where an
        // allocation is allowed to happen.  power_ and frameBand_ used to be resized
        // on the first frame - i.e. inside the host's audio callback.
        power_.assign(kFft / 2, 0.0);
        frameBand_.assign(nBands_, -120.0);
        yinD_.assign(kFft / 2 + 2, 0.0);
        yinCm_.assign(kFft / 2 + 2, 0.0);
        envValid_.assign(nBands_, false);
        reset();
    }

    void reset() {
        std::fill(std::begin(mean_), std::end(mean_), 0.0);
        pSum_.assign(256, 0.0);
        liveP_.assign(256, 0.0);
        peakP_.assign(256, 0.0);
        std::fill(std::begin(m2_), std::end(m2_), 0.0);
        std::fill(std::begin(exMean_), std::end(exMean_), 0.0);
        std::fill(std::begin(exM2_), std::end(exM2_), 0.0);
        std::fill(std::begin(exN_), std::end(exN_), 0.0);
        nFrames_ = 0; nActive_ = 0; nNoisy_ = 0;
        framesSeen_ = 0;
        std::fill(std::begin(lvlHist_), std::end(lvlHist_), 0); lvlCount_ = 0;
        peak_ = 0.0; clipCount_ = 0; dcSum_ = 0.0; dcN_ = 0;
        minActiveRms_ = 1e9; maxRms_ = -1e9;
        sdHist_.fill(0);
        f0Hist_.assign(60, 0);     // 60 Hz .. 600 Hz, 10 Hz bins
        // harmonic-envelope accumulators (see envelopeOfFrame)
        std::fill(std::begin(envMean_), std::end(envMean_), 0.0);
        std::fill(std::begin(envM2_), std::end(envM2_), 0.0);
        std::fill(std::begin(envN_), std::end(envN_), 0);
        lastF0_ = 0.0;
        f0Pub_.store(0.0, std::memory_order_relaxed);
        voicedPub_.store(0, std::memory_order_relaxed);
        sibFrameSum_ = 0.0; sibFrames_ = 0;
        framesInRing_ = 0; ringPos_ = 0;
        std::fill(std::begin(ring_), std::end(ring_), 0.0);
        totalSamples_ = 0;
    }

    // feed audio (mono view is enough for a vocal)
    void processBlock(const float* x, int n) {
        for (int i = 0; i < n; ++i) {
            const double s = static_cast<double>(x[i]);
            const double a = std::abs(s);
            if (a > peak_) peak_ = a;
            if (a >= 0.9995) ++clipCount_;
            dcSum_ += s; ++dcN_;

            ring_[ringPos_] = s;
            ringPos_ = (ringPos_ + 1) % kFft;
            if (framesInRing_ < kFft) ++framesInRing_;

            if (++sinceHop_ >= kHop) { sinceHop_ = 0; }
            if (ringPos_ % kHop == 0 && framesInRing_ == kFft) processFrame();
        }
        totalSamples_ += n;
    }

    // ---------------- results ----------------
    bool   ready() const { return nActive_ >= 40; }
    // The display may start with the very first frame.  ready() means "enough to
    // trust a decision", which is 1.7 s of sung audio - correct for the solver, far
    // too long to leave the analyser flat when someone presses play.
    long   framesSeen() const { return framesSeen_; }
    bool   hasSpectrum() const { return framesSeen_ > 0; }   // nFrames_ is reset-only
    int    bandCount() const { return nBands_; }
    long   activeFrames() const { return nActive_; }
    long   samplesSeen()  const { return totalSamples_; }   // every sample handed to processBlock
    double lastGateDb() const { return lastGate_; }
    const BandGrid& grid() const { return grid_; }
    double sampleRate() const { return fs_; }

    // absolute band levels (dB) and their inter-frame sd, on the analysis grid
    std::vector<double> bandLevelsDb() const {
        std::vector<double> out(nBands_, -120.0);
        for (int i = 0; i < nBands_; ++i)
            out[i] = (nActive_ > 0) ? 10.0 * std::log10(std::max(pSum_[i] / nActive_, 1e-20)) : -120.0;
        return out;
    }
    // the moving spectrum for the display (120 ms), and its peak hold
    std::vector<double> liveLevelsDb() const {
        std::vector<double> out(nBands_, -120.0);
        for (int i = 0; i < nBands_; ++i)
            out[i] = 10.0 * std::log10(std::max(liveP_[i], 1e-20));
        return out;
    }
    std::vector<double> peakHoldDb() const {
        std::vector<double> out(nBands_, -120.0);
        for (int i = 0; i < nBands_; ++i)
            out[i] = 10.0 * std::log10(std::max(peakP_[i], 1e-20));
        return out;
    }

    std::vector<double> bandSdDb() const {
        std::vector<double> out(nBands_, 0.0);
        for (int i = 0; i < nBands_; ++i) {
            // m2_ accumulates (dB - mean)^2, so its variance is already in dB^2
            if (nActive_ > 1) out[i] = std::sqrt(std::max(m2_[i] / (nActive_ - 1), 0.0));
        }
        return out;
    }
    // ---- harmonic envelope -------------------------------------------------
    // Absolute band levels of the *envelope*, i.e. of the smooth vocal-tract
    // shape rather than the ladder of harmonics.  This is the measurement the
    // decisions move to: the raw levels depend on where the singer's harmonics
    // happen to fall (r = -0.91 against F0 in the 110-260 Hz band), the
    // envelope largely does not (r = -0.72, and 3.55x smaller spread across the
    // 15 reference mixes - docs/14).
    std::vector<double> envelopeLevelsDb() const {
        std::vector<double> out(nBands_, -120.0);
        for (int i = 0; i < nBands_; ++i)
            if (envN_[i] > 0) out[i] = envMean_[i];
        return out;
    }
    std::vector<double> envelopeSdDb() const {
        std::vector<double> out(nBands_, 0.0);
        for (int i = 0; i < nBands_; ++i)
            if (envN_[i] > 1) out[i] = std::sqrt(std::max(envM2_[i] / (envN_[i] - 1), 0.0));
        return out;
    }
    // how many frames contributed an envelope value to this band (0 = the band
    // has never been above the fundamental, so nothing can be said about it)
    int envelopeFrames(int band) const {
        return (band >= 0 && band < nBands_) ? envN_[band] : 0;
    }

    // per-band static excess: mean and sd of (band - local baseline) in dB
    void excessStats(std::vector<double>& meanDb, std::vector<double>& sdDb, std::vector<int>& count) const {
        meanDb.assign(nBands_, 0.0); sdDb.assign(nBands_, 0.0); count.assign(nBands_, 0);
        for (int i = 0; i < nBands_; ++i) {
            const double n = exN_[i];
            if (n > 5) {
                meanDb[i] = exMean_[i];   // already a mean (incremental)
                sdDb[i] = std::sqrt(std::max(exM2_[i] / (n - 1), 0.0));
                count[i] = static_cast<int>(n);
            }
        }
    }

    double activeFraction() const { return framesSeen_ ? double(nActive_) / framesSeen_ : 0.0; }
    // how many seconds of actually-sung audio have been analysed (not just the
    // transport time: silence and gaps do not count)
    double activeSeconds() const { return double(nActive_) * kHop / (fs_ > 0.0 ? fs_ : 48000.0); }
    double noiseFraction()  const { return nActive_ ? double(nNoisy_) / nActive_ : 0.0; }
    double clipRate()       const { return totalSamples_ ? double(clipCount_) / totalSamples_ : 0.0; }
    int    clipSamples()    const { return clipCount_; }
    double dcOffset()       const { return dcN_ ? dcSum_ / dcN_ : 0.0; }
    double peakDbfs()       const { return 20.0 * std::log10(std::max(peak_, 1e-12)); }
    double minActiveRmsDb() const { return minActiveRms_ < 1e8 ? minActiveRms_ : -120.0; }
    double maxRmsDb()       const { return maxRms_ > -1e8 ? maxRms_ : -120.0; }
    double dynamicRangeDb() const { return std::max(0.0, maxRmsDb() - minActiveRmsDb()); }

    // median F0 of the performance (Hz), 0 if unknown - used to adapt the HPF
    // Pitch, for the audio thread.  lastF0_ is written here and read there, so it is
    // published through atomics; a torn double would put the tracked band at a
    // random frequency, which is a chirp, not a rounding error.
    //
    // The estimate arrives once per analysis pass (~4 Hz on the plugin timer).  That
    // is enough for a band that follows a *note*: at Q = 6 the band is ~17 % wide and
    // vibrato is +-1.5 %, i.e. the vibrato is inside the band's own bandwidth, so the
    // band does not have to chase it.  Register changes (the thing that actually moves
    // the harmonics out of the band) are much slower than 4 Hz.
    double currentF0() const { return f0Pub_.load(std::memory_order_relaxed); }
    bool   voicedNow()  const { return voicedPub_.load(std::memory_order_relaxed) != 0; }

    double medianF0() const {
        double tot = 0;
        for (int c : f0Hist_) tot += c;
        if (tot <= 0) return 0.0;
        double acc = 0;
        for (int i = 0; i < static_cast<int>(f0Hist_.size()); ++i) {
            acc += f0Hist_[i];
            // the lower edge of the bin.  It is 0-10 Hz below the true median, and every
            // existing decision (low cut, partial rejection, problem report) is tuned to
            // that number - changing it to the bin centre moved the top three resonances
            // and broke gate G3.  The dynamic layers add half a bin themselves, where it
            // matters, instead of moving the number under everything else.
            if (acc >= 0.5 * tot) return 60.0 + 10.0 * i;
        }
        return 0.0;
    }

    // sibilance: median and 90th percentile of (5-10k energy / 1-4k energy) in dB
    void sibilance(double& medDb, double& p90Db) const {
        medDb = p90Db = -60.0;
        double tot = 0; for (int c : sdHist_) tot += c;
        if (tot <= 0) return;
        double acc = 0; bool gotMed = false;
        for (int i = 0; i < static_cast<int>(sdHist_.size()); ++i) {
            acc += sdHist_[i];
            const double frac = acc / tot;
            const double db = sdBinLo_ + i * sdBinW_;
            if (!gotMed && frac >= 0.5) { medDb = db; gotMed = true; }
            if (frac >= 0.9) { p90Db = db; return; }
        }
        p90Db = sdBinLo_ + static_cast<double>(sdHist_.size() - 1) * sdBinW_;
    }

private:
    // 120 ms moving average + 18 dB/s peak hold, on the same band powers the
    // measurement uses.  Cheap: one pass over the bands, no allocation.
    void updateLiveSpectrum() {
        const double hopSec = double(kHop) / (fs_ > 0.0 ? fs_ : 48000.0);
        // attack 30 ms, release 120 ms: fast enough that the curve is obviously
        // following the voice (9 dB of fall in the first 250 ms after a phrase ends),
        // slow enough that it does not flicker on every syllable.
        const double aUp   = std::clamp(hopSec / 0.030, 0.05, 1.0);
        const double aDown = std::clamp(hopSec / 0.120, 0.03, 1.0);
        const double drop  = std::pow(10.0, (-18.0 * hopSec) / 10.0);
        for (int i = 0; i < nBands_; ++i) {
            double p = 0.0;
            const int nB = bandBinHi_[i] - bandBinLo_[i] + 1;
            for (int b = bandBinLo_[i]; b <= bandBinHi_[i]; ++b) p += power_[b];
            p /= std::max(1, nB);
            const double a = (p > liveP_[size_t(i)]) ? aUp : aDown;
            liveP_[size_t(i)] += a * (p - liveP_[size_t(i)]);
            peakP_[size_t(i)] = std::max(liveP_[size_t(i)], peakP_[size_t(i)] * drop);
        }
    }

    // ---- FFT (iterative radix-2, in-place) ----
    void fftInit() {
        std::vector<double> re(kFft), im(kFft);
        cosT_.resize(kFft / 2); sinT_.resize(kFft / 2);
        for (int i = 0; i < kFft / 2; ++i) {
            cosT_[i] = std::cos(-2.0 * kPi * i / kFft);
            sinT_[i] = std::sin(-2.0 * kPi * i / kFft);
        }
        for (int i = 0; i < kFft; ++i) {
            double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (kFft - 1));   // Hann
            window_[i] = w;
            wss_ += w * w;
        }
    }
    void fft(std::vector<double>& re, std::vector<double>& im) {
        for (int i = 1, j = 0; i < kFft; ++i) {
            int bit = kFft >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= kFft; len <<= 1) {
            const int half = len >> 1, step = kFft / len;
            for (int i = 0; i < kFft; i += len) {
                for (int k = 0; k < half; ++k) {
                    const int t = k * step;
                    const double wr = cosT_[t], wi = sinT_[t];
                    const int a = i + k, b = i + k + half;
                    const double xr = re[b] * wr - im[b] * wi;
                    const double xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr;        im[a] += xi;
                }
            }
        }
    }

    // ---- per frame ----
    void processFrame() {
        for (int i = 0; i < kFft; ++i) {
            raw_[i] = ring_[(ringPos_ + i) % kFft];      // kept unwindowed for pitch
            bufRe_[i] = raw_[i] * window_[i];
        }
        std::fill(bufIm_.begin(), bufIm_.end(), 0.0);
        fft(bufRe_, bufIm_);

        double rms = 0.0;
        for (int i = 0; i < kFft; ++i) rms += bufRe_[i] * bufRe_[i];
        // power spectrum (recompute after inverse-free magnitude)
        if (int(power_.size()) != kFft / 2) power_.assign(kFft / 2, 0.0);
        double flatNum = 0.0, flatDen = 0.0;
        for (int b = 1; b < kFft / 2; ++b) {
            const double p = (bufRe_[b] * bufRe_[b] + bufIm_[b] * bufIm_[b]) / (wss_ * fs_);
            power_[b] = p;
            flatNum += std::log(p + 1e-20);
            flatDen += p;
        }
        const int nb = kFft / 2 - 1;
        const double flatness = std::exp(flatNum / nb) / (flatDen / nb + 1e-20);

        // frame level over the analysis band (40 Hz .. 16 kHz)
        double framePower = 0.0;
        for (int b = bandBinLo_[0]; b <= bandBinHi_[nBands_ - 1]; ++b) framePower += power_[b];
        const double rmsDb = 10.0 * std::log10(std::max(framePower, 1e-20));
        (void) rms;

        // ---- robust activity gate over the WHOLE take so far -------------
        // A histogram instead of stored frames: constant memory, and the
        // percentiles are computed exactly like the Python reference so the two
        // implementations agree (this is an acceptance gate, not a detail).
        ++framesSeen_;
        {
            int bin = std::clamp(static_cast<int>(rmsDb) + 120, 0, kLvlBins - 1);
            ++lvlHist_[bin];
            ++lvlCount_;
        }
        double p10 = -100.0, p95 = -20.0;
        if (lvlCount_ > 30) {
            const long target10 = static_cast<long>(0.10 * lvlCount_);
            const long target95 = static_cast<long>(0.95 * lvlCount_);
            long acc = 0; bool g10 = false, g95 = false;
            for (int i = 0; i < kLvlBins; ++i) {
                acc += lvlHist_[i];
                if (!g10 && acc >= target10) { p10 = -120.0 + i; g10 = true; }
                if (!g95 && acc >= target95) { p95 = -120.0 + i; g95 = true; break; }
            }
        }
        const double gate = std::max(p10 + 9.0, p95 - 42.0);

        // same rule as the Python reference: +9 dB over the 10th percentile and
        // no more than 42 dB below the loud end (below that it is noise, not voice)
        const bool active = (lvlCount_ > 30) && (rmsDb > gate) && (rmsDb > -70.0);

        // The display spectrum is updated for *every* frame, active or not.  It used
        // to live inside the active-only part of this function, so during a pause -
        // or the gaps between phrases, which is most of a vocal take - the curve
        // simply froze at the last value and then jumped when the next word arrived.
        // Combined with a mean-of-everything measurement behind it, that is exactly
        // "the analyser is incredibly slow".
        updateLiveSpectrum();

        if (!active) return;

        ++nActive_;
        lastGate_ = gate;
        if (rmsDb < minActiveRms_) minActiveRms_ = rmsDb;
        if (rmsDb > maxRms_) maxRms_ = rmsDb;

        const bool noisy = flatness > 0.30;      // fricative / breath
        if (noisy) ++nNoisy_;
        sumFlat_ += flatness;

        // band powers -> dB, Welford accumulate
        if (int(frameBand_.size()) != nBands_) frameBand_.assign(nBands_, -120.0);
        for (int i = 0; i < nBands_; ++i) {
            // mean spectral density inside the band (NOT the bin sum: bands are
            // log-spaced while bins are linear, so a sum would inflate the top
            // end by 10*log10(bandwidth) and quietly break every HF decision)
            double p = 0.0;
            const int nB = bandBinHi_[i] - bandBinLo_[i] + 1;
            for (int b = bandBinLo_[i]; b <= bandBinHi_[i]; ++b) p += power_[b];
            p /= std::max(1, nB);
            const double db = 10.0 * std::log10(std::max(p, 1e-20));
            frameBand_[i] = db;
            pSum_[i] += p;
            const double d = db - mean_[i];
            mean_[i] += d / nActive_;
            m2_[i] += d * (db - mean_[i]);
        }

        // static-excess statistics: remove a broad (1/3-oct) baseline per frame
        const int sm = 8;                    // +-8 bins of 1/12 oct = 2/3 octave baseline
        for (int i = 0; i < nBands_; ++i) {
            double acc = 0.0; int cnt = 0;
            for (int k = std::max(0, i - sm); k <= std::min(nBands_ - 1, i + sm); ++k) { acc += frameBand_[k]; ++cnt; }
            const double base = acc / cnt;
            const double ex = frameBand_[i] - base;
            const double n = ++exN_[i];
            const double d = ex - exMean_[i];
            exMean_[i] += d / n;
            exM2_[i] += d * (ex - exMean_[i]);
        }

        // ---- harmonic envelope for this frame --------------------------
        envelopeOfFrame();

        // sibilance index for this frame
        {
            double lo = 0, hiE = 0;
            for (int b = 0; b < kFft / 2; ++b) {
                const double fq = b * binHz_;
                if (fq >= 1000 && fq < 4000) lo += power_[b];
                else if (fq >= 5000 && fq < 10000) hiE += power_[b];
            }
            const double ratio = 10.0 * std::log10((hiE + 1e-20) / (lo + 1e-20));
            int bin = static_cast<int>((ratio - sdBinLo_) / sdBinW_);
            bin = std::clamp(bin, 0, static_cast<int>(sdHist_.size()) - 1);
            ++sdHist_[bin];
            sibFrameSum_ += ratio; ++sibFrames_;
        }

        // pitch (for HPF adaptation and harmonic-lock tests), every 3rd frame
        if ((framesSeen_ % 8) == 0) estimateF0();   // cheap enough for the audio thread
    }

    // Build this frame's envelope - the smooth vocal-tract curve with the
    // harmonic ladder taken out.
    //
    // Two earlier versions were wrong, and the gates caught both:
    //   1. interpolating across the harmonics built the envelope from the VALLEYS
    //      (40 dB under the signal).
    //   2. peak picking worked below ~1 kHz and inverted above it, where the
    //      harmonics are closer together than the analysis bands.
    //
    // What is left is the method the literature actually uses, implemented with
    // the FFT the analyser already has: real cepstrum of the log spectrum ->
    // subtract the rahmonics at 1/F0 and its multiples (that is the ladder seen
    // from the quefrency side) -> keep only quefrencies below 3.5 ms (the lifter)
    // -> transform back.  What returns is the log spectrum smoothed at the
    // resolution of the vocal tract instead of the pitch.
    //
    // This is the same algorithm as analysis/scripts/envelope_demo.py, on
    // purpose: two implementations that must agree is a gate, not a coincidence.
    void envelopeOfFrame() {
        const int half = kFft / 2;
        // 1. log spectrum, mirrored so the transform is real and even
        cRe_.assign(kFft, 0.0); cIm_.assign(kFft, 0.0);
        for (int k = 1; k < half; ++k) {
            const double v = std::log(power_[k] + 1e-30);
            cRe_[k] = v; cRe_[kFft - k] = v;
        }
        cRe_[0] = cRe_[1]; cRe_[half] = cRe_[kFft - half] = cRe_[half];
        // 2. real cepstrum = inverse transform of the log spectrum
        fft(cRe_, cIm_);
        for (int q = 0; q < kFft; ++q) cRe_[q] *= 1.0 / kFft;
        // 3. rahmonic subtraction: the ladder is a ripple in the log spectrum with
        //    period F0 Hz, so it sits at quefrency 1/F0 (and k/F0) in the cepstrum
        const double f0 = lastF0_;
        const bool comb = (f0 > 55.0 && f0 < 700.0);
        if (comb) {
            const double q0 = fs_ / f0;                 // samples per period
            // notch width in quefrency, not in periods: the rahmonics of a
            // 150 Hz voice sit at 294 samples and of a 350 Hz voice at 126, and
            // scaling the width with q0 (a first attempt) ate the envelope along
            // with the rahmonic and made the pitch dependence *worse*
            const double width = std::max(1.5, 0.00018 * fs_);
            const double peak = cRe_[int(std::round(q0))];
            for (int k = 1; k <= 4; ++k) {
                const double qc = k * q0;
                if (qc > kFft * 0.5) break;
                const int lo = std::max(1, int(qc - 4 * width));
                const int hi = std::min(kFft / 2 - 1, int(qc + 4 * width));
                for (int q = lo; q <= hi; ++q) {
                    const double w = std::exp(-0.5 * std::pow((q - qc) / width, 2.0));
                    cRe_[q] -= w * peak;
                    cRe_[kFft - q] -= w * peak;
                }
            }
        }
        // 4. the lifter: keep the slow part only (3.5 ms)
        const int qMax = std::max(8, int(0.0035 * fs_));
        for (int q = qMax + 1; q < kFft - qMax; ++q) { cRe_[q] = 0.0; cIm_[q] = 0.0; }
        // 5. back to the frequency domain: the envelope, in ln-power per bin
        fft(cRe_, cIm_);
        // accumulate per analysis band
        for (int i = 0; i < nBands_; ++i) {
            const double f = grid_.f[i];
            const bool valid = comb ? (f >= 0.9 * f0) : true;
            envValid_[i] = valid;
            if (!valid) { frameEnv_[i] = -120.0; continue; }
            double acc = 0.0; int n = 0;
            // no extra 1/N here: the cepstrum already carries it (cep = F(L)/N),
            // so the way back is a plain forward transform.  Getting this wrong
            // scales the envelope by 1/8192 - it reads as silence, not as an error.
            for (int b = bandBinLo_[i]; b <= bandBinHi_[i]; ++b) { acc += cRe_[b]; ++n; }
            frameEnv_[i] = n ? acc / n * 4.342944819 : -120.0;      // ln -> dB
        }
        for (int b = 0; b < nBands_; ++b) {
            if (!envValid_[b]) continue;
            const double v = frameEnv_[b];
            const int n = ++envN_[b];
            const double d = v - envMean_[b];
            envMean_[b] += d / n;
            envM2_[b] += d * (v - envMean_[b]);
        }
    }

    void ringDrop() {}

    // YIN (de Cheveigne & Kawahara 2002), ported from the validated Python
    // reference: difference function -> cumulative-mean normalisation ->
    // absolute threshold -> parabolic refinement.  Plain autocorrelation locks
    // onto the 2nd/3rd harmonic on high-passed vocals, which is why this exists.
    void estimateF0() {
        // Use the central, un-windowed part of the frame: running YIN on a
        // Hann-tapered frame biases the lag estimate (the taper modulates the
        // waveform and the tracker jumps an octave).
        const int N = kFft / 2;                     // 93 ms integration window
        const int off = (kFft - N) / 2;
        const double* src = raw_ + off;
        const int tauMax = std::min(static_cast<int>(fs_ / 65.0), N / 2);
        const int tauMin = std::max(2, static_cast<int>(fs_ / 600.0));
        const int W = N - tauMax;
        if (W < 128) return;

        // members, not thread_local statics: a static vector allocates on first
        // use, and "first use" was the first audio callback
        auto& d  = yinD_;
        auto& cm = yinCm_;
        if (int(d.size()) < tauMax + 1) { d.assign(size_t(tauMax) + 1, 0.0); cm.assign(size_t(tauMax) + 1, 0.0); }
        std::fill(d.begin(), d.end(), 0.0);
        std::fill(cm.begin(), cm.end(), 0.0);

        double e0 = 0.0;
        for (int i = 0; i < W; ++i) e0 += src[i] * src[i];
        if (e0 < 1e-12) { voicedPub_.store(0, std::memory_order_relaxed); return; }

        // difference function
        for (int tau = 1; tau <= tauMax; ++tau) {
            double s = 0.0, e1 = 0.0;
            for (int i = 0; i < W; ++i) {
                const double diff = src[i] - src[i + tau];
                s += diff * diff;
                e1 += src[i + tau] * src[i + tau];
            }
            d[tau] = s;
        }
        // cumulative mean normalised difference
        cm[0] = 1.0;
        double run = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau) {
            run += d[tau];
            cm[tau] = run > 1e-20 ? d[tau] * tau / run : 1.0;
        }
        // absolute threshold: first dip under 0.20, else the global minimum
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau) {
            if (cm[tau] < 0.20) {
                while (tau + 1 <= tauMax && cm[tau + 1] < cm[tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) {
            double bv = 1e9;
            for (int tau = tauMin; tau <= tauMax; ++tau) if (cm[tau] < bv) { bv = cm[tau]; best = tau; }
            if (bv > 0.45) { voicedPub_.store(0, std::memory_order_relaxed); return; }   // too aperiodic to call it a pitch
        }
        // parabolic refinement
        double tauF = best;
        if (best > 1 && best < tauMax) {
            const double a = cm[best - 1], b = cm[best], c = cm[best + 1];
            const double den = a - 2 * b + c;
            if (std::abs(den) > 1e-12) tauF = best + 0.5 * (a - c) / den;
        }
        const double f0 = fs_ / std::max(tauF, 1.0);
        if (f0 < 60 || f0 > 600) { voicedPub_.store(0, std::memory_order_relaxed); return; }
        lastF0_ = f0;                       // held between estimates, used by the envelope
        f0Pub_.store(f0, std::memory_order_relaxed);
        voicedPub_.store(1, std::memory_order_relaxed);
        int bin = std::clamp(static_cast<int>((f0 - 60.0) / 10.0), 0, static_cast<int>(f0Hist_.size()) - 1);
        ++f0Hist_[bin];
    }

    double fs_ = 48000.0, binHz_ = 11.7, wss_ = 0.0;
    BandGrid grid_;
    int nBands_ = 0;
    std::vector<int> bandBinLo_, bandBinHi_;
    std::vector<double> cosT_, sinT_;
    double window_[kFft]{};
    double raw_[kFft]{};
    std::vector<double> bufRe_{std::vector<double>(kFft)};
    std::vector<double> bufIm_{std::vector<double>(kFft)};
    std::vector<double> power_;
    std::vector<double> frameBand_;

    double ring_[kFft]{};
    int ringPos_ = 0, framesInRing_ = 0, sinceHop_ = 0;

    double mean_[256]{}, m2_[256]{};
    std::vector<double> yinD_, yinCm_;          // pitch scratch, sized in prepare (see prepare())
    std::vector<double> pSum_{256, 0.0};
    // Live display spectrum.  bandLevelsDb() is the *measurement* - the mean of
    // everything heard since load - which is exactly what the solver needs and
    // exactly what a moving display must not be: after ten seconds it stops
    // moving, so the analyser looked broken ("كمية بطء في الأنالايزر رهيب").
    // These two run on their own: a 120 ms moving average for the curve, and a
    // peak hold that falls 18 dB/s for the outline.
    std::vector<double> liveP_, peakP_;   // sized in prepare()/reset() - a
                                         // std::vector<double>{256, 0.0} is two
                                         // elements, not 256, which is a heap
                                         // overflow waiting to be written
    double exMean_[256]{}, exM2_[256]{}, exN_[256]{};
    std::vector<double> cRe_, cIm_;             // scratch for the cepstral lift
    std::vector<double> frameEnv_;              // this frame's envelope, dB
    std::vector<bool>   envValid_;              // above the fundamental
    std::array<double, 256> envMean_{}, envM2_{};
    std::array<int, 256>    envN_{};
    double lastF0_ = 0.0;
    std::atomic<double> f0Pub_{0.0};
    std::atomic<int>    voicedPub_{0};
    int nFrames_ = 0, nActive_ = 0, nNoisy_ = 0;
    long framesSeen_ = 0;
    static constexpr int kLvlBins = 130;         // -120 .. +10 dBFS, 1 dB bins
    std::array<long, kLvlBins> lvlHist_{};
    long lvlCount_ = 0;
    double peak_ = 0; long clipCount_ = 0; double dcSum_ = 0; long dcN_ = 0;
    double minActiveRms_ = 1e9, maxRms_ = -1e9;
    double lastGate_ = -100.0;
    double sumFlat_ = 0.0, sibFrameSum_ = 0.0; int sibFrames_ = 0;
    std::vector<int> f0Hist_;

    static constexpr double sdBinLo_ = -40.0, sdBinW_ = 1.0;
    std::array<int, 90> sdHist_{};
    long totalSamples_ = 0;
};

} // namespace aeq
