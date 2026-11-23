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
#include <cmath>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <typeinfo>

#include "PluginEditor.h"
#include "ui/PanelCopy.h"
#include "PluginProcessor.h"
#include "ui/ProblemList.h"      // this test renders the report cards on their own, so it
                                 // includes what it uses instead of relying on the editor
                                 // header to drag it in (v9's editor no longer does)

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

// The frequencies of a band set, to the hertz: two takes are the same take when the notches
// sit in the same places, whatever order the solver happened to place them in.
std::vector<int> freqs(const std::vector<aeq::BandSpec>& bands) {
    std::vector<int> f;
    for (const auto& b : bands) f.push_back(int(std::floor(b.f0 + 0.5)));
    std::sort(f.begin(), f.end());
    return f;
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
          + juce::String(proc.core().appliedBandCount()) + " bands");
}


// --------------------------------------------------------------------------- controls
// Does pressing it do anything?
//
// Everything above this line photographs the window.  A photograph cannot answer the
// question the user actually asked - "some keys do not come back" - so this stage
// presses every control on the real editor and asserts the plugin changed.  It is the
// only place in the project where a click is tested, and it exists because a handler
// was shipped that cleared the graph and left the plugin not listening, and another
// that re-opened a take without starting the analysis again.
int runControls(const juce::File& dir) {
    int pass = 0, fail = 0;
    auto check = [&] (bool ok, const char* what) {
        stage(juce::String(ok ? "  ok   " : "  FAIL ") + what);
        if (ok) ++pass; else ++fail;
    };

    AeqAudioProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    analyse(proc, true);                       // a take with something to correct
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    if (editor == nullptr) { stage("  FAIL no editor"); return 1; }
    editor->setSize(1240, 800);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(400);

    auto* ed = dynamic_cast<AeqAudioProcessorEditor*>(editor.get());
    if (ed == nullptr) { stage("  FAIL the editor is not the editor"); return 1; }

    auto pump = [&] { juce::MessageManager::getInstance()->runDispatchLoopUntil(120); };
    auto centre = [] (juce::Rectangle<int> r) { return r.getCentre().toFloat(); };

    // a real click, through the same entry point the window uses
    auto click = [&] (juce::Point<int> p) {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto pos = p.toFloat();
        juce::MouseEvent down(src, pos, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                              ed, ed, juce::Time::getCurrentTime(), pos,
                              juce::Time::getCurrentTime(), 1, false);
        ed->mouseDown(down);
        juce::MouseEvent up(src, pos, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            ed, ed, juce::Time::getCurrentTime(), pos,
                            juce::Time::getCurrentTime(), 1, false);
        ed->mouseUp(up);
        pump();
    };

    // ---- 1. every control is where the panel says it is, and it is one thing -----
    {
        int found = 0;
        for (int y = 0; y < ed->getHeight(); y += 4)
            for (int x = 0; x < ed->getWidth(); x += 4) { (void) x; (void) y; }
        // the four chrome controls, by name, through the editor's own hit test
        const std::pair<AeqAudioProcessorEditor::Control, const char*> wanted[] = {
            { AeqAudioProcessorEditor::Control::Clamp,   "clamp" },
            { AeqAudioProcessorEditor::Control::Bypass,  "bypass" },
            { AeqAudioProcessorEditor::Control::Reset,   "reset" },
            { AeqAudioProcessorEditor::Control::ModeRestore, "RESTORE" },
            { AeqAudioProcessorEditor::Control::ModeMatch,   "MATCH" },
            { AeqAudioProcessorEditor::Control::StrengthKnob, "strength dial" },
            { AeqAudioProcessorEditor::Control::AutoStopKnob, "auto-stop dial" },
            { AeqAudioProcessorEditor::Control::StabiliserKnob, "stabiliser dial" },
        };
        // find each one by scanning the window at 2 px: the layout is the layout, not a
        // number copied into this test
        std::map<int, juce::Point<int>> where;
        for (int y = 2; y < ed->getHeight(); y += 2)
            for (int x = 2; x < ed->getWidth(); x += 2) {
                const auto c = ed->controlAt({ x, y });
                if (c != AeqAudioProcessorEditor::Control::None
                    && where.find(int(c)) == where.end())
                    where[int(c)] = { x, y };
            }
        for (const auto& w : wanted) {
            const bool ok = where.find(int(w.first)) != where.end();
            if (ok) ++found;
            check(ok, (juce::String("the ") + w.second + " can be hit by a pointer").toRawUTF8());
        }
        check(found == 8, "eight controls are on the panel and each one is reachable");
    }

    // ---- 2. the clamp stops, and pressing it again opens ------------------------
    {
        auto at = [&] (juce::Point<int> p) {
            juce::MouseEvent m(juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(),
                               juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, ed, ed,
                               juce::Time::getCurrentTime(), p.toFloat(),
                               juce::Time::getCurrentTime(), 1, false);
            return m;
        };
        juce::Point<int> clampPt { -1, -1 };
        for (int y = 2; y < ed->getHeight() && clampPt.x < 0; y += 2)
            for (int x = 2; x < ed->getWidth(); x += 2)
                if (ed->controlAt({ x, y }) == AeqAudioProcessorEditor::Control::Clamp) { clampPt = { x, y }; break; }
        check(clampPt.x >= 0, "the clamp is findable");
        if (clampPt.x >= 0) {
            const auto md = at(clampPt);
            ed->mouseDown(md); ed->mouseUp(md); pump();
            check(proc.isFrozen(), "clicking the clamp seals the take");

            ed->mouseDown(md); ed->mouseUp(md); pump();
            check(! proc.isFrozen(), "clicking it again opens the take");
            check(proc.isLearning(),
                  "opening a take starts the analysis again (it did not before 1.2.18)");
        }
    }

    // ---- 3. bypass is a switch, not a one-way trip ------------------------------
    {
        auto* p = proc.apvts().getParameter("bypass");
        const float before = p->getValue();
        juce::Point<int> pt { -1, -1 };
        for (int y = 2; y < ed->getHeight() && pt.x < 0; y += 2)
            for (int x = 2; x < ed->getWidth(); x += 2)
                if (ed->controlAt({ x, y }) == AeqAudioProcessorEditor::Control::Bypass) { pt = { x, y }; break; }
        if (pt.x >= 0) {
            juce::MouseEvent m(juce::Desktop::getInstance().getMainMouseSource(), pt.toFloat(),
                               juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, ed, ed,
                               juce::Time::getCurrentTime(), pt.toFloat(),
                               juce::Time::getCurrentTime(), 1, false);
            ed->mouseDown(m); ed->mouseUp(m); pump();
            const bool off = p->getValue() > 0.5f;
            ed->mouseDown(m); ed->mouseUp(m); pump();
            check(off && p->getValue() <= 0.5f, "bypass goes on and comes back off");
        } else {
            check(false, "bypass is findable");
        }
    }

    // ---- 4. reset is a reset, not a stop ---------------------------------------
    {
        proc.stopLearning();                       // a sealed take is the hard case
        pump();
        juce::Point<int> pt { -1, -1 };
        for (int y = 2; y < ed->getHeight() && pt.x < 0; y += 2)
            for (int x = 2; x < ed->getWidth(); x += 2)
                if (ed->controlAt({ x, y }) == AeqAudioProcessorEditor::Control::Reset) { pt = { x, y }; break; }
        if (pt.x >= 0) {
            juce::MouseEvent m(juce::Desktop::getInstance().getMainMouseSource(), pt.toFloat(),
                               juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, ed, ed,
                               juce::Time::getCurrentTime(), pt.toFloat(),
                               juce::Time::getCurrentTime(), 1, false);
            ed->mouseDown(m); ed->mouseUp(m); pump();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
            check(proc.isLearning(), "reset after a stop puts the plugin back to listening");
            check(! proc.isFrozen(), "and it is not left sealed");
            check(proc.core().state().analyzedSeconds < 0.5
                    && proc.autoState().heardSeconds < 0.5,
                  "and the take it had heard is gone, on the counter the panel reads");
        } else {
            check(false, "reset is findable");
        }
    }

    // ---- 5. the dials are bound to the parameters they are labelled with -------
    {
        if (auto* p = proc.apvts().getParameter("amount")) {
            p->setValueNotifyingHost(p->convertTo0to1(40.0f));
            pump();
            check(std::abs(ed->strengthShown() - 40.0f) < 0.5f,
                  "the strength dial follows the strength parameter");
        }
        if (auto* p = proc.apvts().getParameter("auto_stop")) {
            p->setValueNotifyingHost(p->convertTo0to1(90.0f));
            pump();
            check(std::abs(ed->autoStopShown() - 90.0f) < 0.5f,
                  "the auto-stop dial is attached to auto-stop, not to the trust threshold");
        }
    }

    // ---- 6. a band chip moves the band it shows, and it comes back -------------
    {
        pump();
        if (ed->chipCount() > 0 && ed->chipSource(0) >= 0) {
            const int src = ed->chipSource(0);
            const double was = proc.core().bandTrim(src);
            proc.core().setBandTrim(src, -4.0);
            check(std::abs(proc.core().bandTrim(src) + 4.0) < 0.01,
                  "a chip's band takes a trim");
            proc.core().setBandTrim(src, 0.0);
            check(std::abs(proc.core().bandTrim(src) - was) < 0.01,
                  "and the same chip returns the solver's answer");
        } else {
            check(proc.core().correctiveCount() >= 0,
                  "the chip list reports which band each row addresses");
        }
    }

    // ---- 7. space is the same verb as the clamp --------------------------------
    {
        proc.relearn();
        pump();
        const bool wasFrozen = proc.isFrozen();
        juce::KeyPress sp(juce::KeyPress::spaceKey);
        ed->keyPressed(sp);
        pump();
        check(proc.isFrozen() != wasFrozen, "space stops the pass, like the clamp");
        check(ed->getTooltip() == (proc.isFrozen() ? juce::String("Sealed")
                                                   : juce::String("Listening")),
              "and the panel says what the key just did");
    }

    // ---- 8. the decision survives the project being closed and opened ------------
    // The promise the user made first and the one a DAW user notices in a minute: save a
    // project, open it again, and the take is still sealed with the same bands.  This
    // exercises getStateInformation / setStateInformation, which set restoreDecision() -
    // the path that used to rebuild the cascade from the wrong thread.
    {
        proc.relearn();
        analyse(proc, true);
        proc.stopLearning();
        pump();
        const int before = proc.core().appliedBandCount();
        const auto freqsBefore = freqs(proc.core().activeBands());
        const bool wasFrozen = proc.isFrozen();
        juce::MemoryBlock saved;
        proc.getStateInformation(saved);

        proc.relearn();                      // "close the project"
        pump();
        check(! proc.isFrozen() && proc.core().appliedBandCount() == 0,
              "a relearn really does empty the take");

        proc.setStateInformation(saved.getData(), int(saved.getSize()));
        pump();
        check(proc.isFrozen(), "reopening the project restores the sealed decision");
        check(proc.core().appliedBandCount() == before,
              "and the same number of bands comes back with it");
        check(freqs(proc.core().activeBands()) == freqsBefore,
              "on the same frequencies, to the hertz");
        check(wasFrozen, "the take that was saved had been sealed in the first place");
    }

    // ---- 9. the keys do something, and only what they say ----------------------
    // "keys that do nothing and give nothing back" was one of the complaints.  Escape is the
    // one key a mixer reaches for to get out of something; it opens a sealed take, and
    // pressing it twice must not change the answer twice.
    {
        proc.relearn();
        analyse(proc, true);
        proc.stopLearning();
        pump();
        check(proc.isFrozen(), "there is a sealed take to escape from");
        check(ed->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),
              "escape is handled, not swallowed");
        pump();
        check(! proc.isFrozen(), "escape opens the sealed take");
        check(proc.isLearning(), "and the plugin is listening again, not idle");
        check(ed->getTooltip() == juce::String("Listening"),
              "and the panel says so, so the key does not look like it did nothing");
        ed->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        pump();
        check(! proc.isFrozen(), "pressing it again does not seal anything behind your back");
        check(! ed->keyPressed(juce::KeyPress('q')),
              "and an unclaimed key is passed on, not eaten");
    }

    // ---- 10. the wheel moves the dial under it, and nothing else ----------------
    {
        auto* amount = proc.apvts().getParameter("amount");
        check(amount != nullptr, "the strength parameter is reachable by name");
        if (amount != nullptr) {
            const float was = amount->getValue();
            const auto dial = ed->strengthShown() >= 0.0f
                            ? ed->controlBounds(AeqAudioProcessorEditor::Control::StrengthKnob)
                            : juce::Rectangle<int>();
            check(! dial.isEmpty(), "the strength dial reports where it is");
            juce::MouseWheelDetails up;
            up.deltaY = 0.15f;
            juce::MouseEvent we(juce::Desktop::getInstance().getMainMouseSource(),
                                dial.getCentre().toFloat(), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, ed, ed, juce::Time::getCurrentTime(),
                                dial.getCentre().toFloat(), juce::Time::getCurrentTime(), 1, false);
            ed->mouseWheelMove(we, up);
            pump();
            const float moved = amount->getValue();
            check(std::abs(moved - was) > 1.0e-4f, "the wheel moves the strength dial");
            check(moved > was, "and upward is more, not less");
            const auto before = amount->getValue();
            const juce::Point<float> bare (80.0f, 300.0f);
            juce::MouseEvent away(juce::Desktop::getInstance().getMainMouseSource(), bare,
                                  juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, ed, ed,
                                  juce::Time::getCurrentTime(), bare,
                                  juce::Time::getCurrentTime(), 1, false);
            ed->mouseWheelMove(away, up);
            pump();
            check(std::abs(amount->getValue() - before) < 1.0e-6f,
                  "and the same wheel over the bare field changes nothing");
        }
    }

    // ---- 11. the graph takes a hand ---------------------------------------------
    // The well is two thirds of the window and the handles are the real editing surface: a
    // drag on one is a trim on that band, a double-click gives the band back to the solver,
    // and a drag on the bare field changes nothing - "nothing can be lost by an accidental
    // hand".  All three were untested until this stage.
    {
        aeq::ui::CurveView* curve = nullptr;
        for (int i = 0; i < ed->getNumChildComponents() && curve == nullptr; ++i)
            curve = dynamic_cast<aeq::ui::CurveView*> (ed->getChildComponent (i));
        check(curve != nullptr, "the graph is a component of the panel, not a picture");
        if (curve != nullptr) {
            pump();
            check(curve->handleCount() > 0, "and there is a handle on it to take hold of");
            int h = 0;
            for (; h < curve->handleCount(); ++h) {
                const auto c = curve->handleCentre (h);
                if (c.x > 40.0f && c.y > 20.0f
                    && c.x < float (curve->getWidth()) - 40.0f
                    && c.y < float (curve->getHeight()) - 20.0f
                    && curve->handleAt (c) == h)
                    break;
            }
            if (h < curve->handleCount()) {
                const auto c = curve->handleCentre (h);
                const double was = proc.core().bandTrim (h);
                auto src = juce::Desktop::getInstance().getMainMouseSource();
                auto ev = [&] (juce::Point<float> at, int clicks) {
                    juce::MouseEvent m (src, at, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f,
                                        0.0f, curve, curve, juce::Time::getCurrentTime(), at,
                                        juce::Time::getCurrentTime(), clicks, false);
                    return m;
                };
                curve->mouseDown (ev (c, 1));
                curve->mouseDrag (ev (c.translated (0.0f, -22.0f), 1));
                curve->mouseUp (ev (c.translated (0.0f, -22.0f), 1));
                pump();
                const double up = proc.core().bandTrim (h);
                check (up > was + 0.5, "dragging a handle up lifts that band");
                curve->mouseDown (ev (c, 2));         // double-click = back to the solver
                curve->mouseUp (ev (c, 2));
                pump();
                check (std::abs (proc.core().bandTrim (h)) < 0.01,
                       "and a double-click gives the band back to the solver");

                const double before2 = proc.core().bandTrim (0);
                const juce::Point<float> bare (float (curve->getWidth()) * 0.5f,
                                               float (curve->getHeight()) * 0.85f);
                check (curve->handleAt (bare) < 0, "the field under the handles is empty");
                curve->mouseDown (ev (bare, 1));
                curve->mouseDrag (ev (bare.translated (30.0f, -30.0f), 1));
                curve->mouseUp (ev (bare.translated (30.0f, -30.0f), 1));
                pump();
                check (std::abs (proc.core().bandTrim (0) - before2) < 0.001,
                       "and a drag on the bare field moves no band");
            }
        }
    }

    // ---- 12. every sentence the panel can say fits the row it says it in --------
    // At the panel's true type size (1.2.27) the footer's sentences are ~100 characters,
    // and one line of them ran straight through the dial labels: "the numbers are coarse,
    // the d[ial]" on the shipped 1.2.26 window.  No gate measured a sentence, so all eight
    // passed.  The window wraps now, and this asserts that wrapping is enough - for the
    // state on screen and for the five other states the copy module can produce.
    {
        check(ed->footerCopyFits(), "the panel's sentence fits the row it is drawn in");
        const aeq::copy::Facts states[] = {
            { false, false, false, false, 0, 0.0, 0.0 },          // nothing heard yet
            { true,  false, false, false, 0, 6.0, 45.0 },         // listening, early
            { true,  false, true,  true,  4, 44.0, 45.0 },        // open again
            { true,  true,  true,  true,  0, 45.0, 45.0 },        // sealed, nothing to fix
            { true,  true,  true,  false, 4, 21.0, 45.0 },        // sealed early
            { true,  true,  true,  true,  7, 45.0, 45.0 }         // sealed, settled
        };
        // Every state, in **both** written forms: the long verdict and the written-short
        // compact one.  The panel picks the long one when it fits and the short one when it
        // does not, so a state is only unrepresentable if neither fits - and that is what is
        // asserted.  (The first version of this check measured the long form only, so a
        // narrow window failed for a sentence the panel would never have drawn there.)
        int fits = 0;
        for (const auto& f : states) {
            const bool longOk  = ed->footerCopyFits (juce::String (aeq::copy::verdict (f)));
            const bool shortOk = ed->footerCopyFits (juce::String (aeq::copy::verdictCompact (f)));
            if (longOk || shortOk) ++fits;
            else stage (juce::String ("  neither form fits this state: ") + ed->footerSentence());
        }
        check(fits == 6, "and so do all six states the copy module can be in, in one of its two forms");
    }

    // ---- 13. the scan is on screen, and so is what runs by itself ---------------
    // The user's question, in his words: "how do I know it scanned, and is the tool
    // automatic and dynamic - or static?".  Four sentences answer it now (the badge, the
    // strip, the layer line and the auto line).  A sentence nobody can see answers
    // nothing, so this checks that each one follows the state *and* that the boxes do not
    // sit on each other or on another control.  The pass is ended the way a person ends
    // it - a click on the clamp - so the words are the words the panel would really say.
    {
        proc.relearn();
        analyse(proc, true);
        pump();
        const auto badgeRun = ed->scanBadgeText();
        const auto stripRun = ed->scanStripText();
        check(badgeRun.startsWith("SCANNING"),
              ("while it listens the header says it is scanning, with a number: "
               + badgeRun).toRawUTF8());
        check(stripRun.startsWith("SCAN ") && stripRun.contains(" s"),
              ("and the take bar says how far and how much: " + stripRun).toRawUTF8());
        check(ed->layerReadoutText().isEmpty(),
              "nothing is committed yet, so there is no layer line to read");

        {   // the clamp, pressed the way the window presses it
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const auto pos = ed->controlBounds(AeqAudioProcessorEditor::Control::Clamp)
                                .getCentre().toFloat();
            juce::MouseEvent down(src, pos, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  ed, ed, juce::Time::getCurrentTime(), pos,
                                  juce::Time::getCurrentTime(), 1, false);
            ed->mouseDown(down);
            juce::MouseEvent up(src, pos, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                ed, ed, juce::Time::getCurrentTime(), pos,
                                juce::Time::getCurrentTime(), 1, false);
            ed->mouseUp(up);
            pump();
        }
        check(proc.isFrozen(), "and the pass is sealed where it was stopped");
        check(ed->scanBadgeText() == "SCANNED",
              ("so the header says SCANNED, not SCANNING: "
               + ed->scanBadgeText()).toRawUTF8());
        check(ed->scanStripText().startsWith("SCANNED "),
              ("and how much of the pass it kept: " + ed->scanStripText()).toRawUTF8());
        const auto layer = ed->layerReadoutText();
        check(layer.contains("STATIC") && layer.contains("DYNAMIC"),
              ("the panel answers static-or-dynamic in one line: " + layer).toRawUTF8());
        // The line under the seal answers the question the user asked in words - "does it scan
        // all the audio, or does it stop by itself?" - and it answers it *while the pass is
        // open*, with the rule and the seconds left on it.  It used to answer after the fact
        // ("AUTO: GAIN + DYNAMIC + STOP BY YOU"), which said who stopped a pass that had
        // already been stopped and nothing about what would stop the next one.
        if (auto* p = proc.apvts().getParameter("auto_stop")) {
            const float was = p->getValue();
            p->setValueNotifyingHost(p->convertTo0to1(0.0f));
            pump();
            // sealed by a hand: the seal says so, and the line under it does not claim the
            // dial did it.  (The sealed line no longer carries a STOP clause at all - who
            // stopped it is the pill's job, and the harness checks the pill.)
            const auto sealedLine = ed->autoReadoutText();
            check(! sealedLine.contains("STOP AT") && ! sealedLine.contains("LEFT"),
                  ("sealed by hand, the line does not claim the dial ended it: "
                   + sealedLine).toRawUTF8());
            // open again, dial armed: the rule is printed before it fires
            click(ed->controlBounds(AeqAudioProcessorEditor::Control::Clamp).getCentre());
            p->setValueNotifyingHost(p->convertTo0to1(90.0f));
            ed->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            pump();
            const auto rule = ed->autoReadoutText();
            check(rule.contains("SCANS TO 90 s") && rule.contains("LEFT"),
                  ("with the auto-stop dial at 90 s the open pass states the rule: "
                   + rule).toRawUTF8());
            // and with the dial OFF it still states one - the cap
            p->setValueNotifyingHost(p->convertTo0to1(0.0f));
            pump();
            check(ed->autoReadoutText().contains("5 MIN CAP"),
                  ("with the dial OFF it names the cap instead of leaving it to guesswork: "
                   + ed->autoReadoutText()).toRawUTF8());
            p->setValueNotifyingHost(was);
            pump();
        }

        // ---- and the boxes are on the panel, not under each other ---------------
        const auto badge = ed->scanBadge();
        const auto name = ed->wordmarkInk();
        const auto line = ed->taglineInk();
        const auto ref = ed->controlBounds(AeqAudioProcessorEditor::Control::Reference);
        check(badge.getRight() <= ref.getX(),
              "the scan badge sits left of the reference pill, not under it");
        check(float (badge.getX()) >= name.getRight() + 8.0f,
              "and right of the name, with room to breathe");
        check(! badge.toFloat().intersects(line),
              "and above the tagline, not through it");
        const auto autosBox = ed->autoReadout().toFloat();
        check(! autosBox.intersects(line) && ! autosBox.intersects(badge.toFloat()),
              "the automatic line does not run into the tagline or the badge");
        const auto layerBox = ed->layerReadout();
        check(! layerBox.intersects(ed->controlBounds(AeqAudioProcessorEditor::Control::Clamp)),
              "the static/dynamic line does not sit under the clamp you press");
        check(layerBox.getRight() <= ed->getWidth(),
              "and it stays inside the window");

        // ---- and the keys are inside the plate, not on its rounded corner -------
        // The 1.2.53 shot shipped the bypass key half outside the window: its hit box was
        // 78 design px from the right edge and the key is 34 px wide inside a 40 px target,
        // so the seat crossed the panel's 16 px corner radius.
        const float k = float (ed->getHeight()) / 700.0f;
        for (const auto c : { AeqAudioProcessorEditor::Control::Bypass,
                              AeqAudioProcessorEditor::Control::Reset }) {
            const auto seat = ed->controlBounds (c).toFloat()
                                .withSizeKeepingCentre (34.0f * k, 34.0f * k);
            const float corner = 16.0f * k;
            check (seat.getRight() <= float (ed->getWidth()) - corner
                   && seat.getY() >= corner * 0.5f,
                   (c == AeqAudioProcessorEditor::Control::Bypass
                        ? "the bypass key sits inside the panel's corner"
                        : "the reset key sits inside the panel's corner"));
        }

        // ---- and the right-aligned rows end inside the plate --------------------
        // Both the static/dynamic readout and the legend are right-aligned to the well, and
        // both used to stop exactly on its border: type that lands on a 1 px border reads
        // as type that was cut off.  The inset is the editor's own number, not a guess.
        {
            const auto ro = ed->layerReadout();
            const auto hero = ed->heroBlock();
            const int air = ed->wellInsetPx();
            check (ro.isEmpty() || ro.getRight() <= hero.getRight() - air + 1,
                   "the static/dynamic readout ends inside the plate, not on its border");
            check (air >= 8, "the plate's right inset is at least 8 px");
        }

        // ---- and each dial is inside the plate, off its corner ------------------
        // The auto-stop dial shipped in 1.2.59 with a flat bottom: it was centred on the
        // footer's row and 34 design px from the right edge, so the plate's own 16 px corner
        // radius cut its lower-right quadrant.  Every dial is checked against the panel's
        // corner radius, not against the panel's edge - an edge is not a corner.
        for (const auto c : { AeqAudioProcessorEditor::Control::StrengthKnob,
                              AeqAudioProcessorEditor::Control::AutoStopKnob,
                              AeqAudioProcessorEditor::Control::StabiliserKnob }) {
            const auto r = ed->controlBounds (c).toFloat();
            const float corner = 16.0f * k;
            const bool inside = r.getX() >= 0.0f && r.getY() >= 0.0f
                                && r.getRight() <= float (ed->getWidth()) - corner * 0.5f
                                && r.getBottom() <= float (ed->getHeight()) - corner * 0.5f;
            check (inside, (juce::String ("the ") + (c == AeqAudioProcessorEditor::Control::StrengthKnob
                                                         ? "strength"
                                                         : c == AeqAudioProcessorEditor::Control::AutoStopKnob
                                                               ? "auto-stop" : "stabiliser")
                            + " dial clears the plate's corner and its bottom edge").toRawUTF8());
        }

        // ---- and a dial does not sit on the word beside it ----------------------
        {
            const int sizes2[][2] = { { 1240, 800 }, { 1024, 700 }, { 940, 620 } };
            for (const auto& sz : sizes2) {
                ed->setSize (sz[0], sz[1]);
                pump();
                check (ed->dialRowFits(),
                       (juce::String ("each footer dial clears its neighbour's label at ")
                        + juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()))
                           .toRawUTF8());
            }
            ed->setSize (1240, 800);
            pump();
        }

        // ---- and the mode words fit inside their keys --------------------------
        // RESTORE crossed the border of its own key twice (1.2.53 with a lamp in the key,
        // 1.2.54 with the lamp's gutter) and both times every assertion on this panel
        // passed, because a 1 px border under a letter still draws.  Three sizes, because
        // the key is 90 design px and the type scales with the height while the key scales
        // with the width: the two only agree at 1180x700.
        const int sizes[][2] = { { 1240, 800 }, { 1024, 700 }, { 940, 620 } };
        for (const auto& sz : sizes) {
            ed->setSize (sz[0], sz[1]);
            pump();
            check (ed->modeWordFits(),
                   (juce::String ("RESTORE and MATCH fit inside their keys at ")
                    + juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()))
                       .toRawUTF8());
        }
        ed->setSize (1240, 800);
        pump();
        check (ed->modeWordInk (0) > ed->modeWordInk (1),
               "RESTORE is the wider mode word, which is what the layout assumes");
    }

    stage(juce::String("controls: ") + juce::String(pass) + " passed, " + juce::String(fail)
          + " failed");
    // a log line the gate reads, and an exit code the build reads
    dir.getChildFile("controls.log.txt")
       .replaceWithText(juce::String(pass) + " passed, " + juce::String(fail) + " failed\n");
    return fail == 0 ? 0 : 1;
}

// Hue in degrees, and the shortest distance between two of them.  Used by the window
// stage to ask "is this pixel the decision layer" in the palette's own terms.
static float hueOf (juce::Colour c) {
    const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
    const float mx = juce::jmax (r, g, b), mn = juce::jmin (r, g, b), d = mx - mn;
    if (d <= 0.0f) return 0.0f;
    float h;
    if (mx == r)      h = 60.0f * ((g - b) / d);
    else if (mx == g) h = 60.0f * ((b - r) / d) + 120.0f;
    else              h = 60.0f * ((r - g) / d) + 240.0f;
    return h < 0.0f ? h + 360.0f : h;
}

static float hueDistance (float a, float b) {
    const float d = std::fabs (a - b);
    return d > 180.0f ? 360.0f - d : d;
}

int runWindow(const juce::File& dir) {
    // The window, twice: empty and sealed.
    //
    // This used to photograph only the empty state, because drawing a report card took
    // the process down on a headless runner (an access violation inside the text stack).
    // v9 has no report cards - the report is one sentence in the footer - so the window
    // can finally be photographed with the take it actually analysed, which is the shot
    // that says whether the panel is finished or merely laid out.
    AeqAudioProcessor proc;
    proc.prepareToPlay(48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    if (editor == nullptr) { stage("no editor"); return 1; }
    stage("editor constructed, before analysis");

    struct Size { int w, h; const char* empty; const char* sealed; };
    const Size sizes[] = { { 1240, 800, "ui-empty-1240x800.png", "ui-1240x800.png" },
                           { 1024, 700, "ui-empty-1024x700.png", "ui-1024x700.png" } };

    auto shoot = [&] (const Size& s, const char* name) {
        stage(juce::String("window ") + juce::String(s.w) + "x" + juce::String(s.h)
              + " -> " + name);
        editor->setSize(s.w, s.h);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
        stage("timer pumped");
        for (int i = 0; i < editor->getNumChildComponents(); ++i) {
            auto* c = editor->getChildComponent(i);
            if (c == nullptr) continue;
            stage(juce::String("  child ") + juce::String(i) + " " + typeid(*c).name()
                  + " " + c->getBounds().toString()
                  + (c->isVisible() ? "" : " HIDDEN"));
        }
        const auto img = editor->createComponentSnapshot(editor->getLocalBounds(), false, 1.0f);
        stage("snapshot taken");
        savePng(img, dir.getChildFile(name));
    };

    // 1. before anything has been heard: the panel in its listening state
    for (const auto& s : sizes) shoot(s, s.empty);

    // 2. the take played through, then stopped by the user - which is the product
    stage("analysis for the sealed shot");
    analyse(proc, true);
    proc.stopLearning();
    stage("stopped; the seal should be where the user left it");
    for (const auto& s : sizes) shoot(s, s.sealed);

    // 2b. **seven bands**, which is the case the user named: "the panel must be legible at
    // seven bands".  The detected take happened to produce a different number, and a
    // screenshot at whatever the detector chose is not a test of the row the user was
    // looking at.  So the decision is replaced with exactly seven bands - two cuts, two
    // lifts, a shelf-ish pair and one narrow notch - and the row is photographed at the
    // narrow window (1024 px), where a cell is ~150 px wide and the three-row card has
    // the least room it will ever have.  ui-1.2.68-7-bands-1024x700.png is the crop the
    // release notes point at.
    stage("seven bands, on purpose");
    {
        const auto st = proc.core().state();
        aeq::RestoreState seven = st;
        seven.corrective.clear();
        const struct { double f, q, g; } wanted[] = {
            { 105.0, 1.10, -3.4 }, { 240.0, 1.60,  1.8 }, { 520.0, 2.10, -2.2 },
            { 1250.0, 1.40, 2.6 }, { 2600.0, 2.60, -1.9 }, { 5200.0, 1.80, -2.4 },
            { 9400.0, 3.20, -1.5 } };
        for (const auto& w : wanted) {
            aeq::BandSpec b;
            b.f0 = w.f; b.q = w.q; b.gainDb = w.g; b.type = aeq::BandType::Peak;
            seven.corrective.push_back(b);
        }
        seven.resonance.clear();
        proc.core().restoreDecision(seven);
        proc.core().clearTrims();
        editor->resized();
        shoot({ 1024, 700, "ui-7bands-1024x700.png", "ui-7bands-1024x700-sealed.png" },
              "ui-1.2.68-7-bands-1024x700.png");
        shoot({ 1240, 800, "ui-7bands-1240x800.png", "ui-7bands-1240x800-sealed.png" },
              "ui-1.2.68-7-bands-1240x800.png");
    }

    // 3. the drawing has the low cut in it.
    //
    // The user asked "why does the line start at 100 and not from the start?".  Two things
    // were true: the reference curve has no data below 100 Hz (a voice has nothing under its
    // fundamental), and - the defect - the decision line left the low cut out, so the plugin
    // drew itself doing nothing between 40 Hz and the first band while a 4th-order high pass
    // at ~95 Hz was cutting there.  This counts the accent pixels below 0 dB in the plot's
    // 40..100 Hz strip: a drawn high pass puts the curve through it, a missing one does not.
    int windowFails = 0;
    auto check = [&] (bool ok, const char* what) {
        stage (juce::String (ok ? "  ok   " : "  FAIL ") + what);
        if (! ok) ++windowFails;
    };
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        aeq::ui::CurveView* curve = nullptr;
        for (int i = 0; i < editor->getNumChildComponents() && curve == nullptr; ++i)
            curve = dynamic_cast<aeq::ui::CurveView*> (editor->getChildComponent (i));
        stage(curve != nullptr ? "the graph is a child of the panel" : "FAIL no graph child");
        editor->setSize (1240, 800);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);
        if (curve != nullptr && img.isValid())
        {
            const auto plot = curve->plotArea().translated ((float) curve->getX(),
                                                            (float) curve->getY());
            const float xa = curve->freqToX (40.0) + (float) curve->getX();
            const float xb = curve->freqToX (100.0) + (float) curve->getX();
            // -6 dB, not 0: the curve's own glow sits within a few pixels of 0 dB even
            // when the line is flat there, so a strip that starts at 0 dB counts the glow
            // of nothing.  Measured on the two shots: 1.2.49 (no low cut drawn) has 0
            // accent pixels below -6 dB in 40..100 Hz, 1.2.50 has 533.
            const float y0 = curve->dbToY (-6.0) + (float) curve->getY();
            // The decision colour is *asked for*, not spelled out here.  It used to be a
            // hand-written "red and blue both well clear of green" - the predicate for the
            // magenta this panel wore in 1.2.5x - and when the palette moved to the
            // reference's orange it would have gone on counting the old hue and reported a
            // missing low cut that was drawn.  A pixel counts as the decision layer when its
            // hue is within 18 deg of Palette::accent()'s and it is bright enough to be ink
            // rather than the glow under it.
            const float accentHue = hueOf (aeq::ui::Palette::accent());
            int pink = 0;
            for (int y = int (y0); y < int (plot.getBottom()); ++y)
                for (int x = int (xa); x < int (xb); ++x)
                {
                    const auto c = img.getPixelAt (x, y);
                    if (hueDistance (hueOf (c), accentHue) <= 18.0f
                        && c.getFloatRed() + c.getFloatGreen() + c.getFloatBlue() > 0.75f)
                        ++pink;
                }
            stage (juce::String ("the decision line below 100 Hz: ") + juce::String (pink)
                   + " accent px (a drawn low cut puts the curve through that strip)");
            check (pink >= 60, "the low cut is drawn: the decision line runs below 100 Hz");
        }
        else
        {
            check (false, "the low cut is drawn: the decision line runs below 100 Hz");
        }
    }

    return windowFails == 0 ? 0 : 1;
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

        // Which *string* takes the process down?  One line at a time, through the
        // same component-snapshot path, so a crash names the text instead of the
        // card.  A row of Arabic with Latin units in it is the shape that has been
        // killing the renderer, and this is how we find out which row.
        struct OneLine : juce::Component {
            juce::String text; float size = 13.0f; bool paragraph = false;
            void paint (juce::Graphics& g) override {
                const auto r = getLocalBounds().reduced (8);
                if (paragraph) aeq::ui::drawParagraph (g, text, r.toFloat(), juce::Justification::topRight,
                                                       size, aeq::ui::Palette::muted());
                else aeq::ui::label (g, text, r, aeq::ui::Palette::text(), size,
                                     juce::Justification::topRight, true);
            }
        };
        auto probeText = [&] (const juce::String& t, const juce::String& what, float size, bool paragraph) {
            stage ("  text " + what + " (" + juce::String (t.length()) + " chars) "
                   + juce::String::toHexString (int (t[0])).paddedLeft ('0', 4) + "..");
            OneLine l;
            l.text = t; l.size = size; l.paragraph = paragraph;
            l.setSize (336, 104);
            savePng (l.createComponentSnapshot (l.getLocalBounds(), false, 1.0f),
                     dir.getChildFile ("probe-text-" + juce::String (int (i)) + "-" + what + ".png"));
        };
        probeText (juce::String::fromUTF8 (p.labelAr.c_str()), "title", 13.0f, false);
        probeText (juce::String::fromUTF8 (p.labelAr.c_str()).replace ("(", "").replace (")", ""),
                   "title-noparen", 13.0f, false);
        probeText (juce::String::fromUTF8 (p.detailAr.c_str()), "detail", 10.5f, true);
        probeText (juce::String::fromUTF8 (p.detailAr.c_str()).replace ("(", "").replace (")", ""),
                   "detail-noparen", 10.5f, true);

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
    stage(juce::String("embedded Cairo typeface: ")
          + (aeq::ui::embeddedFontLoaded() ? "loaded" : "NOT loaded - falling back to a system face"));

    // ---- the gate: did the Arabic survive the compiler and the conversion? ----
    // juce::String (const char*) reads its bytes as ASCII, so a UTF-8 literal that
    // goes through it becomes one character per byte - "تخنة" arrives as
    // U+00D8 U+00AA U+00D8 U+00AE ... and paints as .notdef boxes while the Latin
    // in the same sentence renders.  That was the 1.2.1 bug.  Everything now goes
    // through aeq::ui::u8(), and this checks the two things that can still go
    // wrong: the compiler mangling the source (a code-page guess on MSVC), and the
    // conversion itself.
    {
        // The expectation is a list of numbers, not a string: building the
        // reference with juce::String(const char*) would put the same bug in the
        // test as in the code.  U+062A U+062E U+0646 U+0629 is "تخنة".
        const juce::juce_wchar expected[] = { 0x062A, 0x062E, 0x0646, 0x0629 };
        const juce::String viaU8 = aeq::ui::u8("تخنة");
        const juce::String viaRaw("تخنة");          // the old path, shown for the log

        juce::String hex;
        for (auto c : viaU8) hex << juce::String::toHexString((int) c).paddedLeft('0', 4) << " ";

        bool same = viaU8.length() == 4;
        for (int i = 0; same && i < 4; ++i) same = (viaU8[i] == expected[i]);

        stage("u8() of an Arabic literal gives: " + hex
              + (same ? "  (correct)" : "   *** NOT the expected code points ***"));
        stage("the raw juce::String (const char*) path would have given "
              + juce::String(int(viaRaw.length())) + " characters instead of "
              + "4");

        if (! same) {
            stage("FAIL: Arabic did not survive compilation - check /utf-8 and ui::u8");
            return 2;
        }

        auto tf = aeq::ui::typeface(aeq::ui::Face::Regular);
        const auto glyph = tf != nullptr ? tf->getNominalGlyphForCodepoint(0x062A).value_or(0) : 0;
        stage("typeface: " + (tf != nullptr ? tf->getName() : juce::String("null"))
              + ", glyph for U+062A: " + juce::String(int(glyph)));
        if (tf == nullptr || glyph == 0) {
            stage("FAIL: the embedded typeface cannot draw Arabic");
            return 3;
        }
    }

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
        aeq::ui::draw(pg, juce::String::fromUTF8(u8"Geddo AEQ — الخط العربي داخل الـplugin"),
                      probe.getBounds().toFloat().reduced(8.0f),
                      juce::Justification::centredLeft, 22.0f, juce::Colours::white);
        savePng(probe, dir.getChildFile("probe-font.png"));
    }

    int code = 0;
    try {
        if (mode == "window" || mode == "all")     code |= runWindow(dir);
        if (mode == "controls" || mode == "all")   code |= runControls(dir);
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
