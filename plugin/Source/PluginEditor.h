#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/CurveView.h"
#include "ui/PanelCopy.h"
#include "ui/Theme.h"

// ---------------------------------------------------------------------------
// AEQ Restore - the window, v9.
//
// The panel is four blocks and nothing else, because five stacked bands of equal
// weight is a form and this is an instrument:
//
//   header  (40)   the mark, the tagline, the seal, the master bypass
//   hero   (486)   the graph, and inside it the take: it fills while it learns and
//                  a machined clamp sits where the person stopped it
//   chips   (64)   the four bands in the signal path - a row, not a table
//   footer  (36)   the mode, one sentence, and the two things a hand turns
//
// Every number here is the same number as design/preview/ui_v9.py (margins 22,
// radii 16/14/12/11/4, type 8/10/12/15), so the render that was signed off and the
// window that ships are the same panel, not two opinions.
//
// Interaction is the story: the clamp IS the stop button (click it when you have
// sung the take), the chips ARE the band trims (drag one), and a double click on
// either one puts it back.
// ---------------------------------------------------------------------------
class AeqAudioProcessorEditor : public juce::AudioProcessorEditor,
                                private juce::Timer,
                                public juce::TooltipClient {
public:
    explicit AeqAudioProcessorEditor (AeqAudioProcessor&);
    ~AeqAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    // Space stops and opens a pass; the arrows nudge the knob that has focus.  A control
    // a person can only reach with a mouse is a control half the people cannot reach.
    bool keyPressed (const juce::KeyPress&) override;
    juce::String getTooltip() override { return tooltip_; }

    // ---- the control test (Tests/ui_shot.cpp) -----------------------------------
    // "Are the keys working" cannot be answered by a screenshot, so the gate presses
    // them: it asks the editor what is at a point, clicks it, and checks that the
    // plugin changed.  That needs the hit rectangles to be answerable from outside
    // paint(), which is all this is.
    enum class Control { None, Clamp, Bypass, Reset, ModeRestore, ModeMatch,
                         Chip, StrengthKnob, AutoStopKnob };
    Control controlAt (juce::Point<int>) const;
    // The one place that answers "where is this control" - for the hit test above, and for
    // the tests that click the real panel instead of trusting a number they were told.
    juce::Rectangle<int> controlBounds (Control c) const;

    float strengthShown() const { return float (amountKnob_.getValue()); }
    float autoStopShown() const { return float (settleKnob_.getValue()); }
    int   chipCount() const { return int (chips_.size()); }
    int   chipSource (int i) const {
        return (i >= 0 && i < int (chips_.size())) ? chips_[size_t (i)].src : -1;
    }

private:
    // ---- the four blocks, as fractions of the window so a resize is proportional
    struct Blocks {
        juce::Rectangle<int> header, hero, chips, footer;
    };
    Blocks blocks() const;
    juce::Rectangle<int> heroRow (float y0, float y1) const;   // design y -> window rect
    juce::Rectangle<int> well() const;       // the plot area inside the ribbon
    juce::Rectangle<int> ribbon() const;
    juce::Rectangle<int> clampHit() const;
    // A chip is one thing in the chain, and which thing it is comes from the core:
    // band >= 0 is a static band of the committed cascade, tracked >= 0 is a dynamic
    // layer that sits on a harmonic of the pitch.  The row is built from what exists,
    // so it cannot claim a layer the plugin is not running.
    //
    // `src` is the index into the core's band list - which is what setBandTrim wants -
    // while the chip's position in the row is not that index, because bands at 0 dB are
    // not shown.  They were the same number in 1.2.16, and a drag on the third visible
    // band moved whichever band happened to be third in the array.
    struct Chip { int band = -1; int tracked = -1; int src = -1; };
    bool chipDraggable (size_t index) const;
    // What the thing under the mouse is called and what it does.  Every control on this
    // panel is a shape; a shape with no words is a puzzle.
    juce::String tooltipAt (juce::Point<int>) const;
    std::vector<juce::Rectangle<int>> chipRects() const;
    juce::Rectangle<int> modeSeg (int i) const;
    juce::Rectangle<int> bypassHit() const;
    juce::Rectangle<int> resetHit() const;
    int chipAt (juce::Point<int>) const;

    void timerCallback() override;
    void buildControls();
    static int defaultWidth();
    static int defaultHeight();

    void drawHeader (juce::Graphics&, juce::Rectangle<int>);
    void drawWell (juce::Graphics&, juce::Rectangle<int>);
    void drawTake (juce::Graphics&, juce::Rectangle<int>);
    void drawChips (juce::Graphics&, juce::Rectangle<int>);
    void drawFooter (juce::Graphics&, juce::Rectangle<int>);
    void drawTooltip (juce::Graphics&);

    // what the panel knows about the pass; the sentences themselves are in ui/PanelCopy.h
    aeq::copy::Facts panelFacts() const;
    juce::String verdictLine() const;
    juce::String chipMode (size_t index) const;
    bool  hasTrackedLayer() const;

    AeqAudioProcessor& proc_;

    aeq::ui::CurveView curve_;

    juce::Slider amountKnob_, settleKnob_;
    juce::Label  amountName_, settleName_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aAmount_, aSettle_;
    std::unique_ptr<juce::LookAndFeel> look_;

    // the seal: how much of the take was heard when it was stopped
    double sealedAt_ = 0.0;        // 0..1 of the ribbon
    bool   committedSeen_ = false;
    bool   stoppedByUser_ = true;  // who ended the pass - the label has to know
    int    hoverChip_ = -1;
    bool   hoverClamp_ = false;
    int    draggedChip_ = -1;
    double dragStartTrim_ = 0.0;
    juce::Point<int> dragOrigin_;

    aeq::RestoreState state_;
    aeq::Measurements meas_;
    std::vector<aeq::BandSpec> bands_;
    std::vector<Chip> chips_;
    std::vector<double> trims_;
    // what is under the mouse, for the tooltip: the panel names its own controls
    juce::String tooltip_;
    bool haveData_ = false;

    long lastVersion_ = -1;
    int  lastTick_ = -1;
    unsigned long long lastLiveVersion_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeqAudioProcessorEditor)
};
