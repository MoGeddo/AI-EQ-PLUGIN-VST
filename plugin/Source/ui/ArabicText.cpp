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

// The panel's sizes are *em* sizes: 15 px of micro type means a 15 px em, which is what the
// render, the layout constants and the gates all mean by "15".  JUCE's Font height is not the
// em - it is ascent + descent, and for the embedded Cairo those are 1.303 and 0.571 em - so a
// Font asked for 15 px draws 8 px of type (cap height 5.6 px, measured on the 1.2.26
// screenshot).  The panel had been rendering every label at 53 % of the size its own design
// said, which is exactly what "الخطوط صغيرة و مش باينة" was pointing at, while the gate that
// guards the type floor read the floor's number and passed.  Convert once, here, so every
// caller in the plugin means ems and the window and the render finally agree.
static juce::Font atEmHeight (const juce::Font& proto, float em) {
    // How many em the typeface packs into one unit of JUCE "height".  getHeightInPoints() is
    // the height in points, i.e. in ems, and it is the public half of the factor JUCE uses
    // internally (getHeightToPointsFactor is private in JUCE 8).  Cairo is 1.874 em per unit
    // of height; a font whose metrics cannot be read keeps the usual 1.2.
    const float probe = 100.0f;
    const auto at100 = proto.withHeight (probe);
    const float emPerHeight = at100.getHeightInPoints() / probe;
    const float box = emPerHeight > 0.05f ? em / emPerHeight : em * 1.2f;
    return proto.withHeight (box);
}

juce::Font font(float height, Face face) {
    if (auto tf = typeface(face))
        return atEmHeight (juce::Font(juce::FontOptions().withTypeface(tf).withHeight(1.0f)),
                           height);

    // The embedded face could not be built.  Rather than hand the OS a font that
    // may have no Arabic glyphs and paint a wall of boxes - the 1.2.0 failure -
    // ask for a family that does, in order of how likely it is to be installed.
    static const char* const candidates[] = { "Segoe UI", "Tahoma", "Arial", "DejaVu Sans" };
    juce::Font best(juce::Font(juce::FontOptions().withHeight(1.0f)));
    for (auto* name : candidates) {
        juce::Font f(juce::Font(juce::FontOptions().withHeight(1.0f).withName(name)));
        if (f.getTypefacePtr() != nullptr && f.getTypefacePtr()->getName().isNotEmpty()) { best = f; break; }
    }
    return atEmHeight (best, height);
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

std::vector<juce::String> wrapWords(const juce::String& text, float fontSize, float width,
                                    Face face) {
    std::vector<juce::String> lines;
    if (text.isEmpty()) return lines;
    const auto words = juce::StringArray::fromTokens (text, " ", "");
    juce::String line;
    for (const auto& w : words) {
        const juce::String trial = line.isEmpty() ? w : line + " " + w;
        if (! line.isEmpty() && measure (trial, fontSize, face) > width) {
            lines.push_back (line);
            line = w;
        } else {
            line = trial;
        }
    }
    if (line.isNotEmpty()) lines.push_back (line);
    return lines;
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
