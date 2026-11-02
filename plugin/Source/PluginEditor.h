#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/CurveView.h"
#include "ui/ProblemList.h"
#include "ui/Theme.h"

// ---------------------------------------------------------------------------
// AEQ Restore - the window.
//
// Layout, top to bottom:
//   header   what this is, what it is doing right now, and the master bypass
//   curve    the standard, your take, the tolerance corridor, the correction
//   report   one card per finding, and the verdict card that answers "do I touch
//            this take at all?"
//   transport what a mixer actually turns: six knobs and the analysis time
//
// The window is resizable: a plugin that only looks right at 880x560 is the thing
// this redesign was for.
// ---------------------------------------------------------------------------
class AeqAudioProcessorEditor : public juce::AudioProcessorEditor,
                                private juce::Timer {
public:
    explicit AeqAudioProcessorEditor (AeqAudioProcessor&);
    ~AeqAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildControls();
    void layoutHeader (juce::Rectangle<int>);
    void layoutCurve (juce::Rectangle<int>);
    void layoutReport (juce::Rectangle<int>);
    void layoutTransport (juce::Rectangle<int>);

    // the verdict: one sentence, three numbers underneath
    void drawVerdict (juce::Graphics&, juce::Rectangle<int>);
    void drawHeader (juce::Graphics&, juce::Rectangle<int>);
    void drawTransportFrame (juce::Graphics&, juce::Rectangle<int>);
    juce::String verdictTitle() const;
    juce::Colour verdictColour() const;

    AeqAudioProcessor& proc_;

    aeq::ui::CurveView curve_;
    aeq::ui::ProblemList problems_;
    juce::Viewport reportView_;

    juce::TextButton learnBtn_ { "تحليل مستمر" }, resetBtn_ { "إعادة" }, bypassBtn_ { "Bypass" };
    juce::ComboBox mode_;
    juce::OwnedArray<juce::Slider> knobs_;
    juce::OwnedArray<juce::Label> knobNames_;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttachments_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aMode_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aBypass_;

    std::unique_ptr<juce::LookAndFeel> look_;

    aeq::RestoreState state_;
    aeq::Measurements meas_;
    std::vector<aeq::BandSpec> bands_;
    std::vector<double> trims_;
    double peakZ_ = 0.0;
    double peakZf_ = 0.0;
    bool haveData_ = false;
    juce::Rectangle<int> verdictBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeqAudioProcessorEditor)
};
