#include "PluginEditor.h"

using namespace juce;

static double logFreq(double norm, double fmin = 40.0, double fmax = 16000.0) {
    return fmin * std::pow(fmax / fmin, norm);
}

AeqAudioProcessorEditor::AeqAudioProcessorEditor(AeqAudioProcessor& p)
    : AudioProcessorEditor(&p), proc_(p) {
    setSize(880, 560);
    auto& s = proc_.apvts();

    addAndMakeVisible(learnBtn_);
    addAndMakeVisible(resetBtn_);
    learnBtn_.setClickingTogglesState(true);
    learnBtn_.setToggleState(true, juce::dontSendNotification);
    learnBtn_.onClick = [this] { proc_.setLearning(learnBtn_.getToggleState()); };
    resetBtn_.onClick = [this] { proc_.requestReset(); haveData_ = false; repaint(); };

    struct K { Slider& sl; const char* id; const char* name; };
    const K knobs[] = {
        {amount_, "amount", "Standard %"},
        {hpf_,    "hpf_hz", "Low cut"},
        {deEss_,  "deess",  "De-ess"},
        {axis1_,  "axis_warm_presence", "Warm / Presence"},
        {axis2_,  "axis_body_air",      "Body / Air"},
        {air_,    "air_db",             "Air trim"},
    };
    for (auto& k : knobs) {
        addAndMakeVisible(k.sl);
        k.sl.setSliderStyle(Slider::LinearHorizontal);
        k.sl.setTextBoxStyle(Slider::TextBoxRight, false, 66, 18);
        k.sl.setName(k.name);
        k.sl.setTooltip(k.name);
    }
    aAmount_ = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(s, "amount", amount_);
    aHpf_    = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(s, "hpf_hz", hpf_);
    aDeEss_  = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(s, "deess", deEss_);
    aA1_     = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(s, "axis_warm_presence", axis1_);
    aA2_     = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(s, "axis_body_air", axis2_);
    aAir_    = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(s, "air_db", air_);

    addAndMakeVisible(mode_);
    mode_.addItemList({"Restore (fix problems)", "Match (corpus tone)"}, 1);
    aMode_ = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(s, "mode", mode_);

    addAndMakeVisible(bypass_);
    aBypass_ = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(s, "bypass", bypass_);

    addAndMakeVisible(status_);
    status_.setFont(FontOptions(12.0f));
    startTimerHz(8);
}

void AeqAudioProcessorEditor::resized() {
    auto r = getLocalBounds().reduced(10);
    auto bottom = r.removeFromBottom(150);
    auto row = bottom.removeFromTop(28);
    learnBtn_.setBounds(row.removeFromLeft(90));
    resetBtn_.setBounds(row.removeFromLeft(80));
    row.removeFromLeft(10);
    mode_.setBounds(row.removeFromLeft(200));
    bypass_.setBounds(row.removeFromLeft(80));

    auto grid = bottom.reduced(0, 6);
    const int ch = grid.getHeight() / 3;
    auto l1 = grid.removeFromTop(ch), l2 = grid.removeFromTop(ch), l3 = grid;
    amount_.setBounds(l1.removeFromLeft(l1.getWidth() / 3).reduced(2));
    hpf_.setBounds(l1.removeFromLeft(l1.getWidth() / 2).reduced(2));
    deEss_.setBounds(l1.reduced(2));
    axis1_.setBounds(l2.removeFromLeft(l2.getWidth() / 2).reduced(2));
    axis2_.setBounds(l2.reduced(2));
    air_.setBounds(l3.removeFromLeft(l3.getWidth() / 2).reduced(2));
    status_.setBounds(l3.reduced(2));
}

void AeqAudioProcessorEditor::timerCallback() {
    lastState_ = proc_.core().state();
    lastMeas_ = proc_.core().measurements();
    haveData_ = lastState_.valid;
    if (haveData_) {
        String s;
        s << "F0 " << (int)lastState_.measuredF0 << " Hz   |   low cut "
          << (int)lastState_.hpfHz << " Hz   |   " << lastState_.corrective.size()
          << " bands   |   problems " << lastState_.problems.size()
          << "   |   fixed " << String(lastState_.fixBeforeDb - lastState_.fixAfterDb, 2) << " dB RMS";
        status_.setText(s, juce::dontSendNotification);
    } else {
        status_.setText("Play the take from the beginning - I learn as it plays", juce::dontSendNotification);
    }
    repaint();
}

void AeqAudioProcessorEditor::drawCurves(Graphics& g, Rectangle<int> area) {
    g.setColour(Colour(0xff14161a));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
    auto plot = area.reduced(8);

    // grid
    g.setColour(Colour(0xff2a2f38));
    const double ticks[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
    for (double f : ticks) {
        const double t = std::log(f / 40.0) / std::log(16000.0 / 40.0);
        const float x = float(plot.getX() + t * plot.getWidth());
        g.drawVerticalLine(int(x), float(plot.getY()), float(plot.getBottom()));
    }
    for (int db = -24; db <= 12; db += 6) {
        const float y = float(plot.getBottom() - (db + 24) / 36.0 * plot.getHeight());
        g.drawHorizontalLine(int(y), float(plot.getX()), float(plot.getRight()));
    }

    auto toY = [&](double db) {
        return float(plot.getBottom() - (db + 24.0) / 36.0 * plot.getHeight());
    };
    auto toX = [&](double f) {
        return float(plot.getX() + std::log(f / 40.0) / std::log(16000.0 / 40.0) * plot.getWidth());
    };

    if (!haveData_ || lastMeas_.freqs.empty()) {
        g.setColour(Colours::grey);
        g.drawText("no measurement yet", plot, Justification::centred);
        return;
    }

    // target
    Path target;
    bool first = true;
    for (int i = 0; i < 400; ++i) {
        const double f = logFreq(i / 399.0, 100.0, 14000.0);
        const double db = aeq::TargetCurve::atSmoothed(f, 0.5) - 6.0;   // visual offset
        if (first) { target.startNewSubPath(toX(f), toY(db)); first = false; }
        else target.lineTo(toX(f), toY(db));
    }
    g.setColour(Colour(0xffc0392b));
    g.strokePath(target, PathStrokeType(1.6f));

    // measured
    Path meas;
    first = true;
    double ref = 0.0;
    for (size_t i = 0; i < lastMeas_.freqs.size(); ++i)
        if (lastMeas_.freqs[i] >= 900 && lastMeas_.freqs[i] <= 1100) ref = lastMeas_.bandDb[i];
    for (size_t i = 0; i < lastMeas_.freqs.size(); ++i) {
        const double f = lastMeas_.freqs[i];
        if (f < 40 || f > 16000) continue;
        const double db = lastMeas_.bandDb[i] - ref - 6.0;
        if (first) { meas.startNewSubPath(toX(f), toY(db)); first = false; }
        else meas.lineTo(toX(f), toY(db));
    }
    g.setColour(Colour(0xff5dade2));
    g.strokePath(meas, PathStrokeType(1.4f));

    // the correction actually applied
    Path corr;
    first = true;
    const auto bands = proc_.core().activeBands();
    for (int i = 0; i < 400; ++i) {
        const double f = logFreq(i / 399.0, 40.0, 16000.0);
        double db = 0.0;
        for (const auto& b : bands) db += aeq::bandMagDb(b, f, 48000.0);
        if (first) { corr.startNewSubPath(toX(f), toY(db)); first = false; }
        else corr.lineTo(toX(f), toY(db));
    }
    g.setColour(Colour(0xfff1c40f));
    g.strokePath(corr, PathStrokeType(1.2f));

    g.setColour(Colours::grey);
    g.setFont(FontOptions(10.0f));
    g.drawText("target", plot.getRight() - 150, plot.getY(), 60, 12, Justification::centredRight);
}

void AeqAudioProcessorEditor::paint(Graphics& g) {
    g.fillAll(Colour(0xff1b1e24));
    auto r = getLocalBounds().reduced(10);
    auto bottom = r.removeFromBottom(150);
    drawCurves(g, r.removeFromTop(r.getHeight() - 110));

    // problem list
    auto list = r.removeFromTop(std::min(104, r.getHeight()));
    g.setColour(Colour(0xff20242b));
    g.fillRoundedRectangle(list.toFloat(), 6.0f);
    auto text = list.reduced(10, 6);
    g.setColour(Colours::white);
    g.setFont(FontOptions(12.0f));
    if (lastState_.problems.empty()) {
        g.setColour(Colour(0xff7dcea0));
        g.drawText(hasData_ ? "No problems found - this take is already clean. Leaving it alone."
                            : "Waiting for audio...",
                   text, Justification::topLeft);
    } else {
        int y = text.getY();
        for (const auto& p : lastState_.problems) {
            g.setColour(p.action == aeq::Action::WarnOnly ? Colour(0xfff39c12) : Colour(0xff5dade2));
            g.drawText(juce::String::fromUTF8(p.labelAr.c_str()), text.getX(), y, 150, 18, Justification::topLeft);
            g.setColour(Colours::lightgrey);
            g.drawText(juce::String::fromUTF8(p.detailAr.c_str()), text.getX() + 152, y, text.getWidth() - 152, 18,
                       Justification::topLeft);
            y += 19;
            if (y > text.getBottom() - 14) break;
        }
    }
    juce::ignoreUnused(bottom);
}
