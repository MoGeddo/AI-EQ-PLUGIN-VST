#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
constexpr const char* kAmount = "amount";
constexpr const char* kHpf    = "hpf_hz";
constexpr const char* kDeEss  = "deess";
constexpr const char* kMode   = "mode";
constexpr const char* kBypass = "bypass";
constexpr const char* kAxis1  = "axis_warm_presence";
constexpr const char* kAxis2  = "axis_body_air";
constexpr const char* kAir    = "air_db";
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AeqAudioProcessor::createLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterFloat>(kAmount, "Standard %",
              NormalisableRange<float>(0.f, 100.f, 0.1f), 100.f,
              AudioParameterFloatAttributes().withLabel("%")));
    l.add(std::make_unique<AudioParameterFloat>(kHpf, "Low cut (Hz)",
              NormalisableRange<float>(20.f, 220.f, 1.f), 0.f,
              AudioParameterFloatAttributes().withLabel("Hz")
                .withStringFromValueFunction([](float v, int) {
                    return v < 1.f ? juce::String("AUTO") : juce::String((int)v);
                })));
    l.add(std::make_unique<AudioParameterFloat>(kDeEss, "De-esser",
              NormalisableRange<float>(0.f, 100.f, 1.f), 0.f,
              AudioParameterFloatAttributes().withLabel("%")));
    l.add(std::make_unique<AudioParameterChoice>(kMode, "Mode",
              juce::StringArray{"Restore (fix problems)", "Match (corpus tone)"}, 0));
    l.add(std::make_unique<AudioParameterFloat>(kAxis1, "Warm <-> Presence",
              NormalisableRange<float>(-1.f, 1.f, 0.01f), 0.f));
    l.add(std::make_unique<AudioParameterFloat>(kAxis2, "Body <-> Air",
              NormalisableRange<float>(-1.f, 1.f, 0.01f), 0.f));
    l.add(std::make_unique<AudioParameterFloat>(kAir, "Air trim",
              NormalisableRange<float>(-6.f, 6.f, 0.1f), 0.f,
              AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterBool>(kBypass, "Bypass", false));
    return l;
}

AeqAudioProcessor::AeqAudioProcessor()
    : AudioProcessor(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "AEQ_RESTORE", createLayout()) {
    startTimerHz(4);   // solve + refresh 4x per second on the message thread
}

void AeqAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    core_.prepare(sampleRate, samplesPerBlock);
}

void AeqAudioProcessor::releaseResources() {}

bool AeqAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void AeqAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    if (resetRequested_.exchange(false)) core_.reset();

    // parameters -> settings (cheap, no allocation)
    settings_.correctionAmount = apvts_.getRawParameterValue(kAmount)->load() / 100.f;
    settings_.hpfHz            = apvts_.getRawParameterValue(kHpf)->load();
    settings_.deEssAmount      = apvts_.getRawParameterValue(kDeEss)->load() / 100.f;
    settings_.mode             = (int) apvts_.getRawParameterValue(kMode)->load();
    settings_.axis1            = apvts_.getRawParameterValue(kAxis1)->load();
    settings_.axis2            = apvts_.getRawParameterValue(kAxis2)->load();
    settings_.airTrimDb        = apvts_.getRawParameterValue(kAir)->load();
    settings_.bypass           = apvts_.getRawParameterValue(kBypass)->load() > 0.5f;
    settings_.learn            = learning_.load();
    core_.setSettings(settings_);

    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    core_.processBlock(l, r, n);
}

void AeqAudioProcessor::timerCallback() {
    // Analysis/solving happens here, never in processBlock.
    if (learning_.load()) core_.refresh();
}

void AeqAudioProcessor::requestReset() { resetRequested_ = true; }

void AeqAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts_.copyState();
    state.setProperty("version", 1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void AeqAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (tree.isValid()) apvts_.replaceState(tree);
    }
}

juce::AudioProcessorEditor* AeqAudioProcessor::createEditor() {
    return new AeqAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AeqAudioProcessor();
}
