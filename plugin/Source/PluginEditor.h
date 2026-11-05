#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/CurveView.h"
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
                                private juce::Timer {
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

private:
    // ---- the four blocks, as fractions of the window so a resize is proportional
    struct Blocks {
        juce::Rectangle<int> header, hero, chips, footer;
    };
    Blocks blocks() const;
    juce::Rectangle<int> well() const;       // the plot area inside the ribbon
    juce::Rectangle<int> ribbon() const;
    juce::Rectangle<int> clampHit() const;
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

    juce::String verdictLine() const;
    juce::String chipMode (size_t index) const;
    int   staticBandCount() const;
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
    int    hoverChip_ = -1;
    bool   hoverClamp_ = false;
    int    draggedChip_ = -1;
    double dragStartTrim_ = 0.0;
    juce::Point<int> dragOrigin_;

    aeq::RestoreState state_;
    aeq::Measurements meas_;
    std::vector<aeq::BandSpec> bands_;
    std::vector<double> trims_;
    bool haveData_ = false;

    long lastVersion_ = -1;
    int  lastTick_ = -1;
    unsigned long long lastLiveVersion_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeqAudioProcessorEditor)
};
