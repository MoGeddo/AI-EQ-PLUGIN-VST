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
#include <typeinfo>

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

// Breadcrumbs.  A renderer that dies inside a DAW-less CI session has to leave a
// trace of the step it died on, or the only thing anyone learns is "exit code
// -1073741819".  Written unbuffered, to stdout and to a file next to the images.
juce::File g_log;
void stage(const char* what) {
    std::printf("[shot] %s\n", what);
    std::fflush(stdout);
    if (g_log != juce::File())
        g_log.appendText (juce::String("[shot] ") + what + "\n");
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not eat the breadcrumbs
    stage("start");
    const juce::ScopedJuceInitialiser_GUI init;
    stage("juce gui initialised");

    juce::File dir = juce::File::getCurrentWorkingDirectory();
    if (argc > 1) {
        const juce::File given (juce::String::fromUTF8 (argv[1]));
        dir = juce::File::isAbsolutePath (argv[1]) ? given : dir.getChildFile (argv[1]);
    }
    dir.createDirectory();
    g_log = dir.getChildFile ("shot.log.txt");
    stage (("output directory " + dir.getFullPathName()).toRawUTF8());

    try {

    AeqAudioProcessor proc;
    stage("processor constructed");
    proc.prepareToPlay(48000.0, 512);
    proc.setLearning(true);
    stage("prepared");

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
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
    }
    proc.core().refresh();
    std::printf("analysis: %d %.1f s, valid=%d settled=%d, %d bands\n",
                (int) proc.core().state().problems.size(),
                proc.core().state().analyzedSeconds,
                (int) proc.core().state().valid,
                (int) proc.core().state().settled,
                (int) proc.core().activeBands().size());

    // ---- probe 1: can this process paint an image at all? -----------------
    // A headless CI session has no desktop and possibly no Direct2D device.  If
    // even a filled rectangle crashes, the problem is the renderer and not this
    // plugin; if only the editor crashes, it is our own paint code.  The
    // breadcrumbs say which, instead of "access violation".
    stage("probe: plain image");
    {
        juce::Image probe (juce::Image::RGB, 64, 24, true);
        juce::Graphics pg (probe);
        pg.setColour (juce::Colours::red);
        pg.fillRect (probe.getBounds());
        savePng (probe, dir.getChildFile ("probe-plain.png"));
    }

    // ---- probe 2: the embedded font, shaped ---------------------------------
    stage("probe: embedded font");
    {
        juce::Image probe (juce::Image::RGB, 480, 90, true);
        juce::Graphics pg (probe);
        pg.fillAll (juce::Colour::fromRGB (0x10, 0x12, 0x16));
        aeq::ui::draw (pg, juce::String::fromUTF8 (u8"AEQ Restore — الاختبار: خط عربي"),
                       probe.getBounds().toFloat().reduced (8.0f),
                       juce::Justification::centredLeft, 22.0f, juce::Colours::white);
        savePng (probe, dir.getChildFile ("probe-font.png"));
    }

    // ---- probe 3: the components, one at a time -----------------------------
    // The editor as a whole crashed under createComponentSnapshot while a plain
    // image and a shaped string did not, so the fault is in our own paint code.
    // Each component is snapshotted on its own; the breadcrumb names the one that
    // dies instead of leaving an access violation to guess about.
    {
        stage("probe: curve view");
        aeq::ui::CurveView cv;
        cv.setSize (846, 540);
        {
            aeq::ui::CurveView::Data d;
            d.valid = true;
            d.settled = true;
            d.analysedSeconds = 21.4;
            cv.setData (std::move (d));
        }
        savePng (cv.createComponentSnapshot (cv.getLocalBounds(), false, 1.0f),
                 dir.getChildFile ("probe-curve-empty.png"));
    }
    {
        stage("probe: problem list");
        aeq::ui::ProblemList pl;
        pl.setSize (330, 400);
        std::vector<aeq::Problem> probs;
        for (int i = 0; i < 3; ++i) {
            aeq::Problem p;
            p.labelAr = "زيادة في المنطقة المنخفضة";
            p.detailAr = "قياس ٢٤٠ هرتز أعلى من المعيار بـ ٤ dB";
            probs.push_back (p);
        }
        pl.setProblems (probs);
        pl.setSize (330, juce::jmax (400, pl.contentHeight()));
        savePng (pl.createComponentSnapshot (pl.getLocalBounds(), false, 1.0f),
                 dir.getChildFile ("probe-problems.png"));
    }
    stage("probe: components done");

    // ---- probe 4: the real report, one card at a time -----------------------
    // The list crashes with the real problems and not with three invented ones.
    // Each card is rendered alone, with its numbers in the log, so the one that
    // dies names its own data.
    {
        const auto probs = proc.core().state().problems;
        stage((juce::String ("real problems: ") + juce::String (int (probs.size()))).toRawUTF8());
        for (size_t i = 0; i < probs.size(); ++i) {
            const auto& pp = probs[i];
            stage((juce::String ("card ") + juce::String (int (i))
                   + " action=" + juce::String (int (pp.action))
                   + " f=" + juce::String (pp.freq, 1)
                   + " q=" + juce::String (pp.q, 3)
                   + " g=" + juce::String (pp.gainDb, 2)
                   + " sev=" + juce::String (pp.severity, 3)
                   + " label=" + juce::String (int (pp.labelAr.size())) + "B"
                   + " detail=" + juce::String (int (pp.detailAr.size())) + "B").toRawUTF8());
            // the strings themselves, in hex, and drawn on their own: if a string
            // kills the shaper, that is not the card's fault
            if (i == 2) {
                juce::String hex = "detail bytes: ";
                for (unsigned char ch : pp.detailAr) hex << juce::String::toHexString ((int) ch).paddedLeft ('0', 2) << " ";
                stage (hex.toRawUTF8());
                juce::String hex2 = "label bytes: ";
                for (unsigned char ch : pp.labelAr) hex2 << juce::String::toHexString ((int) ch).paddedLeft ('0', 2) << " ";
                stage (hex2.toRawUTF8());

                const juce::String strs[] = { juce::String::fromUTF8 (pp.labelAr.c_str()),
                                              juce::String::fromUTF8 (pp.detailAr.c_str()) };
                for (int k = 0; k < 2; ++k) {
                    stage (("string " + juce::String (k) + " alone").toRawUTF8());
                    juce::Image im (juce::Image::RGB, 320, 60, true);
                    juce::Graphics gg (im);
                    gg.fillAll (Palette::surface());
                    aeq::ui::draw (gg, strs[k], im.getBounds().toFloat().reduced (4.0f),
                                   juce::Justification::topRight, k == 0 ? 13.0f : 11.5f,
                                   juce::Colours::white);
                    savePng (im, dir.getChildFile ("probe-str-" + juce::String (k) + ".png"));
                }
                stage ("strings survived");
            }
            aeq::ui::ProblemList one;
            one.setSize (336, 400);
            one.setProblems (std::vector<aeq::Problem> { pp });
            savePng (one.createComponentSnapshot (one.getLocalBounds(), false, 1.0f),
                     dir.getChildFile ("probe-card-" + juce::String (int (i)) + ".png"));
        }
        stage("all cards survived");
    }

    stage("analysis fed");
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    if (editor == nullptr) { stage("no editor"); return 1; }
    stage("editor constructed");

    // Which child of the editor dies?  One snapshot each, one breadcrumb each.
    editor->setSize (1240, 800);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (700);
    for (int i = 0; i < editor->getNumChildComponents(); ++i) {
        auto* c = editor->getChildComponent (i);
        if (c == nullptr || c->getWidth() <= 0 || c->getHeight() <= 0) continue;
        stage ((juce::String ("child ") + juce::String (i) + " " + typeid (*c).name()
                + " " + c->getBounds().toString()).toRawUTF8());
        // A viewport is a container: snapshot what it *holds* first, so a crash
        // says whether the content or the container is at fault
        if (auto* vp = dynamic_cast<juce::Viewport*> (c)) {
            if (auto* viewed = vp->getViewedComponent()) {
                stage (("viewport content " + juce::String (viewed->getWidth()) + "x"
                        + juce::String (viewed->getHeight())).toRawUTF8());
                savePng (viewed->createComponentSnapshot (viewed->getLocalBounds(), false, 1.0f),
                         dir.getChildFile ("probe-viewed.png"));
                stage ("viewport content survived");
            }
        }
        stage ("snapshot child");
        savePng (c->createComponentSnapshot (c->getLocalBounds(), false, 1.0f),
                 dir.getChildFile ("probe-child-" + juce::String (i) + ".png"));
    }
    stage ("children survived");

    struct Size { int w, h; const char* name; };
    const Size sizes[] = { { 1240, 800, "ui-1240x800.png" },
                           { 1024, 700, "ui-1024x700.png" } };

    for (const auto& s : sizes) {
        stage((juce::String("setSize ") + juce::String(s.w) + "x" + juce::String(s.h)).toRawUTF8());
        editor->setSize(s.w, s.h);
        // let the editor's 12 Hz timer fire a few times: it is what pulls the
        // analysis into the window, and a snapshot taken before it would show the
        // empty state
        juce::MessageManager::getInstance()->runDispatchLoopUntil(700);
        stage("timer pumped");
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);
        stage("snapshot taken");
        savePng (img, dir.getChildFile (s.name));
    }

    editor.reset();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    stage("done");
    return 0;
    } catch (const std::exception& e) {
        stage ((juce::String ("exception: ") + e.what()).toRawUTF8());
    } catch (...) {
        stage ("unknown exception");
    }
    return 1;
}
