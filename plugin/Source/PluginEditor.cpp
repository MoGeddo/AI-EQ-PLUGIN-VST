#include "PluginEditor.h"

using namespace juce;
using aeq::ui::Palette;
using aeq::ui::Icon;
using aeq::ui::Face;

namespace {

// The design grid, straight out of design/preview/ui_v9.py.  Everything in this file
// is expressed in these numbers, so "the render" and "the window" cannot drift apart
// without someone editing a constant that both of them name.
constexpr float kDesignW = 1180.0f, kDesignH = 700.0f;
constexpr float kMargin = 22.0f;
constexpr float kPanelR = 16.0f, kWellR = 14.0f, kChipR = 12.0f, kPillR = 11.0f;
constexpr float kMicro = 8.0f, kCap = 10.0f, kBody = 12.0f, kValue = 15.0f;

// the hero in design coordinates: y 70..556.  The plot takes everything down to 486,
// the take strip is 494..520, and the caption row is 528..542.  These are y values,
// not fractions: the earlier version of this file multiplied a design y by the height
// scale and the strip collapsed to a line on every window taller than the design.
constexpr float kHeroTop = 70.0f, kHeroBottom = 556.0f;
constexpr float kPlotBottom = 486.0f;
constexpr float kRibbonTop = 494.0f, kRibbonBottom = 520.0f;
constexpr float kCaptionTop = 528.0f, kCaptionBottom = 542.0f;

const char* kTagline   = "بيسمع التيك كله، وإنت اللي بتوقفه";
const char* kSealed    = "مختوم";
const char* kListening = "بيتعلم";
const char* kSubSeal   = "مش بيتغيّر لوحده";
const char* kStopMark  = "الوقفة · إنت وقفته";
const char* kRestart   = "دوس تعلّم تاني";
const char* kMeasuredKey = "اللي اتقاس";
const char* kDecisionKey = "القرار";
const char* kStatic    = "ثابت";
const char* kDynamic   = "ديناميك";
const char* kTracked   = "متابع النغمة";
const char* kEmptyChip = "لسه";
const char* kModeRestore = "تصحيح";
const char* kModeMatch   = "مظروف";
const char* kStrength  = "القوة";
const char* kListenFor = "السماع";

// A tracked-out Latin caption.  Arabic must never be tracked (it breaks the joins),
// so this path exists only for the Latin micro-lines.
void trackedText (Graphics& g, const String& s, Rectangle<float> area, Colour c,
                  float size, float tracking = 1.3f, Justification j = Justification::centredLeft,
                  Face face = Face::Regular) {
    AttributedString as;
    as.setJustification (j);
    as.append (s.toUpperCase(), aeq::ui::font (size, face).withExtraKerningFactor (tracking / size), c);
    TextLayout lay;
    lay.createLayout (as, area.getWidth());
    lay.draw (g, area);
}

void arabic (Graphics& g, const char* s, Rectangle<float> area, Colour c, float size,
             Justification j = Justification::centredLeft, Face face = Face::Regular) {
    aeq::ui::draw (g, aeq::ui::u8 (s), area, j, size, c, face);
}

// The knob: a machined part drawn as paths, because the binary carries no sprites -
// anodised to the accent so a metal never introduces a third hue into the panel.
void drawKnob (Graphics& g, Point<float> centre, float size, double value, bool hover) {
    const float r = size * 0.5f;
    const auto accent = Palette::accent();

    // seat: a recess with a lit top edge and a shadowed bottom one
    const float seat = r * 1.08f;
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillEllipse (centre.x - seat, centre.y - seat + 1.0f, seat * 2, seat * 2);
    g.setColour (Palette::hairline().withAlpha (0.55f));
    g.drawEllipse (centre.x - seat, centre.y - seat, seat * 2, seat * 2, 1.0f);

    // body: radial, darker at the rim, with a specular arc at the top
    ColourGradient body (Palette::surfaceHi().brighter (0.10f), centre.x, centre.y - r * 0.35f,
                         Palette::bg().darker (0.35f), centre.x, centre.y + r, false);
    g.setGradientFill (body);
    g.fillEllipse (centre.x - r, centre.y - r, size, size);
    g.setColour (Colours::white.withAlpha (hover ? 0.22f : 0.14f));
    g.drawEllipse (centre.x - r + 0.5f, centre.y - r + 0.5f, size - 1.0f, size - 1.0f, 1.0f);
    g.setColour (Palette::accentHi().withAlpha (0.16f));
    Path cap;
    cap.addCentredArc (centre.x, centre.y, r * 0.86f, r * 0.86f, 0.0f,
                       MathConstants<float>::pi * 1.12f, MathConstants<float>::pi * 1.88f, true);
    g.strokePath (cap, PathStrokeType (1.2f));

    // the value ring: a groove from -135 to +135, and the accent from the centre detent
    const float ringR = r * 1.24f;
    Path groove;
    groove.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                          degreesToRadians (-135.0f), degreesToRadians (135.0f), true);
    g.setColour (Palette::hairline().withAlpha (0.75f));
    g.strokePath (groove, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    const float a = degreesToRadians (-135.0f + 270.0f * float (value));
    Path arc;
    arc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, a, 0.0f, true);
    g.setColour (accent.withAlpha (0.90f));
    g.strokePath (arc, PathStrokeType (2.4f, PathStrokeType::curved, PathStrokeType::rounded));

    // needle, from the centre out: never a dot in the middle
    const Point<float> tip (centre.x + std::sin (a) * r * 0.66f,
                            centre.y - std::cos (a) * r * 0.66f);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.drawLine (Line<float> (centre.translated (0.0f, 1.0f), tip.translated (0.0f, 1.0f)), 3.6f);
    g.setColour (Palette::accentHi());
    g.drawLine (Line<float> (centre, tip), 2.8f);
    g.fillEllipse (centre.x - 2.0f, centre.y - 2.0f, 4.0f, 4.0f);
}

} // namespace

// ---------------------------------------------------------------------------
class AeqSliderLook : public LookAndFeel_V4 {
public:
    void drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                           float, float, float, Slider& s) override {
        drawKnob (g, { x + width * 0.5f, y + height * 0.5f }, float (jmin (width, height)),
                  s.getValue(), s.isMouseOverOrDragging());
    }
    void drawLabel (Graphics&, Label&) override {}   // every label here is drawn by the editor
};

// ---------------------------------------------------------------------------
AeqAudioProcessorEditor::AeqAudioProcessorEditor (AeqAudioProcessor& p)
    : AudioProcessorEditor (&p), proc_ (p) {
    look_ = std::make_unique<AeqSliderLook>();
    setLookAndFeel (look_.get());

    setResizable (true, true);
    setResizeLimits (1000, 660, 1800, 1200);

    curve_.setEmbedded (true);
    addAndMakeVisible (curve_);
    curve_.onTrim = [this] (int index, double trimDb) { proc_.core().setBandTrim (index, trimDb); };
    curve_.onTrimReset = [this] (int index) { proc_.core().setBandTrim (index, 0.0); };

    buildControls();
    setSize (defaultWidth(), defaultHeight());
    resized();
    startTimerHz (30);
}

// v9 has two knobs, because everything else on this panel is a decision the plugin
// makes by itself.  The five parameters that used to be knobs (low cut, de-ess, the
// two axes, air) are still real parameters: they stay in the APVTS, they are
// automatable, and what the plugin decided about them is in the report line.  What
// they are not any more is five more things to look at.
void AeqAudioProcessorEditor::buildControls() {
    struct K {
        Slider* slider; Label* name;
        const char* id; const char* label; int decimals;
    };
    const K specs[] = {
        { &amountKnob_, &amountName_, "amount",   kStrength,  0 },
        { &settleKnob_, &settleName_, "settle_s", kListenFor, 0 },
    };

    for (const auto& k : specs) {
        k.slider->setSliderStyle (Slider::RotaryVerticalDrag);
        k.slider->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        k.slider->setRotaryParameters (MathConstants<float>::pi * 1.25f,
                                       MathConstants<float>::pi * 2.75f, true);
        k.slider->setNumDecimalPlacesToDisplay (k.decimals);
        if (auto* param = proc_.apvts().getParameter (k.id))
            k.slider->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        addAndMakeVisible (k.slider);

        k.name->setText (aeq::ui::u8 (k.label), dontSendNotification);
        k.name->setJustificationType (Justification::centred);
        k.name->setInterceptsMouseClicks (false, false);
        k.name->setVisible (false);       // the editor draws every label in v9's type
        addChildComponent (k.name);
    }

    // both attachments live for the editor's lifetime; the parameters are the model
    aAmount_ = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (
        proc_.apvts(), "amount", amountKnob_);
    aSettle_ = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (
        proc_.apvts(), "settle_s", settleKnob_);
}

AeqAudioProcessorEditor::~AeqAudioProcessorEditor() {
    setLookAndFeel (nullptr);
}

int AeqAudioProcessorEditor::defaultWidth()  { return int (kDesignW); }
int AeqAudioProcessorEditor::defaultHeight() { return int (kDesignH); }

// --------------------------------------------------------------------------- geometry
AeqAudioProcessorEditor::Blocks AeqAudioProcessorEditor::blocks() const {
    const float sx = float (getWidth()) / kDesignW;
    const float sy = float (getHeight()) / kDesignH;
    auto R = [sx, sy] (float x0, float y0, float x1, float y1) {
        return Rectangle<int> (roundToInt (x0 * sx), roundToInt (y0 * sy),
                               roundToInt ((x1 - x0) * sx), roundToInt ((y1 - y0) * sy));
    };
    Blocks b;
    b.header = R (kMargin, 14, kDesignW - kMargin, 54);
    b.hero   = R (kMargin, 70, kDesignW - kMargin, 556);
    b.chips  = R (kMargin, 572, kDesignW - kMargin, 636);
    b.footer = R (kMargin, 646, kDesignW - kMargin, 682);
    return b;
}

Rectangle<int> AeqAudioProcessorEditor::heroRow (float y0, float y1) const {
    const auto hero = blocks().hero;
    const float sy = float (getHeight()) / kDesignH;
    return { hero.getX(), hero.getY() + roundToInt ((y0 - kHeroTop) * sy), hero.getWidth(),
             jmax (2, roundToInt ((y1 - y0) * sy)) };
}

Rectangle<int> AeqAudioProcessorEditor::ribbon() const {
    return heroRow (kRibbonTop, kRibbonBottom);
}

Rectangle<int> AeqAudioProcessorEditor::well() const {
    // everything the curve owns: the hero without the take strip and its caption
    return heroRow (kHeroTop, kPlotBottom);
}

std::vector<Rectangle<int>> AeqAudioProcessorEditor::chipRects() const {
    const auto c = blocks().chips;
    const int gap = roundToInt (10.0f * float (getWidth()) / kDesignW);
    const int n = 4;
    const int w = (c.getWidth() - gap * (n - 1)) / n;
    std::vector<Rectangle<int>> out;
    for (int i = 0; i < n; ++i)
        out.push_back ({ c.getX() + i * (w + gap), c.getY(), w, c.getHeight() });
    return out;
}

Rectangle<int> AeqAudioProcessorEditor::modeSeg (int i) const {
    const auto f = blocks().footer;
    const int h = roundToInt (24.0f * float (getHeight()) / kDesignH);
    const int w = roundToInt (66.0f * float (getWidth()) / kDesignW);
    return { f.getX() + i * w, f.getCentreY() - h / 2, w, h };
}

Rectangle<int> AeqAudioProcessorEditor::bypassHit() const {
    const auto hd = blocks().header;
    const int s = roundToInt (40.0f * float (getHeight()) / kDesignH);
    return { hd.getRight() - roundToInt (78.0f * float (getWidth()) / kDesignW),
             hd.getCentreY() - s / 2, s, s };
}

Rectangle<int> AeqAudioProcessorEditor::resetHit() const {
    auto b = bypassHit();
    b.translate (-roundToInt (44.0f * float (getWidth()) / kDesignW), 0);
    return b;
}

// The clamp is the stop button.  It sits on the seal, and the hit area is the part
// plus a thumb's worth of slack, so it is a 40 px target and not a hairline.
Rectangle<int> AeqAudioProcessorEditor::clampHit() const {
    const auto r = ribbon();
    const float sx = float (getWidth()) / kDesignW;
    const int x = r.getX() + roundToInt (float (r.getWidth()) * float (sealedAt_));
    const int w = roundToInt (26.0f * sx);
    return { x - w, r.getY() - roundToInt (10.0f), w * 2, r.getHeight() + roundToInt (20.0f) };
}

int AeqAudioProcessorEditor::chipAt (Point<int> p) const {
    const auto rects = chipRects();
    for (int i = 0; i < int (rects.size()); ++i)
        if (rects[size_t (i)].contains (p)) return i;
    return -1;
}

// --------------------------------------------------------------------------- paint
void AeqAudioProcessorEditor::paint (Graphics& g) {
    const auto b = blocks();
    const float sy = float (getHeight()) / kDesignH;
    const float sx = float (getWidth()) / kDesignW;
    const auto bounds = getLocalBounds().toFloat();

    // the ground: warm-violet ink, top-lit, with two enormous blooms and nothing else.
    // Depth comes from light here, never from a texture - the fabric plate was the
    // thing that made this panel look older than it is.
    {
        ColourGradient ink (Palette::inkTop(), 0.0f, 0.0f, Palette::bg(), 0.0f, bounds.getBottom(), false);
        ink.addColour (0.62, Palette::bg().interpolatedWith (Palette::inkTop(), 0.18f));
        g.setGradientFill (ink);
        g.fillRoundedRectangle (bounds, kPanelR * jmin (sx, sy));
    }
    {
        // two blooms, both off the panel so only their light lands on it
        const auto accent = Palette::accent();
        const auto second = Palette::take();
        auto bloom = [&g, &bounds, sx, sy] (Colour c, float alpha, Point<float> centre, float radius) {
            ColourGradient gr (c.withAlpha (alpha), centre.x, centre.y,
                               c.withAlpha (0.0f), centre.x + radius, centre.y, true);
            g.setGradientFill (gr);
            g.fillEllipse (bounds.expanded (2.0f));
            juce::ignoreUnused (sx, sy);
        };
        bloom (accent, 0.10f, { bounds.getWidth() * 0.10f, -bounds.getHeight() * 0.26f },
               bounds.getWidth() * 0.72f);
        bloom (second, 0.09f, { bounds.getWidth() * 0.96f, bounds.getHeight() * 1.18f },
               bounds.getWidth() * 0.66f);
    }
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), kPanelR * jmin (sx, sy), 1.0f);

    drawHeader (g, b.header);
    drawWell (g, b.hero);
    drawTake (g, b.hero);
    drawChips (g, b.chips);
    drawFooter (g, b.footer);

    juce::ignoreUnused (sy);
}

void AeqAudioProcessorEditor::drawHeader (Graphics& g, Rectangle<int> hd) {
    const float sy = float (getHeight()) / kDesignH;
    const auto h = hd.toFloat();

    // wordmark: "AEQ" in the accent, "RESTORE" in the light ink, both tracked
    const float ws = kBody * sy;
    const Font mark = aeq::ui::font (ws, Face::SemiBold).withExtraKerningFactor (3.4f / ws);
    const float w1 = aeq::ui::measure ("AEQ", ws, Face::SemiBold);
    g.setFont (mark);
    g.setColour (Palette::accentHi());
    g.drawText ("AEQ", h.withHeight (ws * 1.6f).translated (0.0f, 0.0f),
                Justification::centredLeft, false);
    const Font rest = aeq::ui::font (ws, Face::Regular).withExtraKerningFactor (3.4f / ws);
    g.setFont (rest);
    g.setColour (Palette::text().withAlpha (0.72f));
    g.drawText ("RESTORE", h.withHeight (ws * 1.6f).translated (w1 + 8.0f * sy, 0.0f),
                Justification::centredLeft, false);

    // tagline, one line, under the mark
    arabic (g, kTagline, { h.getX(), h.getY() + h.getHeight() * 0.42f, h.getWidth() * 0.55f,
                           h.getHeight() * 0.58f },
            Palette::muted(), kBody * sy);

    // right cluster: master bypass, reset, the seal pill with its LED, and the promise
    const bool frozen = proc_.isFrozen() || proc_.isCommitted();
    const auto by = bypassHit();
    const bool bypassed = proc_.apvts().getRawParameterValue ("bypass")->load() > 0.5f;
    aeq::ui::icon (g, Icon::Power, by.toFloat().reduced (by.getWidth() * 0.18f),
                   bypassed ? Palette::warn() : Palette::muted(), 1.6f * sy);
    const auto rs = resetHit();
    aeq::ui::icon (g, Icon::Reset, rs.toFloat().reduced (rs.getWidth() * 0.18f),
                   Palette::muted(), 1.5f * sy);

    const String sealText = aeq::ui::u8 (frozen ? kSealed : kListening);
    const float textW = aeq::ui::measure (sealText, kCap * sy);
    const float pillW = textW + 44.0f * sy;
    const Rectangle<float> pill (float (rs.getX()) - pillW - 10.0f * sy,
                                 h.getCentreY() - 11.0f * sy, pillW, 22.0f * sy);
    g.setColour (Palette::accent().withAlpha (0.10f));
    g.fillRoundedRectangle (pill, kPillR * sy);
    g.setColour (Palette::accent().withAlpha (0.45f));
    g.drawRoundedRectangle (pill.reduced (0.5f), kPillR * sy, 1.0f);
    g.setColour (Palette::accentHi());
    g.fillEllipse (pill.getX() + 12.0f * sy, pill.getCentreY() - 3.0f * sy, 6.0f * sy, 6.0f * sy);
    aeq::ui::draw (g, sealText, pill.translated (10.0f * sy, 0.0f).withTrimmedRight (12.0f * sy),
                   Justification::centred, kCap * sy, Palette::accentHi());

    arabic (g, kSubSeal, { pill.getRight() - 120.0f * sy, pill.getBottom() + 2.0f * sy,
                           120.0f * sy, 12.0f * sy },
            Palette::dim(), kMicro * sy, Justification::centredRight);
}

void AeqAudioProcessorEditor::drawWell (Graphics& g, Rectangle<int> hero) {
    const float sy = float (getHeight()) / kDesignH;
    const auto wellR = hero.toFloat();
    const float r = kWellR * sy;

    // the plate: a touch darker than the ground, so the graph sits *in* the panel
    g.setColour (Colours::black.withAlpha (0.42f));
    g.fillRoundedRectangle (wellR, r);
    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (wellR.reduced (0.5f), r, 1.0f);
    g.setColour (Colours::white.withAlpha (0.14f));
    g.drawLine (wellR.getX() + r, wellR.getY() + 1.0f, wellR.getRight() - r, wellR.getY() + 1.0f, 1.0f);

    // the light inside the well: two very soft pools of the two accents
    {
        ColourGradient a (Palette::take().withAlpha (0.10f), wellR.getX() + wellR.getWidth() * 0.24f,
                          wellR.getY() + wellR.getHeight() * 0.66f,
                          Palette::take().withAlpha (0.0f), wellR.getX() + wellR.getWidth() * 0.24f
                              + wellR.getWidth() * 0.55f,
                          wellR.getY() + wellR.getHeight() * 0.66f, true);
        g.setGradientFill (a);
        g.fillRoundedRectangle (wellR, r);
        ColourGradient d (Palette::accent().withAlpha (0.09f), wellR.getX() + wellR.getWidth() * 0.78f,
                          wellR.getY() + wellR.getHeight() * 0.30f,
                          Palette::accent().withAlpha (0.0f), wellR.getX() + wellR.getWidth() * 0.78f
                              + wellR.getWidth() * 0.50f,
                          wellR.getY() + wellR.getHeight() * 0.30f, true);
        g.setGradientFill (d);
        g.fillRoundedRectangle (wellR, r);
    }
}

void AeqAudioProcessorEditor::drawTake (Graphics& g, Rectangle<int>) {
    const float sy = float (getHeight()) / kDesignH;
    const float sx = float (getWidth()) / kDesignW;
    auto rib = ribbon().toFloat();
    const float r = 4.0f * sy;

    // the empty track
    g.setColour (Colours::white.withAlpha (0.05f));
    g.fillRoundedRectangle (rib, r);

    // the learnt part, from the left, turning into the accent as it reaches the clamp:
    // the one place the two hues touch is the moment it was stopped
    const float cut = rib.getWidth() * float (sealedAt_);
    if (cut > 1.0f) {
        const Rectangle<float> fill (rib.getX(), rib.getY(), cut, rib.getHeight());
        ColourGradient gr (Palette::take().darker (0.55f), rib.getX(), rib.getY(),
                           Palette::take().brighter (0.10f), rib.getX() + cut, rib.getY(), false);
        gr.addColour (0.86, Palette::take().brighter (0.02f));
        gr.addColour (1.0, Palette::accent());
        g.setGradientFill (gr);
        g.fillRoundedRectangle (fill, r);
    }

    // the clamp: a machined part, anodised to the accent, with a shadow under it
    const int cx = rib.getX() + roundToInt (cut);
    const int cw = roundToInt (11.0f * sx), chh = roundToInt (float (rib.getHeight()) + 10.0f * sy);
    const Rectangle<float> clamp (float (cx) - cw * 0.5f,
                                  rib.getCentreY() - chh * 0.5f, float (cw), float (chh));
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (clamp.translated (2.0f * sx, 3.0f * sy), 3.0f * sy);
    ColourGradient metal (Palette::accent().brighter (0.55f), clamp.getX(), clamp.getY(),
                          Palette::accent().darker (0.45f), clamp.getX() + clamp.getWidth(),
                          clamp.getY() + clamp.getHeight(), false);
    g.setGradientFill (metal);
    g.fillRoundedRectangle (clamp, 3.0f * sy);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (clamp.reduced (0.5f), 3.0f * sy, 1.0f);
    g.setColour (Colours::black.withAlpha (0.40f));
    for (int i = 0; i < 2; ++i)
        g.fillEllipse (clamp.getCentreX() - 1.5f * sx,
                       clamp.getY() + clamp.getHeight() * (0.22f + 0.56f * float (i)) - 1.5f * sy,
                       3.0f * sx, 3.0f * sy);

    // the seal, labelled above itself
    arabic (g, kStopMark, { clamp.getCentreX() - 90.0f * sx, rib.getY() - 16.0f * sy,
                            180.0f * sx, 14.0f * sy },
            Palette::accentHi(), kMicro * sy, Justification::centred);

    // the caption row, inside the well: what was learnt (left) and the keys (right)
    const auto caption = heroRow (kCaptionTop, kCaptionBottom).toFloat();
    const auto st = proc_.core().state();
    const String learned = "LEARNED " + String (st.analyzedSeconds, 1) + " s  ·  "
                         + String (int (bands_.size())) + " BANDS  ·  15 SONGS  ·  "
                         + String (int (proc_.getSampleRate())) + " Hz";
    trackedText (g, learned, caption, Palette::muted(), kMicro * sy, 1.1f);

    float rx = caption.getRight();
    const auto keys = std::array<std::pair<const char*, Colour>, 2> {
        std::make_pair (kDecisionKey, Palette::accent()),
        std::make_pair (kMeasuredKey, Palette::take())
    };
    for (const auto& k : keys) {
        const String s = aeq::ui::u8 (k.first);
        const float w = aeq::ui::measure (s, kCap * sy);
        aeq::ui::draw (g, s, { rx - w, caption.getY(), w, caption.getHeight() },
                       Justification::centredLeft, kCap * sy, Palette::muted());
        g.setColour (k.second);
        g.fillEllipse (rx - w - 14.0f * sy, caption.getCentreY() - 4.0f * sy,
                       8.0f * sy, 8.0f * sy);
        rx -= w + 30.0f * sy;
    }
}

void AeqAudioProcessorEditor::drawChips (Graphics& g, Rectangle<int>) {
    const float sy = float (getHeight()) / kDesignH;
    const auto rects = chipRects();
    const int nStatic = staticBandCount();
    const bool tracked = hasTrackedLayer();

    for (int i = 0; i < int (rects.size()); ++i) {
        const auto r = rects[size_t (i)].toFloat();
        const bool hover = hoverChip_ == i;
        const bool hot = tracked && i == int (rects.size()) - 1;

        g.setColour (Palette::surfaceHi().withAlpha (0.92f));
        g.fillRoundedRectangle (r, kChipR * sy);
        g.setColour (hot ? Palette::accent().withAlpha (0.58f)
                         : Colours::white.withAlpha (hover ? 0.16f : 0.12f));
        g.drawRoundedRectangle (r.reduced (0.5f), kChipR * sy, 1.0f);

        const bool haveBand = i < int (bands_.size());
        const auto& spec = haveBand ? bands_[size_t (i)] : aeq::BandSpec{};
        const double trim = i < int (trims_.size()) ? trims_[size_t (i)] : 0.0;
        const double gain = haveBand ? spec.gainDb + trim : 0.0;

        // index + mode
        trackedText (g, String (i + 1).paddedLeft ('0', 2),
                     { r.getX() + 12.0f * sy, r.getY() + 7.0f * sy, 40.0f * sy, 12.0f * sy },
                     hot ? Palette::accentHi() : Palette::dim(), kMicro * sy, 1.3f);
        const String mode = haveBand ? chipMode (size_t (i)) : aeq::ui::u8 (kEmptyChip);
        const float mw = aeq::ui::measure (mode, kCap * sy);
        aeq::ui::draw (g, mode, { r.getRight() - 12.0f * sy - mw, r.getY() + 4.0f * sy, mw,
                                  r.getHeight() * 0.4f },
                       Justification::centredRight, kCap * sy,
                       hot ? Palette::accentHi() : (i == nStatic - 1 && haveBand ? Palette::take()
                                                                                 : Palette::muted()));

        // the number, then the micro line under it
        if (haveBand) {
            const String num = (gain >= 0.0 ? "+" : "") + String (gain, 1);
            const Font big = aeq::ui::font (kValue * sy, Face::SemiBold);
            g.setFont (big);
            g.setColour (hot ? Palette::accentHi() : Palette::text());
            g.drawText (num, r.withTrimmedLeft (12.0f * sy).withTrimmedTop (r.getHeight() * 0.30f)
                                .withHeight (kValue * sy * 1.4f),
                        Justification::centredLeft, false);
            const float nw = aeq::ui::measure (num, kValue * sy, Face::SemiBold);
            trackedText (g, "dB", { r.getX() + 15.0f * sy + nw, r.getY() + r.getHeight() * 0.42f,
                                    24.0f * sy, 12.0f * sy },
                         Palette::dim(), kMicro * sy, 0.4f);

            const String micro = spec.f0 >= 1000.0
                ? String (spec.f0 / 1000.0, 2) + " kHz  ·  Q " + String (spec.q, 2)
                : String (int (spec.f0)) + " Hz  ·  Q " + String (spec.q, 2);
            trackedText (g, micro, { r.getX() + 12.0f * sy, r.getY() + r.getHeight() * 0.66f,
                                     r.getWidth() - 24.0f * sy, 12.0f * sy },
                         Palette::dim(), kMicro * sy, 0.9f);

            // the live bar: a dynamic or a tracked band is moving right now
            if (hot) {
                const double lvl = proc_.core().trackedCount() > 0
                                 ? jlimit (0.0, 1.0, (proc_.core().trackedLevelDb (0) + 60.0) / 60.0)
                                 : 0.0;
                g.setColour (Palette::take().withAlpha (0.75f));
                g.fillRoundedRectangle (r.getX() + 12.0f * sy, r.getBottom() - 9.0f * sy,
                                        float ((r.getWidth() - 24.0f * sy) * lvl), 2.0f * sy,
                                        1.0f * sy);
            }
        }
    }
}

void AeqAudioProcessorEditor::drawFooter (Graphics& g, Rectangle<int> area) {
    const float sy = float (getHeight()) / kDesignH;
    const float sx = float (getWidth()) / kDesignW;
    const auto cy = float (area.getCentreY());

    // the mode: two segments, because this product has two modes and no third
    const auto t = proc_.apvts().getRawParameterValue ("mode")->load();
    const int active = t > 0.5f ? 1 : 0;
    const auto s0 = modeSeg (0).toFloat(), s1 = modeSeg (1).toFloat();
    const Rectangle<float> pill (s0.getX(), s0.getY(), s1.getRight() - s0.getX(), s0.getHeight());
    g.setColour (Colours::white.withAlpha (0.05f));
    g.fillRoundedRectangle (pill, kPillR * sy);
    const auto& seg = active == 0 ? s0 : s1;
    g.setColour (Palette::accent().withAlpha (0.14f));
    g.fillRoundedRectangle (seg.reduced (3.0f * sy), (kPillR - 3.0f) * sy);
    g.setColour (Palette::accent().withAlpha (0.62f));
    g.drawRoundedRectangle (seg.reduced (3.0f * sy), (kPillR - 3.0f) * sy, 1.0f);
    const char* labels[] = { kModeRestore, kModeMatch };
    for (int i = 0; i < 2; ++i) {
        const auto r = i == 0 ? s0 : s1;
        const String s = aeq::ui::u8 (labels[i]);
        aeq::ui::draw (g, s, r, Justification::centred, kCap * sy,
                       i == active ? Palette::accentHi() : Palette::muted());
    }

    // one sentence, then nothing else - the report card is gone
    arabic (g, verdictLine().toRawUTF8(),
            { pill.getRight() + 24.0f * sx, cy - 9.0f * sy, area.getWidth() * 0.52f, 18.0f * sy },
            Palette::muted(), kBody * sy, Justification::centredLeft);

    // the two things a hand turns: the knobs themselves are child sliders drawn by
    // the look and feel, so this only writes their two labels and their two numbers
    const Point<float> k2 (float (area.getRight()) - 30.0f * sx, cy);
    const Point<float> k1 (float (area.getRight()) - 134.0f * sx, cy);
    const float knobSize = 42.0f * sy;

    auto labelFor = [&] (const char* s, Point<float> c, float dx) {
        const String t2 = aeq::ui::u8 (s);
        const float w = aeq::ui::measure (t2, kCap * sy);
        aeq::ui::draw (g, t2, { c.x - dx - w, cy - 7.0f * sy, w, 14.0f * sy },
                       Justification::centredRight, kCap * sy, Palette::muted());
    };
    labelFor (kStrength, k1, knobSize * 0.62f + 8.0f * sy);
    labelFor (kListenFor, k2, knobSize * 0.62f + 8.0f * sy);

    g.setFont (aeq::ui::font (kCap * sy, Face::SemiBold));
    g.setColour (Palette::text());
    const String v1 = String (roundToInt (amountKnob_.getValue())) + "%";
    const String v2 = roundToInt (settleKnob_.getValue()) >= 1
                        ? String (roundToInt (settleKnob_.getValue())) + " s"
                        : String ("OFF");
    g.drawText (v1, Rectangle<float> (k1.x - 46.0f * sy, cy + 10.0f * sy, 46.0f * sy, 14.0f * sy),
                Justification::centredRight, false);
    g.drawText (v2, Rectangle<float> (k2.x - 46.0f * sy, cy + 10.0f * sy, 46.0f * sy, 14.0f * sy),
                Justification::centredRight, false);
}

// --------------------------------------------------------------------------- copy
int AeqAudioProcessorEditor::staticBandCount() const {
    return int (bands_.size()) - (hasTrackedLayer() ? 1 : 0);
}

bool AeqAudioProcessorEditor::hasTrackedLayer() const {
    return proc_.core().trackedCount() > 0;
}

String AeqAudioProcessorEditor::chipMode (size_t index) const {
    if (hasTrackedLayer() && index + 1 == bands_.size()) return aeq::ui::u8 (kTracked);
    if (int (index) == staticBandCount() - 1 && bands_.size() > 3) return aeq::ui::u8 (kDynamic);
    return aeq::ui::u8 (kStatic);
}

String AeqAudioProcessorEditor::verdictLine() const {
    if (! haveData_)
        return aeq::ui::u8 ("لسه بيسمع. سيب التيك يشتغل، وأول ما تخلص دوس المشبك.");
    if (! state_.settled)
        return aeq::ui::u8 ("بيقيس كل ثانية بتعدّي ويقارنها بالـ١٥ ميكس. الحكم بينزل لما يثبت.");
    if (! proc_.isFrozen() && ! proc_.isCommitted())
        return aeq::ui::u8 ("لسه بيتعلم. إنت اللي بتوقفه لما تحس إنه سمع التيك كله.");
    if (bands_.empty())
        return aeq::ui::u8 ("كل حاجة داخل مدى المهندس. مفيش حركة هنا غير إنك تسيبه.");
    return aeq::ui::u8 ("مختوم. ") + String (int (bands_.size()))
         + aeq::ui::u8 (" باندات، والقرار مش بيتغيّر لوحده.");
}

// --------------------------------------------------------------------------- layout
void AeqAudioProcessorEditor::resized() {
    const auto b = blocks();
    const float sy = float (getHeight()) / kDesignH;

    curve_.setBounds (well());

    const float sx = float (getWidth()) / kDesignW;
    const float knobSize = 42.0f * sy;
    const int cy = b.footer.getCentreY();
    const int box = roundToInt (knobSize);
    const int c1 = b.footer.getRight() - roundToInt (134.0f * sx);
    const int c2 = b.footer.getRight() - roundToInt (30.0f * sx);
    amountKnob_.setBounds (c1 - box / 2, cy - box / 2, box, box);
    settleKnob_.setBounds (c2 - box / 2, cy - box / 2, box, box);
}

// --------------------------------------------------------------------------- input
void AeqAudioProcessorEditor::mouseMove (const MouseEvent& e) {
    const int chip = chipAt (e.getPosition());
    const bool clamp = clampHit().contains (e.getPosition());
    if (chip != hoverChip_ || clamp != hoverClamp_) {
        hoverChip_ = chip;
        hoverClamp_ = clamp;
        setMouseCursor (clamp ? MouseCursor::PointingHandCursor
                              : (chip >= 0 ? MouseCursor::UpDownResizeCursor
                                           : MouseCursor::NormalCursor));
        repaint();
    }
}

void AeqAudioProcessorEditor::mouseExit (const MouseEvent&) {
    hoverChip_ = -1;
    hoverClamp_ = false;
    repaint();
}

void AeqAudioProcessorEditor::mouseDown (const MouseEvent& e) {
    const auto p = e.getPosition();

    // the clamp: stop when listening, learn again when sealed
    if (clampHit().contains (p)) {
        if (proc_.isCommitted() || proc_.isFrozen()) proc_.relearn();
        else                                          proc_.stopLearning();
        lastVersion_ = -1;
        repaint();
        return;
    }
    if (bypassHit().contains (p)) {
        if (auto* param = proc_.apvts().getParameter ("bypass")) {
            param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
            repaint();
        }
        return;
    }
    if (resetHit().contains (p)) {
        proc_.requestReset();
        proc_.core().clearTrims();
        haveData_ = false;
        repaint();
        return;
    }
    for (int i = 0; i < 2; ++i) {
        if (modeSeg (i).contains (p)) {
            if (auto* param = proc_.apvts().getParameter ("mode"))
                param->setValueNotifyingHost (i == 0 ? 0.0f : 1.0f);
            repaint();
            return;
        }
    }

    // a chip is a band: dragging it is a trim, the same edit the curve handle makes
    const int chip = chipAt (p);
    if (chip >= 0 && chip < int (bands_.size())) {
        draggedChip_ = chip;
        dragStartTrim_ = chip < int (trims_.size()) ? trims_[size_t (chip)] : 0.0;
        dragOrigin_ = p;
        repaint();
    }
}

void AeqAudioProcessorEditor::mouseDrag (const MouseEvent& e) {
    if (draggedChip_ < 0) return;
    // 1 dB per 8 design px of vertical travel; up is louder, as every EQ does it
    const float sy = float (getHeight()) / kDesignH;
    const double dB = -double (e.getPosition().y - dragOrigin_.y) / (8.0 * double (sy));
    proc_.core().setBandTrim (draggedChip_, jlimit (-12.0, 12.0, dragStartTrim_ + dB));
    repaint();
}

void AeqAudioProcessorEditor::mouseUp (const MouseEvent&) {
    draggedChip_ = -1;
}

void AeqAudioProcessorEditor::mouseDoubleClick (const MouseEvent& e) {
    const int chip = chipAt (e.getPosition());
    if (chip >= 0) {
        proc_.core().setBandTrim (chip, 0.0);
        repaint();
    }
}

// --------------------------------------------------------------------------- timer
void AeqAudioProcessorEditor::timerCallback() {
    // A poll, not a copy of the world: the analyser publishes at 30 Hz and the solver
    // at about 4 Hz, so the cheap path is taken almost every tick and the expensive
    // path only when something a person can see actually changed.
    const auto live = proc_.core().live();
    if (live.valid && live.version != lastLiveVersion_) {
        lastLiveVersion_ = live.version;
        curve_.setLive (live.liveDb, live.peakDb);
    }

    const auto st = proc_.autoState();
    const int tick = int (st.heardSeconds);
    const bool frozen = proc_.isFrozen() || proc_.isCommitted();
    if (proc_.core().version() == lastVersion_ && tick == lastTick_ && frozen == committedSeen_)
        return;
    lastVersion_ = proc_.core().version();
    lastTick_ = tick;
    committedSeen_ = frozen;

    state_ = proc_.core().state();
    meas_ = proc_.core().measurements();
    bands_ = proc_.core().activeBands();
    haveData_ = state_.valid;

    trims_.clear();
    for (int i = 0; i < int (bands_.size()); ++i) trims_.push_back (proc_.core().bandTrim (i));

    // the seal: where the clamp is.  While learning it rides the head of the take;
    // once stopped it stays exactly where the user left it, which is the promise.
    const double need = jmax (1.0, st.needSeconds);
    if (frozen && sealedAt_ <= 0.001)
        sealedAt_ = jlimit (0.06, 0.94, st.heardSeconds / need);
    else if (! frozen)
        sealedAt_ = jlimit (0.02, 0.94, st.heardSeconds / need);

    aeq::ui::CurveView::Data d;
    d.meas = meas_;
    d.bands = bands_;
    d.problems = state_.problems;
    d.trims = trims_;
    d.valid = haveData_;
    d.settled = state_.settled;
    d.analysedSeconds = state_.analyzedSeconds;
    curve_.setData (std::move (d));

    // only the parts of the window that show live state are repainted by this timer
    repaint();
}
