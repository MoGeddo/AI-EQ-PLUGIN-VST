#include "ArabicText.h"

namespace aeq::ui {
namespace {

// The bytes come from juce_add_binary_data (see CMakeLists.txt):
//   CairoRegular_ttf / CairoSemiBold_ttf
juce::Typeface::Ptr buildTypeface(Face face) {
    // Bytes are compiled in by juce_add_binary_data (CMakeLists.txt), so this never
    // touches the filesystem and never asks the OS which font to use.
    if (face == Face::SemiBold)
        return juce::Typeface::createSystemTypefaceFor(
            BinaryData::CairoSemiBold_ttf, BinaryData::CairoSemiBold_ttfSize);
    return juce::Typeface::createSystemTypefaceFor(
        BinaryData::CairoRegular_ttf, BinaryData::CairoRegular_ttfSize);
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

juce::Font font(float height, Face face) {
    return juce::Font(juce::FontOptions().withTypeface(typeface(face)).withHeight(height));
}

void draw(juce::Graphics& g, const juce::String& utf8, juce::Rectangle<float> area,
          juce::Justification just, float fontSize, juce::Colour colour, Face face,
          float extraLineSpacing) {
    if (utf8.isEmpty() || area.getWidth() < 2.0f) return;
    auto layout = buildLayout(utf8, area.getWidth(), fontSize, colour, face, just, extraLineSpacing);
    g.setColour(colour);
    layout.draw(g, area);
}

void drawParagraph(juce::Graphics& g, const juce::String& utf8, juce::Rectangle<float> area,
                   juce::Justification just, float fontSize, juce::Colour colour, Face face) {
    if (utf8.isEmpty() || area.getWidth() < 2.0f) return;
    juce::AttributedString as;
    as.setJustification(just);
    as.setLineSpacing(0.12f);
    as.append(utf8, font(fontSize, face), colour);
    as.setWordWrap(juce::AttributedString::WordWrap::byWord);
    juce::TextLayout layout;
    layout.createLayout(as, area.getWidth());
    g.setColour(colour);
    layout.draw(g, area);
}

float measure(const juce::String& utf8, float fontSize, Face face) {
    if (utf8.isEmpty()) return 0.0f;
    auto f = font(fontSize, face);
    return juce::GlyphArrangement::getStringWidth(f, utf8);
}

} // namespace aeq::ui
