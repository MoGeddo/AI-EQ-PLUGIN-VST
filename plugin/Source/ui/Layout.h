#pragma once
// The band card and the footer dial group, as arithmetic.
//
// The screenshot the user sent (7 bands) shows three collisions at once, and all three are the
// same bug: a *fixed* offset inside a row whose contents vary.  The band row's cells were
// `(rowWidth - gap * (n - 1)) / n`, so at seven bands a cell is ~150 px wide while it still drew
// a 26 px number, a "dB" beside it and a right-aligned 150 px box holding "371 Hz · Q 0.90" -
// they landed on each other ("+3.4 dB Q 2.20" printed on top of itself).  In the footer the
// dials' labels, their values and the stabiliser's live dB were placed from constants (370 /
// 202 / 52 px from the right edge, a 60 px value box, a 14 px vertical step for a 26 px number),
// so "100.0 dB" ran under the strength dial's "100%".
//
// So the numbers come out of the paint code and live here, where `test_layout.cpp` can walk
// every band count (1..8) and every window width and check the relations:
//
//   0. a card's three rows never overlap each other, and none leaves the card
//   1. the micro line has a form that fits, at every cell width (full / freq / none)
//   2. the footer's sentence box never reaches the dial group
//   3. a dial's label, value and sub-readout never overlap
//   4. the rightmost dial clears the panel's rounded corner
//
// Widths are *measured* by the caller with the font it draws with (aeq::ui::measure); this
// header only compares those numbers.
#include <algorithm>
#include <cstddef>

#include "FontMetrics.h"

namespace aeq::ui::layout {

// ---- the band card ---------------------------------------------------------------
// Three rows and a rule, the way the reference's kit-03 cards stack a title, a value and a
// caption.  Everything is a function of the card's own box, so a narrow cell re-flows instead
// of overprinting.
struct ChipRows
{
    float padX;
    float headerY, headerH;   // "01" left, "STATIC" right
    float ruleY;              // the hairline under the header
    float valueY, valueH;     // the gain, kValue type
    float microY, microH;     // "371 Hz · Q 0.90" and its two shorter forms
    int   microForm;          // 0 = full, 1 = frequency only, 2 = nothing
    bool  headerFits;         // the header can be drawn: the index always has room
    bool  modeFits;           // ... and the mode word fits beside it (dropped when it does not)
};

// The design type sizes the card draws with, and the row's design height.  These are *inputs*
// to the arithmetic now, not assumptions inside it: `chipRows` used to hard-code `valueH 32`
// and `microY = cellH - 5 - 14` for an 80 px card, and the real card is 80 design px tall at
// the design window but only **69** at 1024x700 - so the micro line was placed 11 px below
// where the layout thought it was, straight through the number.  A card that is laid out
// from its own box has to be told what the type inside it is.
constexpr float kChipValueType = 26.0f;   // kValue at design size
constexpr float kChipMicroType = 15.0f;   // kMicro at design size
constexpr float kChipPadY      = 7.0f;    // top air, and the same under the micro line
constexpr float kChipGapY      = 3.0f;    // between the header rule and the value
// The header row's height, in the same units as the type it holds.
//
// It was a flat 16 while the row's type is `kChipMicroType` (15) drawn at a height of
// ascent + descent - 1.874 em for this face, i.e. ~28 px for a 15 px em - and the drawer
// centres the index and the mode word inside this box.  So the box was always shorter than
// the glyphs it was centring, and `chipCaptionsFit()` asked a question that could only be
// answered "no": `rows.headerH + 0.5 < font getHeight()` was true at **every** window size,
// which is why the seven-band caption check failed on 1.2.72 and 1.2.73 and every release
// before them.  A row tall enough for its own type is not a nicety: it is what "the caption
// fits its card" means.
constexpr float kChipHeaderH   = 16.0f;
// ... and the pitch a line of `kChipMicroType` occupies in this card.
//
// This is **1.25**, the same multiplier `microH` uses two lines below, and not the face's
// 1.874 em ascent + descent that `aeq::ui::font` converts through.  Those are two different
// numbers for two different jobs: 1.874 is the *box* JUCE allocates for a line, which
// includes the leading above the ascender and below the descender and which nothing on this
// card draws into; 1.25 is the *pitch* the card has always laid its micro lines on, and the
// header holds the same size of type as the micro row.  Using 1.874 here asked every card
// for 28 px of header where its own design has 96 px for 98 px of content - the card could
// not pay, and the header took 26 px of the 32 it had claimed.  Same type, same pitch.
constexpr float kChipLineEm    = 1.25f;
// the card's horizontal air, before the type scale.  One value, not a 9/12 step at 150 px -
// see `chipRows` for the 5.87 px inward cliff that step produced.
constexpr float kChipPadX      = 10.0f;
// The room a form must have before the card will draw it.  Below this the caption is not
// "tight", it is inside the shaper's rounding: HarfBuzz, FreeType and the pixel grid disagree
// by more than a pixel on the same string, so a caption that fits by 0.9 px fits on one
// machine and not on the next.  4 px is the smallest margin that is not a coincidence.
constexpr float kChipFitSlack  = 4.0f;
// the band card's design height, so the paint code can ask 'how big is my type here'
constexpr float kChipsDesignH  = 96.0f;

// `cellW`, `cellH`: the card, in window px.  `typeScale` = the card's own type scale
// (height/design at the design window, min(sx,sy) otherwise): the type inside a card is sized
// by the window, so its pitch is too.  `indexW`, `modeW`, `fullW`, `freqW`: measured advances.
// How tall a card's header row wants to be, at that card's type scale.  A function and not
// just a local in `chipRows`, because it is the *claim* a test has to be able to see: the
// header gives space back when a card is short, so the height it ends up with cannot tell a
// correct `kChipHeaderH` from the flat 16 px that made every seven-band caption check fail.
inline float chipHeaderWants (float typeScale) noexcept
{
    const float line = kChipMicroType * kChipLineEm;
    return (line > kChipHeaderH ? line : kChipHeaderH) * typeScale;
}

inline ChipRows chipRows (float cellW, float cellH, float indexW, float modeW,
                          float fullW, float freqW, float typeScale = 1.0f) noexcept
{
    ChipRows r {};
    // The horizontal pad scales with the card's type, like `padY`, `gapY` and `headerH` below.
    // It was the one length in this row that did not: a flat 9 or 12 px whatever the type was.
    // At 1800x1160 / 8 bands the caption grows to 178.13 px while the pad stays 9, leaving
    // **0.87 px** of room in a 179.0 px inner box - the same knife-edge that made the 1.2.72
    // seven-band card fail on one machine and pass on another.  A pad that does not scale with
    // the text it pads is not air, it is a fixed share that shrinks as the type grows.
    // ... and it is one pad, not two.  The step at 150 px (`9` under it, `12` over it) is a
    // cliff: at 1.146 scale a 149 px card has 128.38 px inside and a 150 px card has 122.50 -
    // a wider card with less room, exactly where the caption is tightest.  The step also lands
    // mid-range for 6-8 bands, so whether the kHz/Q line survives depended on which side of
    // 150 the window's width put the card.  A single pad has no cliff to fall off.
    r.padX = kChipPadX * typeScale;
    const float padY    = kChipPadY * typeScale;
    const float gapY    = kChipGapY * typeScale;
    const float lineH   = kChipMicroType * typeScale * 1.25f;   // one micro line's pitch
    // The header holds the index and the mode word at `kChipMicroType`, so it wants that
    // type's own line height - but it is the row that can give space back, and it must:
    // the value and the micro line are the content, and a header wide enough for its type
    // that pushes the number out of the card is a different way of being wrong.  So the
    // header claims its line height *if the card can pay for it*, and otherwise takes what
    // The card is four rows and a rule, and it is laid out **from the foot upwards**,
    // because that is the order it is read in from below: the micro line is the last row, the
    // number sits directly above it, the rule above that, and the header takes whatever is
    // left at the top.
    //
    // Laying it out from the top down - header first, everything else after - is what
    // produced the collision this replaces: the header was a flat `kChipHeaderH * typeScale`
    // (16 design px) for a 15 px em of type whose ascent + descent is 1.874 em (~28 px), so
    // it was always too short for the content it centred, `chipCaptionsFit()` asked exactly
    // that question and got "no" at **every** window size on **every** release, and giving
    // it its true height then pushed the number out through its own caption.  Both were the
    // same mistake: three rows each claiming a height, with nothing measuring the sum.
    const float headerWants = chipHeaderWants (typeScale);
    const float ruleNeed = 2.0f * typeScale;
    const float gapNeed  = kChipGapY * typeScale;
    r.valueH = kChipValueType * typeScale * 1.25f;
    r.microH = lineH;

    // the content block, measured from the foot up...
    r.microY  = cellH - padY - r.microH;
    r.valueY  = r.microY - gapNeed - r.valueH;
    r.ruleY   = r.valueY - gapNeed;
    // ... and the header takes the rest, capped at its own line height.  When the card
    // cannot pay for the content at all (a 113 px card at 8 bands of a tall window is the
    // tightest the resize range allows) the *content* is what gives: the value keeps its
    // line, the micro line is dropped - `microForm = 3` says so, and it is a state the
    // drawer already handles by printing no caption.  A layout that silently overlaps two
    // rows because the box was too small is the bug this whole block exists to prevent.
    r.headerY = padY;
    const float headerRoom = r.ruleY - ruleNeed - r.headerY;
    r.headerH = headerRoom >= headerWants ? headerWants
              : (headerRoom > 0.0f ? headerRoom : 0.0f);

    const float inner = cellW - 2.0f * r.padX;
    // A form has to clear by `kChipFitSlack`, not by epsilon.  The rule used to be
    // `inner >= fullW`, so a caption that fitted by 0.09 px was drawn in full and a caption
    // that fitted by 0.87 px was drawn in full too - both of them living inside the shaper's
    // own rounding, which is why the same 7-band card passed on this machine and failed on the
    // Windows runner.  "Fits" has to mean "fits with room to spare", or it means "fits
    // according to the machine you asked".
    if (r.microForm == 0)
        r.microForm = inner >= fullW + kChipFitSlack ? 0
                    : (inner >= freqW + kChipFitSlack ? 1 : 2);
    // The header's two words are measured by the *drawer*, at the size the drawer uses, and
    // that size follows the window's height (`kCap * sy`) while the card's width follows its
    // width.  At 1000x1160 - the tallest, narrowest corner of the resize range - the type is
    // 1.66x and the card is 113 px, so `STATIC` alone is 84 px and `01 + STATIC` cannot be
    // laid out in 80 px of inner box.  The old rule then refused the header, `chipFits()`
    // went false, and the seven-band caption gate failed on every release since 1.2.72 for a
    // card that **drew correctly**: the drawer skips the mode word when `headerFits` is
    // false, which is exactly the right behaviour and exactly what the gate was not told.
    //
    // So the gate is not "is the header full", it is "can the header be drawn at all" - the
    // index must fit, and the mode word is dropped when it does not.  That is what the
    // drawer does; a gate that asks for more than the drawer promises is a gate that lies.
    r.headerFits   = indexW + kChipFitSlack <= inner;   // the index must always fit
    r.modeFits     = indexW + 8.0f + modeW + kChipFitSlack <= inner;   // the word is optional
    return r;
}

// Do the rows stay out of each other's way, and inside the card?
inline bool chipFits (const ChipRows& r, float cellH) noexcept
{
    return r.headerY + r.headerH <= r.ruleY
        && r.ruleY <= r.valueY
        && r.valueY + r.valueH <= r.microY + 0.5f
        && r.microY + r.microH <= cellH + 0.5f
        && r.headerFits
        && r.microForm >= 0 && r.microForm <= 2;   // 3 = the card is too short: not a fit
}

// ---- the footer's dial group ------------------------------------------------------
// Three columns, laid right-to-left, each holding a label / a value / an optional sub-readout
// *around* its dial.  The column is what the dial may not grow beyond; the dial's diameter is
// the smaller of what the row's height allows and what the column allows.
struct DialColumn
{
    float left, right;      // the column
    float centreX;          // where the dial is drawn
    float labelY, valueY, subY;
    float valueBoxW;
    float labelBoxW;        // the label's own box: left of the dial, inside the column
    float microLikeHeight;  // the height the sub-readout is drawn in (kMicro's line)
    bool  subFits;          // false -> the live dB is not drawn (better than drawn twice)
};

constexpr int kDialColumns = 3;
// The column has to hold the dial (62) + the inset that keeps it off the corner (16) + the
// widest label under it ("stabiliser", 82 px of Cairo 15) with air between them.  150 was the
// first number and it forgot the label: with 62 + 16 there were 72 px left for a word that
// needs 82, so the label's box crossed into the previous column's dial.  The 1.2.67 CI run
// failed on exactly that ("each footer dial clears its neighbour's label", at 1240, 1024 and
// 940), which is why the number and the assertion are both here now.
constexpr float kDialColW = 186.0f;
// The rightmost column stops this far inside the footer's own right edge: the panel's corner
// radius is 16, and a dial drawn on it is a dial cut by it (the 1.2.59 defect, again).
constexpr float kDialCornerInset = 16.0f;
constexpr float kDialMaxDiameter = 62.0f;   // the design size
constexpr float kDialEdgeAir = 8.0f;        // air between a dial and the label beside it

// `scale` is the dial group's own scale, and it must be min(sx, sy) - the smaller of the
// window's two scales, not its height scale, and never 1.
//
// Two bugs, one lesson, both found by the layout test after shipping:
//   * the group was laid out in design pixels while its row is in window pixels, so at 940 px
//     the three columns took 450 of the 896 px footer and the sentence had 52 px ("each
//     footer dial clears its neighbour's label", 1.2.67 CI);
//   * scaling it by sy alone then starved a *tall narrow* window (1000x1160: sy/sx ~ 2, the
//     group wanted 934 px of a 963 px row and the sentence went to -204 px).
// The row's width scales with sx and its height with sy, so whatever sits in the row and may
// not eat it has to follow the smaller one.
inline DialColumn dialColumn (int i, float rowRight, float rowTop, float rowH,
                              float scale = 1.0f) noexcept
{
    DialColumn c {};
    const float colW  = kDialColW * scale;
    const float inset = kDialCornerInset * scale;
    const float dial  = kDialMaxDiameter * scale;
    const float air   = kDialEdgeAir * scale;
    c.right = rowRight - inset - float (kDialColumns - 1 - i) * colW;
    c.left = c.right - colW;
    // the dial sits at the column's right end, so its own diameter and the panel's corner
    // both fit; the label and the value live in what is left of the column
    c.centreX = c.right - inset - dial * 0.5f;
    c.valueBoxW = 74.0f * scale;    // "300%" at 26 px is ~56 px
    c.labelBoxW = std::max (0.0f, c.centreX - dial * 0.5f - air - c.left);
    c.labelY = rowTop + rowH * 0.5f - 26.0f * scale;
    c.valueY = rowTop + rowH * 0.5f - 4.0f * scale;
    c.subY = c.valueY + 22.0f * scale;
    c.microLikeHeight = 16.0f * scale;
    c.subFits = c.subY + 14.0f * scale <= rowTop + rowH + 8.0f * scale;
    return c;
}

// The three columns' left edge: the thing the sentence may not cross.
inline float dialGroupLeft (float rowRight, float scale = 1.0f) noexcept
{
    return rowRight - kDialCornerInset * scale - float (kDialColumns) * kDialColW * scale;
}

// ---- the footer's sentence -------------------------------------------------------
struct FooterCopy
{
    float left, right;
    bool  fits;
};

inline FooterCopy footerCopy (float rowLeft, float rowRight, float modeW, float scale = 1.0f) noexcept
{
    FooterCopy f {};
    f.left = rowLeft + modeW + 20.0f * scale;
    f.right = dialGroupLeft (rowRight, scale) - 16.0f * scale;
    f.fits = f.left + 120.0f <= f.right;
    return f;
}

// ---- the footer's sentence, as geometry ------------------------------------------
// The sentence is drawn with `wrapWords` (ArabicText.cpp) at kCap * sy with a 1.25 pitch, and
// it is wrong for it to be the only thing on the panel that no arithmetic gate measures.
// These two functions reproduce that wrap with the font's own advances (ui/FontMetrics.h), so
// a test can answer "does this sentence fit this box" without a window.
struct Sentence { int lines; float widest, needed; bool fits; };

// One line, not a paragraph: the strip's sentence and its legend are single lines that must
// not reach each other.  `stripLine()` is the layout for the caption row under the plot -
// what was scanned on the left, the legend on the right, and what is left between them for
// the sentence.  On the 7-band window that sentence printed through the legend, because the
// legend was placed *after* it and from the window's edge rather than from the sentence's.
struct StripRow
{
    float textLeft, textRight;   // where the sentence may be drawn
    float legendLeft;            // where the legend starts
    bool  fits;                  // the sentence's two facts have room
};

inline StripRow stripLine (float rowLeft, float rowRight, float stripInk, float legendInk,
                           float scale = 1.0f) noexcept
{
    StripRow r {};
    r.legendLeft = rowRight - legendInk;
    r.textLeft   = rowLeft + stripInk + 20.0f * scale;
    r.textRight  = r.legendLeft - 12.0f * scale;
    r.fits       = r.textRight - r.textLeft >= 150.0f * scale;
    return r;
}

inline Sentence wrapSentence (const char* s, float sizePx, float boxW, float boxH) noexcept
{
    Sentence out { 0, 0.0f, 0.0f, false };
    if (s == nullptr || *s == '\0' || boxW < 8.0f) return out;
    float lineW = 0.0f;
    const char* lineStart = s;
    const char* wordStart = s;
    auto hasWord = false;
    auto flushWord = [&] (const char* end) {
        const std::size_t n = std::size_t (end - wordStart);
        const float w = metrics::advance (wordStart, n, sizePx);
        const float space = hasWord ? metrics::advance (" ", 1, sizePx) : 0.0f;
        if (hasWord && lineW + space + w > boxW) {              // wrap before this word
            if (lineW > out.widest) out.widest = lineW;
            ++out.lines;
            lineW = w;
        } else {
            lineW += space + w;
        }
        hasWord = true;
    };
    for (const char* p = s; ; ++p) {
        if (*p == ' ' || *p == '\0') {
            if (p > wordStart) flushWord (p);
            if (*p == '\0') break;
            wordStart = p + 1;
        }
    }
    if (lineW > out.widest) out.widest = lineW;
    if (hasWord) ++out.lines;
    out.needed = float (out.lines) * sizePx * 1.25f;
    out.fits = out.lines > 0 && out.needed <= boxH + 0.5f;
    (void) lineStart;
    return out;
}

} // namespace aeq::ui::layout
