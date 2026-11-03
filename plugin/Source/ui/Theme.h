#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include "ArabicText.h"

// ---------------------------------------------------------------------------
// One palette, one geometry, one text path.  Everything visual in the plugin
// comes from here, so a change of mood is a change of one file.
//
// The look: a near-black console, three surfaces at increasing height, hairlines
// instead of borders, and colour used only where it means something:
//
//   neutral white   the standard (the 15 mixes) - it is the reference, not a mood
//   blue            your take, what is actually coming in
//   mint            the correction, the only thing this plugin is allowed to do
//   amber / red     a finding that wants attention / one that is wrong
//
// Type: Cairo (embedded), three sizes, two weights.  Numbers are drawn with the
// same face in semi-bold and never inside a knob, because "0.0" sitting on top of
// a label is what the last build looked like.
// ---------------------------------------------------------------------------
namespace aeq::ui {

struct Palette {
    static juce::Colour bg()        { return juce::Colour::fromRGB (0x08, 0x09, 0x0b); }
    static juce::Colour surface()   { return juce::Colour::fromRGB (0x10, 0x12, 0x16); }
    static juce::Colour surfaceHi() { return juce::Colour::fromRGB (0x17, 0x1a, 0x20); }
    static juce::Colour hairline()  { return juce::Colour::fromRGB (0x24, 0x28, 0x2f); }
    static juce::Colour text()      { return juce::Colour::fromRGB (0xec, 0xee, 0xf2); }
    static juce::Colour muted()     { return juce::Colour::fromRGB (0x8b, 0x91, 0x9c); }
    static juce::Colour dim()       { return juce::Colour::fromRGB (0x5a, 0x60, 0x6c); }

    static juce::Colour accent()    { return juce::Colour::fromRGB (0x2b, 0xd9, 0xae); }  // what we do
    static juce::Colour take()      { return juce::Colour::fromRGB (0x4c, 0x8d, 0xff); }  // what you play
    static juce::Colour standard()  { return juce::Colour::fromRGB (0xc8, 0xcf, 0xda); }  // the reference
    static juce::Colour ok()        { return juce::Colour::fromRGB (0x4e, 0xc9, 0x8a); }
    static juce::Colour warn()      { return juce::Colour::fromRGB (0xf2, 0xb0, 0x5c); }
    static juce::Colour danger()    { return juce::Colour::fromRGB (0xef, 0x6a, 0x6a); }

    // kept for the older call sites; same meaning as accent2/take
    static juce::Colour panel()     { return surface(); }
    static juce::Colour panelHi()   { return surfaceHi(); }
    static juce::Colour line()      { return hairline(); }
    static juce::Colour accent2()   { return take(); }
};

// ---------------------------------------------------------------------------
// Type scale.  Small caps-style labels at 10.5, body at 12.5, the headline at 17.
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
                  float radius = 10.0f, float edgeW = 1.0f) {
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

inline void glow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c,
                  float radius = 10.0f) {
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
    as.append (text.toUpperCase(), aeq::ui::font (9.5f, Face::SemiBold).withExtraKerningFactor (0.14f), c);
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
    aeq::ui::draw (g, text, r, juce::Justification::centred, 11.0f, c, Face::SemiBold);
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
