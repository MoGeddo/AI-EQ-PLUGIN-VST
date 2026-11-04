#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/dsp/RestoreCore.h"

// AEQ Restore - VST3 wrapper.
//
// The wrapper is deliberately thin: every risky decision lives in aeq::RestoreCore,
// which is compiled and gated by Tests/test_core.cpp without JUCE in the way.
class AeqAudioProcessor : public juce::AudioProcessor,
                          private juce::Timer {
public:
    AeqAudioProcessor();
    ~AeqAudioProcessor() override = default;

    // --- AudioProcessor -----------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AEQ Restore"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // --- used by the editor -------------------------------------------------
    aeq::RestoreCore& core() { return core_; }
    juce::AudioProcessorValueTreeState& apvts() { return apvts_; }
    void requestReset();
    bool isLearning() const { return learning_; }

    // The whole product in one struct: is it still listening, how much has it heard,
    // what does it need, and what is it doing right now.
    struct AutoState {
        enum Stage { Listening = 0, Applied = 1 };
        int    stage = Listening;
        double heardSeconds = 0.0;      // sung audio analysed so far
        double needSeconds  = 8.0;      // what it is waiting for
        int    bands        = 0;        // bands in the signal path once applied
        double hpfHz        = 0.0;      // the low cut it decided on
    };
    AutoState autoState() const;
    void setLearning(bool on) { learning_ = on; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void timerCallback() override;
    void settingsFromParams();

    aeq::RestoreCore core_;
    aeq::RestoreSettings settings_, lastPushed_;
    juce::AudioProcessorValueTreeState apvts_;
    std::atomic<bool> learning_{true}, resetRequested_{false};
    int slowTick_ = 0;             // see timerCallback(): slow adaptation once settled
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AeqAudioProcessor)
};
