// The wordmark's arithmetic, with no JUCE in it so a gate can run it.
//
// The header prints the product name as two words in two colours: "GEDDO" in the accent,
// "AEQ" in the light ink.  Two `drawText` calls need the width of the first, and the width
// of a word drawn with extra kerning is not the width of the same word measured without it.
//
// That is not a rounding difference.  At 3.4 px of tracking, five glyphs carry four gaps,
// so the drawn word is 13.6 px wider than the measurement - and with an 8 px gap between
// the words the second one started 5.6 px *inside* the first.  The window shipped reading
// "GEDDOAEQ", one word, and no gate saw it: the name gate read the source and the source
// was self-consistent, and the picture gate compared blocks, and a wordmark is not a block.
//
// So the offset has its own function, one line long, and a test that runs it.
#pragma once

namespace aeq {
namespace ui {

// Width of `glyphs` characters as drawn with `tracking` px of extra space between them.
// Ink, not advance: the gap that matters is the one the eye sees, between the last stroke
// of one word and the first of the next, so the trailing gap after the last glyph is not
// counted.
inline float trackedInkWidth (float untracked, int glyphs, float tracking) noexcept
{
    return untracked + tracking * float (glyphs > 1 ? glyphs - 1 : 0);
}

// Where the second word starts, given the first word's *untracked* measured width.
inline float secondWordX (float untrackedFirst, int firstGlyphs, float tracking,
                          float gap) noexcept
{
    return trackedInkWidth (untrackedFirst, firstGlyphs, tracking) + gap;
}

} // namespace ui
} // namespace aeq
