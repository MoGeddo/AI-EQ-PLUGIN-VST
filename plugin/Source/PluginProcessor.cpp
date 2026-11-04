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
constexpr const char* kSettle = "settle_s";

// RestoreSettings is a plain struct; comparing it is cheaper than rebuilding the
// whole biquad cascade, and rebuild means an allocation - which must not happen
// once per audio block.
bool sameSettings(const aeq::RestoreSettings& a, const aeq::RestoreSettings& b) {
    return a.mode == b.mode && a.maxBands == b.maxBands && a.maxGainDb == b.maxGainDb
        && a.correctionAmount == b.correctionAmount && a.hpfHz == b.hpfHz
        && a.deEssAmount == b.deEssAmount && a.airTrimDb == b.airTrimDb
        && a.bypass == b.bypass && a.axis1 == b.axis1 && a.axis2 == b.axis2
        && a.learn == b.learn && a.settleSeconds == b.settleSeconds;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AeqAudioProcessor::createLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterFloat>(kAmount, "Standard %",
              NormalisableRange<float>(0.f, 100.f, 0.1f), 100.f,
              AudioParameterFloatAttributes().withLabel("%")));
    // NOTE: 0 must be inside the range - it is the "AUTO" position.  A default
    // outside NormalisableRange is silently clamped by JUCE, which would pin the
    // low cut to 20 Hz and quietly disable the automatic decision.
    l.add(std::make_unique<AudioParameterFloat>(kHpf, "Low cut (Hz)",
              NormalisableRange<float>(0.f, 220.f, 1.f), 0.f,
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
    // How long it listens before the correction is allowed into the signal path.
    // 30 s was the first guess and it was wrong for the way the tool is used: with a
    // verse in front of it, half a minute of "nothing happened" reads as "it does not
    // work".  Eight seconds is a phrase; the correction keeps refining afterwards.
    l.add(std::make_unique<AudioParameterFloat>(kSettle, "Analyze first",
              NormalisableRange<float>(0.f, 90.f, 1.f), 8.f,
              AudioParameterFloatAttributes().withLabel("s")
                .withStringFromValueFunction([](float v, int) {
                    return v < 1.f ? juce::String("OFF") : juce::String((int)v) + " s";
                })));
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
    settingsFromParams();               // so the first block already has them
    core_.setSettings(settings_);
    lastPushed_ = settings_;
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
    settingsFromParams();
    // only touch the core when something actually moved: setSettings rebuilds
    // the cascade, and doing that on every block is an allocation on the audio
    // thread for no reason
    if (!sameSettings(settings_, lastPushed_)) {
        core_.setSettings(settings_);
        lastPushed_ = settings_;
    }

    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    core_.processBlock(l, r, n);
}

void AeqAudioProcessor::settingsFromParams() {
    settings_.correctionAmount = apvts_.getRawParameterValue(kAmount)->load() / 100.f;
    settings_.hpfHz            = apvts_.getRawParameterValue(kHpf)->load();
    settings_.deEssAmount      = apvts_.getRawParameterValue(kDeEss)->load() / 100.f;
    settings_.mode             = (int) apvts_.getRawParameterValue(kMode)->load();
    settings_.axis1            = apvts_.getRawParameterValue(kAxis1)->load();
    settings_.axis2            = apvts_.getRawParameterValue(kAxis2)->load();
    settings_.airTrimDb        = apvts_.getRawParameterValue(kAir)->load();
    settings_.settleSeconds    = apvts_.getRawParameterValue(kSettle)->load();
    settings_.bypass           = apvts_.getRawParameterValue(kBypass)->load() > 0.5f;
    settings_.learn            = learning_.load();
}

void AeqAudioProcessor::timerCallback() {
    // Analysis and solving happen here, never in processBlock, and never only
    // because the editor is open: the timer lives in the processor, so the plugin
    // works with its window closed - which is what "automatic" has to mean.
    if (! learning_.load()) {
        core_.drainPending();
        return;
    }
    const auto st = core_.state();
    const bool settled = st.valid && st.settled;
    // While it is still learning the take, solve as often as the timer fires (4 Hz).
    // Once it has, keep adapting slowly: re-solving four times a second forever is
    // the message thread doing work nobody asked for.
    if (! settled || (++slowTick_ % 8 == 0)) core_.refresh();
    else                                  core_.drainPending();
}

AeqAudioProcessor::AutoState AeqAudioProcessor::autoState() const {
    AutoState a;
    const auto st = core_.state();
    a.stage        = (st.valid && st.settled) ? AutoState::Applied : AutoState::Listening;
    a.heardSeconds = st.analyzedSeconds;
    a.needSeconds  = settings_.settleSeconds > 0.0 ? settings_.settleSeconds : 0.0;
    a.hpfHz        = st.hpfHz;
    a.bands        = int(core_.activeBands().size());
    return a;
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
