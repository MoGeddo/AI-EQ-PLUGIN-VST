#include "PluginEditor.h"

using namespace juce;
using aeq::ui::Palette;

namespace {

// ---------------------------------------------------------------------------
// Look and feel: flat, quiet, and consistent with the palette in Theme.h.
// ---------------------------------------------------------------------------
class AeqLookAndFeel : public LookAndFeel_V4 {
public:
    AeqLookAndFeel() {
        setColour (Slider::textBoxTextColourId, Palette::text());
        setColour (PopupMenu::backgroundColourId, Palette::panelHi());
        setColour (PopupMenu::textColourId, Palette::text());
        setColour (PopupMenu::highlightedBackgroundColourId, Palette::accent().withAlpha (0.25f));
    }

    void drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle, Slider& s) override {
        const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const float size = jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();
        const float radius = size * 0.5f - 6.0f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        // track
        Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::line());
        g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

        // filled arc from the centre value (so a boost reads as a boost)
        const float zero = startAngle + 0.5f * (endAngle - startAngle);
        Path fill;
        fill.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                            jmin (zero, angle), jmax (zero, angle), true);
        g.setColour (s.getValue() > 0.5f ? Palette::accent() : Palette::accent2());
        g.strokePath (fill, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

        // knob body
        const auto body = Rectangle<float> (centre.x - radius + 7.0f, centre.y - radius + 7.0f,
                                            (radius - 7.0f) * 2.0f, (radius - 7.0f) * 2.0f);
        g.setColour (Palette::panelHi());
        g.fillEllipse (body);
        g.setColour (Palette::line());
        g.drawEllipse (body, 1.0f);

        // pointer
        const auto p1 = centre.getPointOnCircumference (radius - 14.0f, angle);
        const auto p2 = centre.getPointOnCircumference (radius - 4.0f, angle);
        g.setColour (Palette::text());
        g.drawLine ({ p1, p2 }, 2.0f);

        // value, in the middle of the body
        g.setColour (Palette::text());
        g.setFont (aeq::ui::font (11.0f, true));
        const auto text = s.getTextFromValue (s.getValue());
        g.drawText (text, body.toNearestInt().withTrimmedTop (int (body.getHeight()) - 18),
                    Justification::centred, false);
    }

    void drawButtonBackground (Graphics& g, Button& b, const Colour&,
                               bool over, bool down) override {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto fill = on ? Palette::accent().withAlpha (0.18f) : Palette::panelHi();
        if (down) fill = fill.brighter (0.08f);
        else if (over) fill = fill.brighter (0.05f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (on ? Palette::accent() : Palette::line());
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }

    void drawButtonText (Graphics& g, TextButton& b, bool, bool) override {
        g.setColour (b.getToggleState() ? Palette::accent() : Palette::muted());
        g.setFont (aeq::ui::font (12.0f, b.getToggleState()));
        g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred, false);
    }

    void drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int,
                       ComboBox& box) override {
        const auto r = Rectangle<float> (0.5f, 0.5f, float (width) - 1.0f, float (height) - 1.0f);
        g.setColour (Palette::panelHi());
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (Palette::line());
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        g.setColour (Palette::text());
        g.setFont (aeq::ui::font (12.0f));
        g.drawText (box.getText(), box.getLocalBounds().reduced (10, 0).withTrimmedRight (16),
                    Justification::centredLeft, false);
        // caret
        const float cx = float (width) - 14.0f, cy = float (height) * 0.5f;
        Path caret;
        caret.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (Palette::muted());
        g.fillPath (caret);
    }

    Font getComboBoxFont (ComboBox&) override { return aeq::ui::font (12.0f); }
    Font getPopupMenuFont() override { return aeq::ui::font (13.0f); }
};

} // namespace

// ---------------------------------------------------------------------------
AeqAudioProcessorEditor::AeqAudioProcessorEditor (AeqAudioProcessor& p)
    : AudioProcessorEditor (&p), proc_ (p) {
    look_ = std::make_unique<AeqLookAndFeel>();
    setLookAndFeel (look_.get());
    setResizable (true, true);
    setResizeLimits (980, 640, 1900, 1200);
    setSize (1240, 760);

    addAndMakeVisible (curve_);
    addAndMakeVisible (reportView_);
    reportView_.setViewedComponent (&problems_, false);
    reportView_.setScrollBarsShown (true, false);

    curve_.onTrim = [this] (int index, double trimDb) {
        proc_.core().setBandTrim (index, trimDb);
    };
    curve_.onTrimReset = [this] (int index) {
        proc_.core().setBandTrim (index, 0.0);
    };

    buildControls();

    learnBtn_.setClickingTogglesState (true);
    learnBtn_.setToggleState (true, dontSendNotification);
    learnBtn_.onClick = [this] { proc_.setLearning (learnBtn_.getToggleState()); };
    resetBtn_.onClick = [this] {
        proc_.requestReset();
        proc_.core().clearTrims();
        haveData_ = false;
        repaint();
    };

    startTimerHz (12);
}

AeqAudioProcessorEditor::~AeqAudioProcessorEditor() {
    setLookAndFeel (nullptr);
    look_.reset();
}

void AeqAudioProcessorEditor::buildControls() {
    addAndMakeVisible (learnBtn_);
    addAndMakeVisible (resetBtn_);
    addAndMakeVisible (bypassBtn_);
    bypassBtn_.setClickingTogglesState (true);
    aBypass_ = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
        proc_.apvts(), "bypass", bypassBtn_);

    addAndMakeVisible (mode_);
    mode_.addItemList ({ "Restore — يصحّح المشاكل", "Match — يمشي على المعيار" }, 1);
    aMode_ = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc_.apvts(), "mode", mode_);

    struct K { const char* id; const char* name; const char* suffix; };
    const K specs[] = {
        { "amount",              "المعيار",     "%" },
        { "hpf_hz",              "القصّ تحت",   " Hz" },
        { "deess",               "De-ess",      " dB" },
        { "axis_warm_presence",  "دفء ↔ حضور",  "" },
        { "axis_body_air",       "جسم ↔ هواء",  "" },
        { "air_db",              "Air",         " dB" },
        { "settle_s",            "تحليل أولاً", " s" },
    };
    for (const auto& k : specs) {
        auto* s = knobs_.add (new Slider());
        s->setSliderStyle (Slider::RotaryVerticalDrag);
        s->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s->setColour (Slider::textBoxTextColourId, Palette::text());
        s->setRotaryParameters (MathConstants<float>::pi * 1.15f,
                                MathConstants<float>::pi * 2.85f, true);
        s->setTextValueSuffix (String::fromUTF8 (k.suffix));
        if (String (k.id) == "amount")
            s->setNumDecimalPlacesToDisplay (0);
        addAndMakeVisible (s);
        knobAttachments_.add (new AudioProcessorValueTreeState::SliderAttachment (
            proc_.apvts(), k.id, *s));

        auto* l = knobNames_.add (new Label());
        l->setText (String::fromUTF8 (k.name), dontSendNotification);
        l->setJustificationType (Justification::centred);
        l->setColour (Label::textColourId, Palette::muted());
        l->setFont (aeq::ui::font (11.5f));
        l->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    }
}

// ---------------------------------------------------------------------------
void AeqAudioProcessorEditor::paint (Graphics& g) {
    // background: a very soft vertical wash, no busy texture
    g.setGradientFill (ColourGradient (Colour::fromRGB (0x10, 0x14, 0x1b), 0.0f, 0.0f,
                                       Palette::bg(), 0.0f, float (getHeight()), false));
    g.fillAll();

    auto r = getLocalBounds();
    auto header = r.removeFromTop (64);
    auto transport = r.removeFromBottom (132);

    drawHeader (g, header);
    drawTransportFrame (g, transport);
    drawVerdict (g, {});
}

void AeqAudioProcessorEditor::drawHeader (Graphics& g, Rectangle<int> r) {
    auto area = r.reduced (18, 0);

    // mark: a rounded tile with the product's accent, then the wordmark
    auto mark = area.removeFromLeft (34).withSizeKeepingCentre (34, 34).toFloat();
    g.setGradientFill (ColourGradient (Palette::accent(), mark.getX(), mark.getY(),
                                       Palette::accent2(), mark.getRight(), mark.getBottom(), false));
    g.fillRoundedRectangle (mark, 9.0f);
    g.setColour (Palette::bg());
    g.setFont (aeq::ui::font (15.0f, true));
    g.drawText ("EQ", mark.toNearestInt(), Justification::centred, false);

    area.removeFromLeft (12);
    auto title = area.removeFromLeft (200);
    aeq::ui::label (g, "AEQ RESTORE", title.removeFromTop (22), Palette::text(), 15.5f,
                    Justification::centredLeft, true);
    aeq::ui::label (g, "أول EQ في السلسلة — المعيار: 15 ميكس لأمير محروس", title,
                    Palette::muted(), 11.0f);

    // right side: live numbers as chips, then the bypass pill
    auto right = area.removeFromRight (330);
    bypassBtn_.setBounds (right.removeFromRight (86).withSizeKeepingCentre (86, 28));
    right.removeFromRight (10);

    juce::String f0 = haveData_ ? (String (int (state_.measuredF0)) + " Hz") : "—";
    juce::String secs = String (state_.analyzedSeconds, 0) + " s";
    juce::String bands = String (int (bands_.size())) + " bands";
    struct C { juce::String text; Colour c; int w; };
    const std::vector<C> chips = {
        { "F0 " + f0, Palette::accent2(), 92 },
        { "سمع " + secs, Palette::muted(), 84 },
        { bands, Palette::accent(), 76 },
    };
    for (const auto& c : chips) {
        auto box = right.removeFromRight (c.w).withSizeKeepingCentre (c.w, 24);
        aeq::ui::chip (g, c.text, box.toFloat(), c.c, true);
        right.removeFromRight (6);
    }
}

void AeqAudioProcessorEditor::drawTransportFrame (Graphics& g, Rectangle<int> r) {
    auto area = r.reduced (18, 6).toFloat();
    aeq::ui::card (g, area, Palette::panel().withAlpha (0.75f), Palette::line(), 14.0f);
}

// The verdict card sits in the report column; it is painted, not a component,
// because it is one sentence and three numbers.
void AeqAudioProcessorEditor::drawVerdict (Graphics& g, Rectangle<int>) {
    if (! verdictBounds_.isEmpty()) {
        auto r = verdictBounds_.toFloat();
        const auto c = verdictColour();
        aeq::ui::card (g, r, c.withAlpha (state_.settled ? 0.10f : 0.06f), c.withAlpha (0.35f), 12.0f);

        auto inner = r.reduced (14.0f, 12.0f);
        aeq::ui::label (g, verdictTitle(), inner.removeFromTop (26).toNearestInt(),
                        c, 16.0f, Justification::topRight, true);

        // three numbers: what it heard, how far it is, what it will do
        auto row = inner.removeFromTop (22);
        const double far = std::max (state_.distBeforeDb, 0.0);
        const juce::String nums = haveData_
            ? ("انحراف " + String (far, 2) + " dB RMS   ·   "
               + String (int (bands_.size())) + " باند   ·   "
               + "σ " + String (peakZ_, 2))
            : juce::String ("—");
        aeq::ui::label (g, nums, row.toNearestInt(), Palette::muted(), 11.5f,
                        Justification::topRight);

        if (! state_.settled && haveData_) {
            // a progress rail: how much of the take has been heard
            auto barArea = inner.removeFromTop (6).reduced (0.0f, 0.0f);
            const float w = barArea.getWidth();
            g.setColour (Palette::line());
            g.fillRoundedRectangle (barArea, 3.0f);
            g.setColour (c);
            g.fillRoundedRectangle (barArea.withWidth (w * juce::jlimit (0.02f, 1.0f, float (proc_.core().learnProgress()))), 3.0f);
        }
    }
}

juce::String AeqAudioProcessorEditor::verdictTitle() const {
    if (! haveData_) return "بيحلّل… شغّل الأغنية";
    if (! state_.settled)
        return "لسه بيسمع (" + String (state_.analyzedSeconds, 0) + " ثانية)";
    int actions = 0;
    for (const auto& p : state_.problems)
        if (p.action != aeq::Action::None && p.action != aeq::Action::WarnOnly) ++actions;
    if (actions == 0) return "التسجيل داخل المعيار — مفيش تدخّل";
    return String (actions) + " تدخّل مقترح";
}

juce::Colour AeqAudioProcessorEditor::verdictColour() const {
    if (! haveData_ || ! state_.settled) return Palette::accent2();
    int actions = 0;
    for (const auto& p : state_.problems)
        if (p.action != aeq::Action::None && p.action != aeq::Action::WarnOnly) ++actions;
    return actions == 0 ? Palette::ok() : Palette::warn();
}

// ---------------------------------------------------------------------------
void AeqAudioProcessorEditor::resized() {
    auto r = getLocalBounds();
    auto header = r.removeFromTop (64);
    auto transport = r.removeFromBottom (132);
    auto body = r.reduced (18, 8);

    layoutHeader (header);
    layoutTransport (transport);

    // report column on the right (the reading-start side for this report)
    const int reportW = jlimit (300, 420, body.getWidth() / 3);
    auto rightCol = body.removeFromRight (reportW);
    body.removeFromRight (12);

    verdictBounds_ = rightCol.removeFromTop (86);
    rightCol.removeFromTop (10);
    layoutReport (rightCol);
    layoutCurve (body);
}

void AeqAudioProcessorEditor::layoutHeader (Rectangle<int> r) {
    // the header's interactive bits are positioned while painting the chips
    juce::ignoreUnused (r);
}

void AeqAudioProcessorEditor::layoutCurve (Rectangle<int> r) {
    curve_.setBounds (r);
}

void AeqAudioProcessorEditor::layoutReport (Rectangle<int> r) {
    reportView_.setBounds (r);
    problems_.setSize (r.getWidth() - 10, problems_.contentHeight());
}

void AeqAudioProcessorEditor::layoutTransport (Rectangle<int> r) {
    auto area = r.reduced (18, 6);
    auto inner = area.reduced (16, 12);

    auto top = inner.removeFromTop (26);
    learnBtn_.setBounds (top.removeFromLeft (118));
    top.removeFromLeft (8);
    resetBtn_.setBounds (top.removeFromLeft (74));
    top.removeFromLeft (12);
    mode_.setBounds (top.removeFromLeft (240));

    inner.removeFromTop (6);
    const int n = knobs_.size();
    if (n <= 0) return;
    const int w = inner.getWidth() / n;
    for (int i = 0; i < n; ++i) {
        auto cell = inner.removeFromLeft (w);
        auto nameSlot = cell.removeFromBottom (16);
        knobNames_[i]->setBounds (nameSlot.withSizeKeepingCentre (jmin (w, 110), 16));
        knobs_[i]->setBounds (cell.withSizeKeepingCentre (jmin (w, 84), jmin (cell.getHeight(), 84)));
    }
}

// ---------------------------------------------------------------------------
void AeqAudioProcessorEditor::timerCallback() {
    state_ = proc_.core().state();
    meas_ = proc_.core().measurements();
    bands_ = proc_.core().activeBands();
    haveData_ = state_.valid;

    trims_.clear();
    for (int i = 0; i < int (bands_.size()); ++i) trims_.push_back (proc_.core().bandTrim (i));

    // the sigma readout: where this take sits against the 15 reference mixes after
    // matching the filter to the width of a real fault (see docs/14 section 9)
    peakZ_ = 0.0;
    peakZf_ = 0.0;
    if (haveData_ && ! meas_.freqs.empty()) {
        const auto z = aeq::scanZ (meas_, 3);
        if (z.peakBand >= 0) {
            peakZ_ = z.peakZ;
            peakZf_ = meas_.freqs[size_t (z.peakBand)];
        }
    }

    aeq::ui::CurveView::Data d;
    d.meas = meas_;
    d.bands = bands_;
    d.problems = state_.problems;
    d.trims = trims_;
    d.valid = haveData_;
    d.settled = state_.settled;
    d.analysedSeconds = state_.analyzedSeconds;
    curve_.setData (std::move (d));

    problems_.setProblems (state_.problems);
    problems_.setSize (reportView_.getWidth() - 10, problems_.contentHeight());

    repaint();
}
