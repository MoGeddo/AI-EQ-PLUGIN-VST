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
    float microLikeHeight;  // the height the sub-readout is drawn in (kMicro's line)
    bool  subFits;          // false -> the live dB is not drawn (better than drawn twice)
};

constexpr int kDialColumns = 3;
constexpr float kDialColW = 150.0f;     // >= 2 x (dial 62/2) + 74 of text
// The rightmost column stops this far inside the footer's own right edge: the panel's corner
// radius is 16, and a dial drawn on it is a dial cut by it (the 1.2.59 defect, again).
constexpr float kDialCornerInset = 16.0f;
constexpr float kDialMaxDiameter = 62.0f;   // the design size
constexpr float kDialEdgeAir = 8.0f;        // air between a dial and its column's edges

inline DialColumn dialColumn (int i, float rowRight, float rowTop, float rowH) noexcept
{
    DialColumn c {};
    c.right = rowRight - kDialCornerInset - float (kDialColumns - 1 - i) * kDialColW;
    c.left = c.right - kDialColW;
    // the dial sits at the column's right end, 16 px in from it, so its own 62 px and the
    // panel's corner both fit
    c.centreX = c.right - 16.0f - 31.0f;
    c.valueBoxW = 74.0f;    // "300%" at 26 px is ~56 px
    c.labelY = rowTop + rowH * 0.5f - 26.0f;
    c.valueY = rowTop + rowH * 0.5f - 4.0f;
    c.subY = c.valueY + 22.0f;
    c.microLikeHeight = 16.0f;
    c.subFits = c.subY + 14.0f <= rowTop + rowH + 8.0f;
    return c;
}

// The three columns' left edge: the thing the sentence may not cross.
inline float dialGroupLeft (float rowRight) noexcept
{
    return rowRight - kDialCornerInset - float (kDialColumns) * kDialColW;
}

// ---- the footer's sentence -------------------------------------------------------
struct FooterCopy
{
    float left, right;
    bool  fits;
};

inline FooterCopy footerCopy (float rowLeft, float rowRight, float modeW) noexcept
{
    FooterCopy f {};
    f.left = rowLeft + modeW + 20.0f;
    f.right = dialGroupLeft (rowRight) - 16.0f;
    f.fits = f.left + 120.0f <= f.right;
    return f;
}

} // namespace aeq::ui::layout
