// The wordmark's arithmetic, with no JUCE in it so a gate can run it.
//
// The header prints the product name as two words in two colours: "GEDDO" in the accent,
// "AEQ" in the light ink.  Two `drawText` calls need the width of the first, and the width
// of a word drawn with extra kerning is not the width of the same word measured without it.
// That is not a rounding difference: at 3.4 px of tracking, five glyphs, the drawn word is
// wider than the measurement, and 1.2.37 placed the second word from the measurement - so
// the tracking ate the space between them and the window read `GEDDOAEQ`.
//
// Two numbers, and they are not the same number:
//
//   ink width     untracked + tracking * (glyphs - 1)   what the eye sees the word occupy
//   drawn advance untracked + tracking * glyphs         what the NEXT word starts after
//
// JUCE's own answer for the advance is `Font::getStringWidthFloat`:
//   w + height * scale * extraKerningFactor * text.length()      (juce_Font.cpp, 8.0.4)
// i.e. the tracking is added once per glyph, including the last.  Laying a second word out
// needs the advance, so that is what this header computes - and a test pins both numbers,
// because using the ink width for the advance is exactly the 5.6 px mistake of 1.2.37.
#pragma once

namespace aeq {
namespace ui {

// What the eye sees `glyphs` characters occupy when drawn with `tracking` px between them.
// The gap after the last glyph is not ink, so it is not counted.
inline float trackedInkWidth (float untracked, int glyphs, float tracking) noexcept
{
    return untracked + tracking * float (glyphs > 1 ? glyphs - 1 : 0);
}

// How far a `drawText` of that run advances - JUCE adds the kerning once per glyph, so this
// is the number the next word has to start after, not `trackedInkWidth`.
inline float drawnAdvance (float untracked, int glyphs, float tracking) noexcept
{
    return untracked + tracking * float (glyphs > 0 ? glyphs : 0);
}

// Where the second word starts, given the first word's *untracked* measured width.
inline float secondWordX (float untrackedFirst, int firstGlyphs, float tracking,
                          float gap) noexcept
{
    return drawnAdvance (untrackedFirst, firstGlyphs, tracking) + gap;
}

} // namespace ui
} // namespace aeq
