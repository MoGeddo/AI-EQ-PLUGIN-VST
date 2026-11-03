#include "PluginEditor.h"

using namespace juce;
using aeq::ui::Palette;
using aeq::ui::Icon;

namespace {

// ---------------------------------------------------------------------------
// Look and feel.
//
// The knob is the piece of this UI a mixer looks at for hours, so it is drawn as
// an object rather than an arc with a number dumped in the middle:
//
//   ticks    outside the track, 0.25 alpha, they say "this is a control"
//   track    a 3 px groove from -135 deg to +135 deg
//   value    mint, from the centre detent to the current position, so a cut reads
//            as a cut and not as a shorter arc
//   body     a filled disc with a subtle vertical gradient and a hairline edge
//   pointer  a 2 px line from the body edge to the track, never a dot in the centre
//
// The value itself is NOT drawn here.  It lives in the cell below the knob, which
// is why "0.0" can no longer land on top of the Arabic label - the failure that
// made the previous build look unreadable.
// ---------------------------------------------------------------------------
class AeqLookAndFeel : public LookAndFeel_V4 {
public:
    AeqLookAndFeel() {
        setColour (Slider::textBoxTextColourId, Palette::text());
        setColour (PopupMenu::backgroundColourId, Palette::surfaceHi());
        setColour (PopupMenu::textColourId, Palette::text());
        setColour (PopupMenu::highlightedBackgroundColourId, Palette::accent().withAlpha (0.18f));
        setColour (ComboBox::textColourId, Palette::text());
    }

    void drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle, Slider& s) override {
        const auto cell = Rectangle<int> (x, y, width, height).toFloat();
        const auto bounds = cell.reduced (7.0f);
        const float size = jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = cell.getCentre();
        const float rTrack = size * 0.5f - 1.0f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float mid = (startAngle + endAngle) * 0.5f;
        const bool detached = std::abs (pos - 0.5f) > 0.002f;

        // ticks: a quiet dial scale, twelve marks
        for (int i = 0; i <= 10; ++i) {
            const float a = startAngle + (endAngle - startAngle) * (float (i) / 10.0f);
            const auto p1 = centre.getPointOnCircumference (rTrack + 3.0f, a);
            const auto p2 = centre.getPointOnCircumference (rTrack + (i % 5 == 0 ? 6.5f : 5.0f), a);
            g.setColour (Palette::hairline().brighter (i % 5 == 0 ? 0.25f : 0.0f));
            g.drawLine ({ p1, p2 }, 1.2f);
        }

        // track
        Path track;
        track.addCentredArc (centre.x, centre.y, rTrack, rTrack, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::surfaceHi());
        g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

        // value, measured from the detent
        if (detached) {
            Path fill;
            fill.addCentredArc (centre.x, centre.y, rTrack, rTrack, 0.0f,
                                jmin (mid, angle), jmax (mid, angle), true);
            g.setColour (s.getValue() > 0.5f ? Palette::accent() : Palette::take());
            g.strokePath (fill, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // body
        const float rBody = rTrack - 8.0f;
        const auto body = Rectangle<float> (rBody * 2.0f, rBody * 2.0f).withCentre (centre);
        g.setGradientFill (ColourGradient (Palette::surfaceHi(), centre.x, body.getY(),
                                           Palette::surface(), centre.x, body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (Palette::hairline());
        g.drawEllipse (body, 1.0f);

        // pointer
        const auto tip = centre.getPointOnCircumference (rTrack - 2.5f, angle);
        const auto root = centre.getPointOnCircumference (rBody * 0.35f, angle);
        g.setColour (detached ? Palette::text() : Palette::muted());
        g.drawLine ({ root, tip }, 2.0f);
    }

    void drawButtonBackground (Graphics& g, Button& b, const Colour&,
                               bool over, bool down) override {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto fill = on ? Palette::accent().withAlpha (0.14f) : Palette::surfaceHi();
        if (down) fill = fill.brighter (0.07f);
        else if (over) fill = fill.brighter (0.04f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (on ? Palette::accent().withAlpha (0.75f) : Palette::hairline());
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
    }

    void drawButtonText (Graphics& g, TextButton& b, bool, bool) override {
        const bool on = b.getToggleState();
        auto textArea = b.getLocalBounds().reduced (10, 0);
        // room for the icon on the left when the button has one
        const int iconW = b.getProperties().contains ("icon") ? 20 : 0;
        if (iconW > 0) {
            auto ib = textArea.removeFromLeft (iconW).withSizeKeepingCentre (14, 14).toFloat();
            aeq::ui::icon (g, (Icon) int (b.getProperties()["icon"]), ib,
                           on ? Palette::accent() : Palette::muted(), 1.5f);
        }
        aeq::ui::draw (g, b.getButtonText(), textArea.toFloat(), Justification::centred, 12.0f,
                       on ? Palette::accent() : Palette::muted(),
                       aeq::ui::Face::SemiBold);
    }

    void drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int,
                       ComboBox& box) override {
        const auto r = Rectangle<float> (0.5f, 0.5f, float (width) - 1.0f, float (height) - 1.0f);
        g.setColour (Palette::surfaceHi());
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (box.isMouseOver() ? Palette::hairline().brighter (0.3f) : Palette::hairline());
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        aeq::ui::draw (g, box.getText(), box.getLocalBounds().reduced (12, 0).withTrimmedRight (18).toFloat(),
                       Justification::centredLeft, 12.0f, Palette::text());
        aeq::ui::icon (g, Icon::ChevronDown,
                       Rectangle<float> (float (width) - 24.0f, (height - 14.0f) * 0.5f, 14.0f, 14.0f),
                       Palette::muted());
    }

    Font getComboBoxFont (ComboBox&) override { return aeq::ui::font (12.0f); }
    Font getPopupMenuFont() override { return aeq::ui::font (13.0f); }
    Font getLabelFont (Label&) override { return aeq::ui::font (11.0f); }
};

constexpr int kTitleH = 60;
constexpr int kDockH  = 190;
constexpr int kReportW = 344;

} // namespace

// ---------------------------------------------------------------------------
AeqAudioProcessorEditor::AeqAudioProcessorEditor (AeqAudioProcessor& p)
    : AudioProcessorEditor (&p), proc_ (p) {
    look_ = std::make_unique<AeqLookAndFeel>();
    setLookAndFeel (look_.get());

    setResizable (true, true);
    setResizeLimits (1024, 700, 1800, 1200);
    setSize (1240, 800);

    addAndMakeVisible (curve_);
    addAndMakeVisible (reportView_);
    reportView_.setViewedComponent (&problems_, false);
    reportView_.setScrollBarsShown (true, false);
    reportView_.setScrollBarThickness (7);

    curve_.onTrim = [this] (int index, double trimDb) { proc_.core().setBandTrim (index, trimDb); };
    curve_.onTrimReset = [this] (int index) { proc_.core().setBandTrim (index, 0.0); };

    buildControls();

    learnBtn_.setClickingTogglesState (true);
    learnBtn_.setToggleState (true, dontSendNotification);
    learnBtn_.getProperties().set ("icon", int (Icon::Wave));
    learnBtn_.onClick = [this] { proc_.setLearning (learnBtn_.getToggleState()); };

    resetBtn_.getProperties().set ("icon", int (Icon::Reset));
    resetBtn_.onClick = [this] {
        proc_.requestReset();
        proc_.core().clearTrims();
        haveData_ = false;
        repaint();
    };

    bypassBtn_.getProperties().set ("icon", int (Icon::Power));

    startTimerHz (12);
}

AeqAudioProcessorEditor::~AeqAudioProcessorEditor() {
    stopTimer();
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
    // fromUTF8, not the char* constructor: see ui::u8 in ArabicText.h
    mode_.addItemList ({ aeq::ui::u8 ("Restore — يصحّح المشاكل"),
                         aeq::ui::u8 ("Match — يمشي على المعيار") }, 1);
    aMode_ = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc_.apvts(), "mode", mode_);

    struct K { const char* id; const char* name; int decimals; };
    const K specs[] = {
        { "amount",             "المعيار",     0 },
        { "hpf_hz",             "القصّ تحت",   0 },
        { "deess",              "De-ess",      1 },
        { "axis_warm_presence", "دفء ↔ حضور",  1 },
        { "axis_body_air",      "جسم ↔ هواء",  1 },
        { "air_db",             "Air",         1 },
        { "settle_s",           "زمن السماع",  0 },
    };

    for (const auto& k : specs) {
        auto* s = knobs_.add (new Slider());
        s->setSliderStyle (Slider::RotaryVerticalDrag);
        s->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters (MathConstants<float>::pi * 1.25f,
                                MathConstants<float>::pi * 2.75f, true);
        s->setNumDecimalPlacesToDisplay (k.decimals);
        if (auto* param = proc_.apvts().getParameter (k.id))
            s->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        s->setColour (Slider::textBoxTextColourId, Palette::text());
        addAndMakeVisible (s);
        knobAttachments_.add (new AudioProcessorValueTreeState::SliderAttachment (
            proc_.apvts(), k.id, *s));

        auto* l = knobNames_.add (new Label());
        l->setText (String::fromUTF8 (k.name), dontSendNotification);
        l->setJustificationType (Justification::centred);
        l->setColour (Label::textColourId, Palette::muted());
        l->setFont (aeq::ui::font (10.5f));
        l->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    }
}

// ---------------------------------------------------------------------------
void AeqAudioProcessorEditor::paint (Graphics& g) {
    g.setColour (Palette::bg());
    g.fillAll();

    // a single very soft light from the top-left; enough to stop the field reading
    // as a flat rectangle, not enough to notice
    g.setGradientFill (ColourGradient (Colour::fromRGB (0x16, 0x1b, 0x22), 0.0f, 0.0f,
                                       Palette::bg(), float (getWidth()) * 0.9f,
                                       float (getHeight()), false));
    g.fillAll();

    auto r = getLocalBounds();
    drawTitleBar (g, r.removeFromTop (kTitleH));
    drawDockFrame (g, r.removeFromBottom (kDockH));

    if (! verdictBounds_.isEmpty()) drawVerdict (g, verdictBounds_);

    // Each knob's number, on its own baseline under the knob.  This is drawn here
    // rather than by a Label so the face, the weight and the cell are the same as
    // every other number in the window.
    for (int i = 0; i < knobs_.size() && i < int (knobValueRows_.size()); ++i) {
        auto r2 = knobValueRows_[size_t (i)];
        aeq::ui::value (g, knobs_[i]->getTextFromValue (knobs_[i]->getValue()), r2,
                        i == 0 ? Palette::accent() : Palette::text(), 12.5f);
    }

    // the report column header, above the cards
    if (! verdictBounds_.isEmpty()) {
        auto head = verdictBounds_.withY (verdictBounds_.getBottom() + 14).withHeight (16);
        aeq::ui::caption (g, "the report", head.reduced (2, 0), Palette::dim(),
                          Justification::centredLeft);
        aeq::ui::label (g, "التقرير", head.reduced (2, 0), Palette::muted(), 10.5f,
                        Justification::centredRight);
    }
    juce::ignoreUnused (curveBounds_);
}

void AeqAudioProcessorEditor::drawTitleBar (Graphics& g, Rectangle<int> r) {
    auto area = r.reduced (18, 0);

    // mark: a tile with a drawn waveform, not the two letters "EQ"
    auto mark = area.removeFromLeft (32).withSizeKeepingCentre (32, 32).toFloat();
    g.setGradientFill (ColourGradient (Palette::accent(), mark.getX(), mark.getY(),
                                       Palette::accent().darker (0.45f), mark.getRight(),
                                       mark.getBottom(), false));
    g.fillRoundedRectangle (mark, 9.0f);
    aeq::ui::icon (g, Icon::Wave, mark.reduced (5.0f), Palette::bg(), 1.8f);

    area.removeFromLeft (12);
    auto title = area.removeFromLeft (250);
    aeq::ui::label (g, "AEQ Restore", title.removeFromTop (21), Palette::text(), 15.5f,
                    Justification::centredLeft, true);
    aeq::ui::label (g, "أول EQ في السلسلة · المعيار: ١٥ ميكس لأمير محروس", title,
                    Palette::muted(), 11.0f);

    // right: live readouts as quiet chips, then the bypass switch (the switch's
    // bounds are set in resized(); painting must not move components)
    auto right = area;
    right.removeFromRight (96 + 10);

    const String f0 = haveData_ ? String (int (state_.measuredF0)) + " Hz" : aeq::ui::u8 ("—");
    const String secs = String (state_.analyzedSeconds, 0) + " s";
    const String bandCount = String (int (bands_.size()));
    struct C { String text; Colour c; int w; };
    const std::vector<C> chips = {
        { aeq::ui::u8 ("F0  ") + f0,              Palette::take(),   100 },
        { aeq::ui::u8 ("سمع  ") + secs,           Palette::muted(),   92 },
        { aeq::ui::u8 ("باند  ") + bandCount,     Palette::accent(),  96 },
    };
    for (const auto& c : chips) {
        auto box = right.removeFromRight (c.w).withSizeKeepingCentre (c.w, 24);
        aeq::ui::chip (g, c.text, box.toFloat(), c.c, true);
        right.removeFromRight (7);
    }

    aeq::ui::hairline (g, 18.0f, float (r.getBottom()) - 1.0f, float (getWidth()) - 18.0f);
}

void AeqAudioProcessorEditor::drawDockFrame (Graphics& g, Rectangle<int> r) {
    auto area = r.reduced (18, 10).toFloat();
    aeq::ui::card (g, area, Palette::surface(), Palette::hairline(), 12.0f);
    aeq::ui::hairline (g, area.getX() + 16.0f, area.getY() + 46.0f,
                       area.getRight() - 16.0f, Palette::hairline().brighter (0.1f));
}

void AeqAudioProcessorEditor::drawVerdict (Graphics& g, Rectangle<int> bounds) {
    auto r = bounds.toFloat();
    const auto c = verdictColour();

    aeq::ui::card (g, r, Palette::surface(), c.withAlpha (0.28f), 12.0f);
    // a 3 px colour strip on the leading edge: the state is legible from across
    // the room without reading a word
    g.setColour (c);
    g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 6.0f), 1.5f);

    auto inner = r.reduced (16.0f, 12.0f);
    inner.removeFromLeft (4.0f);

    // the sentence
    auto titleRow = inner.removeFromTop (30.0f);
    aeq::ui::label (g, verdictTitle(), titleRow.toNearestInt(), c, 17.0f,
                    Justification::centredRight, true);

    // the explanation, wrapped by the layout engine rather than clipped
    auto sub = inner.removeFromTop (40.0f);
    aeq::ui::drawParagraph (g, verdictSubtitle(), sub, Justification::topRight, 11.5f,
                            Palette::muted());

    inner.removeFromTop (4.0f);

    // the numbers: three cells, label under value, equal thirds
    auto row = inner.removeFromTop (34.0f);
    const float w = row.getWidth() / 3.0f;
    struct M { String v; String k; };
    const bool has = haveData_;
    const auto dash = aeq::ui::u8 ("—");
    const std::vector<M> metrics = {
        { has ? String (jmax (0.0, state_.distBeforeDb), 2) + " dB" : dash, aeq::ui::u8 ("الانحراف") },
        { has ? String (int (bands_.size())) : dash,                        aeq::ui::u8 ("باند") },
        { has ? String (peakZ_, 2) + aeq::ui::u8 ("σ") : dash,              aeq::ui::u8 ("أقصى انحراف") },
    };
    for (int i = 0; i < 3; ++i) {
        auto cell = row.removeFromLeft (w);
        aeq::ui::value (g, metrics[size_t (i)].v, cell.removeFromTop (18).toNearestInt(),
                        Palette::text(), 13.0f, Justification::centredRight);
        aeq::ui::label (g, metrics[size_t (i)].k, cell.toNearestInt(), Palette::dim(), 10.0f,
                        Justification::centredRight);
    }

    // the learning progress, only while there is something to wait for
    if (haveData_ && ! state_.settled) {
        auto bar = inner.removeFromTop (5.0f);
        g.setColour (Palette::surfaceHi());
        g.fillRoundedRectangle (bar, 2.5f);
        g.setColour (c);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.02f, 1.0f,
                                     float (proc_.core().learnProgress()))), 2.5f);
    }
}

String AeqAudioProcessorEditor::verdictTitle() const {
    if (! haveData_) return aeq::ui::u8 ("بيحلّل — شغّل الأغنية من أولها");
    if (! state_.settled)
        return aeq::ui::u8 ("لسه بيسمع (") + String (state_.analyzedSeconds, 0)
               + aeq::ui::u8 (" ثانية)");
    const int actions = actionCount();
    if (actions == 0) return aeq::ui::u8 ("التسجيل داخل المعيار — مفيش تدخّل");
    return String (actions) + aeq::ui::u8 (" تدخّل مقترح على التسجيل");
}

String AeqAudioProcessorEditor::verdictSubtitle() const {
    if (! haveData_)
        return aeq::ui::u8 ("مفيش قياس لسه. أول ما الصوت يعدّي، الباندات بتتظبط على مقاس "
                            "المشكلة اللي بتتسمع فعلًا، ومش بنلمس حاجة غيرها.");
    if (! state_.settled)
        return aeq::ui::u8 ("بنقيس كل ثانية بتعدّي ونقارنها بالميكسات الـ١٥. الحكم النهائي "
                            "مش بينزل غير ما التحليل يستقر.");
    const int actions = actionCount();
    if (actions == 0)
        return aeq::ui::u8 ("كل منطقة داخل مدى المهندس. أي حركة هاندل هنا هتكون تقليل "
                            "للجودة، فالاقتراح الوحيد هو إنك تسيبه.");
    return aeq::ui::u8 ("الأرقام اللي تحت هي المسافة عن المعيار، ومش مقترح نلمس غير "
                        "المناطق اللي خارج المدى.");
}

Colour AeqAudioProcessorEditor::verdictColour() const {
    if (! haveData_ || ! state_.settled) return Palette::take();
    return actionCount() == 0 ? Palette::ok() : Palette::warn();
}

int AeqAudioProcessorEditor::actionCount() const {
    int n = 0;
    for (const auto& p : state_.problems)
        if (p.action != aeq::Action::None && p.action != aeq::Action::WarnOnly) ++n;
    return n;
}

// ---------------------------------------------------------------------------
void AeqAudioProcessorEditor::resized() {
    auto r = getLocalBounds();

    r.removeFromTop (kTitleH);
    const auto dock = r.removeFromBottom (kDockH);

    // the master bypass, top right
    {
        auto head = getLocalBounds().removeFromTop (kTitleH).reduced (18, 0);
        bypassBtn_.setBounds (head.removeFromRight (96).withSizeKeepingCentre (96, 30));
    }

    // ---- dock: a control row, then the knob row -----------------------------
    auto dockInner = dock.reduced (18, 10).reduced (16, 12);
    auto row1 = dockInner.removeFromTop (34);
    learnBtn_.setBounds (row1.removeFromLeft (128));
    row1.removeFromLeft (8);
    resetBtn_.setBounds (row1.removeFromLeft (96));
    row1.removeFromLeft (14);
    mode_.setBounds (row1.removeFromLeft (250));

    dockInner.removeFromTop (14);
    const int n = jmax (1, knobs_.size());
    const int cellW = dockInner.getWidth() / n;
    knobValueRows_.assign (size_t (knobs_.size()), {});
    for (int i = 0; i < knobs_.size(); ++i) {
        auto cell = dockInner.removeFromLeft (i == knobs_.size() - 1 ? dockInner.getWidth() + cellW
                                                                   : cellW);
        auto labelSlot = cell.removeFromTop (15);
        knobNames_[i]->setBounds (labelSlot.withSizeKeepingCentre (jmin (cellW, 130), 15));
        knobValueRows_[size_t (i)] = cell.removeFromBottom (17);
        const int knobSize = jmin (cell.getWidth() - 10, cell.getHeight() - 2);
        knobs_[i]->setBounds (cell.withSizeKeepingCentre (knobSize, knobSize));
    }

    // ---- main: curve on the left, verdict + report on the right -------------
    auto body = r.reduced (18, 12);
    auto rightCol = body.removeFromRight (kReportW);
    body.removeFromRight (14);

    verdictBounds_ = rightCol.removeFromTop (140);
    rightCol.removeFromTop (36);              // the report column caption
    reportView_.setBounds (rightCol);
    problems_.setSize (jmax (100, rightCol.getWidth() - 8), problems_.contentHeight());

    curveBounds_ = body;
    curve_.setBounds (body);
}

// ---------------------------------------------------------------------------
void AeqAudioProcessorEditor::timerCallback() {
    // Everything below runs on the message thread and only reads copies.  The
    // editor never touches the filter objects: that is the audio thread's job, and
    // the previous build crashed FL Studio by doing exactly that.
    state_ = proc_.core().state();
    meas_ = proc_.core().measurements();
    bands_ = proc_.core().activeBands();
    haveData_ = state_.valid;

    trims_.clear();
    for (int i = 0; i < int (bands_.size()); ++i) trims_.push_back (proc_.core().bandTrim (i));

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
    problems_.setSize (jmax (100, reportView_.getWidth() - 8), problems_.contentHeight());

    repaint();
}
