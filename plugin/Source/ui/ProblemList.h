#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "Theme.h"
#include "../../Source/dsp/ProblemDetector.h"

namespace aeq::ui {

// ---------------------------------------------------------------------------
// The report, as cards instead of lines of text.
//
// Each card carries the three things a mixer needs before believing a number:
// what it is, how big it is compared with the engineer's own range (the bar), and
// what the plugin proposes to do about it.  Evidence that is not an action - the
// near-misses the tighter envelope measurement can now see - gets a card too, in
// amber and with no action line, because "this is 0.2 dB under the bar" is the
// most useful thing the tool can say.
// ---------------------------------------------------------------------------
class ProblemList : public juce::Component {
public:
    ProblemList() = default;

    void setProblems (std::vector<aeq::Problem> p) {
        problems_ = std::move (p);
        updateHeight();
        repaint();
    }

    int contentHeight() const { return height_; }

    // the viewport calls this once; the list is its own content component
    void updateHeight() {
        const int rows = int (problems_.size());
        height_ = juce::jmax (60, rows * 74 + 8);
        setSize (getWidth(), height_);
    }

    void paint (juce::Graphics& g) override {
        if (problems_.empty()) {
            card (g, getLocalBounds().toFloat().reduced (1.0f), Palette::panelHi(),
                  Palette::line(), 10.0f);
            label (g, "مفيش حاجة محتاجة تدخّل", getLocalBounds().reduced (14, 10).removeFromTop (24),
                   Palette::ok(), 13.0f, juce::Justification::topLeft, true);
            label (g, "كل منطقة داخل مدى المهندس — مش بنلمس حاجة",
                   getLocalBounds().reduced (14, 10).withTrimmedTop (26).removeFromTop (20).translated (0, 0),
                   Palette::muted(), 11.5f);
            return;
        }

        int y = 2;
        for (const auto& p : problems_) {
            auto row = juce::Rectangle<float> (1.0f, float (y), float (getWidth()) - 2.0f, 68.0f);
            drawCard (g, row, p);
            y += 74;
        }
    }

private:
    void drawCard (juce::Graphics& g, juce::Rectangle<float> row, const aeq::Problem& p) {
        const bool evidence = (p.action == aeq::Action::None);
        const auto accent = evidence ? Palette::warn() : severityColour (p.severity);

        card (g, row, Palette::panelHi(), Palette::line(), 10.0f);

        // severity rail on the right (this is an RTL-language report; keep the rail
        // on the reading-start side, which for Arabic is the right edge)
        auto rail = row.removeFromRight (4.0f).reduced (0.0f, 8.0f);
        g.setColour (Palette::line());
        g.fillRoundedRectangle (rail, 2.0f);
        const float h = juce::jmax (4.0f, rail.getHeight() * float (juce::jlimit (0.05, 1.0, p.severity)));
        g.setColour (accent);
        g.fillRoundedRectangle (rail.withHeight (h).withY (rail.getBottom() - h), 2.0f);

        auto inner = row.reduced (12.0f, 8.0f);

        // title line
        auto title = inner.removeFromTop (20.0f);
        g.setColour (accent);
        g.fillEllipse (title.getRight() - 8.0f, title.getY() + 6.0f, 6.0f, 6.0f);
        label (g, juce::String::fromUTF8 (p.labelAr.c_str()), title.toNearestInt().withTrimmedRight (14),
               Palette::text(), 13.0f, juce::Justification::topRight, true);

        // the measured evidence, in the same words the CLI prints
        auto detail = inner.removeFromTop (18.0f);
        label (g, juce::String::fromUTF8 (p.detailAr.c_str()), detail.toNearestInt(),
               Palette::muted(), 11.5f, juce::Justification::topRight);

        // the action, or the reason there is none
        auto act = inner.removeFromTop (16.0f);
        juce::String actionText;
        switch (p.action) {
            case aeq::Action::CutBroad:
                actionText = "خصم " + juce::String (std::abs (p.gainDb), 1) + " dB عند "
                           + juce::String (int (p.freq)) + " Hz (Q " + juce::String (p.q, 1) + ")";
                break;
            case aeq::Action::CutNarrow:
                actionText = "notch عند " + juce::String (int (p.freq)) + " Hz  Q "
                           + juce::String (p.q, 1) + "  " + juce::String (p.gainDb, 1) + " dB";
                break;
            case aeq::Action::RaiseHpf:
                actionText = "قصّ تحت " + juce::String (int (p.freq)) + " Hz";
                break;
            case aeq::Action::DeEss:
                actionText = "de-esser ديناميكي، أقصى " + juce::String (std::abs (p.gainDb), 1) + " dB";
                break;
            case aeq::Action::AddAir:
                actionText = "رفع خفيف فوق 8 kHz  " + juce::String (p.gainDb, 1) + " dB";
                break;
            case aeq::Action::WarnOnly:
                actionText = "تحذير فقط — الـEQ مش هيحلها";
                break;
            case aeq::Action::None:
                actionText = "دليل، مش قرار: داخل المدى بس قريب من الحد";
                break;
        }
        label (g, actionText, act.toNearestInt(),
               evidence ? Palette::warn().withAlpha (0.85f) : Palette::accent(),
               11.0f, juce::Justification::topRight, true);
    }

    std::vector<aeq::Problem> problems_;
    int height_ = 60;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProblemList)
};

} // namespace aeq::ui
