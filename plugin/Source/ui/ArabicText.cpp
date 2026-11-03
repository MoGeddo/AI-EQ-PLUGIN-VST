#include "ArabicText.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace aeq::ui {
namespace {

// DirectWrite is happy with a font that lives in memory (it builds a custom
// collection from the bytes).  GDI is not: it looks a font up by name, and JUCE's
// software renderer draws glyphs through GDI.  So on a machine that falls back to
// the software renderer - a remote session, a GPU-less box, and every offscreen
// Image - the embedded face would be substituted for something without Arabic and
// the window would fill with boxes, which is the 1.2.0 failure wearing a
// different hat.  Registering the same bytes with GDI as well costs nothing and
// makes both renderers agree.
void registerWithGdi(const void* data, int size) {
   #if JUCE_WINDOWS
    static bool done = false;
    if (done || data == nullptr || size <= 0) return;
    done = true;
    DWORD installed = 0;
    AddFontMemResourceEx (const_cast<void*> (data), (DWORD) size, nullptr, &installed);
   #else
    juce::ignoreUnused (data, size);
   #endif
}

// The bytes come from juce_add_binary_data (see CMakeLists.txt):
//   CairoRegular_ttf / CairoSemiBold_ttf
juce::Typeface::Ptr buildTypeface(Face face) {
    // Bytes are compiled in by juce_add_binary_data (CMakeLists.txt), so this never
    // touches the filesystem and never asks the OS which font to use.
    if (face == Face::SemiBold) {
        registerWithGdi (BinaryData::CairoSemiBold_ttf, BinaryData::CairoSemiBold_ttfSize);
        return juce::Typeface::createSystemTypefaceFor(
            BinaryData::CairoSemiBold_ttf, BinaryData::CairoSemiBold_ttfSize);
    }
    registerWithGdi (BinaryData::CairoRegular_ttf, BinaryData::CairoRegular_ttfSize);
    return juce::Typeface::createSystemTypefaceFor(
        BinaryData::CairoRegular_ttf, BinaryData::CairoRegular_ttfSize);
}

// The base direction of a paragraph is decided by its first strong character
// (bidi rule P2/P3).  In an Arabic report one card reads
//
//     202 Hz ثابت على مدى الأداء (متوسط +40.1 dB، تغيّر 15.0 dB)
//
// so the paragraph is laid out left-to-right with Arabic runs inside it, and the
// Windows text stack takes the process down on exactly those: every card whose
// paragraph began with Arabic survived, and every one that began with a number
// died (found by rendering the cards' lines one at a time on the runner - see
// probe-text-*.png).  A RIGHT-TO-LEFT MARK in front of the text fixes the base
// level and prints nothing; the design wanted an RTL report anyway, and a right
// aligned paragraph with a digit in front of it is a bug even where it does not
// crash.
juce::String withRtlBase(const juce::String& s) {
    if (s.isEmpty()) return s;
    const juce::juce_wchar c = s[0];
    const bool strongLtr = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    return strongLtr ? juce::String::charToString(0x200F) + s : s;
}

// One TextLayout, shaped and bidi, for every string in the interface.  This is the
// path JUCE 8 runs BidiAlgorithm + SimpleShapedText on; Graphics::drawText is not.
juce::TextLayout buildLayout(const juce::String& utf8, float width, float fontSize,
                             juce::Colour colour, Face face, juce::Justification just,
                             float extraLineSpacing) {
    juce::AttributedString as;
    as.setJustification(just);
    as.setLineSpacing(extraLineSpacing);
    as.append(utf8, font(fontSize, face), colour);
    as.setWordWrap(juce::AttributedString::WordWrap::none);

    juce::TextLayout layout;
    layout.createLayout(as, width);
    return layout;
}

} // namespace

juce::Typeface::Ptr typeface(Face face) {
    // Function-local statics in C++11 and later are thread-safe and initialised
    // exactly once; the message thread is the only caller but the plugin can be
    // instantiated from more than one instance of the editor.
    static juce::Typeface::Ptr regular = buildTypeface(Face::Regular);
    static juce::Typeface::Ptr semibold = buildTypeface(Face::SemiBold);
    return face == Face::SemiBold ? semibold : regular;
}

bool embeddedFontLoaded() { return typeface(Face::Regular) != nullptr; }

juce::Font font(float height, Face face) {
    if (auto tf = typeface(face))
        return juce::Font(juce::FontOptions().withTypeface(tf).withHeight(height));

    // The embedded face could not be built.  Rather than hand the OS a font that
    // may have no Arabic glyphs and paint a wall of boxes - the 1.2.0 failure -
    // ask for a family that does, in order of how likely it is to be installed.
    static const char* const candidates[] = { "Segoe UI", "Tahoma", "Arial", "DejaVu Sans" };
    juce::Font best(juce::FontOptions().withHeight(height));
    for (auto* name : candidates) {
        juce::Font f(juce::FontOptions().withHeight(height).withName(name));
        if (f.getTypefacePtr() != nullptr && f.getTypefacePtr()->getName().isNotEmpty()) { best = f; break; }
    }
    return best;
}

void draw(juce::Graphics& g, const char* utf8, juce::Rectangle<float> area,
          juce::Justification just, float fontSize, juce::Colour colour, Face face,
          float extraLineSpacing) {
    draw (g, u8 (utf8), area, just, fontSize, colour, face, extraLineSpacing);
}

void draw(juce::Graphics& g, const juce::String& utf8, juce::Rectangle<float> area,
          juce::Justification just, float fontSize, juce::Colour colour, Face face,
          float extraLineSpacing) {
    if (utf8.isEmpty() || area.getWidth() < 2.0f) return;
    auto layout = buildLayout(withRtlBase(utf8), area.getWidth(), fontSize, colour, face, just, extraLineSpacing);
    g.setColour(colour);
    layout.draw(g, area);
}

void drawParagraph(juce::Graphics& g, const char* utf8, juce::Rectangle<float> area,
                   juce::Justification just, float fontSize, juce::Colour colour, Face face) {
    drawParagraph (g, u8 (utf8), area, just, fontSize, colour, face);
}

void drawParagraph(juce::Graphics& g, const juce::String& utf8, juce::Rectangle<float> area,
                   juce::Justification just, float fontSize, juce::Colour colour, Face face) {
    if (utf8.isEmpty() || area.getWidth() < 2.0f) return;
    const auto text = withRtlBase(utf8);
    juce::AttributedString as;
    as.setJustification(just);
    as.setLineSpacing(0.12f);
    as.append(text, font(fontSize, face), colour);
    as.setWordWrap(juce::AttributedString::WordWrap::byWord);
    juce::TextLayout layout;
    layout.createLayout(as, area.getWidth());
    g.setColour(colour);
    layout.draw(g, area);
}

float measure(const juce::String& utf8, float fontSize, Face face) {
    if (utf8.isEmpty()) return 0.0f;
    auto f = font(fontSize, face);
    return juce::GlyphArrangement::getStringWidth(f, withRtlBase(utf8));
}

float measure(const char* utf8, float fontSize, Face face) {
    return measure (u8 (utf8), fontSize, face);
}

} // namespace aeq::ui
