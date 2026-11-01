#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Simple, honest UI:
//   top    - the measured spectrum, the target, and the curve being applied
//   middle - the problem list, in words, with the number that justified each line
//   bottom - the controls a mixer actually turns
class AeqAudioProcessorEditor : public juce::AudioProcessorEditor,
                                private juce::Timer {
public:
    explicit AeqAudioProcessorEditor(AeqAudioProcessor&);
    ~AeqAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawCurves(juce::Graphics&, juce::Rectangle<int> area);

    AeqAudioProcessor& proc_;
    juce::TextButton learnBtn_{"Learn"}, resetBtn_{"Reset"};
    juce::Slider amount_, hpf_, deEss_, axis1_, axis2_, air_;
    juce::ComboBox mode_;
    juce::ToggleButton bypass_{"Bypass"};
    juce::Label status_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aAmount_, aHpf_, aDeEss_, aA1_, aA2_, aAir_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aMode_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aBypass_;

    aeq::RestoreState lastState_;
    aeq::Measurements lastMeas_;
    bool haveData_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AeqAudioProcessorEditor)
};
