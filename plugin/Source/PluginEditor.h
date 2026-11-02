#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/CurveView.h"
#include "ui/ProblemList.h"
#include "ui/Theme.h"

// ---------------------------------------------------------------------------
// AEQ Restore - the window.
//
//   title bar   the mark, the name, the live readouts, the master bypass
//   curve       the standard, your take, the tolerance corridor, the correction
//   verdict     the one sentence that matters, with its numbers under it
//   report      one card per finding
//   dock        what a mixer actually turns: seven knobs, a label above each and
//               its value below it - never inside it
//
// Every string is drawn through aeq::ui::draw(), which is the shaped, embedded-font
// path.  Nothing in this file calls Graphics::drawText with a font the OS supplies.
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

    void drawTitleBar (juce::Graphics&, juce::Rectangle<int>);
    void drawVerdict (juce::Graphics&, juce::Rectangle<int>);
    void drawDockFrame (juce::Graphics&, juce::Rectangle<int>);

    juce::String verdictTitle() const;
    juce::String verdictSubtitle() const;
    juce::Colour verdictColour() const;
    int actionCount() const;

    AeqAudioProcessor& proc_;

    aeq::ui::CurveView curve_;
    aeq::ui::ProblemList problems_;
    juce::Viewport reportView_;

    juce::TextButton learnBtn_ { "Analyse" }, resetBtn_ { "Reset" }, bypassBtn_ { "Bypass" };
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

    juce::Rectangle<int> verdictBounds_, dockBounds_, curveBounds_;
    std::vector<juce::Rectangle<int>> knobValueRows_;   // where each knob's number goes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AeqAudioProcessorEditor)
};
