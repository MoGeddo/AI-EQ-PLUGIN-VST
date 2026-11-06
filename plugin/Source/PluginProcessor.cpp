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
constexpr const char* kAutoStop = "auto_stop";

// RestoreSettings is a plain struct; comparing it is cheaper than rebuilding the
// whole biquad cascade, and rebuild means an allocation - which must not happen
// once per audio block.
bool sameSettings(const aeq::RestoreSettings& a, const aeq::RestoreSettings& b) {
    return a.mode == b.mode && a.maxBands == b.maxBands && a.maxGainDb == b.maxGainDb
        && a.correctionAmount == b.correctionAmount && a.hpfHz == b.hpfHz
        && a.deEssAmount == b.deEssAmount && a.airTrimDb == b.airTrimDb
        && a.bypass == b.bypass && a.axis1 == b.axis1 && a.axis2 == b.axis2
        && a.learn == b.learn && a.settleSeconds == b.settleSeconds
        && a.autoStopSeconds == b.autoStopSeconds;
}

// Settings that change the answer rather than how it is played back.  Bypass, learn and
// the two stop settings are deliberately not here: they change what you hear, not what
// was solved.
bool solveRelevant(const aeq::RestoreSettings& a, const aeq::RestoreSettings& b) {
    return a.mode != b.mode || a.correctionAmount != b.correctionAmount
        || a.hpfHz != b.hpfHz || a.deEssAmount != b.deEssAmount
        || a.airTrimDb != b.airTrimDb || a.axis1 != b.axis1 || a.axis2 != b.axis2;
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
    // The knob on the panel.  0 = the pass ends when the person ends it, which is the
    // promise on the header; anything else is the user asking the plugin to stop for
    // them, and then the seal says so.
    l.add(std::make_unique<AudioParameterFloat>(kAutoStop, "Auto-stop after",
              NormalisableRange<float>(0.f, 300.f, 1.f), 0.f,
              AudioParameterFloatAttributes().withLabel("s")
                .withStringFromValueFunction([](float v, int) {
                    return v < 1.f ? juce::String("OFF") : juce::String((int)v) + " s";
                })));
    l.add(std::make_unique<AudioParameterFloat>(kSettle, "Analyze first",
              NormalisableRange<float>(0.f, 300.f, 1.f), 45.f,
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
    // 30 Hz is for the *display*: the analyser's moving spectrum is published here
    // and nowhere else, so the curve moves with the music.  Solving is the expensive
    // part and stays at ~4 Hz (see timerCallback) - the two used to be the same
    // tick, which is why the analyser only moved four times a second.
    startTimerHz(30);
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
        // A control that changes the *rule* - the mode, the strength macro, a taste axis,
        // the low cut - has to re-solve the take it already heard, or it does nothing at
        // all once the pass is sealed.  Which is what they all did in 1.2.12: the seal
        // froze the decision and the panel went on showing a mode that was not in it.
        // Flagged here (audio thread, one store) and solved on the message thread.
        if (solveRelevant(settings_, lastPushed_)) needsResolve_.store (true, std::memory_order_release);
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
    settings_.autoStopSeconds  = apvts_.getRawParameterValue(kAutoStop)->load();
    settings_.bypass           = apvts_.getRawParameterValue(kBypass)->load() > 0.5f;
    settings_.learn            = learning_.load();
}

void AeqAudioProcessor::timerCallback() {
    // Analysis and solving happen here, never in processBlock, and never only
    // because the editor is open: the timer lives in the processor, so the plugin
    // works with its window closed - which is what "automatic" has to mean.
    // Every tick: drain the audio thread's ring and publish the display spectrum.
    core_.pump();

    // A sealed pass still has to answer when the user moves a control that changes the
    // rule.  Throttled: the solve is the expensive half, and a dragged knob must not turn
    // a 4 Hz job into a 30 Hz one.
    if (needsResolve_.load (std::memory_order_acquire) && core_.committed()
        && (++resolveTick_ % 4 == 0)) {
        needsResolve_.store (false, std::memory_order_release);
        core_.resolveNow();
    }
    else if (!core_.committed()) needsResolve_.store (false, std::memory_order_release);

    if (! learning_.load()) return;

    // Solving is the costly half (FFT statistics, solver, detector, report text) and
    // runs at 30/8 = ~3.75 Hz while learning, then once every 2.5 s.  It must never
    // be in the same tick as the display, or the display inherits its cost.
    const auto st = core_.state();
    const bool settled = st.valid && st.settled;
    if (! settled || (++slowTick_ % 8 == 0)) core_.refresh();
}

AeqAudioProcessor::AutoState AeqAudioProcessor::autoState() const {
    AutoState a;
    const auto st = core_.state();
    a.stage        = core_.committed() ? AutoState::Applied : AutoState::Listening;
    a.heardSeconds = st.analyzedSeconds;
    a.needSeconds  = settings_.settleSeconds > 0.0 ? settings_.settleSeconds : 0.0;
    a.hpfHz        = st.hpfHz;
    a.bands        = core_.appliedBandCount();
    return a;
}

void AeqAudioProcessor::requestReset() {
    resetRequested_.store (true);
    learning_.store (true);      // a reset that leaves the plugin not listening is a reset
                                 // that needs a second, invisible button
    core_.refresh();             // ... and the analysis half is done here, not "soon"
}

// Stop the learn pass: the decision is solved from everything heard so far, applied
// with a 200 ms fade, and then frozen (Tests/test_commit.cpp proves all three).
void AeqAudioProcessor::stopLearning() {
    core_.commit();
    learning_ = false;
}

void AeqAudioProcessor::relearn() {
    core_.relearn();
    learning_ = true;
    // Perform the analysis half of the reset now, on this thread - the same thread the
    // 30 Hz timer runs on, so there is nothing to race with.  Without it the counter the
    // panel reads stayed at "HEARD 21.4 S" until the next refresh, and a refresh straight
    // after a reset publishes nothing (the analyser has no window yet) - so the number
    // could sit there for a whole second, or, in a host that is not processing audio,
    // forever.  The control gate caught it: "and the take it had heard is gone".
    core_.refresh();
}

void AeqAudioProcessor::reopenPass() {
    core_.reopen();
    learning_ = true;          // the analysis has to start counting again, or "open" is
                               // a word on a panel and nothing behind it
}

void AeqAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts_.copyState();
    state.setProperty("version", 2, nullptr);
    // The decision is part of the project.  A track that has learned must not have to
    // learn again because the session was reopened - the user's stop is an answer, and
    // answers are saved.
    const auto st = core_.state();
    state.setProperty("committed", core_.committed(), nullptr);
    state.setProperty("hpfHz", st.hpfHz, nullptr);
    state.setProperty("deEssThr", st.deEssThresholdDb, nullptr);
    state.setProperty("deEssGr", st.deEssMaxGrDb, nullptr);
    state.setProperty("heardSeconds", st.analyzedSeconds, nullptr);
    state.setProperty("f0", st.measuredF0, nullptr);
    juce::ValueTree bands("bands");
    for (const auto& b : st.corrective) {
        juce::ValueTree v("band");
        v.setProperty("f0", b.f0, nullptr);
        v.setProperty("q", b.q, nullptr);
        v.setProperty("gain", b.gainDb, nullptr);
        v.setProperty("type", int(b.type), nullptr);
        bands.appendChild(v, nullptr);
    }
    state.appendChild(bands, nullptr);
    // ... and so are the dynamic layers: the bands that follow the note are part of the
    // decision, not a live guess.  Without them a reopened session would sound different
    // from the one that was saved, which is the one thing a mixer must be able to trust.
    juce::ValueTree layers("layers");
    for (int i = 0; i < core_.trackedCount(); ++i) {
        const auto sp = core_.trackedSpec(i);
        juce::ValueTree v("layer");
        v.setProperty("harmonic", sp.harmonic, nullptr);
        v.setProperty("q", sp.q, nullptr);
        v.setProperty("detectQ", sp.detectQ, nullptr);
        v.setProperty("targetDb", sp.targetDb, nullptr);
        v.setProperty("rangeDown", sp.rangeDown, nullptr);
        v.setProperty("rangeUp", sp.rangeUp, nullptr);
        v.setProperty("glideMs", sp.glideMs, nullptr);
        v.setProperty("deadBandDb", sp.deadBandDb, nullptr);
        layers.appendChild(v, nullptr);
    }
    state.appendChild(layers, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void AeqAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (! tree.isValid()) return;
        apvts_.replaceState(tree);

        if (tree.getProperty("committed")) {
            aeq::RestoreState st;
            st.hpfHz = double(tree.getProperty("hpfHz", 0.0));
            st.deEssThresholdDb = double(tree.getProperty("deEssThr", -30.0));
            st.deEssMaxGrDb = double(tree.getProperty("deEssGr", 0.0));
            st.analyzedSeconds = double(tree.getProperty("heardSeconds", 0.0));
            st.measuredF0 = double(tree.getProperty("f0", 0.0));
            if (auto bands = tree.getChildWithName("bands"); bands.isValid()) {
                for (int i = 0; i < bands.getNumChildren(); ++i) {
                    const auto v = bands.getChild(i);
                    aeq::BandSpec b;
                    b.f0 = double(v.getProperty("f0", 1000.0));
                    b.q = double(v.getProperty("q", 1.0));
                    b.gainDb = double(v.getProperty("gain", 0.0));
                    b.type = aeq::BandType(int(v.getProperty("type", 0)));
                    st.corrective.push_back(b);
                }
            }
            std::vector<aeq::TrackedSpec> layers_;
            if (auto layers = tree.getChildWithName("layers"); layers.isValid()) {
                for (int i = 0; i < layers.getNumChildren(); ++i) {
                    const auto v = layers.getChild(i);
                    aeq::TrackedSpec sp;
                    sp.harmonic = int(v.getProperty("harmonic", 1));
                    sp.q = double(v.getProperty("q", 4.0));
                    sp.detectQ = double(v.getProperty("detectQ", 12.0));
                    sp.targetDb = double(v.getProperty("targetDb", -30.0));
                    sp.rangeDown = double(v.getProperty("rangeDown", 4.0));
                    sp.rangeUp = double(v.getProperty("rangeUp", 0.0));
                    sp.glideMs = double(v.getProperty("glideMs", 25.0));
                    sp.deadBandDb = double(v.getProperty("deadBandDb", 3.0));
                    layers_.push_back(sp);
                }
            }
            core_.restoreDecision(st);
            core_.setTrackedLayers(layers_);     // same order: the decision first, then what it moves
            learning_ = false;      // restored already answered
        } else {
            core_.relearn();
            learning_ = true;
        }
    }
}

juce::AudioProcessorEditor* AeqAudioProcessor::createEditor() {
    return new AeqAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AeqAudioProcessor();
}
