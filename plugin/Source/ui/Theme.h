#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include "ArabicText.h"

// ---------------------------------------------------------------------------
// One palette, one geometry, one text path.  Everything visual in the plugin
// comes from here, so a change of mood is a change of one file.
//
// The look: warm lacquer, brass and patina - design/IDENTITY_AR.md.  The palette
// used to be a cold console (near-black, blue take, mint correction, amber warning)
// and that palette was the problem: it is the language every plugin in this
// category already speaks, and three of the four "make it look better" rounds were
// spent arguing about a hue that meant nothing.
//
// The rule that replaced it is one line long, and it is a rule about *meaning*:
//
//   brass   = a decision this plugin made (the curve, the needle, the seal)
//   patina  = a measurement of your audio (the take, the spectrum, the dynamics)
//
// Those two never swap, and there is no third accent in the product.  A warning is
// the one exception and it is deliberately not an accent: it is a state you have to
// be able to see and it must never look like a decision.
//
// Type: Cairo (embedded), four sizes, two weights, tabular numbers for anything
// that updates (MotionSpec.h owns the motion; this file owns the colour).
// ---------------------------------------------------------------------------
namespace aeq::ui {

struct Palette {
    // ---- surfaces: warm black, never a cold grey.
    // v8's ground is a gradient from INK_TOP to INK_BOT with two blooms on it; the
    // three surface tokens are the exact stops of that ground and of the graph plate,
    // so the C++ port cannot drift from design/preview/ui_v8.py.
    static juce::Colour inkTop()    { return juce::Colour::fromRGB (0x12, 0x10, 0x0e); }
    static juce::Colour bg()        { return juce::Colour::fromRGB (0x08, 0x08, 0x09); }
    static juce::Colour surface()   { return juce::Colour::fromRGB (0x0a, 0x0a, 0x0c); }
    static juce::Colour surfaceHi() { return juce::Colour::fromRGB (0x16, 0x14, 0x0f); }
    static juce::Colour hairline()  { return juce::Colour::fromRGB (0x2a, 0x25, 0x1e); }
    static juce::Colour text()      { return juce::Colour::fromRGB (0xf4, 0xf0, 0xe8); }
    static juce::Colour muted()     { return juce::Colour::fromRGB (0xaa, 0xa2, 0x94); }
    static juce::Colour dim()       { return juce::Colour::fromRGB (0x92, 0x8a, 0x7e); }

    // ---- brass: everything this plugin decides
    static juce::Colour accent()    { return juce::Colour::fromRGB (0xd6, 0xac, 0x60); }  // the curve, the needle
    static juce::Colour accentHi()  { return juce::Colour::fromRGB (0xff, 0xe2, 0xb0); }  // a lit brass edge
    static juce::Colour seal()      { return accentHi(); }                                // committed state

    // ---- patina: everything that is a measurement of the take
    static juce::Colour take()      { return juce::Colour::fromRGB (0x78, 0xba, 0xa2); }  // what you play
    static juce::Colour takeDeep()  { return juce::Colour::fromRGB (0x22, 0x3e, 0x38); }  // its body
    static juce::Colour standard()  { return juce::Colour::fromRGB (0xa7, 0xa2, 0x99); }  // the 15 mixes

    // ---- states: not accents.  A warning must never read as a decision.
    static juce::Colour ok()        { return juce::Colour::fromRGB (0x6f, 0xa8, 0x93); }
    static juce::Colour warn()      { return juce::Colour::fromRGB (0xd8, 0x9a, 0x4e); }
    static juce::Colour danger()    { return juce::Colour::fromRGB (0xb4, 0x59, 0x3f); }

    // kept for the older call sites; same meaning as accent2/take
    static juce::Colour panel()     { return surface(); }
    static juce::Colour panelHi()   { return surfaceHi(); }
    static juce::Colour line()      { return hairline(); }
    static juce::Colour accent2()   { return take(); }
};

// ---------------------------------------------------------------------------
// The v8 scale.  Four type sizes, five radii, three bloom stops - the whole of the
// panel's geometry in nine numbers.  v7 used a 42 px header, a 100 px take bar and a
// 134 px ledger of four full-width rows of equal weight; that stack is what read as
// "primitive", so the sizes below are the fix expressed as constants: the graph takes
// everything that is left, and nothing else is allowed a band of its own.
// ---------------------------------------------------------------------------
namespace Type {
    inline constexpr float micro = 8.0f;    // caps labels, units, the Hz · Q line
    inline constexpr float cap   = 10.0f;   // Arabic keys, mode names, the tagline
    inline constexpr float body  = 12.0f;   // one sentence of Arabic, at most
    inline constexpr float value = 15.0f;   // the number a person reads first
}

namespace Radius {
    inline constexpr float panel = 16.0f;
    inline constexpr float graph = 14.0f;
    inline constexpr float chip  = 12.0f;
    inline constexpr float pill  = 11.0f;
    inline constexpr float small = 4.0f;
}

// Light, not texture: every piece of depth on this panel is one of these, a soft
// bloom of an accent hue at very low alpha.  There is no plate, no fabric and no
// drop shadow anywhere in v8.
namespace Light {
    inline constexpr float bloomAlpha  = 0.08f;   // 20/255
    inline constexpr float rimAlpha    = 0.10f;   // the inner highlight on the frame
    inline constexpr float hairlineA   = 0.12f;   // 30/255, a 1 px edge that is not a border
    inline constexpr float grainAlpha  = 0.17f;   // 44/255 - film, never a photograph
}

// ---------------------------------------------------------------------------
// Type scale.  Micro caps at 8 for units, 10 for keys, 12 for a sentence, 15 for a
// value.  Nothing is 17 any more: the headline is a tracked wordmark, not a number.
// ---------------------------------------------------------------------------
inline juce::Font font (float size, bool bold = false) {
    return aeq::ui::font (size, bold ? Face::SemiBold : Face::Regular);
}

// Numerals: the same face, semi-bold, with a fixed advance so a column of values
// does not jitter as it changes.  (Cairo's digits are proportional.)
inline juce::Font numeral (float size) { return aeq::ui::font (size, Face::SemiBold); }

inline juce::String num (double v, int decimals = 1) { return juce::String (v, decimals); }

// ---------------------------------------------------------------------------
// Surfaces
// ---------------------------------------------------------------------------

// a panel: flat fill, one hairline, 10 px radius.  No drop shadow: shadows on a
// dark UI read as smudge, not depth.
inline void card (juce::Graphics& g, juce::Rectangle<float> r,
                  juce::Colour fill = Palette::surface(),
                  juce::Colour edge = Palette::hairline(),
                  float radius = Radius::chip, float edgeW = 1.0f) {
    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
    if (edgeW > 0.0f) {
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, edgeW);
    }
}

// a 1 px separator that fades out at both ends
inline void hairline (juce::Graphics& g, float x1, float y, float x2,
                      juce::Colour c = Palette::hairline()) {
    g.setGradientFill (juce::ColourGradient (c.withAlpha (0.0f), x1, y,
                                             c, (x1 + x2) * 0.5f, y, false));
    g.fillRect (juce::Rectangle<float> (x1, y, (x2 - x1) * 0.5f, 1.0f));
    g.setGradientFill (juce::ColourGradient (c, (x1 + x2) * 0.5f, y,
                                             c.withAlpha (0.0f), x2, y, false));
    g.fillRect (juce::Rectangle<float> ((x1 + x2) * 0.5f, y, (x2 - x1) * 0.5f, 1.0f));
}

// the v8 ground: one enormous soft ellipse of an accent hue, centred off the panel
// and blurred into nothing.  This is the only way depth is made on the panel.
inline void bloom (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c,
                   juce::Point<float> centre, float radius,
                   float alpha = Light::bloomAlpha) {
    juce::ColourGradient grad (c.withAlpha (alpha), centre.x, centre.y,
                               c.withAlpha (0.0f), centre.x + radius, centre.y, true);
    g.setGradientFill (grad);
    g.fillEllipse (r);
}

inline void glow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c,
                  float radius = Radius::chip) {
    g.setGradientFill (juce::ColourGradient (c.withAlpha (0.09f), r.getCentreX(), r.getY(),
                                             c.withAlpha (0.0f), r.getCentreX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
}

// ---------------------------------------------------------------------------
// Text - all of it goes through the shaping path in ArabicText.cpp
// ---------------------------------------------------------------------------
inline void label (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                   juce::Colour c, float size,
                   juce::Justification j = juce::Justification::centredLeft,
                   bool bold = false) {
    aeq::ui::draw (g, text, r.toFloat(), j, size, c, bold ? Face::SemiBold : Face::Regular);
}

// literals land here exactly, so a source-level Arabic string is converted once,
// by u8(), instead of through juce::String's ASCII constructor
inline void label (juce::Graphics& g, const char* text, juce::Rectangle<int> r,
                   juce::Colour c, float size,
                   juce::Justification j = juce::Justification::centredLeft,
                   bool bold = false) {
    label (g, u8 (text), r, c, size, j, bold);
}

// a tracked-out caption, the small all-caps line above a number.  Latin only, so
// letter spacing is safe (Arabic must never be tracked).
inline void caption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                     juce::Colour c = Palette::dim(),
                     juce::Justification j = juce::Justification::centredLeft) {
    juce::AttributedString as;
    as.setJustification (j);
    as.append (text.toUpperCase(), aeq::ui::font (Type::micro, Face::SemiBold).withExtraKerningFactor (0.14f), c);
    juce::TextLayout layout;
    layout.createLayout (as, float (r.getWidth()));
    layout.draw (g, r.toFloat());
}

// (declared after the String version so the forward call resolves)
inline void caption (juce::Graphics& g, const char* text, juce::Rectangle<int> r,
                     juce::Colour c = Palette::dim(),
                     juce::Justification j = juce::Justification::centredLeft) {
    caption (g, u8 (text), r, c, j);
}


// small rounded tag: "F0 260 Hz"
inline void chip (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                  juce::Colour c, bool filled = false) {
    if (filled) {
        g.setColour (c.withAlpha (0.13f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    }
    g.setColour (c.withAlpha (filled ? 0.45f : 0.30f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
    aeq::ui::draw (g, text, r, juce::Justification::centred, Type::cap, c, Face::SemiBold);
}

inline void chip (juce::Graphics& g, const char* text, juce::Rectangle<float> r,
                  juce::Colour c, bool filled = false) {
    chip (g, u8 (text), r, c, filled);
}


// severity 0..1 -> colour, the same ramp the problem cards use
inline juce::Colour severityColour (double s) {
    if (s >= 0.66) return Palette::danger();
    if (s >= 0.33) return Palette::warn();
    return Palette::take();
}

// ---------------------------------------------------------------------------
// Icons.  Drawn as paths so the binary carries no image assets and every icon
// scales with the window (the 1.2.0 build had none at all).
// ---------------------------------------------------------------------------
enum class Icon { Power, Reset, Wave, Play, Stop, ChevronDown, Target, Pointer };

inline void icon (juce::Graphics& g, Icon which, juce::Rectangle<float> r,
                  juce::Colour c, float thickness = 1.6f) {
    const auto centre = r.getCentre();
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    juce::Path p;
    g.setColour (c);

    switch (which) {
        case Icon::Power:
            p.addCentredArc (centre.x, centre.y + s * 0.04f, s * 0.30f, s * 0.30f, 0.0f,
                             juce::MathConstants<float>::pi * 0.32f,
                             juce::MathConstants<float>::pi * 1.68f, true);
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            g.drawLine (centre.x, r.getY() + s * 0.08f, centre.x, centre.y - s * 0.02f, thickness);
            break;
        case Icon::Reset:
            p.addCentredArc (centre.x, centre.y, s * 0.28f, s * 0.28f, 0.0f,
                             juce::MathConstants<float>::pi * 0.6f, juce::MathConstants<float>::pi * 2.1f, true);
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            p.clear();
            p.addTriangle (centre.x + s * 0.30f, centre.y - s * 0.20f,
                           centre.x + s * 0.20f, centre.y - s * 0.34f,
                           centre.x + s * 0.40f, centre.y - s * 0.34f);
            g.fillPath (p);
            break;
        case Icon::Wave:
            for (int i = 0; i < 5; ++i) {
                const float h = s * (0.16f + 0.20f * std::abs (std::sin (i * 1.1f + 0.6f)));
                const float x = r.getX() + s * (0.14f + 0.18f * float (i));
                g.drawLine (x, centre.y - h, x, centre.y + h, thickness);
            }
            break;
        case Icon::Play:
            p.addTriangle (centre.x - s * 0.12f, centre.y - s * 0.20f,
                           centre.x - s * 0.12f, centre.y + s * 0.20f,
                           centre.x + s * 0.20f, centre.y);
            g.fillPath (p);
            break;
        case Icon::Stop:
            g.fillRoundedRectangle (juce::Rectangle<float> (s * 0.28f, s * 0.28f).withCentre (centre), 1.5f);
            break;
        case Icon::ChevronDown:
            g.drawLine (centre.x - s * 0.14f, centre.y - s * 0.06f,
                        centre.x, centre.y + s * 0.10f, thickness);
            g.drawLine (centre.x, centre.y + s * 0.10f,
                        centre.x + s * 0.14f, centre.y - s * 0.06f, thickness);
            break;
        case Icon::Target:
            g.drawEllipse (r.withSizeKeepingCentre (s * 0.66f, s * 0.66f), thickness);
            g.fillEllipse (r.withSizeKeepingCentre (s * 0.18f, s * 0.18f));
            break;
        case Icon::Pointer:
            p.addTriangle (0.0f, 0.0f, 0.0f, s * 0.62f, s * 0.46f, s * 0.40f);
            p.applyTransform (juce::AffineTransform::translation (r.getX(), r.getY()));
            g.fillPath (p);
            break;
    }
}

// "0.0" and "-3.2 dB": a value drawn on its own baseline, never on top of a label
inline void value (juce::Graphics& g, const juce::String& v, juce::Rectangle<int> r,
                   juce::Colour c = Palette::text(), float size = 12.5f,
                   juce::Justification j = juce::Justification::centred) {
    aeq::ui::draw (g, v, r.toFloat(), j, size, c, Face::SemiBold);
}

} // namespace aeq::ui
