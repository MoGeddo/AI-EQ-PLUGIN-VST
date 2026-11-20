#include "PluginEditor.h"
#include "ui/BandRow.h"
#include "ui/KnobSpec.h"
#include "ui/Controls.h"
#include "ui/Logo.h"
#include "ui/Wordmark.h"
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
// px of extra space between glyphs on the three Latin readouts (the scan strip, the layer
// line and the auto line).  One number, so the width a box reserves and the width that is
// drawn cannot disagree.
constexpr float kReadoutTrack = 1.1f;
// and the slack in a readout's box: trackedText wraps by word, so a box that is a pixel
// short of the layout's advance prints a second line - the 1.2.47 empty window did.
constexpr float kReadoutSlack = 40.0f;
// How far inside a plate's own edge its right-aligned rows stop.  Every right-aligned line
// on this panel used to end exactly on the well's border, which reads as truncated type.
constexpr float kWellInset = 10.0f;
// The footer dials are lifted off the row's centre line so the plate's bottom edge and its
// 16 px corner do not cut them: at 54 px, centred, the dial's lower arc was outside the plate.
constexpr float kKnobRowUp = 8.0f;

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
// ... and the third line the graph draws, which had no name on the panel at all: the white
// line that starts at 100 Hz is the reference curve, not the take
const char* kReferenceKey = "reference";
const char* kDecisionKey = "decision";
const char* kStatic    = "STATIC";
const char* kTracked   = "TRACKED";
const char* kEmptyChip = "EMPTY";
const char* kOff       = "OFF";        // a band the user has handed back
const char* kModeRestore = "RESTORE";
const char* kModeMatch   = "MATCH";
// "· " as UTF-8 bytes spelled out, not a character in the source file: the shipped
// 1.2.26 window reads "CONSENSUS Â· AUTO GAIN" because a literal was at the mercy of the
// build's source encoding.  One copy, used by every line that separates two facts.
const char* kMidDot    = "  \xc2\xb7  ";
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

// The advance a tracked line actually takes.  `aeq::ui::measure` returns the *untracked*
// width, and JUCE 8 deprecates Font::getStringWidthFloat - a TextLayout with no wrap is the
// same shaping the draw call makes, so it is the number the next word should be placed past.
float trackedInk (const String& s, float size, float tracking, Face face) {
    AttributedString as;
    as.append (s.toUpperCase(),
               aeq::ui::font (size, face).withExtraKerningFactor (tracking / size),
               Colours::white);
    TextLayout lay;
    lay.createLayout (as, 10000.0f);
    return lay.getWidth();
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
    // The whole radial budget comes from ui/KnobSpec.h - cap, knurled skirt, seat, ring,
    // scale - so that every part of it is inside the square the slider owns and the ring is
    // never crossed by the ticks.  test_dials.cpp holds those relations for every box size.
    //
    // The anatomy is the reference's (dribbble.com/AudioUi, VST kit 01): a dark machined
    // cap, a **knurled skirt** you can see the ridges of, a **thick** coloured arc ring
    // hugging the cap, and a scale only on the dials large enough to carry one.
    const auto k = aeq::ui::knobMetrics (box);
    const float r = k.body;
    const float value = jlimit (0.0f, 1.0f, proportion);
    const auto accent = Palette::accent();
    const auto hot = Palette::accentHi();

    // ---- seat: the recess the whole part sits in, shadowed at the top so the part reads
    // as *in* the plate rather than on it
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillEllipse (centre.x - k.seat, centre.y - k.seat + 1.0f, k.seat * 2, k.seat * 2);
    g.setColour (Palette::hairline().withAlpha (0.45f));
    g.drawEllipse (centre.x - k.seat, centre.y - k.seat, k.seat * 2, k.seat * 2, 1.0f);

    // ---- knurled skirt: 40 ridges, lit from the top.  This is the one detail that says
    // "turned by a hand" at 54 px, and it is why the reference's small knobs read as
    // hardware while ours read as a circle.
    if (aeq::ui::knobHasKnurl (box)) {
        // Valleys first, then ridges: white at 2-7 % of a near-black plate is not a detail,
        // it is nothing - the 1.2.61 shot still shows a smooth circle.  The grooves are cut
        // in black at 45 %, the ridges are lit white up to 30 % on the top half, and the
        // band is either side of the cap's own edge so the part reads as turned metal.
        for (int i = 0; i < k.knurlCount; ++i) {
            const float a = MathConstants<float>::twoPi * float (i) / float (k.knurlCount);
            const float dx = std::sin (a), dy = -std::cos (a);
            const Line<float> grooveLine (centre.translated (dx * k.knurlIn, dy * k.knurlIn),
                                          centre.translated (dx * k.knurlOut, dy * k.knurlOut));
            g.setColour (Colours::black.withAlpha (0.45f));
            g.drawLine (grooveLine, k.knurlStroke * 1.15f);
            // light comes from above: a ridge faces the light when its normal points up
            const float lit = 0.5f + 0.5f * (-dy);
            g.setColour (Colours::white.withAlpha (0.06f + 0.24f * lit * lit));
            g.drawLine (Line<float> (grooveLine.getStart().translated (k.knurlStroke * 0.5f * dx,
                                                                       k.knurlStroke * 0.5f * dy),
                                     grooveLine.getEnd().translated (k.knurlStroke * 0.5f * dx,
                                                                     k.knurlStroke * 0.5f * dy)),
                        k.knurlStroke * 0.55f);
        }
    }

    // ---- cap: radial, darker at the rim, with a specular arc on the lit side
    ColourGradient body (Palette::surfaceHi().brighter (0.12f), centre.x, centre.y - r * 0.40f,
                         Palette::bg().darker (0.30f), centre.x, centre.y + r, false);
    g.setGradientFill (body);
    g.fillEllipse (centre.x - r, centre.y - r, r * 2, r * 2);
    g.setColour (Colours::white.withAlpha (hover ? 0.26f : 0.16f));
    g.drawEllipse (centre.x - r + 0.5f, centre.y - r + 0.5f, r * 2 - 1.0f, r * 2 - 1.0f, 1.0f);
    g.setColour (hot.withAlpha (0.14f));
    Path cap;
    // the specular highlight belongs on the lit side of the part, which is the top
    cap.addCentredArc (centre.x, centre.y, r * 0.82f, r * 0.82f, 0.0f,
                       -MathConstants<float>::pi * 0.34f,
                       MathConstants<float>::pi * 0.34f, true);
    g.strokePath (cap, PathStrokeType (box * 0.026f));

    // ---- the value ring: the travel in the plate's ink, the value in the accent.  It hugs
    // the cap, thicker than the old hairline groove, because on the reference the ring is
    // the number - there is no other place the eye can read a setting from.
    const float ringR = k.groove;
    Path groove;
    groove.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                          degreesToRadians (-135.0f), degreesToRadians (135.0f), true);
    // The travel is a real groove, not a hairline: at #39404d over #1f242d a 1 px line is
    // invisible at 54 px, and an invisible ring means the dial has no read-out at all.  The
    // reference's dials keep an unlit track that is clearly darker than the cap.
    g.setColour (Colours::black.withAlpha (0.60f));
    g.strokePath (groove, PathStrokeType (k.grooveStroke + 1.0f, PathStrokeType::curved,
                                          PathStrokeType::rounded));
    g.setColour (Palette::muted().withAlpha (0.28f));
    g.strokePath (groove, PathStrokeType (k.grooveStroke * 0.45f, PathStrokeType::curved,
                                          PathStrokeType::rounded));

    // The arc runs from the *start of the travel* to the value, not from the value to 12
    // o'clock: JUCE measures angles from the top, clockwise, so the travel is -135..+135
    // and the lit part is -135..a.
    const float a = degreesToRadians (-135.0f + 270.0f * value);
    Path arc;
    arc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                       degreesToRadians (-135.0f), a, true);
    // At zero the lit arc is a zero-length stroke, i.e. one round dot on the ring's start -
    // on the 1.2.54 shot that read as a stray pink speck beside the knob.  A dial at zero
    // has nothing lit, so the start marker is drawn in the ring's own ink instead.
    // A 3 px arc cannot carry the fill orange: #EC4F02 over a near-black plate reads brown
    // (which is what the 1.2.59 shot shows on the auto-stop dial).  The reference's small
    // knobs draw their ring in the *bright* end of their orange - #FF5100 on kit 01 - so the
    // lit arc is the accent lifted toward its own highlight, at full alpha.
    g.setColour (value < 0.02f ? Palette::hairline() : accent.interpolatedWith (hot, 0.55f));
    g.strokePath (arc, PathStrokeType (k.grooveStroke, PathStrokeType::curved,
                                       PathStrokeType::rounded));

    // ---- the scale, only where it can be read: outside the ring, clear of it
    if (aeq::ui::knobHasTicks (box)) {
        for (int i = 0; i <= 10; ++i) {
            const float ta = degreesToRadians (-135.0f + 27.0f * float (i));
            const bool major = (i % 5 == 0);
            const float inner = major ? k.majorIn : k.tickIn;
            const float dx = std::sin (ta), dy = -std::cos (ta);
            g.setColour ((major ? Palette::text() : Palette::muted()).withAlpha (major ? 0.90f
                                                                                       : 0.45f));
            g.drawLine (Line<float> (centre.translated (dx * inner, dy * inner),
                                     centre.translated (dx * k.tickOut, dy * k.tickOut)),
                        major ? k.majorStroke : k.minorStroke);
        }
    }

    // ---- the screw at the bottom of the seat: a detail from the reference's panels, and
    // it also anchors the part - a dial whose seat has no fastener reads as a sticker
    g.setColour (Colours::black.withAlpha (0.65f));
    g.fillEllipse (centre.x - box * 0.020f, centre.y + k.seat - box * 0.032f,
                   box * 0.040f, box * 0.040f);
    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawLine (centre.x - box * 0.012f, centre.y + k.seat - box * 0.012f,
                centre.x + box * 0.012f, centre.y + k.seat - box * 0.012f, 1.0f);

    // ---- needle, from the centre out: never a dot in the middle
    // A pointer that starts *off* the centre, like the reference's: a line from the middle
    // out, with a small round head - not a lollipop.  At 54 px the old 6 px head plus a
    // 3.2 px shaft was half the cap's width and read as a stick with a bead on it.
    const Point<float> tail (centre.x + std::sin (a) * r * 0.18f,
                             centre.y - std::cos (a) * r * 0.18f);
    const Point<float> tip (centre.x + std::sin (a) * r * 0.70f,
                            centre.y - std::cos (a) * r * 0.70f);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.drawLine (Line<float> (tail.translated (0.0f, 1.2f), tip.translated (0.0f, 1.2f)),
                box * 0.055f);
    g.setColour (hot);
    g.drawLine (Line<float> (tail, tip), box * 0.044f);
    g.fillEllipse (tip.x - box * 0.040f, tip.y - box * 0.040f, box * 0.080f, box * 0.080f);
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
        // Press = 0.96 scale, the craft-floor number, and it is a *transform*: nothing is
        // re-laid-out, nothing below moves, and it costs one draw.  Hover is the ring.
        const bool down = s.isMouseButtonDown();
        const float scale = down ? 0.96f : 1.0f;
        const float box = float (jmin (width, height)) * scale;
        drawKnob (g, { x + width * 0.5f, y + height * 0.5f }, box,
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
// The design height is 700 px.  On a 768 px laptop that is the whole screen: the host's
// own title bar, the taskbar and the plugin's frame do not fit, and the bottom row - the
// floor where the verdict and the two dials live - is off the display.  The previous
// version hard-coded the design size and a layout gate caught it: "the default window
// size does not consult the display".  Now the height is the design height, capped to
// what is actually available, and never below the point where the layout stops working.
int AeqAudioProcessorEditor::defaultHeight()
{
    constexpr int kFloor = 560;          // below this the chip row clips its own type
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto usable = d->userArea.getHeight() - 90;   // host chrome + taskbar
        return juce::jlimit (kFloor, int (kDesignH), usable);
    }
    return int (kDesignH);               // no display (tests, headless render): the design
}

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

// The mode pills are drawn 26 px tall, and a 26 px target is a target a hand misses.
// The *hit* rectangle is therefore padded out to the 40 px floor while the *drawn*
// rectangle stays where the design put it - the pointer gets the slack, the picture does
// not move.  (make-interfaces-feel-better: hit areas 44/40; impeccable: the thing you
// press must be the thing you see plus slack, never a different shape.)
Rectangle<int> AeqAudioProcessorEditor::modeSegDraw (int i) const {
    const auto f = blocks().footer;
    const int h = roundToInt (26.0f * float (getHeight()) / kDesignH);
    const int w = roundToInt (90.0f * float (getWidth()) / kDesignW);
    return { f.getX() + i * w, f.getCentreY() - h / 2, w, h };
}

Rectangle<int> AeqAudioProcessorEditor::modeSeg (int i) const {
    auto r = modeSegDraw (i);
    const int minH = roundToInt (40.0f * float (getHeight()) / kDesignH);
    const int slop = roundToInt (3.0f * float (getWidth()) / kDesignW);
    if (r.getHeight() < minH)
        r = r.withSizeKeepingCentre (r.getWidth(), minH);
    return r.expanded (slop, 0);
}

Rectangle<int> AeqAudioProcessorEditor::bypassHit() const {
    const auto hd = blocks().header;
    const int s = roundToInt (40.0f * float (getHeight()) / kDesignH);
    // 106 design px from the right edge, not 78: the key is 34 px in a 40 px target and the
    // panel's corner radius is 16, so at 78 the key sat *on* the rounded corner and hung
    // outside the plate (the 1.2.53 shot shows it half off the window).
    return { hd.getRight() - roundToInt (106.0f * float (getWidth()) / kDesignW),
             hd.getCentreY() - s / 2, s, s };
}

Rectangle<int> AeqAudioProcessorEditor::resetHit() const {
    auto b = bypassHit();
    b.translate (-roundToInt (44.0f * float (getWidth()) / kDesignW), 0);
    return b;
}

// The reference pill: what Match is aiming at, and the way to change it.  It sits in the
// header's empty middle, left of the seal cluster, and it is drawn 30 design px tall - so
// its *hit* box is padded to the 40 px floor exactly like the mode pills, and the pointer
// gets the slack while the picture stays where the grid put it.
Rectangle<int> AeqAudioProcessorEditor::referenceHit() const {
    const auto hd = blocks().header;
    const float sx = float (getWidth()) / kDesignW;
    const float sy = float (getHeight()) / kDesignH;
    const int w = roundToInt (160.0f * sx);
    const int h = roundToInt (30.0f * sy);
    Rectangle<int> r (hd.getX() + roundToInt (620.0f * sx),
                      hd.getCentreY() - h / 2 - roundToInt (6.0f * sy), w, h);
    const int minH = roundToInt (40.0f * sy);
    if (r.getHeight() < minH) r = r.withSizeKeepingCentre (r.getWidth(), minH);
    return r;
}

// The seal pill, answerable from outside paint(): the scan badge is placed against it,
// and the sub-line is placed against its bottom edge.
Rectangle<int> AeqAudioProcessorEditor::sealPill() const {
    const float sy = float (getHeight()) / kDesignH;
    const auto hd = blocks().header;
    const String sealText = String (aeq::copy::stateWord (panelFacts()));
    const float textW = aeq::ui::measure (sealText, kCap * sy);
    const float pillW = textW + 44.0f * sy;
    const auto rs = resetHit();
    return { roundToInt (float (rs.getX()) - pillW - 10.0f * sy),
             hd.getY() + roundToInt (4.0f * sy), roundToInt (pillW), roundToInt (30.0f * sy) };
}

// The tagline's ink (not the 625 px box it is laid out in): a line placed to the right of
// this can be proven not to run into the one sentence that names the product.
Rectangle<float> AeqAudioProcessorEditor::taglineInk() const {
    const float sy = float (getHeight()) / kDesignH;
    const auto h = blocks().header.toFloat();
    const float hsel = h.getHeight() / 52.0f;
    const float markW = 46.0f * hsel * aeq::ui::Logo::aspect();
    const float markRight = h.getX() + markW;
    const float ws = kBody * sy;
    // 8 px under the name, not 2: the scan badge sits on the name/pill row, and the tagline
    // starts below the badge's bottom edge (34*sy) at every window size.  Two rows that can
    // collide at one aspect ratio are two rows that will collide at one aspect ratio.
    const float tagY = h.getY() + 4.0f * sy + ws * 1.15f + 8.0f * sy;
    return { markRight + 10.0f * sy, tagY,
             aeq::ui::measure (kTagline, kCap * sy) + 2.0f * sy, kCap * sy * 1.35f };
}

// The ink of "GEDDO AEQ": the two words of the name, from the mark's right edge to the
// end of the second word's advance.  The scan badge has to sit right of this and left of
// the reference pill, and "it looked fine on my screen" is not a measurement.
Rectangle<float> AeqAudioProcessorEditor::wordmarkInk() const {
    const float sy = float (getHeight()) / kDesignH;
    const auto h = blocks().header.toFloat();
    const float hsel = h.getHeight() / 52.0f;
    const float markW = 46.0f * hsel * aeq::ui::Logo::aspect();
    const float ws = kBody * sy;
    constexpr float kTrack = 3.4f;
    const float wordX = h.getX() + markW + 12.0f * hsel;
    const float w1 = aeq::ui::measure ("GEDDO", ws, Face::SemiBold);
    const float x2 = wordX + aeq::ui::secondWordX (w1, 5, kTrack * sy, 22.0f * sy);
    const float w2 = aeq::ui::measure ("AEQ", ws, Face::Regular) + 5.0f * kTrack * sy;
    return { wordX, h.getY() + 4.0f * sy, x2 + w2 - wordX, ws * 1.15f };
}

// ---- the scan, and what runs by itself --------------------------------------
// Four readouts, in the order a person asks: has it scanned (header badge), how much
// (the strip in the caption), what is frozen and what moves (the row above the take),
// and what happens without a hand (the line under the seal).
String AeqAudioProcessorEditor::scanBadgeText() const {
    return String (aeq::copy::scanWord (panelFacts()));
}

Rectangle<int> AeqAudioProcessorEditor::scanBadge() const {
    const float sy = float (getHeight()) / kDesignH;
    const float sx = float (getWidth()) / kDesignW;
    const auto hd = blocks().header;
    // the LED, the word, and the padding either side of them
    const float w = 18.0f * sy + aeq::ui::measure (scanBadgeText(), kCap * sy) + 12.0f * sy;
    return { referenceHit().getX() - roundToInt (w) - roundToInt (14.0f * sx),
             hd.getY() + roundToInt (4.0f * sy), roundToInt (w), roundToInt (30.0f * sy) };
}

String AeqAudioProcessorEditor::scanStripText() const {
    return String (aeq::copy::scanLine (panelFacts()));
}

String AeqAudioProcessorEditor::layerReadoutText() const {
    return String (aeq::copy::layerLine (panelFacts()));
}

Rectangle<int> AeqAudioProcessorEditor::layerReadout() const {
    const String s = layerReadoutText();
    if (s.isEmpty()) return {};
    const float sy = float (getHeight()) / kDesignH;
    const auto row = heroRow (kStopTop, kStopBottom);
    // the tracked width, not the untracked one: a box narrower than the line it holds is
    // how a right-aligned label ends up hanging off its own left edge
    const int w = roundToInt (aeq::ui::measure (s, kMicro * sy, Face::SemiBold)
                              + kReadoutTrack * float (jmax (0, s.length() - 1))
                              + kReadoutSlack * sy);
    // 10 px inside the plate's own edge, not on it: the line is right-aligned, and a
    // right-aligned line whose last glyph lands on the well's border reads as a line that
    // was cut off (the 1.2.56 shot shows "STILL" touching it).  The legend below uses the
    // same inset, so both rows end on one vertical line.
    return { row.getRight() - roundToInt (kWellInset * float (getWidth()) / kDesignW) - w,
             row.getY(), w, row.getHeight() };
}

String AeqAudioProcessorEditor::autoReadoutText() const {
    const auto f = panelFacts();
    // Before there is a take, there is nothing that runs: the line is the invitation.
    // After one exists it is the answer to "automatic?" in one sentence, and only that:
    // "curve frozen  ·  AUTO: ..." put two clauses on one line whose right edge is the
    // seal pill, and at the window's taller-than-design aspect ratios the pair ran into
    // the tagline by a handful of pixels.  The frozen half still has a home - the
    // footer's verdict says it, in the sentence the user already reads there.
    if (! f.haveData) return String (aeq::copy::stateSub (f));
    return String (aeq::copy::autoLine (f, stopSettingShown()));
}

Rectangle<int> AeqAudioProcessorEditor::autoReadout() const {
    const float sy = float (getHeight()) / kDesignH;
    const auto f = panelFacts();
    const String s = autoReadoutText();
    const float w = aeq::ui::measure (s, kMicro * sy)
                    + kReadoutTrack * float (jmax (0, s.length() - 1)) + kReadoutSlack * sy;
    const auto pill = sealPill();
    return { pill.getRight() - roundToInt (w),
             pill.getBottom() + roundToInt (4.0f * sy), roundToInt (w),
             roundToInt (kMicro * sy * 1.35f) };
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
        case Control::Reference:   return referenceHit();
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
    if (referenceHit().contains (p))     return Control::Reference;
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
    if (referenceHit().contains (p)) {
        const auto name = proc_.referenceLabel();
        if (refNote_.isNotEmpty()) return refNote_ + " (right click to hand it back)";
        return name.isNotEmpty()
                 ? "Reference: " + name + " - Match aims here. Click to change, right click to clear"
                 : String ("Load a reference profile (.aeqprof) for Match to aim at");
    }
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
        const auto second = Palette::takeInk();
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

void AeqAudioProcessorEditor::pickReference() {
    // Two starting points: the project folder if the host gave one, else the last folder a
    // profile came from.  A file dialog that opens at "/" is a dialog that makes the user
    // find their own project again.
    juce::File start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    refChooser_ = std::make_unique<juce::FileChooser> ("Reference profile", start, "*.aeqprof");
    refChooser_->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f == juce::File{}) return;                 // cancelled is not a failure
        const bool ok = proc_.loadReferenceProfile (f);
        if (ok) {
            // A reference is a Match instruction: aim at this.  Leaving the panel in
            // Restore while a record is loaded would look like the file did nothing.
            if (auto* param = proc_.apvts().getParameter ("mode"))
                param->setValueNotifyingHost (1.0f);
            refNote_ = "Reference loaded: " + f.getFileNameWithoutExtension();
        } else {
            refNote_ = f.getFileName() + " is not an .aeqprof file";
        }
        lastVersion_ = -1;
        repaint();
    });
}

void AeqAudioProcessorEditor::drawHeader (Graphics& g, Rectangle<int> hd) {
    const float sy = float (getHeight()) / kDesignH;
    const auto h = hd.toFloat();

    // Two rows a side, placed from the header's own top edge rather than from its centre:
    // the type grew a step (1.2.26) and lines hung off the centre started overlapping the
    // moment one of them got taller - the tagline moved up into the wordmark.
    //
    // The mark is his: the calligraphic wordmark from mostafageddo.com, embedded as the
    // picture his site serves and drawn tinted (Source/ui/Logo.h).  The name is the words
    // beside it, so a browser, a DAW list and the installer all read "Geddo AEQ" - the
    // picture cannot be searched for, and a plugin that only shows a mark is a plugin
    // nobody can find.
    const float hsel = h.getHeight() / 52.0f;    // the header row is 52 design px tall
    const float markH = 46.0f * hsel;
    const float markW = markH * aeq::ui::Logo::aspect();
    const auto markBox = juce::Rectangle<float> (h.getX(), h.getY() + 3.0f * hsel, markW, markH);
    aeq::ui::Logo::draw (g, markBox, Palette::text());

    const float ws = kBody * sy;
    const float wordX = markBox.getRight() + 12.0f * hsel;
    const auto markRow = h.withTop (h.getY() + 4.0f * sy).withHeight (ws * 1.15f);
    constexpr float kTrack = 3.4f;      // px of extra space between glyphs, at design size
    // measured untracked, then placed after the advance the draw call actually makes -
    // the ink width and the advance are not the same number, and using one for the other
    // ran the words together (see Wordmark.h)
    const float w1 = aeq::ui::measure ("GEDDO", ws, Face::SemiBold);
    g.setFont (aeq::ui::font (ws, Face::SemiBold).withExtraKerningFactor (kTrack / ws));
    g.setColour (Palette::accentHi());
    g.drawText ("GEDDO", markRow.translated (wordX, 0.0f), Justification::centredLeft, false);
    g.setFont (aeq::ui::font (ws, Face::Regular).withExtraKerningFactor (kTrack / ws));
    g.setColour (Palette::text().withAlpha (0.72f));
    // 22 px of gap, not 10: the advance `drawText` makes for the first word carries the
    // right sidebearing of its last glyph, so a gap that reads as a space on the glass has
    // to be about twice the space the arithmetic asks for.  Measured, not assumed - 8 px of
    // gap measured 1 px between the words in 1.2.38 and 12 px measured 2 px in 1.2.39.
    g.drawText ("AEQ", markRow.translated (wordX + aeq::ui::secondWordX (w1, 5, kTrack * sy, 22.0f * sy), 0.0f),
                Justification::centredLeft, false);

    // tagline, one line, under the mark - drawn in its own ink box, which is the same
    // rectangle the scan badge and the sub-line are proven not to run into
    arabic (g, kTagline, taglineInk(), Palette::muted(), kCap * sy);

    // right cluster: master bypass, reset, the seal pill with its LED, and the promise.
    // The pill's words come from panelFacts() now, so this function no longer decides
    // what "sealed" means on its own.
    const bool bypassed = proc_.apvts().getRawParameterValue ("bypass")->load() > 0.5f;
    // ---- two real switches, not two glyphs in boxes -------------------------
    // The reference's panels are full of *switches*: kit 01 has a red lamp beside a slanted
    // rocker paddle, kit 03 has round push buttons with a ring.  This panel had a rounded
    // square with a power glyph in it - the thing the user called "أماكن الأزرار" and then
    // "في مفاتيح كتير وكنوبس انت مخدتش منها أي حاجة".  Bypass is now a **rocker**: a machined
    // plate, a paddle that leans along its travel, and a lamp that says what the switch is
    // doing.  Reset is a **round push button**: a ring in a well, an arrow inset, and no lamp,
    // because a reset is a gesture and not a state.  Both keep the 40 px hit target they had.
    {
        const float k = float (getHeight()) / kDesignH;
        auto plate = [&] (Rectangle<float> box) {
            g.setColour (Palette::surface().darker (0.30f));
            g.fillRoundedRectangle (box, 9.0f * k);
            g.setColour (Colours::black.withAlpha (0.45f));
            g.drawRoundedRectangle (box.reduced (0.5f), 9.0f * k, 1.0f);
            g.setColour (Colours::white.withAlpha (0.06f));
            g.drawLine (box.getX() + 8.0f * k, box.getBottom() - 1.0f,
                        box.getRight() - 8.0f * k, box.getBottom() - 1.0f, 1.0f);
        };

        // ---- bypass: the rocker
        // A plate 86 % of the target, a paddle 40 % of it with three grip ridges, a 1.6 px
        // travel, and the lamp in the strip under it.  Every distance comes from
        // `rockerMetrics()`: the first build derived the paddle from "plate minus lamp" and
        // shipped a switch that was a 7.5 px dash in a 34 px box.
        const auto rk = bypassHit().toFloat().withSizeKeepingCentre (34.0f * k, 34.0f * k);
        {
            const auto m = aeq::ui::rockerMetrics (34.0f * k);
            const auto cx = rk.getCentreX();
            const auto cy = rk.getCentreY();
            plate (juce::Rectangle<float> (m.plate, m.plate).withCentre ({ cx, cy }));
            const float stackH = m.paddleH + m.lampGap + m.lampD;
            const float top = cy - stackH * 0.5f;
            const float travel = aeq::ui::rockerTiltFor (34.0f * k, ! bypassed);
            const Rectangle<float> paddle (cx - m.paddleH * 0.92f, top + travel,
                                           m.paddleH * 1.84f, m.paddleH);
            g.setColour (bypassed ? Palette::surfaceHi().brighter (0.03f)
                                  : Palette::surfaceHi().brighter (0.22f));
            g.fillRoundedRectangle (paddle, 2.6f * k);
            g.setColour (Colours::black.withAlpha (0.65f));
            g.drawRoundedRectangle (paddle.reduced (0.5f), 2.6f * k, 1.0f);
            for (int i = 1; i <= 3; ++i) {
                const float y = paddle.getY() + paddle.getHeight() * float (i) / 4.0f;
                g.setColour (Colours::black.withAlpha (0.40f));
                g.drawLine (paddle.getX() + 3.0f * k, y, paddle.getRight() - 3.0f * k, y, 1.0f);
                g.setColour (Colours::white.withAlpha (0.10f));
                g.drawLine (paddle.getX() + 3.0f * k, y + 1.0f, paddle.getRight() - 3.0f * k,
                            y + 1.0f, 1.0f);
            }
            // the raised end carries the light; that edge is the switch's state
            const float litY = bypassed ? paddle.getBottom() - 1.0f : paddle.getY() + 1.0f;
            g.setColour (bypassed ? Colours::black.withAlpha (0.45f)
                                  : Colours::white.withAlpha (0.26f));
            g.drawLine (paddle.getX() + 3.0f * k, litY, paddle.getRight() - 3.0f * k, litY, 1.0f);
            // the lamp: the reference's LED, in the strip under the paddle
            const float d = m.lampD;
            const float ly = top + m.paddleH + m.lampGap + d * 0.5f;
            g.setColour (bypassed ? Palette::warn() : Palette::hairline().withAlpha (0.9f));
            g.fillEllipse (cx - d * 0.5f, ly - d * 0.5f, d, d);
            if (bypassed) {
                g.setColour (Palette::warn().withAlpha (0.22f));
                g.fillEllipse (cx - d, ly - d, d * 2.0f, d * 2.0f);
            }
        }

        // ---- reset: the round push button
        const auto rb = resetHit().toFloat().withSizeKeepingCentre (34.0f * k, 34.0f * k);
        plate (rb);
        {
            const auto c2 = rb.getCentre();
            const float ring = 34.0f * k * 0.30f;
            g.setColour (Colours::black.withAlpha (0.60f));
            g.fillEllipse (c2.x - ring, c2.y - ring, ring * 2, ring * 2);
            g.setColour (Colours::white.withAlpha (0.14f));
            g.drawEllipse (c2.x - ring, c2.y - ring, ring * 2, ring * 2, 1.2f * k);
            // the button face: a lit top-left rim and a shadow bottom-right, so it reads convex
            g.setColour (Palette::surfaceHi().brighter (0.16f));
            g.fillEllipse (c2.x - ring * 0.80f, c2.y - ring * 0.80f,
                           ring * 1.60f, ring * 1.60f);
            aeq::ui::icon (g, Icon::Reset, juce::Rectangle<float> (ring * 1.15f, ring * 1.15f)
                                                .withCentre (c2),
                           Palette::muted(), 1.6f * k);
        }
    }

    const String sealText = String (aeq::copy::stateWord (panelFacts()));
    const Rectangle<float> pill = sealPill().toFloat();
    g.setColour (Palette::accent().withAlpha (0.10f));
    g.fillRoundedRectangle (pill, kPillR * sy);
    g.setColour (Palette::accent().withAlpha (0.45f));
    g.drawRoundedRectangle (pill.reduced (0.5f), kPillR * sy, 1.0f);
    g.setColour (Palette::accentHi());
    g.fillEllipse (pill.getX() + 12.0f * sy, pill.getCentreY() - 3.0f * sy, 6.0f * sy, 6.0f * sy);
    aeq::ui::draw (g, sealText, pill.translated (10.0f * sy, 0.0f).withTrimmedRight (12.0f * sy),
                   Justification::centred, kCap * sy, Palette::accentHi());

    // The reference pill: the record Match aims at, named, and the way to change it.  A
    // Match EQ whose target cannot be seen or chosen is a black box with a slope.
    {
        const auto rr = referenceHit().toFloat();
        const auto pillR = Rectangle<float> (rr.getX(), rr.withSizeKeepingCentre (rr.getWidth(),
                                                     roundToInt (30.0f * sy)).getY(),
                                             rr.getWidth(), 30.0f * sy);
        const auto name = proc_.referenceLabel();
        const bool have = name.isNotEmpty();
        g.setColour ((have ? Palette::accent() : Palette::text()).withAlpha (have ? 0.14f : 0.05f));
        g.fillRoundedRectangle (pillR, kPillR * sy);
        g.setColour ((have ? Palette::accent() : Palette::muted()).withAlpha (have ? 0.60f : 0.25f));
        g.drawRoundedRectangle (pillR.reduced (0.5f), kPillR * sy, 1.0f);
        g.setColour (have ? Palette::accentHi() : Palette::muted());
        g.fillEllipse (pillR.getX() + 11.0f * sy, pillR.getCentreY() - 2.5f * sy,
                       5.0f * sy, 5.0f * sy);
        // The name, or the invitation.  Truncated with the file's own tail, never a
        // shortened word - "AMIR 15 STA..." says which file, "AMIR..." does not.
        String label = have ? name.toUpperCase() : String ("REFERENCE");
        const float fit = pillR.getWidth() - 30.0f * sy;
        while (label.length() > 4 && aeq::ui::measure (label, kMicro * sy) > fit)
            label = label.dropLastCharacters (1);
        aeq::ui::draw (g, label, pillR.translated (22.0f * sy, 0.0f)
                                     .withTrimmedRight (8.0f * sy),
                       Justification::centredLeft, kMicro * sy,
                       have ? Palette::accentHi() : Palette::muted());
    }

    // The scan badge: does a pass exist, and how far has it got - in the header, where a
    // glance lands.  Match EQ makes its captured curve impossible to miss and smart:EQ
    // keeps a progress meter on screen for the same reason: "did it scan?" was a question
    // this panel only answered with a number that quietly went up.  Cyan, because the
    // scan is a measurement and the pink is the decision it produces.
    {
        const auto f = panelFacts();
        const bool scanning = f.haveData && ! aeq::copy::sealed (f);
        const bool done = aeq::copy::sealed (f);
        const float lit = scanning ? 1.0f : (done ? 0.72f : 0.0f);
        const auto br = scanBadge().toFloat();
        g.setColour (Palette::take().withAlpha (0.05f + 0.10f * lit));
        g.fillRoundedRectangle (br, kPillR * sy);
        g.setColour (Palette::take().withAlpha (0.20f + 0.40f * lit));
        g.drawRoundedRectangle (br.reduced (0.5f), kPillR * sy, 1.0f);
        g.setColour (scanning ? Palette::takeInk().brighter (0.15f)
                              : Palette::takeInk().withAlpha (0.55f));
        g.fillEllipse (br.getX() + 9.0f * sy, br.getCentreY() - 2.5f * sy,
                       5.0f * sy, 5.0f * sy);
        // trackedText, like the other three readouts: see drawTake for why the RTL path
        // (aeq::ui::draw) is the wrong funnel for a Latin status word
        trackedText (g, scanBadgeText(),
                     br.translated (18.0f * sy, 0.0f).withTrimmedRight (6.0f * sy),
                     scanning ? Palette::takeInk().brighter (0.30f)
                              : Palette::takeInk().withAlpha (done ? 0.95f : 0.62f),
                     kCap * sy, kReadoutTrack, Justification::centredLeft, Face::SemiBold);
    }

    // The sub-line under the seal: what runs without a hand.  The sentence it replaces
    // said "nothing moves on its own", which was the opposite of the truth and the exact
    // thing the user asked about ("اوتوماتيك ودايناميك ولا ستاتيك؟").
    {
        trackedText (g, autoReadoutText(), autoReadout().toFloat(),
                     panelFacts().haveData ? Palette::accentHi().withAlpha (0.88f)
                                           : Palette::dim(),
                     kMicro * sy, kReadoutTrack, Justification::centredRight);
    }
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
        // ... and it stops short of the layer readout on the right, which is the answer
        // to "static or dynamic" and must never be the thing a label covers up
        const auto readout = layerReadout();
        const float limit = readout.isEmpty() ? rib.getRight()
                                              : float (readout.getX()) - 12.0f * sx;
        const float lx = jlimit (rib.getX(), jmax (rib.getX(), limit - stopW),
                                 clamp.getCentreX() - stopW / 2.0f);
        g.setColour (Colours::black.withAlpha (0.82f));
        g.fillRoundedRectangle ({ lx, stopRow.getY(), stopW, stopRow.getHeight() }, 4.0f * sy);
        g.setColour (Palette::accent().withAlpha (0.50f));
        g.drawRoundedRectangle ({ lx, stopRow.getY(), stopW, stopRow.getHeight() }, 4.0f * sy, 1.0f);
        aeq::ui::draw (g, stopLabel, { lx, stopRow.getY(), stopW, stopRow.getHeight() },
                       Justification::centred, kMicro * sy, Palette::accentHi(), Face::SemiBold);

        // What is frozen and what moves, with the dB the moving layers are taking right
        // now.  One line, on the same row as the stop mark, because the two facts belong
        // together: here is where the pass ended, and here is what has run since.
        if (! readout.isEmpty())
            trackedText (g, layerReadoutText(), readout.toFloat(),
                         Palette::accentHi().withAlpha (0.92f), kMicro * sy, kReadoutTrack,
                         Justification::centredRight, Face::SemiBold);
    }

    // the caption row, inside the well: what was learnt (left) and the keys (right)
    const auto caption = heroRow (kCaptionTop, kCaptionBottom).toFloat();
    const auto st = proc_.core().state();
    // The separators are UTF-8 bytes spelled out (kMidDot), not a character in the
    // source file: a literal "·" is at the mercy of the build's source encoding, and the
    // shipped 1.2.26 window reads "AMIR 15 + 13-TAKE CONSENSUS Â· AUTO GAIN" because of it.
    const String dot = String::fromUTF8 (kMidDot);
    // What the curve is aiming at, named by the thing that decided it - not formatted
    // here from three guesses.  Match aims at Amir's own curve unless a reference is
    // loaded; Restore keeps the family average.  A panel that claims "13-take consensus"
    // while the engine chases a record is lying about its target, and this line is the
    // one place the user can catch it doing that.
    String target = String (proc_.core().targetName());
    if (proc_.core().hasReference()) {
        const String n = String (proc_.core().referenceName());
        target = n.isNotEmpty() ? ("REFERENCE " + n.toUpperCase()) : String ("REFERENCE");
    }
    // The strip leads the caption: "did it scan, and how much" in cyan, then what the
    // scan produced.  The old line opened with "HEARD 21.4 S", which was true and told
    // nobody whether the pass had finished or was still going.
    // Drawn through trackedText, not aeq::ui::draw: `draw` puts a RIGHT-TO-LEFT MARK in
    // front of every string that starts with a letter or a digit (Source/ui/ArabicText.cpp),
    // and in an RTL paragraph "SCAN 0%  ·  0 s" is two LTR runs whose order is the
    // paragraph's, not the string's - the shipped 1.2.46 shot of the empty window reads
    // "s SCAN 0%  ·  0" because of it.  Every readout on this row is tracked Latin, so it
    // takes the path that never sets an RTL base.
    const float stripSize = kMicro * sy;
    // The advance the line *will* take, kerning included: `measure()` returns the untracked
    // width, and the caption's next word was laid out against it - the 1.2.48 shot reads
    // "COARSE3 BANDS" because the render is ~30 px wider than the number that placed it.
    const float stripW = trackedInk (scanStripText(), stripSize, kReadoutTrack,
                                     Face::SemiBold) + 2.0f * sy;
    // The box is the left 45 % of the row, not the measured width: trackedText lays out
    // with AttributedString's default by-word wrap, so a box one pixel short of the
    // advance turns "SCAN 0%  ·  0 s" into two lines (the shipped 1.2.47 shot of the empty
    // window does exactly that).  The offset for the rest of the line still uses stripW.
    trackedText (g, scanStripText(),
                 { caption.getX(), caption.getY(), caption.getWidth() * 0.45f,
                   caption.getHeight() },
                 Palette::takeInk(), stripSize, kReadoutTrack,
                 Justification::centredLeft, Face::SemiBold);

    String learned = String (int (bands_.size())) + " BANDS" + dot + target;
    // the level match runs by itself, so it is a number on the panel rather than a fudge
    // in the dark: how much gain it is holding to keep the take where it was
    if (proc_.isFrozen() || proc_.isCommitted())
        learned += dot + "AUTO GAIN " + aeq::ui::num (proc_.core().levelMatchDb(), 1, 4) + " DB";
    // The distance to the target, before and after the curve: the only honest answer to
    // "does it sound like the thing I asked for yet".  One decimal, fixed width, and it
    // only appears when there is a target to be far from.
    {
        const auto s2 = proc_.core().state();
        if (s2.gapValid)
            learned += dot + "MATCH " + aeq::ui::num (s2.gapBeforeDb, 1, 4) + ">"
                     + aeq::ui::num (s2.gapAfterDb, 1, 4) + " DB";
    }
    trackedText (g, learned, caption.withLeft (roundToInt (caption.getX() + stripW
                                                          + 34.0f * sx)),
                 Palette::muted(), kMicro * sy, 1.1f);

    float rx = caption.getRight() - kWellInset * sx;
    const auto keys = std::array<std::pair<const char*, Colour>, 3> {
        std::make_pair (kDecisionKey, Palette::accent()),
        std::make_pair (kMeasuredKey, Palette::takeInk()),
        std::make_pair (kReferenceKey, Palette::standard())
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

        // A recessed plate, not a floating card: the row is machined into the panel the way
        // the reference kits recess their knob groups - a dark fill, a shadow along the top
        // inner edge and a hairline along the bottom one, so the light falls into it.
        g.setColour (Palette::surface().darker (0.25f));
        g.fillRoundedRectangle (r, kChipR * sy);
        // The shadow and the highlight go *inside* the plate, inset by 8 px.  Drawn on the
        // plate's top edge they ran through the cap height of the index, the mode word and
        // the number - the 1.2.53 shot reads "S̶T̶A̶T̶I̶C̶" - and at the bottom they cut the
        // baseline of the Hz line.
        {
            const auto in = r.reduced (8.0f * sy, 2.0f * sy);
            g.setColour (Colours::black.withAlpha (0.35f));
            g.drawLine (in.getX() + 6.0f * sy, in.getY() + 0.5f,
                        in.getRight() - 6.0f * sy, in.getY() + 0.5f, 1.0f);
            g.setColour (Colours::white.withAlpha (0.05f));
            g.drawLine (in.getX() + 6.0f * sy, in.getBottom() - 0.5f,
                        in.getRight() - 6.0f * sy, in.getBottom() - 0.5f, 1.0f);
        }
        g.setColour (hot ? Palette::accent().withAlpha (0.55f)
                         : Colours::white.withAlpha (hover ? 0.20f : 0.10f));
        g.drawRoundedRectangle (r.reduced (0.5f), kChipR * sy, 1.0f);

        // The header rule: kit 03's cards put their title on a strip with a hairline under it
        // ("Grains", "Transients", "Time").  That one line is what turns four numbers into
        // four *cards*, and the row reads as a table of things rather than a row of fill.
        // It is placed from the card's own geometry and stops short of its edges, so it can
        // never run through the index, the mode word or the number - the 1.2.53 defect.
        {
            const auto cm = aeq::ui::cardMetrics (r.getWidth());
            const float y = r.getY() + cm.headerH;
            g.setColour (Colours::white.withAlpha (0.07f));
            g.drawLine (r.getX() + cm.ruleInset, y, r.getRight() - cm.ruleInset, y, 1.0f);
            g.setColour (Colours::black.withAlpha (0.35f));
            g.drawLine (r.getX() + cm.ruleInset, y + 1.0f, r.getRight() - cm.ruleInset, y + 1.0f,
                        1.0f);
        }

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
    const auto s0 = modeSegDraw (0).toFloat(), s1 = modeSegDraw (1).toFloat();
    const Rectangle<float> pill (s0.getX() - 4.0f * sx, s0.getY(),
                                 s1.getRight() - s0.getX() + 8.0f * sx, s0.getHeight());
    // a recessed plate with two keys in it, so the pair reads as one switch rather than
    // as two labels that happen to be adjacent
    g.setColour (Palette::surface().darker (0.35f));
    g.fillRoundedRectangle (pill, kPillR * sy);
    g.setColour (Colours::black.withAlpha (0.40f));
    g.drawLine (pill.getX() + kPillR * sy, pill.getY() + 1.0f,
                pill.getRight() - kPillR * sy, pill.getY() + 1.0f, 1.0f);
    g.setColour (Colours::white.withAlpha (0.05f));
    g.drawLine (pill.getX() + kPillR * sy, pill.getBottom() - 1.0f,
                pill.getRight() - kPillR * sy, pill.getBottom() - 1.0f, 1.0f);
    const auto& seg = active == 0 ? s0 : s1;
    // The chosen side is marked by an orange pill *inside* the key, the way kit 03 marks its
    // "Last" toggle: the pill is the state, the key is the target.  It is inset from the
    // key's border (`pillMetrics`), so the two never merge into one outline - which is what
    // the 1.2.53 shot showed when the fill ran to the key's own edge.
    const auto pm = aeq::ui::pillMetrics (seg.getWidth(), seg.getHeight());
    const auto segIn = seg.reduced (pm.insetX, pm.insetY);
    g.setColour (Palette::accent().withAlpha (0.16f));
    g.fillRoundedRectangle (segIn, pm.radius);
    g.setColour (Palette::accent().withAlpha (0.75f));
    g.drawRoundedRectangle (segIn.reduced (0.5f), pm.radius, 1.0f);
    const char* labels[] = { kModeRestore, kModeMatch };
    for (int i = 0; i < 2; ++i) {
        const auto r = i == 0 ? s0 : s1;
        // The word is centred in its own key and nothing else is drawn inside it.  For this
        // pair the lit fill and its border *are* the lamp - a dot on top of a lit key is
        // decoration, and here it also cost 5 px of a 90 px key.  Two goes at a lamp inside
        // this box failed visibly: at a 14 px gutter the dot printed through the R (the
        // 1.2.53 shot reads "R̶ESTORE"), and at 40 px the word was pushed past its own
        // border (1.2.54).  RESTORE is the widest word on the panel: at kMicro its advance
        // is ~66 design px inside an 80 px inner key, 7 px clear of the border on each side,
        // and `modeWordFits()` is asserted against these exact numbers in the gate.
        aeq::ui::draw (g, aeq::ui::u8 (labels[i]), r, Justification::centred, kMicro * sy,
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
    const Point<float> k2 (float (area.getRight()) - 52.0f * sx, cy);
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
    const String v1 = aeq::ui::num (roundToInt (amountKnob_.getValue()), 0, 3) + "%";
    const String v2 = roundToInt (settleKnob_.getValue()) >= 1
                        ? aeq::ui::num (roundToInt (settleKnob_.getValue()), 0, 3) + " s"
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
    const String v0 = aeq::ui::num (roundToInt (stableKnob_.getValue()), 0, 3) + "%";
    g.drawText (v0, Rectangle<float> (k0.x - 40.0f * sx - 60.0f * sy, cy - 3.0f * sy,
                                      60.0f * sy, kValue * sy * 1.25f),
                Justification::centredRight, false);
    if (proc_.core().stabilizerBands() > 0) {
        const String live = aeq::ui::num (proc_.core().stabilizerTotalDb(), 1, 4) + " dB";
        aeq::ui::draw (g, live, { k0.x - 74.0f * sy, cy + 11.0f * sy, 74.0f * sy, kCap * sy },
                       Justification::centredRight, kCap * sy,
                       proc_.core().stabilizerTotalDb() < -0.5 ? Palette::accent()
                                                               : Palette::dim());
        // The de-esser is a section of the chain with its own gain reduction, and it was
        // the one section with no number on the panel: it was folded into the moving total
        // and nowhere else.  It says its own dB while it is working, and nothing when it
        // is not - a permanent "0.0 dB" is a number nobody reads.
        const double gr = std::abs (proc_.core().deEssGainReductionDb());
        if (gr >= 0.05) {
            const String de = "DE-ESS " + aeq::ui::num (gr, 1, 4) + " dB";
            aeq::ui::draw (g, de, { k0.x - 190.0f * sy, cy + 11.0f * sy, 110.0f * sy, kCap * sy },
                           Justification::centredRight, kCap * sy, Palette::accent().withAlpha (0.9f));
        }
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
    // the moving layer, from the core: the stabiliser's own ten bands, the de-esser's
    // gain reduction and the pitch-tracked layers, counted as "doing something" at
    // 0.3 dB.  The frozen curve is deliberately not in this number - it cannot move.
    f.dynamicLayers = proc_.core().movingLayersNow();
    f.movingDb = proc_.core().movingDbNow();
    return f;
}

String AeqAudioProcessorEditor::verdictLine() const {
    return String (aeq::copy::verdict (panelFacts()));
}

// The mode words, measured out of the same box the paint fills.  RESTORE is the widest
// word on the panel and it has crossed its own key's border twice (a lamp gutter in 1.2.53
// and again in 1.2.54), both times with every pixel assertion passing: a border is a 1 px
// line and a word that touches it still "renders".  The gate asks this instead.
float AeqAudioProcessorEditor::modeWordInk (int i) const {
    const float sy = float (getHeight()) / kDesignH;
    return aeq::ui::measure (aeq::ui::u8 (i == 0 ? kModeRestore : kModeMatch), kMicro * sy);
}

bool AeqAudioProcessorEditor::modeWordFits() const {
    const float sy = float (getHeight()) / kDesignH;
    const float sx = float (getWidth()) / kDesignW;
    // the inner key: the accent fill is inset 5 px a side, and a word needs 6 px of air
    // inside that or the border reads as struck through the last letter
    const float air = 6.0f * jmin (sx, sy);
    for (int i = 0; i < 2; ++i) {
        const float inner = float (modeSegDraw (i).getWidth()) - 10.0f * sx;
        if (modeWordInk (i) + air > inner) return false;
    }
    return true;
}

// Do the three footer dials fit beside the words that name them?  Each label is drawn to
// the left of its own dial (see drawFooter), so the thing that can collide is one dial's
// ring with the *previous* label's last letter.  At the design size there is 60 px of air
// there; at 1024x700 there was none, and the shipped 1.2.62 shot put the strength dial on
// top of the word "strength".  Same discipline as the mode keys: measure, then assert.
bool AeqAudioProcessorEditor::dialRowFits() const {
    const float sy = float (getHeight()) / kDesignH;
    const float sx = float (getWidth()) / kDesignW;
    const auto b = blocks();
    const int gapDesign = roundToInt (168.0f * sx);
    const int box = roundToInt (jmin (62.0f * sy, 0.30f * float (gapDesign)));
    const float half = float (box) * 0.5f;
    const float c0 = float (b.footer.getRight()) - 370.0f * sx;
    const float c1 = float (b.footer.getRight()) - 202.0f * sx;
    const float c2 = float (b.footer.getRight()) - 52.0f * sx;
    const char* names[] = { "stabiliser", "strength", "auto-stop" };
    const float centres[] = { c0, c1, c2 };
    const float dx = half * 0.62f + 8.0f * sy;      // the same offset drawFooter uses
    float previousRight = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float w = aeq::ui::measure (aeq::ui::u8 (names[i]), kMicro * sy);
        const float labelLeft = centres[i] - dx - w;
        if (i > 0 && labelLeft < previousRight + 4.0f) return false;
        previousRight = centres[i] + half;
    }
    return true;
}

Rectangle<int> AeqAudioProcessorEditor::heroBlock() const { return blocks().hero; }

int AeqAudioProcessorEditor::wellInsetPx() const {
    return roundToInt (kWellInset * float (getWidth()) / kDesignW);
}

Rectangle<float> AeqAudioProcessorEditor::footerCopyBox() const {
    const float sy = float (getHeight()) / kDesignH;
    const float sx = float (getWidth()) / kDesignW;
    const auto area = blocks().footer.toFloat();
    const auto s0 = modeSegDraw (0).toFloat(), s1 = modeSegDraw (1).toFloat();
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
    // The three dials sit 8 px above the row's centre line and 52 px in from the right edge,
    // not 34: the panel's corner radius is 16 and the dials are 54 px wide, so at the old
    // position the auto-stop dial's lower-right quadrant was being cut off by the plate's
    // own rounded corner - visible on the 1.2.59 shot as a knob with a flat bottom.  The
    // assertion for this is in Tests/ui_shot.cpp (`each dial clears the plate's corner`).
    const int cy = b.footer.getCentreY() - roundToInt (kKnobRowUp * sy);
    // The dial is 62 px at the design size, but the *row* is 168 design px of width and the
    // labels beside each dial are type, so they scale with the height.  At 1024x700 the
    // spacing lost 13 % and the type did not, and the 1.2.62 shot shows the strength dial
    // sitting on the word "strength".  So the dial also yields to the spacing it has to fit
    // inside - and `dialRowFits()` is asserted at three sizes rather than trusted.
    const int gapDesign = roundToInt (168.0f * sx);
    const int box = roundToInt (jmin (62.0f * sy, 0.30f * float (gapDesign)));
    const int c0 = b.footer.getRight() - roundToInt (370.0f * sx);
    const int c1 = b.footer.getRight() - roundToInt (202.0f * sx);
    const int c2 = b.footer.getRight() - roundToInt (52.0f * sx);
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
    if (referenceHit().contains (p)) {
        if (e.mods.isPopupMenu()) {
            proc_.clearReferenceProfile();
            refNote_ = "Reference cleared";
        } else {
            pickReference();
        }
        lastVersion_ = -1;
        repaint();
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
    // the low cut is a section of the decision and the only one that acts below the lowest
    // band; the graph draws it with the bands now (Source/ui/DecisionCurve.h)
    d.hpfHz = state_.hpfHz;
    curve_.setData (std::move (d));

    // only the parts of the window that show live state are repainted by this timer
    repaint();
}
