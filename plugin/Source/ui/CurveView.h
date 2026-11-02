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
        bool valid = false;
        bool settled = false;
        double analysedSeconds = 0.0;
    };

    CurveView() { setWantsKeyboardFocus (false); }

    void setData (Data d) {
        data_ = std::move (d);
        repaint();
    }

    // (band index, the trim the user dragged to) and "double-clicked" to reset
    std::function<void (int, double)> onTrim;
    std::function<void (int)> onTrimReset;

    void paint (juce::Graphics& g) override {
        auto area = getLocalBounds().toFloat();
        card (g, area, Palette::panel(), Palette::line(), 14.0f);
        glow (g, area.reduced (1.0f), Palette::accent2(), 14.0f);

        auto r = area.reduced (1.0f);
        auto legend = r.removeFromTop (30.0f);
        plot_ = r.reduced (44.0f, 8.0f).withTrimmedBottom (14.0f);

        drawGrid (g);
        if (! data_.valid || data_.meas.freqs.empty()) {
            drawEmpty (g, area);
            drawLegend (g, legend);
            return;
        }
        drawCorridor (g);
        drawTarget (g);
        drawMeasured (g);
        drawEnvelope (g);
        drawCorrection (g);
        drawMarkers (g);
        drawHandles (g);
        drawLegend (g, legend);
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
    static constexpr double kDbMax = 18.0, kDbMin = -30.0;

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
        g.setColour (Palette::line().withAlpha (0.55f));
        const double ticks[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        for (double f : ticks) {
            const float x = xOf (f);
            g.drawVerticalLine (int (x), plot_.getY(), plot_.getBottom());
            label (g, f >= 1000 ? juce::String (f / 1000.0, 0) + "k" : juce::String (int (f)),
                   { int (x) - 18, int (plot_.getBottom()) + 2, 36, 14 },
                   Palette::muted(), 10.0f, juce::Justification::centred);
        }
        for (int db = int (kDbMin); db <= int (kDbMax); db += 6) {
            const float y = yOf (double (db));
            g.setColour (Palette::line().withAlpha (db == 0 ? 0.9f : 0.45f));
            g.drawHorizontalLine (int (y), plot_.getX(), plot_.getRight());
            label (g, (db > 0 ? "+" : "") + juce::String (db),
                   { int (plot_.getX()) - 38, int (y) - 8, 32, 16 },
                   Palette::muted(), 10.0f, juce::Justification::centredRight);
        }
    }

    void drawEmpty (juce::Graphics& g, juce::Rectangle<float> area) {
        label (g, "لسه مفيش قياس — شغّل التسجيل من أوله",
               area.toNearestInt(), Palette::muted(), 14.0f, juce::Justification::centred);
        label (g, "the meter fills as the take plays; nothing is applied before it settles",
               area.toNearestInt().withTrimmedTop (24), Palette::muted().withAlpha (0.7f),
               11.0f, juce::Justification::centred);
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

    void drawMeasured (juce::Graphics& g) {
        const auto& m = data_.meas;
        if (m.bandDb.empty()) return;
        double ref = 0.0;
        int n = 0;
        for (size_t i = 0; i < m.freqs.size(); ++i)
            if (m.freqs[i] >= 900 && m.freqs[i] < 1150) { ref += m.bandDb[i]; ++n; }
        if (n) ref /= n;

        juce::Path p;
        bool started = false;
        for (size_t i = 0; i < m.freqs.size(); ++i) {
            const double f = m.freqs[i];
            if (f < kFmin || f > kFmax) continue;
            const float x = xOf (f), y = yOf (juce::jlimit (kDbMin, kDbMax, m.bandDb[i] - ref));
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        g.setColour (Palette::accent2().withAlpha (0.95f));
        g.strokePath (p, juce::PathStrokeType (1.7f));
    }

    void drawEnvelope (juce::Graphics& g) {
        const auto& m = data_.meas;
        if (m.envDb.empty()) return;
        double ref = 0.0;
        int n = 0;
        for (size_t i = 0; i < m.freqs.size(); ++i)
            if (m.freqs[i] >= 900 && m.freqs[i] < 1150 && m.envDb[i] > -119) { ref += m.envDb[i]; ++n; }
        if (! n) return;
        ref /= n;

        juce::Path p;
        bool started = false;
        for (size_t i = 0; i < m.freqs.size(); ++i) {
            if (m.envDb[i] <= -119) { started = false; continue; }
            const double f = m.freqs[i];
            if (f < kFmin || f > kFmax) continue;
            const float x = xOf (f), y = yOf (juce::jlimit (kDbMin, kDbMax, m.envDb[i] - ref));
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
        const double fs = 48000.0;
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
                const juce::Rectangle<float> box (x + 12.0f, y - 26.0f, 190.0f, 22.0f);
                g.setColour (Palette::panelHi());
                g.fillRoundedRectangle (box, 6.0f);
                g.setColour (Palette::line());
                g.drawRoundedRectangle (box, 6.0f, 1.0f);
                label (g, text, box.toNearestInt(), Palette::text(), 11.0f,
                       juce::Justification::centred, false);
            }
        }
    }

    void drawLegend (juce::Graphics& g, juce::Rectangle<float> r) {
        struct Item { juce::Colour c; const char* ar; const char* en; };
        const Item items[] = {
            { Palette::standard(), "المعيار", "standard" },
            { Palette::accent2(),  "تسجيلك", "your take" },
            { Palette::accent(),   "التصحيح", "correction" },
        };
        float x = r.getX() + 16.0f;
        for (const auto& it : items) {
            g.setColour (it.c);
            g.fillRoundedRectangle (x, r.getCentreY() - 1.5f, 14.0f, 3.0f, 1.5f);
            x += 20.0f;
            g.setColour (Palette::muted());
            g.setFont (font (11.5f));
            const auto txt = juce::String::fromUTF8 (it.ar) + "  ";
            g.drawText (txt, juce::Rectangle<float> (x, r.getY(), 80.0f, r.getHeight()).toNearestInt(),
                        juce::Justification::centredLeft, false);
            x += juce::GlyphArrangement::getStringWidth (font (11.5f), txt) + 6.0f;
        }
        // the corridor earns its own entry: it is the answer to "why no action?"
        const float sx = r.getRight() - 250.0f;
        g.setColour (Palette::standard().withAlpha (0.22f));
        g.fillRoundedRectangle (sx, r.getCentreY() - 6.0f, 18.0f, 12.0f, 3.0f);
        label (g, "مدى المهندس ±1σ", { int (sx) + 24, int (r.getY()), 210, int (r.getHeight()) },
               Palette::muted(), 11.0f);
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
        label (g, text, box.toNearestInt(), Palette::text(), 10.5f, juce::Justification::centred);
    }

    Data data_;
    juce::Rectangle<float> plot_;
    int hoverHandle_ = -1, drag_ = -1;
    double dragStartTrim_ = 0.0, trimAtStart_ = 0.0, dragOriginDb_ = 0.0;
    juce::Point<float> hoverPos_ { -1.0f, -1.0f };
    juce::uint32 lastPushMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveView)
};

} // namespace aeq::ui
