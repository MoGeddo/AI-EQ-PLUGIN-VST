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
    bool  headerFits;         // the index and the mode word do not collide
};

// `cellW`, `cellH`: the card.  `indexW`, `modeW`, `fullW`, `freqW`: measured advances.
inline ChipRows chipRows (float cellW, float cellH, float indexW, float modeW,
                          float fullW, float freqW) noexcept
{
    ChipRows r {};
    r.padX = cellW < 150.0f ? 9.0f : 12.0f;
    r.headerH = 16.0f;
    r.headerY = 7.0f;
    r.ruleY = r.headerY + r.headerH + 2.0f;
    r.valueH = 32.0f;
    r.valueY = r.ruleY + 4.0f;
    r.microH = 14.0f;
    r.microY = cellH - 5.0f - r.microH;
    // the micro line gets the card's full inner width - that is what makes it fit at 7 bands
    const float inner = cellW - 2.0f * r.padX;
    r.microForm = inner >= fullW ? 0 : (inner >= freqW ? 1 : 2);
    r.headerFits = indexW + 8.0f + modeW <= inner;
    return r;
}

// Do the rows stay out of each other's way, and inside the card?
inline bool chipFits (const ChipRows& r, float cellH) noexcept
{
    return r.headerY + r.headerH <= r.ruleY
        && r.ruleY < r.valueY
        && r.valueY + r.valueH <= r.microY + 0.5f
        && r.microY + r.microH <= cellH
        && r.headerFits;
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
