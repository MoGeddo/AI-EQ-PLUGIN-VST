// ui_shot.cpp - a real screenshot, produced by the real paint code.
//
// docs/ui_preview.png was drawn with Python.  It was a drawing of the interface,
// not the interface, and it is exactly the kind of claim this project is not
// supposed to make.  This program builds the actual plugin, runs a synthetic take
// through the actual processor until the analysis settles, constructs the actual
// editor, lets its timer fire, and asks JUCE to snapshot the component tree.
//
// It runs on Windows in CI, which is the important part: the font and shaping
// paths that turned Arabic into boxes in 1.2.0 are the Windows ones (DirectWrite),
// so a screenshot taken anywhere else would not test the thing that broke.
//
//   aeq_shot <output-directory>
//
// The take is synthetic and the window says so in the caption; nothing here
// pretends a synthetic voice is a recording.

#include <juce_audio_formats/juce_audio_formats.h>

#include <cinttypes>
#include <random>

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// The same glottal source the acceptance tests use: harmonics of f0 with vibrato,
// phrase gating and sibilant noise, so the analyser sees a voice-like spectrum
// rather than a sine.
std::vector<float> voice(double f0, double seconds, unsigned seed = 7) {
    const int n = int(seconds * 48000.0);
    std::vector<float> x(size_t(n), 0.0f);
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, 1.0);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / 48000.0;
        const double vib = 1.0 + 0.012 * std::sin(2 * kPi * 5.2 * t);
        phase += 2 * kPi * f0 * vib / 48000.0;
        double s = 0.0;
        for (int k = 1; k <= 24; ++k) s += std::sin(k * phase) / std::pow(k, 1.1);
        const double cyc = std::fmod(t, 1.55);
        const double env = (cyc < 1.2) ? 1.0 : 0.0;
        double sib = 0.0;
        if (cyc > 1.05 && cyc < 1.2) sib = 0.25 * nd(rng);
        x[size_t(i)] = float(0.12 * env * s + sib);
    }
    return x;
}

// A plain one-pole low shelf and a bell, written by hand so the harness does not
// need the DSP headers: this is a take with a boom and a mud hump, i.e. something
// the plugin is supposed to have an opinion about.
void addFaults(std::vector<float>& x) {
    double lp = 0.0;                       // ~120 Hz boom
    const double aLp = 1.0 - std::exp(-2.0 * kPi * 120.0 / 48000.0);
    double b1 = 0.0, b2 = 0.0;             // resonant bell at 230 Hz
    const double w = 2.0 * kPi * 230.0 / 48000.0, q = 1.1;
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double bb0 = (1.0 + alpha * 0.0 + 1.4 * alpha) / a0;
    const double bb2 = (1.0 - 1.4 * alpha) / a0;
    const double aa1 = (-2.0 * std::cos(w)) / a0;
    const double aa2 = (1.0 - alpha) / a0;
    for (auto& v : x) {
        const double in = v;
        lp += aLp * (in - lp);
        const double y = bb0 * in + b1;
        b1 = bb2 * in - aa1 * y + b2;
        b2 = -aa2 * y;
        v = float(in + 0.55 * lp + 0.16 * y);
    }
}

void savePng(const juce::Image& img, const juce::File& file) {
    file.deleteFile();
    juce::FileOutputStream out(file);
    if (! out.openedOk()) { std::printf("cannot write %s\n", file.getFullPathName().toRawUTF8()); return; }
    juce::PNGImageFormat png;
    png.writeImageToStream(img, out);
    std::printf("wrote %s (%d x %d, %" PRId64 " bytes)\n", file.getFullPathName().toRawUTF8(),
                img.getWidth(), img.getHeight(), (juce::int64) file.getSize());
}

} // namespace

int main(int argc, char** argv) {
    const juce::ScopedJuceInitialiser_GUI init;

    juce::File dir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                              : juce::File::getCurrentWorkingDirectory();
    dir.createDirectory();

    AeqAudioProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.setLearning(true);

    // feed the take through processBlock exactly as a DAW would
    auto take = voice(196.0, 26.0);        // G3, 26 s
    addFaults(take);

    juce::AudioBuffer<float> buf(2, 512);
    const int blocks = int(take.size()) / 512;
    for (int b = 0; b < blocks; ++b) {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample(ch, i, take[size_t(b * 512 + i)] * (ch == 0 ? 1.0f : 0.97f));
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        if (b % 40 == 0) {                 // what the processor's timer does
            proc.core().refresh();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    }
    proc.core().refresh();

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    if (editor == nullptr) { std::printf("no editor\n"); return 1; }

    struct Size { int w, h; const char* name; };
    const Size sizes[] = { { 1240, 800, "ui-1240x800.png" },
                           { 1024, 700, "ui-1024x700.png" } };

    for (const auto& s : sizes) {
        editor->setSize(s.w, s.h);
        // let the editor's 12 Hz timer fire a few times: it is what pulls the
        // analysis into the window, and a snapshot taken before it would show the
        // empty state
        juce::MessageManager::getInstance()->runDispatchLoopUntil(700);
        const auto img = editor->createComponentSnapshot(editor->getLocalBounds(), false, 1.0f);
        savePng(img, dir.getChildFile(s.name));
    }

    editor.reset();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    return 0;
}
