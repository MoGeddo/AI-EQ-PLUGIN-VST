#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ---------------------------------------------------------------------------
// One palette, one geometry, used by every component.
//
// The look is deliberately quiet: a near-black blue-grey field, one accent for
// anything the plugin decided, amber for evidence that is not an action, and no
// decoration that does not carry a number.
// ---------------------------------------------------------------------------
namespace aeq::ui {

struct Palette {
    static juce::Colour bg()        { return juce::Colour::fromRGB (0x0d, 0x10, 0x15); }
    static juce::Colour panel()     { return juce::Colour::fromRGB (0x15, 0x1a, 0x22); }
    static juce::Colour panelHi()   { return juce::Colour::fromRGB (0x1b, 0x21, 0x2b); }
    static juce::Colour line()      { return juce::Colour::fromRGB (0x27, 0x2f, 0x3c); }
    static juce::Colour text()      { return juce::Colour::fromRGB (0xe8, 0xee, 0xf7); }
    static juce::Colour muted()     { return juce::Colour::fromRGB (0x8b, 0x97, 0xa9); }
    static juce::Colour accent()    { return juce::Colour::fromRGB (0x38, 0xd8, 0xb8); }  // what we did
    static juce::Colour accent2()   { return juce::Colour::fromRGB (0x4a, 0x9d, 0xf8); }  // what you play
    static juce::Colour ok()        { return juce::Colour::fromRGB (0x4e, 0xc9, 0x8a); }
    static juce::Colour warn()      { return juce::Colour::fromRGB (0xf2, 0xb0, 0x5c); }
    static juce::Colour danger()    { return juce::Colour::fromRGB (0xef, 0x6a, 0x6a); }
    static juce::Colour standard()  { return juce::Colour::fromRGB (0xe0, 0x5c, 0x8a); }  // Amir's target
};

inline juce::Font font (float size, bool bold = false) {
    return juce::Font (juce::FontOptions (size)
                           .withStyle (bold ? "Bold" : "Regular"));
}

inline juce::Font mono (float size) {
    // a fixed-ish face for numbers: falls back to the system default when absent
    return juce::Font (juce::FontOptions (size).withName ("Consolas")
                           .withStyle ("Regular"));
}

inline juce::String num (double v, int decimals = 1) {
    return juce::String (v, decimals);
}

// rounded panel with a hairline edge and an optional soft top highlight
inline void card (juce::Graphics& g, juce::Rectangle<float> r,
                  juce::Colour fill, juce::Colour edge,
                  float radius = 12.0f, float edgeW = 1.0f) {
    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
    g.setColour (edge);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, edgeW);
}

inline void glow (juce::Graphics& g, juce::Rectangle<float> r,
                  juce::Colour c, float radius = 12.0f) {
    // a very light inward wash; used behind the thing that owns the eye
    juce::ColourGradient grad (c.withAlpha (0.10f), r.getCentreX(), r.getY(),
                               c.withAlpha (0.0f), r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, radius);
}

inline void label (juce::Graphics& g, juce::StringRef text, juce::Rectangle<int> r,
                   juce::Colour c, float size, juce::Justification j = juce::Justification::centredLeft,
                   bool bold = false) {
    g.setColour (c);
    g.setFont (font (size, bold));
    g.drawText (text, r, j, false);
}

// small rounded tag: "F0 260 Hz"
inline void chip (juce::Graphics& g, juce::StringRef text, juce::Rectangle<float> r,
                  juce::Colour c, bool filled = false) {
    g.setColour (filled ? c.withAlpha (0.16f) : juce::Colours::transparentBlack);
    if (filled) g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (c.withAlpha (filled ? 0.55f : 0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
    g.setColour (c);
    g.setFont (font (11.5f, true));
    g.drawText (text, r, juce::Justification::centred, false);
}

// severity 0..1 -> colour, the same ramp the problem cards use
inline juce::Colour severityColour (double s) {
    if (s >= 0.66) return Palette::danger();
    if (s >= 0.33) return Palette::warn();
    return Palette::accent2();
}

} // namespace aeq::ui
