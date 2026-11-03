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
//   aeq_shot <output-directory> [window|components|cards|all]
//
// One stage per process, because an access violation takes the process with it:
// running the window and the components separately means a crash while rendering
// one of them cannot hide the other, and the exit code says which died.
//
// The take is synthetic, and nothing here pretends a synthetic voice is a
// recording.

#include <juce_audio_formats/juce_audio_formats.h>

#include <cinttypes>
#include <cstdlib>
#include <random>
#include <string>
#include <typeinfo>

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// Breadcrumbs.  A renderer that dies inside a CI session has to leave a trace of
// the step it died on, or the only thing anyone learns is "exit code -1073741819".
juce::File g_log;
void stage(const juce::String& what) {
    const auto line = "[shot] " + what;
    std::printf("%s\n", line.toRawUTF8());
    std::fflush(stdout);
    if (g_log != juce::File()) g_log.appendText(line + "\n");
}

// The same glottal source the acceptance tests use.
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

// A boom under 120 Hz and a bell at 230 Hz: something the plugin has an opinion
// about, so the window is not a picture of an empty state.
void addFaults(std::vector<float>& x) {
    double lp = 0.0;
    const double aLp = 1.0 - std::exp(-2.0 * kPi * 120.0 / 48000.0);
    double b1 = 0.0, b2 = 0.0;
    const double w = 2.0 * kPi * 230.0 / 48000.0, q = 1.1;
    const double alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha;
    const double bb0 = (1.0 + 1.4 * alpha) / a0, bb2 = (1.0 - 1.4 * alpha) / a0;
    const double aa1 = (-2.0 * std::cos(w)) / a0, aa2 = (1.0 - alpha) / a0;
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
    if (! img.isValid()) { stage("refusing to write an invalid image"); return; }
    file.deleteFile();
    juce::FileOutputStream out(file);
    if (! out.openedOk()) { stage("cannot open " + file.getFullPathName()); return; }
    juce::PNGImageFormat png;
    png.writeImageToStream(img, out);
    out.flush();
    std::printf("     wrote %s (%d x %d, %" PRId64 " bytes)\n", file.getFileName().toRawUTF8(),
                img.getWidth(), img.getHeight(), (juce::int64) file.getSize());
    std::fflush(stdout);
}

// Run the synthetic take through the processor exactly as a DAW would.
//
// `faults` decides which take: with them, the report is full and the curve shows
// a correction; without them, the window is in its "nothing to fix" state.
void analyse(AeqAudioProcessor& proc, bool faults) {
    proc.prepareToPlay(48000.0, 512);
    proc.setLearning(true);
    auto take = voice(196.0, 26.0);
    if (faults) addFaults(take);

    juce::AudioBuffer<float> buf(2, 512);
    const int blocks = int(take.size()) / 512;
    for (int b = 0; b < blocks; ++b) {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample(ch, i, take[size_t(b * 512 + i)] * (ch == 0 ? 1.0f : 0.97f));
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        if (b % 40 == 0) {
            proc.core().refresh();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    }
    proc.core().refresh();
    const auto st = proc.core().state();
    stage(juce::String("analysis: ") + juce::String(int(st.problems.size())) + " findings, "
          + juce::String(st.analyzedSeconds, 1) + " s, valid=" + juce::String(int(st.valid))
          + ", settled=" + juce::String(int(st.settled)) + ", "
          + juce::String(int(proc.core().activeBands().size())) + " bands");
}

int runWindow(const juce::File& dir) {
    // The window is rendered from a take that needs no correction.  Why: on a
    // headless Windows runner, drawing a report card whose text mixes Arabic with
    // Latin and digits crashes inside DirectWrite's text layout (the same card
    // renders in a desktop session - the 1.2.0 build showed them in FL Studio).
    // That is a limitation of the runner, and it should not decide whether the
    // *window* gets photographed.  The report itself is rendered, with the faulty
    // take, by the components stage.
    AeqAudioProcessor proc;
    analyse(proc, false);

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    if (editor == nullptr) { stage("no editor"); return 1; }
    stage("editor constructed");

    struct Size { int w, h; const char* name; };
    const Size sizes[] = { { 1240, 800, "ui-1240x800.png" }, { 1024, 700, "ui-1024x700.png" } };

    for (const auto& s : sizes) {
        stage(juce::String("window ") + juce::String(s.w) + "x" + juce::String(s.h));
        editor->setSize(s.w, s.h);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(700);
        stage("timer pumped");
        const auto img = editor->createComponentSnapshot(editor->getLocalBounds(), false, 1.0f);
        stage("snapshot taken");
        savePng(img, dir.getChildFile(s.name));
    }
    return 0;
}

int runComponents(const juce::File& dir) {
    AeqAudioProcessor proc;
    analyse(proc, true);

    stage("curve view");
    {
        aeq::ui::CurveView cv;
        cv.setSize(846, 526);
        aeq::ui::CurveView::Data d;
        d.meas = proc.core().measurements();
        d.bands = proc.core().activeBands();
        d.problems = proc.core().state().problems;
        d.valid = true;
        d.settled = proc.core().state().settled;
        d.analysedSeconds = proc.core().state().analyzedSeconds;
        cv.setData(std::move(d));
        savePng(cv.createComponentSnapshot(cv.getLocalBounds(), false, 1.0f),
                dir.getChildFile("ui-curve.png"));
    }

    stage("problem list");
    {
        aeq::ui::ProblemList pl;
        pl.setSize(336, 400);
        pl.setProblems(proc.core().state().problems);
        pl.setSize(336, pl.contentHeight());
        savePng(pl.createComponentSnapshot(pl.getLocalBounds(), false, 1.0f),
                dir.getChildFile("ui-report.png"));
    }
    return 0;
}

int runCards(const juce::File& dir) {
    AeqAudioProcessor proc;
    analyse(proc, true);
    const auto probs = proc.core().state().problems;
    for (size_t i = 0; i < probs.size(); ++i) {
        const auto& p = probs[i];
        stage(juce::String("card ") + juce::String(int(i)) + " " + juce::String::fromUTF8(p.labelAr.c_str())
              + " action=" + juce::String(int(p.action)) + " f=" + juce::String(p.freq, 0)
              + " sev=" + juce::String(p.severity, 2));
        // Which line of the card is fatal?  Render the same card three times with
        // one line removed each time; component snapshots are the path the real UI
        // uses, so these probes can be trusted (a plain Image drops every glyph).
        const char* variant[] = { "detail removed", "title removed", "both removed" };
        for (int v = 0; v < 3; ++v) {
            if (i != 2) break;
            auto q = p;
            if (v == 0 || v == 2) q.detailAr.clear();
            if (v == 1 || v == 2) q.labelAr.clear();
            stage(juce::String("card 2, ") + variant[v]);
            aeq::ui::ProblemList sv;
            sv.setSize(336, 400);
            sv.setProblems(std::vector<aeq::Problem>{ q });
            savePng(sv.createComponentSnapshot(sv.getLocalBounds(), false, 1.0f),
                    dir.getChildFile("probe-card2-" + juce::String(v) + ".png"));
        }

        aeq::ui::ProblemList one;
        one.setSize(336, 400);
        one.setProblems(std::vector<aeq::Problem>{ p });
        savePng(one.createComponentSnapshot(one.getLocalBounds(), false, 1.0f),
                dir.getChildFile("probe-card-" + juce::String(int(i)) + ".png"));
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not eat the breadcrumbs

    juce::File dir = juce::File::getCurrentWorkingDirectory();
    if (argc > 1) {
        const juce::File given(juce::String::fromUTF8(argv[1]));
        dir = juce::File::isAbsolutePath(argv[1]) ? given : dir.getChildFile(argv[1]);
    }
    dir.createDirectory();

    const juce::String mode = argc > 2 ? juce::String::fromUTF8(argv[2]) : juce::String("all");

    const juce::ScopedJuceInitialiser_GUI init;
    g_log = dir.getChildFile("shot.log.txt");
    stage("start, mode " + mode + ", output " + dir.getFullPathName());

    // Can this process paint an image at all, and can it shape the embedded font?
    // Both are cheap, and both answer a question a stack trace would not.
    {
        stage("probe plain image");
        juce::Image probe(juce::Image::ARGB, 64, 24, true);
        juce::Graphics pg(probe);
        pg.setColour(juce::Colours::red);
        pg.fillRect(probe.getBounds());
        savePng(probe, dir.getChildFile("probe-plain.png"));
    }
    {
        stage("probe embedded font");
        juce::Image probe(juce::Image::ARGB, 480, 90, true);
        juce::Graphics pg(probe);
        pg.fillAll(aeq::ui::Palette::surface());
        aeq::ui::draw(pg, juce::String::fromUTF8(u8"AEQ Restore — الخط العربي داخل الـplugin"),
                      probe.getBounds().toFloat().reduced(8.0f),
                      juce::Justification::centredLeft, 22.0f, juce::Colours::white);
        savePng(probe, dir.getChildFile("probe-font.png"));
    }

    int code = 0;
    try {
        if (mode == "window" || mode == "all")     code |= runWindow(dir);
        if (mode == "components" || mode == "all") code |= runComponents(dir);
        if (mode == "cards")                       code |= runCards(dir);
    } catch (const std::exception& e) {
        stage(juce::String("exception: ") + e.what());
        return 1;
    } catch (...) {
        stage("unknown exception");
        return 1;
    }
    stage("done");
    return code;
}
