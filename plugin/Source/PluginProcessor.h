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
    // The workflow: listen to the take, the user stops it, it is frozen - and the
    // decision survives a project reload (a track that learned stays learned).
    void stopLearning();
    // Open a sealed pass again: the take stays, the correction stays in the path, and
    // the analysis resumes.  This is the clamp's second press.
    void reopenPass();
    // Forget the take and listen again: the reset icon.  It has to put `learning_` back
    // too - it did not in 1.2.16, so after one stop a reset left the plugin with nothing
    // analysing and no way back short of reopening the project.
    void relearn();
    bool isFrozen() const { return core_.frozen(); }
    bool isCommitted() const { return core_.committed(); }
    bool isLearning() const { return learning_; }

    // The whole product in one struct: is it still listening, how much has it heard,
    // what does it need, and what is it doing right now.
    struct AutoState {
        enum Stage { Listening = 0, Applied = 1 };
        int    stage = Listening;
        double heardSeconds = 0.0;      // sung audio analysed so far
        double needSeconds  = 45.0;     // the safety cap, not the plan
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
    // A control that changes the rule was moved while the pass is sealed: re-solve it on
    // the message thread (see timerCallback), never on the audio thread.
    std::atomic<bool> needsResolve_{false};
    int resolveTick_ = 0;
    int slowTick_ = 0;             // see timerCallback(): slow adaptation once settled
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AeqAudioProcessor)
};
