#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

#include "Theme.h"
#include "../../Source/dsp/RestoreCore.h"

namespace aeq::ui {

// ---------------------------------------------------------------------------
// The graph.  Four things, in the order they matter:
//
//   the engineer's tolerance  a shaded corridor of +-1 sigma around the standard,
//                             the measured spread of Amir's own 15 mixes.  A curve
//                             inside it is inside his working range - this is what
//                             "no problem" looks like, drawn instead of asserted.
//   the standard              the target curve, in the envelope domain
//   your take                 the raw measurement, and the harmonic envelope dashed
//   the correction            what the plugin is applying, with a handle per band
//
// The handles are draggable: the analysis proposes, the engineer overrules, and the
// trim is applied on top of the proposal without changing the measurement.
// ---------------------------------------------------------------------------
class CurveView : public juce::Component {
public:
    struct Data {
        aeq::Measurements meas;
        std::vector<aeq::BandSpec> bands;
        std::vector<aeq::Problem> problems;
        std::vector<double> trims;
        double sampleRate = 48000.0;   // the correction curve is drawn at the host's rate
        bool valid = false;
        bool settled = false;
        double analysedSeconds = 0.0;
        std::vector<double> liveDb, peakDb;   // the moving analyser, see setLive()
    };

    CurveView() { setWantsKeyboardFocus (false); }

    // Which handle is under a point, or -1.  Public because the panel's own test has to know
    // where the handles are: a graph that covers two thirds of the window is the thing a
    // mixer will touch first, and "does dragging it change the sound" must be answered by a
    // test rather than by looking at a screenshot.
    int handleAt (juce::Point<float> p) const { return hitHandle (p); }
    int handleCount() const { return int (data_.bands.size()); }
    juce::Point<float> handleCentre (int i) const {
        if (i < 0 || i >= int (data_.bands.size())) return { -1.0f, -1.0f };
        return { xOf (data_.bands[size_t (i)].f0), yOf (bandGainWithTrim (i)) };
    }

    // Embedded in v9's hero: the editor draws the well (plate, blooms, hairline) and
    // the legend, so the view stops drawing chrome of its own.  One surface, one owner.
    void setEmbedded (bool shouldBeEmbedded) { embedded_ = shouldBeEmbedded; repaint(); }

    void setData (Data d) {
        data_ = std::move (d);
        repaint();
    }

    // Called ~25 times a second while audio is playing.  Touches two arrays and
    // repaints this component only - never the report, never the header, never the
    // whole editor.  This is the difference between an analyser that moves with the
    // voice and one that redraws the window four times a second.
    void setLive (std::vector<double> live, std::vector<double> peak) {
        data_.liveDb = std::move (live);
        data_.peakDb = std::move (peak);
        repaint();
    }

    // (band index, the trim the user dragged to) and "double-clicked" to reset
    std::function<void (int, double)> onTrim;
    std::function<void (int)> onTrimReset;

    void paint (juce::Graphics& g) override {
        auto area = getLocalBounds().toFloat();
        if (! embedded_) {
            card (g, area, Palette::panel(), Palette::line(), 14.0f);
            glow (g, area.reduced (1.0f), Palette::accent2(), 14.0f);
        }

        auto r = area.reduced (1.0f);
        auto legend = r.removeFromTop (embedded_ ? 0.0f : 30.0f);
        plot_ = embedded_ ? r.reduced (34.0f, 14.0f).withTrimmedTop (12.0f).withTrimmedBottom (10.0f)
                          : r.reduced (44.0f, 8.0f).withTrimmedBottom (14.0f);

        drawGrid (g);
        if (! data_.valid || data_.meas.freqs.empty()) {
            drawEmpty (g, area);
            if (! embedded_) drawLegend (g, legend);
            return;
        }
        drawCorridor (g);
        drawTarget (g);
        drawMeasured (g);
        drawEnvelope (g);
        drawCorrection (g);
        drawMarkers (g);
        drawHandles (g);
        if (! embedded_) drawLegend (g, legend);
        drawHover (g);
    }

    void mouseMove (const juce::MouseEvent& e) override {
        hoverPos_ = e.position;
        hoverHandle_ = hitHandle (e.position);
        setMouseCursor (hoverHandle_ >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                         : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override {
        hoverHandle_ = -1;
        hoverPos_ = { -1.0f, -1.0f };
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override {
        drag_ = hitHandle (e.position);
        dragStartTrim_ = 0.0;
        if (drag_ < 0) return;
        if (e.getNumberOfClicks() >= 2) {
            if (onTrimReset) onTrimReset (drag_);
            drag_ = -1;
            return;
        }
        // trims are addressed by band index; start from what the engine already has
        dragStartTrim_ = static_cast<double> (bandBaseGain (drag_));   // proposal only
        trimAtStart_ = (drag_ < int (data_.trims.size())) ? data_.trims[size_t (drag_)] : 0.0;
        dragStartTrim_ = trimAtStart_;
        dragOriginDb_ = yToDb (e.position.y);
        lastPushMs_ = 0;
    }

    void mouseDrag (const juce::MouseEvent& e) override {
        if (drag_ < 0) return;
        const double wanted = dragOriginDb_ - yToDb (e.position.y);   // + = drag up
        const double trim = juce::jlimit (-12.0, 12.0, dragStartTrim_ + wanted);
        if (drag_ < int (data_.trims.size())) data_.trims[size_t (drag_)] = trim;
        repaint();

        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastPushMs_ > 60 && onTrim) {          // throttle: rebuilding the
            lastPushMs_ = now;                            // cascade 60x/s is waste
            onTrim (drag_, trim);
        }
    }

    void mouseUp (const juce::MouseEvent&) override {
        if (drag_ >= 0 && onTrim && drag_ < int (data_.trims.size()))
            onTrim (drag_, data_.trims[size_t (drag_)]);
        drag_ = -1;
    }

private:
    // ---- geometry ---------------------------------------------------------
    static constexpr double kFmin = 40.0, kFmax = 16000.0;
    // +-30 dB, symmetric: the take's low end can sit 25 dB over its own 1 kHz, and a
    // +18 ceiling drew the whole bass half of a boomy vocal as a line pinned to the
    // top border, which reads as a broken meter rather than as a loud low end.
    static constexpr double kDbMax = 30.0, kDbMin = -30.0;

    float xOf (double f) const {
        const double t = std::log (juce::jlimit (kFmin, kFmax, f) / kFmin) / std::log (kFmax / kFmin);
        return plot_.getX() + float (t) * plot_.getWidth();
    }
    float yOf (double db) const {
        const double t = (db - kDbMin) / (kDbMax - kDbMin);
        return plot_.getBottom() - float (t) * plot_.getHeight();
    }
    double dbAt (float y) const {
        const double t = (plot_.getBottom() - y) / plot_.getHeight();
        return kDbMin + t * (kDbMax - kDbMin);
    }
    double yToDb (float y) const { return dbAt (y); }

    double bandBaseGain (int i) const {
        return (i >= 0 && i < int (data_.bands.size())) ? data_.bands[size_t (i)].gainDb : 0.0;
    }
    double bandGainWithTrim (int i) const {
        const double t = (i < int (data_.trims.size())) ? data_.trims[size_t (i)] : 0.0;
        return juce::jlimit (-18.0, 18.0, bandBaseGain (i) + t);
    }

    int hitHandle (juce::Point<float> p) const {
        for (int i = 0; i < int (data_.bands.size()); ++i) {
            const auto c = juce::Point<float> (xOf (data_.bands[size_t (i)].f0), yOf (bandGainWithTrim (i)));
            if (c.getDistanceFrom (p) < 14.0f) return i;
        }
        return -1;
    }

    // ---- layers -----------------------------------------------------------
    void drawGrid (juce::Graphics& g) {
        // the render's grid is nearly invisible: it is there to be found, not to be
        // read.  At 0.55 the eight decade lines were the loudest thing in the well.
        g.setColour (Palette::line().withAlpha (0.10f));
        const double ticks[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        for (double f : ticks) {
            const float x = xOf (f);
            g.drawVerticalLine (int (x), plot_.getY(), plot_.getBottom());
            label (g, f >= 1000 ? juce::String (f / 1000.0, 0) + "k" : juce::String (int (f)),
                   { int (x) - 18, int (plot_.getBottom()) + 2, 36, 14 },
                   Palette::muted(), 12.0f, juce::Justification::centred);
        }
        for (int db = int (kDbMin); db <= int (kDbMax); db += 6) {
            const float y = yOf (double (db));
            g.setColour (Palette::line().withAlpha (db == 0 ? 0.18f : 0.09f));
            g.drawHorizontalLine (int (y), plot_.getX(), plot_.getRight());
            label (g, (db > 0 ? "+" : "") + juce::String (db),
                   { int (plot_.getX()) - 38, int (y) - 8, 32, 16 },
                   Palette::muted(), 12.0f, juce::Justification::centredRight);
        }
    }

    void drawEmpty (juce::Graphics& g, juce::Rectangle<float> area) {
        // Two lines, centred as a block.  They used to be drawn at the same y with
        // a 24 px trim that the label's own justification ignored, so the Arabic
        // and the English sat on top of each other in the middle of an empty plot.
        const float blockTop = area.getCentreY() - 22.0f;
        label (g, "NO MEASUREMENT YET - PLAY THE TAKE FROM THE TOP",
               juce::Rectangle<int> (int (area.getX()), int (blockTop),
                                     int (area.getWidth()), 24),
               Palette::muted(), 15.0f, juce::Justification::centred);
        label (g, "the meter fills as it plays; nothing is applied before it settles",
               juce::Rectangle<int> (int (area.getX()), int (blockTop) + 26,
                                     int (area.getWidth()), 20),
               Palette::muted().withAlpha (0.75f), 12.5f, juce::Justification::centred);
    }

    void drawCorridor (juce::Graphics& g) {
        // +-1 sigma of the engineer's own 15 mixes, in the envelope domain: the band
        // the standard itself lives in.  Only where the corpus defines the curve.
        juce::Path upper, lower;
        bool started = false;
        for (int i = 0; i <= 300; ++i) {
            const double f = kFmin * std::pow (kFmax / kFmin, i / 300.0);
            if (f < 100.0 || ! aeq::EnvTargetCurve::definedAt (f)) continue;
            const double mid = aeq::EnvTargetCurve::at (f);
            const double sd = juce::jlimit (0.2, 8.0, aeq::EnvTargetCurve::sdAt (f));
            const float x = xOf (f);
            if (! started) {
                upper.startNewSubPath (x, yOf (mid + sd));
                lower.startNewSubPath (x, yOf (mid - sd));
                started = true;
            } else {
                upper.lineTo (x, yOf (mid + sd));
                lower.lineTo (x, yOf (mid - sd));
            }
        }
        if (! started) return;
        // close the shape: forward along the upper edge, back along the lower one
        juce::Path band = upper;
        for (int i = 300; i >= 0; --i) {
            const double f = kFmin * std::pow (kFmax / kFmin, i / 300.0);
            if (f < 100.0 || ! aeq::EnvTargetCurve::definedAt (f)) continue;
            const double mid = aeq::EnvTargetCurve::at (f);
            const double sd = juce::jlimit (0.2, 8.0, aeq::EnvTargetCurve::sdAt (f));
            band.lineTo (xOf (f), yOf (mid - sd));
        }
        band.closeSubPath();
        g.setColour (Palette::standard().withAlpha (0.09f));
        g.fillPath (band);
        g.setColour (Palette::standard().withAlpha (0.22f));
        g.strokePath (band, juce::PathStrokeType (1.0f));
    }

    void drawTarget (juce::Graphics& g) {
        juce::Path p;
        bool started = false;
        for (int i = 0; i <= 300; ++i) {
            const double f = kFmin * std::pow (kFmax / kFmin, i / 300.0);
            if (f < 100.0 || ! aeq::EnvTargetCurve::definedAt (f)) continue;
            const float x = xOf (f), y = yOf (aeq::EnvTargetCurve::at (f));
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        g.setColour (Palette::standard().withAlpha (0.9f));
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }

    // The spectrum the user is watching: the 120 ms moving average (thick, accent)
    // with the mean of the whole take behind it (thin) and the peak hold on top.
    // Display smoothing, and only display.
    //
    // The measured mean arrives per band edge, so two neighbouring points can differ by
    // 10 dB when a band boundary lands on a formant.  Drawn raw that is a staircase with
    // vertical walls - the picture the user called primitive - while the numbers on the
    // chips and in the report are correct and stay untouched.  A +-2 point mean over a
    // log-spaced series is about a sixth of an octave, which is the width every analyser
    // in this market shows.  Silent bins (-119) are carried through, not averaged in.
    std::vector<double> displaySmooth (const std::vector<double>& v, int halfWidth = 2) const {
        if (v.size() < 3 || halfWidth <= 0) return v;
        std::vector<double> out (v.size(), -119.0);
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] <= -119.0) continue;
            double sum = 0.0;
            int n = 0;
            const int lo = int (i) - halfWidth, hi = int (i) + halfWidth;
            for (int j = lo; j <= hi; ++j) {
                if (j < 0 || j >= int (v.size())) continue;
                if (v[size_t (j)] <= -119.0) continue;
                sum += v[size_t (j)];
                ++n;
            }
            if (n > 0) out[i] = sum / double (n);
        }
        return out;
    }

    void drawMeasured (juce::Graphics& g) {
        const auto& m = data_.meas;
        const auto& rawLive = data_.liveDb.size() == m.freqs.size() ? data_.liveDb : m.bandDb;
        if (rawLive.empty() || m.freqs.empty()) return;
        const std::vector<double> live = displaySmooth (rawLive);
        const std::vector<double> peak = displaySmooth (data_.peakDb);
        const std::vector<double> bandMean = displaySmooth (m.bandDb);

        auto reference = [&] (const std::vector<double>& v) {
            double ref = 0.0; int n = 0;
            for (size_t i = 0; i < m.freqs.size() && i < v.size(); ++i)
                if (m.freqs[i] >= 900 && m.freqs[i] < 1150) { ref += v[i]; ++n; }
            return n ? ref / n : 0.0;
        };
        auto pathOf = [&] (const std::vector<double>& v, double ref, bool skipSilent) {
            juce::Path p;
            bool started = false;
            for (size_t i = 0; i < m.freqs.size() && i < v.size(); ++i) {
                const double f = m.freqs[i];
                if (f < kFmin || f > kFmax) continue;
                if (skipSilent && v[i] <= -119.0) { started = false; continue; }
                const double rel = v[i] - ref;
                // A take with a loud low end sits above the top of a fixed +18 dB window;
                // it clips there, the way every analyser in this market clips, instead of
                // leaving a hole in the line where the voice went over.
                const float x = xOf (f), y = yOf (juce::jlimit (kDbMin, kDbMax, rel));
                if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
            }
            return p;
        };

        if (! m.bandDb.empty()) {                       // the settled mean of the take
            g.setColour (Palette::text().withAlpha (0.22f));
            g.strokePath (pathOf (bandMean, reference (bandMean), true), juce::PathStrokeType (1.0f));
        }
        if (data_.peakDb.size() == m.freqs.size()) {    // peak hold, falling 18 dB/s
            g.setColour (Palette::accent2().withAlpha (0.28f));
            g.strokePath (pathOf (peak, reference (live), true), juce::PathStrokeType (1.0f));
        }
        // the body under the curve: the render shows the take as an area, and a bare
        // line on a dark well reads as a wire rather than as the voice
        {
            juce::Path body;
            bool started = false;
            const double ref = reference (live);
            for (size_t i = 0; i < m.freqs.size() && i < live.size(); ++i) {
                const double f = m.freqs[i];
                if (f < kFmin || f > kFmax || live[i] <= -119.0) { continue; }
                const float x = xOf (f), y = yOf (juce::jlimit (kDbMin, kDbMax, live[i] - ref));
                if (! started) { body.startNewSubPath (x, plot_.getBottom()); started = true; }
                body.lineTo (x, y);
            }
            if (started) {
                body.lineTo (plot_.getRight(), plot_.getBottom());
                body.closeSubPath();
                juce::ColourGradient fill (Palette::accent2().withAlpha (0.26f), 0.0f, plot_.getY(),
                                           Palette::accent2().withAlpha (0.02f), 0.0f,
                                           plot_.getBottom(), false);
                g.setGradientFill (fill);
                g.fillPath (body);
            }
        }

        g.setColour (Palette::accent2());               // the live analyser
        g.strokePath (pathOf (live, reference (live), true), juce::PathStrokeType (1.8f));
    }

    void drawEnvelope (juce::Graphics& g) {
        const auto& m = data_.meas;
        if (m.envDb.empty()) return;
        const std::vector<double> env = displaySmooth (m.envDb, 3);
        double ref = 0.0;
        int n = 0;
        for (size_t i = 0; i < m.freqs.size() && i < env.size(); ++i)
            if (m.freqs[i] >= 900 && m.freqs[i] < 1150 && env[i] > -119) { ref += env[i]; ++n; }
        if (! n) return;
        ref /= n;

        juce::Path p;
        bool started = false;
        for (size_t i = 0; i < m.freqs.size() && i < env.size(); ++i) {
            if (env[i] <= -119) { started = false; continue; }
            const double f = m.freqs[i];
            if (f < kFmin || f > kFmax) continue;
            const float x = xOf (f), y = yOf (juce::jlimit (kDbMin, kDbMax, env[i] - ref));
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        const float dashes[] = { 7.0f, 5.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (Palette::accent().withAlpha (0.6f));
        g.strokePath (dashed, juce::PathStrokeType (1.2f));
    }

    void drawCorrection (juce::Graphics& g) {
        if (data_.bands.empty()) return;
        // The band edges of a shelf move with the sample rate, so a curve drawn at 48 kHz
        // in a 44.1 kHz session is a different curve - by a couple of dB at the top, which
        // is exactly where the panel is claiming something.  It comes from the host now.
        const double fs = data_.sampleRate > 8000.0 ? data_.sampleRate : 48000.0;
        juce::Path p, fillDown;
        bool started = false;
        for (int i = 0; i <= 300; ++i) {
            const double f = kFmin * std::pow (kFmax / kFmin, i / 300.0);
            double db = 0.0;
            for (int b = 0; b < int (data_.bands.size()); ++b) {
                auto band = data_.bands[size_t (b)];
                band.gainDb = float (bandGainWithTrim (b));
                db += aeq::bandMagDb (band, f, fs);
            }
            const float x = xOf (f), y = yOf (juce::jlimit (kDbMin, kDbMax, db));
            if (! started) { p.startNewSubPath (x, y); fillDown.startNewSubPath (x, yOf (0.0)); fillDown.lineTo (x, y); started = true; }
            else { p.lineTo (x, y); fillDown.lineTo (x, y); }
        }
        fillDown.lineTo (xOf (kFmax), yOf (0.0));
        fillDown.closeSubPath();
        g.setColour (Palette::accent().withAlpha (0.13f));
        g.fillPath (fillDown);
        // the glow is the same path stroked twice: JUCE has no cheap blur, and at this
        // size two passes are indistinguishable from one at 40 % alpha
        g.setColour (Palette::accent().withAlpha (0.16f));
        g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        g.setColour (Palette::accent().withAlpha (0.30f));
        g.strokePath (p, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        g.setColour (Palette::accent());
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    void drawMarkers (juce::Graphics& g) {
        for (const auto& pr : data_.problems) {
            if (pr.action == aeq::Action::None) continue;      // near-misses: no marker
            const bool warn = pr.action == aeq::Action::WarnOnly;
            if (warn || pr.freq < 20.0) continue;
            const float x = xOf (pr.freq);
            const auto c = severityColour (pr.severity);
            g.setColour (c.withAlpha (0.18f));
            g.drawVerticalLine (int (x), plot_.getY(), plot_.getBottom());
            g.setColour (c);
            g.fillEllipse (x - 3.0f, plot_.getY() + 2.0f, 6.0f, 6.0f);
        }
    }

    void drawHandles (juce::Graphics& g) {
        for (int i = 0; i < int (data_.bands.size()); ++i) {
            const auto& b = data_.bands[size_t (i)];
            const float x = xOf (b.f0), y = yOf (bandGainWithTrim (i));
            const bool active = (i == hoverHandle_ || i == drag_);
            const float rad = active ? 8.5f : 6.5f;
            g.setColour (Palette::panel().withAlpha (0.95f));
            g.fillEllipse (x - rad, y - rad, rad * 2, rad * 2);
            g.setColour (Palette::accent());
            g.drawEllipse (x - rad, y - rad, rad * 2, rad * 2, active ? 2.4f : 1.6f);
            if (active) {
                const auto text = juce::String (int (b.f0)) + " Hz   Q " + juce::String (b.q, 1)
                                + "   " + (bandGainWithTrim (i) >= 0 ? "+" : "")
                                + juce::String (bandGainWithTrim (i), 1) + " dB";
                const juce::Rectangle<float> box (x + 12.0f, y - 30.0f, 250.0f, 26.0f);
                g.setColour (Palette::panelHi());
                g.fillRoundedRectangle (box, 6.0f);
                g.setColour (Palette::line());
                g.drawRoundedRectangle (box, 6.0f, 1.0f);
                label (g, text, box.toNearestInt(), Palette::text(), 13.0f,
                       juce::Justification::centred, false);
            }
        }
    }

    void drawLegend (juce::Graphics& g, juce::Rectangle<float> r) {
        struct Item { juce::Colour c; juce::String ar; const char* en; };
        const Item items[] = {
            { Palette::standard(), "standard",  "standard" },
            { Palette::accent2(),  "your take", "your take" },
            { Palette::accent(),   "correction", "correction" },
        };
        float x = r.getX() + 16.0f;
        for (const auto& it : items) {
            g.setColour (it.c);
            g.fillRoundedRectangle (x, r.getCentreY() - 1.5f, 14.0f, 3.0f, 1.5f);
            x += 20.0f;
            // through the shaping path: this text is Arabic, and g.drawText with a
            // system font is what turned it into boxes
            const auto txt = it.ar + "  ";
            aeq::ui::draw (g, txt, juce::Rectangle<float> (x, r.getY(), 80.0f, r.getHeight()),
                           juce::Justification::centredLeft, 13.0f, Palette::muted());
            x += aeq::ui::measure (txt, 13.0f) + 8.0f;
        }
        // the corridor earns its own entry: it is the answer to "why no action?"
        const float sx = r.getRight() - 250.0f;
        g.setColour (Palette::standard().withAlpha (0.22f));
        g.fillRoundedRectangle (sx, r.getCentreY() - 6.0f, 18.0f, 12.0f, 3.0f);
        label (g, "engineer's range (1 sd)", { int (sx) + 26, int (r.getY()), 230, int (r.getHeight()) },
               Palette::muted(), 13.0f);
    }

    void drawHover (juce::Graphics& g) {
        if (hoverPos_.x < plot_.getX() || hoverPos_.x > plot_.getRight()
            || hoverPos_.y < plot_.getY() || hoverPos_.y > plot_.getBottom())
            return;
        const double t = (hoverPos_.x - plot_.getX()) / plot_.getWidth();
        const double f = kFmin * std::pow (kFmax / kFmin, t);
        g.setColour (Palette::muted().withAlpha (0.25f));
        g.drawVerticalLine (int (hoverPos_.x), plot_.getY(), plot_.getBottom());
        const auto text = f >= 1000.0 ? juce::String (f / 1000.0, 2) + " kHz"
                                      : juce::String (int (f)) + " Hz";
        const juce::Rectangle<float> box (hoverPos_.x + 8.0f,
                                          plot_.getY() + 4.0f, 74.0f, 18.0f);
        g.setColour (Palette::panelHi());
        g.fillRoundedRectangle (box, 4.0f);
        label (g, text, box.toNearestInt(), Palette::text(), 13.0f, juce::Justification::centred);
    }

    bool embedded_ = false;
    Data data_;
    juce::Rectangle<float> plot_;
    int hoverHandle_ = -1, drag_ = -1;
    double dragStartTrim_ = 0.0, trimAtStart_ = 0.0, dragOriginDb_ = 0.0;
    juce::Point<float> hoverPos_ { -1.0f, -1.0f };
    juce::uint32 lastPushMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveView)
};

} // namespace aeq::ui
