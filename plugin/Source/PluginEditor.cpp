#include "PluginEditor.h"
#include "ui/BandRow.h"
#include "ui/PanelCopy.h"

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
// 1.2.10: the type was too small to read at arm's length and the copy was Arabic.
// Both changed at the user's instruction: the window is English now and everything is
// one size up, twice in the case of the numbers people actually read.
// 1.2.18: the user read the panel and said the labels on the controls were too small to
// make out.  Every size went up one step (11/13/16/21 -> 13/15/18/24) and the four
// steps are still four: a fifth size is how a panel turns into a ransom note.
constexpr float kMicro = 15.0f, kCap = 17.0f, kBody = 20.0f, kValue = 26.0f;

// the hero in design coordinates: y 70..556.  The plot takes everything down to 486,
// the take strip is 494..520, and the caption row is 528..542.  These are y values,
// not fractions: the earlier version of this file multiplied a design y by the height
// scale and the strip collapsed to a line on every window taller than the design.
// 1.2.18: the type went up a step, so the hero gives 16 px of height back to the two rows
// under it (the graph is still 67 % of the window) and every row inside the hero moved up
// with it.  These are the same numbers as design/preview/ui_v9.py.
constexpr float kHeroTop = 70.0f, kHeroBottom = 536.0f;
constexpr float kPlotBottom = 454.0f;      // the plot stops here; the strip has its own band
constexpr float kStopTop = 460.0f, kStopBottom = 476.0f;   // the label above the clamp
constexpr float kRibbonTop = 482.0f, kRibbonBottom = 508.0f;
constexpr float kCaptionTop = 514.0f, kCaptionBottom = 532.0f;

// The copy, in English.  Short enough to stay one line at the larger type, and it still
// says what the instrument does rather than what it would like to be.
const char* kTagline   = "listens to the whole take; you stop it";
const char* kSealed    = "SEALED";
const char* kListening = "LISTENING";
const char* kSubSeal   = "nothing moves on its own";
const char* kStopMark  = "STOPPED BY YOU";     // the pass ended where the user ended it
const char* kStopAuto  = "STOPPED AT THE CAP"; // ... or where they told it to stop for them
const char* kStop      = "STOP";               // the clamp at rest: this is where the pass ends
const char* kStopHere  = "STOP HERE";          // ... and on hover, while listening
const char* kRestart   = "learn again";
const char* kMeasuredKey = "measured";
const char* kDecisionKey = "decision";
const char* kStatic    = "STATIC";
const char* kTracked   = "TRACKED";
const char* kEmptyChip = "EMPTY";
const char* kOff       = "OFF";        // a band the user has handed back
const char* kModeRestore = "RESTORE";
const char* kModeMatch   = "MATCH";
const char* kStrength  = "strength";
const char* kAutoStop  = "auto-stop";
const char* kStabiliser = "stabiliser";

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
// `box` is the width of the whole control, ring and seat included; the body is 78 % of
// it.  The caller passes a box, not a body, because the value ring is drawn 1.19 x the
// body radius and a child is clipped to its own bounds: at 42 px the ring was being cut
// off at the top of every knob.
void drawKnob (Graphics& g, Point<float> centre, float box, float proportion, bool hover) {
    const float r = box * 0.39f;
    const float value = jlimit (0.0f, 1.0f, proportion);
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
    g.fillEllipse (centre.x - r, centre.y - r, r * 2, r * 2);
    g.setColour (Colours::white.withAlpha (hover ? 0.22f : 0.14f));
    g.drawEllipse (centre.x - r + 0.5f, centre.y - r + 0.5f, r * 2 - 1.0f, r * 2 - 1.0f, 1.0f);
    g.setColour (Palette::accentHi().withAlpha (0.16f));
    Path cap;
    // the specular highlight belongs on the lit side of the part, which is the top: at
    // 1.12 pi..1.88 pi it was being drawn across the bottom of every knob
    cap.addCentredArc (centre.x, centre.y, r * 0.86f, r * 0.86f, 0.0f,
                       -MathConstants<float>::pi * 0.34f,
                       MathConstants<float>::pi * 0.34f, true);
    g.strokePath (cap, PathStrokeType (1.2f));

    // the value ring: a groove from -135 to +135, and the accent from the centre detent
    const float ringR = r * 1.19f;
    Path groove;
    groove.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                          degreesToRadians (-135.0f), degreesToRadians (135.0f), true);
    g.setColour (Palette::hairline().withAlpha (0.75f));
    g.strokePath (groove, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // The arc runs from the *start of the travel* to the value, not from the value to 12
    // o'clock: JUCE measures angles from the top, clockwise, so the travel is -135..+135
    // and the lit part is -135..a.  Drawn the other way round the ring sat on the wrong
    // side of the knob and its length had nothing to do with the number.
    const float a = degreesToRadians (-135.0f + 270.0f * value);
    Path arc;
    arc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                       degreesToRadians (-135.0f), a, true);
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
        // The slider's value is in *parameter* units (0..100 %, 0..300 s); the dial is in
        // proportions.  Passing getValue() straight into the angle arithmetic is how the
        // knobs came to spin: 100 % was 26 865 degrees.  valueToProportionOfLength also
        // respects a skewed range, so the needle points where the number says.
        const float p = float (s.valueToProportionOfLength (s.getValue()));
        drawKnob (g, { x + width * 0.5f, y + height * 0.5f }, float (jmin (width, height)),
                  p, s.isMouseOverOrDragging());
    }
    void drawLabel (Graphics&, Label&) override {}   // every label here is drawn by the editor
};

// ---------------------------------------------------------------------------
AeqAudioProcessorEditor::AeqAudioProcessorEditor (AeqAudioProcessor& p)
    : AudioProcessorEditor (&p), proc_ (p) {
    look_ = std::make_unique<AeqSliderLook>();
    setLookAndFeel (look_.get());

    setResizable (true, true);
    setWantsKeyboardFocus (true);
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
        { &settleKnob_, &settleName_, "auto_stop", kAutoStop, 0 },
        { &stableKnob_, &stabName_,   "stable",    kStabiliser, 0 },
    };

    for (const auto& k : specs) {
        k.slider->setSliderStyle (Slider::RotaryVerticalDrag);
        // 320 px of travel for the whole range: a hand can find 1 % on a 42 px knob.
        k.slider->setMouseDragSensitivity (320);
        k.slider->setVelocityBasedMode (false);
        k.slider->setRepaintsOnMouseActivity (true);
        k.slider->setTooltip (k.label);
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
    // The second dial is "auto-stop", and it has to be attached to the parameter it is
    // labelled with.  In 1.2.15 it was still bound to "settle_s" (the trust threshold),
    // so the knob that says OFF at 0 was showing 45 s and was driving something else.
    aSettle_ = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (
        proc_.apvts(), "auto_stop", settleKnob_);
    aStable_ = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (
        proc_.apvts(), "stable", stableKnob_);
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
    b.header = R (kMargin, 10, kDesignW - kMargin, 68);
    b.hero   = R (kMargin, kHeroTop, kDesignW - kMargin, kHeroBottom);
    b.chips  = R (kMargin, 552, kDesignW - kMargin, 632);
    b.footer = R (kMargin, 640, kDesignW - kMargin, 686);
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
    const int n = jmax (1, int (chips_.size()));
    const int w = (c.getWidth() - gap * (n - 1)) / n;
    std::vector<Rectangle<int>> out;
    for (int i = 0; i < n; ++i)
        out.push_back ({ c.getX() + i * (w + gap), c.getY(), w, c.getHeight() });
    return out;
}

Rectangle<int> AeqAudioProcessorEditor::modeSeg (int i) const {
    const auto f = blocks().footer;
    const int h = roundToInt (26.0f * float (getHeight()) / kDesignH);
    const int w = roundToInt (90.0f * float (getWidth()) / kDesignW);
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
    const int x = r.getX() + jlimit (10, jmax (10, r.getWidth() - 10),
                                     roundToInt (float (r.getWidth()) * float (sealedAt_)));
    const int w = roundToInt (26.0f * sx);
    return { x - w, r.getY() - roundToInt (10.0f), w * 2, r.getHeight() + roundToInt (20.0f) };
}

int AeqAudioProcessorEditor::chipAt (Point<int> p) const {
    const auto rects = chipRects();
    for (int i = 0; i < int (rects.size()); ++i)
        if (rects[size_t (i)].contains (p)) return i;
    return -1;
}

Rectangle<int> AeqAudioProcessorEditor::controlBounds (Control c) const {
    switch (c) {
        case Control::Clamp:       return clampHit();
        case Control::Bypass:      return bypassHit();
        case Control::Reset:       return resetHit();
        case Control::ModeRestore: return modeSeg (0);
        case Control::ModeMatch:   return modeSeg (1);
        case Control::Chip: {
            const auto rects = chipRects();
            if (rects.empty()) return {};
            Rectangle<int> all = rects[0];
            for (const auto& r : rects) all = all.getUnion (r);
            return all;
        }
        case Control::StrengthKnob: return amountKnob_.getBounds();
        case Control::AutoStopKnob: return settleKnob_.getBounds();
        case Control::StabiliserKnob: return stableKnob_.getBounds();
        case Control::None:         break;
    }
    return {};
}

AeqAudioProcessorEditor::Control AeqAudioProcessorEditor::controlAt (Point<int> p) const {
    if (clampHit().contains (p))         return Control::Clamp;
    if (bypassHit().contains (p))        return Control::Bypass;
    if (resetHit().contains (p))         return Control::Reset;
    if (modeSeg (0).contains (p))        return Control::ModeRestore;
    if (modeSeg (1).contains (p))        return Control::ModeMatch;
    if (amountKnob_.getBounds().contains (p))  return Control::StrengthKnob;
    if (settleKnob_.getBounds().contains (p))  return Control::AutoStopKnob;
    if (stableKnob_.getBounds().contains (p))  return Control::StabiliserKnob;
    if (chipAt (p) >= 0)                 return Control::Chip;
    return Control::None;
}

bool AeqAudioProcessorEditor::chipDraggable (size_t index) const {
    return index < chips_.size() && chips_[index].src >= 0;
}

String AeqAudioProcessorEditor::tooltipAt (Point<int> p) const {
    if (clampHit().contains (p))
        return proc_.isFrozen() ? String ("Open the take again - every sample stays")
                                : String ("Stop the pass here and seal this decision");
    if (bypassHit().contains (p))
        return String ("Bypass: the same take, untouched");
    if (resetHit().contains (p))
        return String ("Forget this take and listen again");
    for (int i = 0; i < 2; ++i)
        if (modeSeg (i).contains (p))
            return i == 0 ? String ("Restore: fix what is wrong, keep the singer")
                          : String ("Match: pull the take onto the reference curve");
    const int chip = chipAt (p);
    if (chip >= 0 && chip < int (chips_.size())) {
        const auto& c = chips_[size_t (chip)];
        if (c.tracked >= 0)
            return String ("Dynamic layer on harmonic ") + String (proc_.core().trackedSpec (c.tracked).harmonic)
                 + " - it follows the note, so there is no frequency to drag";
        if (c.src >= 0)
            return String ("Drag up or down to trim this band; right click hands it back; "
                           "double click returns the solver's answer");
    }
    if (stableKnob_.getBounds().contains (p))
        return String ("Stabiliser: ten bands that hold every part of the voice at the level this take gave it");
    if (ribbon().contains (p)) return String ("The take: it fills while the plugin listens");
    if (well().contains (p))   return String ("Measured (blue) against the decision (pink)");
    return {};
}

bool AeqAudioProcessorEditor::keyPressed (const KeyPress& key) {
    if (key == KeyPress::spaceKey) {
        if (proc_.isFrozen()) { proc_.reopenPass(); stoppedByUser_ = false; }
        else                  { proc_.stopLearning(); stoppedByUser_ = true; }
        lastVersion_ = -1;
        tooltip_ = proc_.isFrozen() ? "Sealed" : "Listening";
        repaint();
        return true;
    }
    if (key == KeyPress::escapeKey) {
        proc_.reopenPass();
        stoppedByUser_ = false;
        lastVersion_ = -1;
        tooltip_ = "Listening";       // a key that changes the state must say so, or it looks
                                     // like it did nothing - which is what the panel was
                                     // reported for: "keys that give nothing back"
        repaint();
        return true;
    }
    return false;
}

void AeqAudioProcessorEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) {
    // The knob under the pointer takes the wheel; everywhere else the wheel does nothing
    // rather than something surprising.
    for (auto* s : { &stableKnob_, &amountKnob_, &settleKnob_ })
        if (s->getBounds().contains (e.getPosition())) {
            s->mouseWheelMove (e.getEventRelativeTo (s), w);
            return;
        }
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

    drawTooltip (g);
    juce::ignoreUnused (sy);
}

void AeqAudioProcessorEditor::drawTooltip (Graphics& g) {
    if (tooltip_.isEmpty() || ! isMouseOver (true)) return;
    const float sy = float (getHeight()) / kDesignH;
    const auto m = getMouseXYRelative().toFloat();
    const float w = aeq::ui::measure (tooltip_, kMicro * sy) + 24.0f * sy;
    const float h = kMicro * sy * 2.0f;
    auto box = Rectangle<float> (m.x + 14.0f, m.y + 18.0f, w, h);
    if (box.getRight() > getWidth() - 6.0f) box.setX (getWidth() - 6.0f - w);
    if (box.getBottom() > getHeight() - 6.0f) box.setY (m.y - h - 10.0f);
    g.setColour (Colours::black.withAlpha (0.90f));
    g.fillRoundedRectangle (box, 6.0f * sy);
    g.setColour (Palette::hairline().withAlpha (0.35f));
    g.drawRoundedRectangle (box, 6.0f * sy, 1.0f);
    aeq::ui::draw (g, tooltip_, box.reduced (12.0f * sy, 0.0f), Justification::centredLeft,
                   kMicro * sy, Palette::text());
}

void AeqAudioProcessorEditor::drawHeader (Graphics& g, Rectangle<int> hd) {
    const float sy = float (getHeight()) / kDesignH;
    const auto h = hd.toFloat();

    // Two rows a side, placed from the header's own top edge rather than from its centre:
    // the type grew a step (1.2.26) and lines hung off the centre started overlapping the
    // moment one of them got taller - the tagline moved up into the wordmark.
    // wordmark: "AEQ" in the accent, "RESTORE" in the light ink, both tracked
    const float ws = kBody * sy;
    const float markH = ws * 1.15f;
    const auto markRow = h.withTop (h.getY() + 4.0f * sy).withHeight (markH);
    const Font mark = aeq::ui::font (ws, Face::SemiBold).withExtraKerningFactor (3.4f / ws);
    const float w1 = aeq::ui::measure ("AEQ", ws, Face::SemiBold);
    g.setFont (mark);
    g.setColour (Palette::accentHi());
    g.drawText ("AEQ", markRow, Justification::centredLeft, false);
    const Font rest = aeq::ui::font (ws, Face::Regular).withExtraKerningFactor (3.4f / ws);
    g.setFont (rest);
    g.setColour (Palette::text().withAlpha (0.72f));
    g.drawText ("RESTORE", markRow.translated (w1 + 8.0f * sy, 0.0f),
                Justification::centredLeft, false);

    // tagline, one line, under the mark
    const float tagY = h.getY() + 4.0f * sy + markH + 2.0f * sy;
    arabic (g, kTagline, { h.getX(), tagY, h.getWidth() * 0.55f, kCap * sy * 1.35f },
            Palette::muted(), kCap * sy);

    // right cluster: master bypass, reset, the seal pill with its LED, and the promise.
    // The pill's words come from panelFacts() now, so this function no longer decides
    // what "sealed" means on its own.
    const auto by = bypassHit();
    const bool bypassed = proc_.apvts().getRawParameterValue ("bypass")->load() > 0.5f;
    aeq::ui::icon (g, Icon::Power, by.toFloat().reduced (by.getWidth() * 0.18f),
                   bypassed ? Palette::warn() : Palette::muted(), 1.6f * sy);
    const auto rs = resetHit();
    aeq::ui::icon (g, Icon::Reset, rs.toFloat().reduced (rs.getWidth() * 0.18f),
                   Palette::muted(), 1.5f * sy);

    const String sealText = String (aeq::copy::stateWord (panelFacts()));
    const float textW = aeq::ui::measure (sealText, kCap * sy);
    const float pillW = textW + 44.0f * sy;
    const Rectangle<float> pill (float (rs.getX()) - pillW - 10.0f * sy,
                                 h.getY() + 4.0f * sy, pillW, 30.0f * sy);
    g.setColour (Palette::accent().withAlpha (0.10f));
    g.fillRoundedRectangle (pill, kPillR * sy);
    g.setColour (Palette::accent().withAlpha (0.45f));
    g.drawRoundedRectangle (pill.reduced (0.5f), kPillR * sy, 1.0f);
    g.setColour (Palette::accentHi());
    g.fillEllipse (pill.getX() + 12.0f * sy, pill.getCentreY() - 3.0f * sy, 6.0f * sy, 6.0f * sy);
    aeq::ui::draw (g, sealText, pill.translated (10.0f * sy, 0.0f).withTrimmedRight (12.0f * sy),
                   Justification::centred, kCap * sy, Palette::accentHi());

    // 13 px type in a 12 px box is a clipped line: the box is measured from the type now
    arabic (g, String (aeq::copy::stateSub (panelFacts())).toRawUTF8(),
            { pill.getRight() - 250.0f * sy, pill.getBottom() + 4.0f * sy,
              250.0f * sy, kMicro * sy * 1.35f },
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
    g.setColour (Palette::take().withAlpha (0.04f));
    g.fillRoundedRectangle ({ rib.getX() + cut, rib.getY(), rib.getWidth() - cut, rib.getHeight() }, r);
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
    const int cx = rib.getX() + jlimit (10, jmax (10, int (rib.getWidth()) - 10), roundToInt (cut));
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

    // the seal, labelled in the row the plot leaves for it - and only once something has
    // actually been stopped.  An empty window that says "STOPPED BY YOU" next to a pill
    // that says LISTENING is the panel contradicting itself, and a decision the plugin
    // made for itself is not one the user made either: the label follows the truth.
    // The clamp is the only control that starts and stops the pass, and its label used to
    // appear only on hover or after the fact - so "where do I stop it" had no answer on
    // screen.  It says its own verb in every state: STOP while the pass is running,
    // OPEN once it is sealed.
    const auto facts = panelFacts();
    String stopLabel;
    if (aeq::copy::sealed (facts))
        stopLabel = stoppedByUser_ ? kStopMark : kStopAuto;
    else
        stopLabel = hoverClamp_ ? kStopHere : kStop;
    {
        const auto stopRow = heroRow (kStopTop, kStopBottom).toFloat();
        const float stopW = aeq::ui::measure (stopLabel, kMicro * sy, Face::SemiBold) + 22.0f * sx;
        // clamped inside the ribbon: the clamp rides towards the right edge as the take
        // grows, and a label that hangs off the end is unreadable at exactly the moment
        // it matters
        const float lx = jlimit (rib.getX(), jmax (rib.getX(), rib.getRight() - stopW),
                                 clamp.getCentreX() - stopW / 2.0f);
        g.setColour (Colours::black.withAlpha (0.82f));
        g.fillRoundedRectangle ({ lx, stopRow.getY(), stopW, stopRow.getHeight() }, 4.0f * sy);
        g.setColour (Palette::accent().withAlpha (0.50f));
        g.drawRoundedRectangle ({ lx, stopRow.getY(), stopW, stopRow.getHeight() }, 4.0f * sy, 1.0f);
        aeq::ui::draw (g, stopLabel, { lx, stopRow.getY(), stopW, stopRow.getHeight() },
                       Justification::centred, kMicro * sy, Palette::accentHi(), Face::SemiBold);
    }

    // the caption row, inside the well: what was learnt (left) and the keys (right)
    const auto caption = heroRow (kCaptionTop, kCaptionBottom).toFloat();
    const auto st = proc_.core().state();
    // The separators are UTF-8 bytes spelled out, not a character in the source file:
    // "· " in a literal is at the mercy of the build's source encoding, and the shipped
    // 1.2.26 window reads "AMIR 15 + 13-TAKE CONSENSUS Â· AUTO GAIN" because of it.
    const String dot = String::fromUTF8 ("  \xc2\xb7  ");
    String learned = "HEARD " + String (st.analyzedSeconds, 1) + " S" + dot
                   + String (int (bands_.size())) + " BANDS" + dot + "AMIR 15 + 13-TAKE CONSENSUS";
    // the level match runs by itself, so it is a number on the panel rather than a fudge
    // in the dark: how much gain it is holding to keep the take where it was
    if (proc_.isFrozen() || proc_.isCommitted())
        learned += dot + "AUTO GAIN " + String (proc_.core().levelMatchDb(), 1) + " DB";
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
        g.fillEllipse (rx - w - 16.0f * sy, caption.getCentreY() - 4.0f * sy,
                       8.0f * sy, 8.0f * sy);
        rx -= w + 30.0f * sy;
    }
}

void AeqAudioProcessorEditor::drawChips (Graphics& g, Rectangle<int>) {
    const float sy = float (getHeight()) / kDesignH;

    // Nothing measured yet: the row keeps its four seats.  Dropping the row entirely left
    // a 100 px hole between the well and the footer and made the window look unfinished;
    // a seat with no number in it says "this is where the bands will be" and claims
    // nothing.
    if (chips_.empty()) {
        const auto c = blocks().chips;
        const int gap = roundToInt (10.0f * float (getWidth()) / kDesignW);
        const int w = (c.getWidth() - gap * 3) / 4;
        for (int i = 0; i < 4; ++i) {
            const Rectangle<float> r (float (c.getX() + i * (w + gap)), float (c.getY()),
                                      float (w), float (c.getHeight()));
            g.setColour (Palette::surfaceHi().withAlpha (0.55f));
            g.fillRoundedRectangle (r, kChipR * sy);
            g.setColour (Colours::white.withAlpha (0.07f));
            g.drawRoundedRectangle (r.reduced (0.5f), kChipR * sy, 1.0f);
            trackedText (g, String (i + 1).paddedLeft ('0', 2),
                         { r.getX() + 12.0f * sy, r.getY() + 7.0f * sy, 46.0f * sy, 15.0f * sy },
                         Palette::dim().withAlpha (0.55f), kMicro * sy, 1.3f);
            trackedText (g, "-", { r.getX() + 12.0f * sy, r.getY() + 22.0f * sy,
                                   60.0f * sy, 26.0f * sy },
                         Palette::dim().withAlpha (0.45f), kValue * sy, 0.0f,
                         Justification::centredLeft, Face::SemiBold);
        }
        return;
    }

    const auto rects = chipRects();

    for (int i = 0; i < int (rects.size()) && i < int (chips_.size()); ++i) {
        const auto r = rects[size_t (i)].toFloat();
        const auto chip = chips_[size_t (i)];
        const bool hover = hoverChip_ == i;
        const bool hot = chip.tracked >= 0;

        g.setColour (Palette::surfaceHi().withAlpha (0.92f));
        g.fillRoundedRectangle (r, kChipR * sy);
        g.setColour (hot ? Palette::accent().withAlpha (0.58f)
                         : Colours::white.withAlpha (hover ? 0.16f : 0.12f));
        g.drawRoundedRectangle (r.reduced (0.5f), kChipR * sy, 1.0f);

        // index, and what kind of thing this is
        const int index = hot ? chip.tracked + 1 : chip.band + 1;
        trackedText (g, String (index).paddedLeft ('0', 2),
                     { r.getX() + 12.0f * sy, r.getY() + 7.0f * sy, 46.0f * sy, 15.0f * sy },
                     hot ? Palette::accentHi() : Palette::dim(), kMicro * sy, 1.3f);
        const bool vetoed = ! hot && chip.band >= 0 && chip.band < int (bands_.size())
                            && std::abs (bands_[size_t (chip.band)].gainDb) > 0.5
                            && chip.band < int (trims_.size())
                            && std::abs (bands_[size_t (chip.band)].gainDb
                                         + trims_[size_t (chip.band)]) < 0.05;
        const String mode = hot ? aeq::ui::u8 (kTracked)
                                : aeq::ui::u8 (vetoed ? kOff : kStatic);
        const float mw = aeq::ui::measure (mode, kCap * sy);
        aeq::ui::draw (g, mode, { r.getRight() - 12.0f * sy - mw, r.getY() + 5.0f * sy, mw,
                                  18.0f * sy },
                       Justification::centredRight, kCap * sy,
                       hot ? Palette::accentHi()
                           : (vetoed ? Palette::dim().withAlpha (0.75f) : Palette::muted()));

        // the number: the band's cut, or how far the layer is moving the harmonic now
        // A tracked layer has no fixed frequency - it sits on a harmonic of the note
        // being sung, which is the whole point of it - so the chip names the harmonic
        // and the gain it is applying right now, and never invents a Hz it does not have.
        double gain = 0.0, f0 = 0.0, q = 0.0;
        String micro;
        if (hot) {
            const auto spec = proc_.core().trackedSpec (chip.tracked);
            gain = proc_.core().trackedGainDb (chip.tracked);
            q = spec.q;
            micro = "H" + String (spec.harmonic) + String::fromUTF8 ("  \xc2\xb7  Q ") + String (q, 2);
        } else if (chip.band < int (bands_.size())) {
            const auto& spec = bands_[size_t (chip.band)];
            gain = spec.gainDb + (chip.band < int (trims_.size()) ? trims_[size_t (chip.band)] : 0.0);
            f0 = spec.f0;
            q = spec.q;
        }

        const String num = (gain >= 0.0 ? "+" : "") + String (gain, 1);
        g.setFont (aeq::ui::font (kValue * sy, Face::SemiBold));
        g.setColour (hot ? Palette::accentHi() : Palette::text());
        g.drawText (num, r.withTrimmedLeft (12.0f * sy).withTrimmedTop (22.0f * sy)
                            .withHeight (kValue * sy * 1.3f),
                    Justification::centredLeft, false);
        const float nw = aeq::ui::measure (num, kValue * sy, Face::SemiBold);
        trackedText (g, "dB", { r.getX() + 15.0f * sy + nw, r.getY() + 30.0f * sy,
                                28.0f * sy, 14.0f * sy },
                     Palette::dim(), kMicro * sy, 0.4f);

        if (! hot)
            micro = f0 >= 1000.0
                ? String (f0 / 1000.0, 2) + " kHz" + String::fromUTF8 ("  \xc2\xb7  Q ") + String (q, 2)
                : String (int (f0)) + " Hz" + String::fromUTF8 ("  \xc2\xb7  Q ") + String (q, 2);
        trackedText (g, micro, { r.getRight() - 12.0f * sy - 150.0f * sy,
                                 r.getY() + 31.0f * sy, 150.0f * sy, 14.0f * sy },
                     Palette::dim(), kMicro * sy, 0.9f, Justification::centredRight);

        // a tracked layer is a level, so it gets a meter; a static band has no state
        if (hot) {
            const double lvl = jlimit (0.0, 1.0,
                                       (proc_.core().trackedLevelDb (chip.tracked) + 60.0) / 60.0);
            g.setColour (Palette::take().withAlpha (0.20f));
            g.fillRoundedRectangle (r.getX() + 12.0f * sy, r.getBottom() - 5.0f * sy,
                                    r.getWidth() - 24.0f * sy, 3.0f * sy, 1.5f * sy);
            g.setColour (Palette::take().withAlpha (0.85f));
            g.fillRoundedRectangle (r.getX() + 12.0f * sy, r.getBottom() - 5.0f * sy,
                                    float ((r.getWidth() - 24.0f * sy) * lvl), 3.0f * sy,
                                    1.5f * sy);
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
    const Rectangle<float> pill (s0.getX() - 4.0f * sx, s0.getY(),
                                 s1.getRight() - s0.getX() + 8.0f * sx, s0.getHeight());
    g.setColour (Colours::white.withAlpha (0.05f));
    g.fillRoundedRectangle (pill, kPillR * sy);
    const auto& seg = active == 0 ? s0 : s1;
    const auto segIn = seg.withTrimmedLeft (5.0f * sx).withTrimmedRight (5.0f * sx)
                          .withTrimmedTop (3.0f * sy).withTrimmedBottom (3.0f * sy);
    g.setColour (Palette::accent().withAlpha (0.14f));
    g.fillRoundedRectangle (segIn, (kPillR - 3.0f) * sy);
    g.setColour (Palette::accent().withAlpha (0.62f));
    g.drawRoundedRectangle (segIn, (kPillR - 3.0f) * sy, 1.0f);
    const char* labels[] = { kModeRestore, kModeMatch };
    for (int i = 0; i < 2; ++i) {
        const auto r = i == 0 ? s0 : s1;
        const String s = aeq::ui::u8 (labels[i]);
        aeq::ui::draw (g, s, r, Justification::centred, kCap * sy,
                       i == active ? Palette::accentHi() : Palette::muted());
    }

    // One sentence, then nothing else - the report card is gone.  It wraps: at the panel's
    // true type size (1.2.27 made a "15 px" label a 15 px em) these sentences are ~100
    // characters, and a single line of them ran straight through the dial labels.  The box
    // is measured, and the screenshot test asserts the sentence fits it - the failure was
    // invisible to every gate for three versions because no gate measured a sentence.
    const auto copyBox = footerCopyBox();
    const auto lines = aeq::ui::wrapWords (verdictLine(), kCap * sy, copyBox.getWidth());
    const float pitch = aeq::ui::linePitch (kCap * sy);
    const float top = copyBox.getCentreY() - float (lines.size()) * pitch * 0.5f;
    for (size_t i = 0; i < lines.size(); ++i) {
        const Rectangle<float> row (copyBox.getX(), top + float (i) * pitch,
                                    copyBox.getWidth(), pitch);
        aeq::ui::draw (g, lines[i], row, Justification::centredLeft, kCap * sy,
                       Palette::muted());
    }

    // the two things a hand turns: the knobs themselves are child sliders drawn by
    // the look and feel, so this only writes their two labels and their two numbers
    const Point<float> k2 (float (area.getRight()) - 34.0f * sx, cy);
    const Point<float> k1 (float (area.getRight()) - 202.0f * sx, cy);
    const float knobSize = 42.0f * sy;          // the body; the box below adds the ring

    auto labelFor = [&] (const char* s, Point<float> c, float dx) {
        const String t2 = aeq::ui::u8 (s);
        const float w = aeq::ui::measure (t2, kMicro * sy);
        aeq::ui::draw (g, t2, { c.x - dx - w, cy - 20.0f * sy, w, kCap * sy * 1.35f },
                       Justification::centredRight, kCap * sy, Palette::muted());
    };
    const Point<float> k0 (float (area.getRight()) - 370.0f * sx, cy);
    labelFor (kStabiliser, k0, knobSize * 0.62f + 8.0f * sy);
    labelFor (kStrength, k1, knobSize * 0.62f + 8.0f * sy);
    labelFor (kAutoStop, k2, knobSize * 0.62f + 8.0f * sy);

    g.setFont (aeq::ui::font (kValue * sy, Face::SemiBold));
    g.setColour (Palette::text());
    const String v1 = String (roundToInt (amountKnob_.getValue())) + "%";
    const String v2 = roundToInt (settleKnob_.getValue()) >= 1
                        ? String (roundToInt (settleKnob_.getValue())) + " s"
                        : String ("OFF");
    // the label and the value are one block beside the dial, and the value box is
    // measured from the type: at 24 px in an 18 px box the bottom of every number was
    // clipped by the row it sat in
    g.drawText (v1, Rectangle<float> (k1.x - 40.0f * sx - 60.0f * sy, cy - 3.0f * sy,
                                      60.0f * sy, kValue * sy * 1.25f),
                Justification::centredRight, false);
    g.drawText (v2, Rectangle<float> (k2.x - 40.0f * sx - 60.0f * sy, cy - 3.0f * sy,
                                      60.0f * sy, kValue * sy * 1.25f),
                Justification::centredRight, false);
    // the stabiliser dial shows its own doing: the amount it is set to, and under it the
    // dB the ten bands are actually taking off right now.  A dial that moves a layer
    // nobody can see is the "unclear" the panel kept being reported for.
    const String v0 = String (roundToInt (stableKnob_.getValue())) + "%";
    g.drawText (v0, Rectangle<float> (k0.x - 40.0f * sx - 60.0f * sy, cy - 3.0f * sy,
                                      60.0f * sy, kValue * sy * 1.25f),
                Justification::centredRight, false);
    if (proc_.core().stabilizerBands() > 0) {
        const String live = String (proc_.core().stabilizerTotalDb(), 1) + " dB";
        aeq::ui::draw (g, live, { k0.x - 74.0f * sy, cy + 11.0f * sy, 74.0f * sy, kCap * sy },
                       Justification::centredRight, kCap * sy,
                       proc_.core().stabilizerTotalDb() < -0.5 ? Palette::accent()
                                                               : Palette::dim());
    }
}

// --------------------------------------------------------------------------- copy
bool AeqAudioProcessorEditor::hasTrackedLayer() const {
    return proc_.core().trackedCount() > 0;
}

// The state of the pass, as the sentences need it.  One place builds it, so the pill, its
// sub-line and the footer cannot answer the same question differently.
aeq::copy::Facts AeqAudioProcessorEditor::panelFacts() const {
    const auto st = proc_.autoState();
    aeq::copy::Facts f;
    f.haveData   = haveData_;
    f.frozen     = proc_.isFrozen();
    f.committed  = proc_.isCommitted();
    f.settled    = state_.settled;
    f.bands      = int (bands_.size());
    f.heardSeconds = st.heardSeconds;
    f.trustSeconds = st.needSeconds;
    return f;
}

String AeqAudioProcessorEditor::verdictLine() const {
    return String (aeq::copy::verdict (panelFacts()));
}

Rectangle<float> AeqAudioProcessorEditor::footerCopyBox() const {
    const float sy = float (getHeight()) / kDesignH;
    const float sx = float (getWidth()) / kDesignW;
    const auto area = blocks().footer.toFloat();
    const auto s0 = modeSeg (0).toFloat(), s1 = modeSeg (1).toFloat();
    const float pillRight = s1.getRight() + 4.0f * sx;
    const float knobsX = area.getRight() - 370.0f * sx - 110.0f * sx;
    return { pillRight + 24.0f * sx, area.getY(),
             jmax (120.0f, knobsX - pillRight - 40.0f * sx), area.getHeight() };
}

bool AeqAudioProcessorEditor::footerCopyFits (const String& line) const {
    // The same wrap, the same pitch and the same box the paint code uses.  Not the font's
    // own line box: at 1.874 em per line, two lines of 19 px type measure 72 px in a 52 px
    // row and the sentence would "not fit" while drawing perfectly - the pitch is the
    // number that decides whether lines collide.
    const float sy = float (getHeight()) / kDesignH;
    const auto box = footerCopyBox();
    if (box.getWidth() < 40.0f || box.getHeight() < 8.0f) return false;
    if (line.isEmpty()) return true;
    const auto lines = aeq::ui::wrapWords (line, kCap * sy, box.getWidth());
    const float needed = float (lines.size()) * aeq::ui::linePitch (kCap * sy);
    return needed <= box.getHeight() + 0.5f;
}

bool AeqAudioProcessorEditor::footerCopyFits() const { return footerCopyFits (verdictLine()); }

// --------------------------------------------------------------------------- layout
void AeqAudioProcessorEditor::resized() {
    const auto b = blocks();
    const float sy = float (getHeight()) / kDesignH;

    curve_.setBounds (well());

    const float sx = float (getWidth()) / kDesignW;
    const int cy = b.footer.getCentreY();
    const int box = roundToInt (54.0f * sy);    // body (42) + ring + seat
    const int c0 = b.footer.getRight() - roundToInt (370.0f * sx);
    const int c1 = b.footer.getRight() - roundToInt (202.0f * sx);
    const int c2 = b.footer.getRight() - roundToInt (34.0f * sx);
    stableKnob_.setBounds (c0 - box / 2, cy - box / 2, box, box);
    amountKnob_.setBounds (c1 - box / 2, cy - box / 2, box, box);
    settleKnob_.setBounds (c2 - box / 2, cy - box / 2, box, box);
}

// --------------------------------------------------------------------------- input
void AeqAudioProcessorEditor::mouseMove (const MouseEvent& e) {
    const int chip = chipAt (e.getPosition());
    const bool clamp = clampHit().contains (e.getPosition());
    const bool drag = chip >= 0 && chipDraggable (size_t (chip));
    if (chip != hoverChip_ || clamp != hoverClamp_) {
        hoverChip_ = chip;
        hoverClamp_ = clamp;
        // An up-down cursor over something that does not move when you drag it is the
        // shape of "the buttons do not work": a tracked layer has no frequency to drag,
        // so it does not advertise a drag.
        setMouseCursor (clamp ? MouseCursor::PointingHandCursor
                              : (drag ? MouseCursor::UpDownResizeCursor
                                      : (chip >= 0 ? MouseCursor::PointingHandCursor
                                                   : MouseCursor::NormalCursor)));
        tooltip_ = tooltipAt (e.getPosition());
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
    grabKeyboardFocus();          // space and escape only work once the window has focus

    // The clamp: stop when it is listening, open the take again when it is sealed.  It
    // used to call relearn() when sealed, i.e. one accidental click after a good take
    // threw the whole pass away.  Reopening keeps every sample and every band; the reset
    // icon is the one that forgets.
    if (clampHit().contains (p)) {
        if (proc_.isFrozen()) {
            // open the take again: same samples, same correction, and the analysis
            // resumes (core_.reopen() alone left the processor not listening, so the
            // panel said "open" over a pass that had stopped counting)
            proc_.reopenPass();
            stoppedByUser_ = false;
        } else {
            proc_.stopLearning();
            stoppedByUser_ = true;
        }
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
        // Forget the take and listen again.  This used to call requestReset(), which
        // cleared the core but left the processor not learning: one reset after one stop
        // and the plugin never analysed anything again for the rest of the session.
        proc_.relearn();
        proc_.core().clearTrims();
        haveData_ = false;
        stoppedByUser_ = false;
        sealedAt_ = 0.0;
        lastVersion_ = -1;
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
    if (e.mods.isPopupMenu()) {
        // right click: hand this band back, or take it away again.  The trim is the
        // user's own offset, so "off" is an offset of -gain - no second mechanism, and
        // double click still returns it to the solver's answer.
        if (chip >= 0 && chip < int (chips_.size()) && chips_[size_t (chip)].band >= 0) {
            const int band = chips_[size_t (chip)].src;
            if (band < 0) return;
            if (band < int (bands_.size())) {
                const double spec = bands_[size_t (band)].gainDb;
                const double trim = band < int (trims_.size()) ? trims_[size_t (band)] : 0.0;
                const bool off = std::abs (spec + trim) < 0.05 && std::abs (spec) > 0.5;
                proc_.core().setBandTrim (band, off ? 0.0 : -spec);
                repaint();
            }
        }
        return;
    }
    if (chip >= 0 && chip < int (chips_.size()) && chips_[size_t (chip)].band >= 0) {
        // the row position is not the band index: bands at 0 dB are not drawn, so a drag
        // has to be addressed by the index the core knows (`src`)
        const auto& c = chips_[size_t (chip)];
        if (c.src < 0) return;                       // a resonance/manual band has no trim
        draggedChip_ = c.src;
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
    if (chip >= 0 && chip < int (chips_.size()) && chips_[size_t (chip)].src >= 0) {
        proc_.core().setBandTrim (chips_[size_t (chip)].src, 0.0);
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

    // activeBands() also carries the corpus' taste bands; at rest they are all 0 dB and
    // the cascade skips them.  Show the bands that are doing something, so the count on
    // the caption, the chips and the handles all agree with appliedBandCount().
    // Bands at 0 dB are not drawn (they would be a row of handles lying on the 0 line),
    // so the drawn index and the core's index diverge.  Keep both: `src` is the one
    // setBandTrim() and bandTrim() take.
    bands_.clear();
    chips_.clear();
    trims_.clear();
    {
        const auto all = proc_.core().activeBands();
        std::vector<double> gains;
        gains.reserve (all.size());
        for (const auto& b : all) gains.push_back (b.gainDb);
        for (const auto& row : aeq::bands::visible (gains.data(), int (gains.size()),
                                                   proc_.core().correctiveCount())) {
            bands_.push_back (all[size_t (row.index)]);
            chips_.push_back ({ int (bands_.size()) - 1, -1, row.src });
            trims_.push_back (row.src >= 0 ? proc_.core().bandTrim (row.src) : 0.0);
        }
    }
    haveData_ = state_.valid;

    // ... then the dynamic layers that are actually running
    for (int i = 0; i < proc_.core().trackedCount(); ++i) chips_.push_back ({ -1, i, -1 });


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
    d.sampleRate = proc_.getSampleRate() > 8000.0 ? proc_.getSampleRate() : 48000.0;
    d.analysedSeconds = state_.analyzedSeconds;
    curve_.setData (std::move (d));

    // only the parts of the window that show live state are repainted by this timer
    repaint();
}
