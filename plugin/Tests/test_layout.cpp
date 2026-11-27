// test_layout.cpp - the band row and the footer never overlap, at any band count.
//
// The screenshot the user sent is the specification for this file: at **7 bands** the cells
// read "+3.4 dB Q 2.20" on top of each other, and at the footer "100.0 dB" (the stabiliser
// readout) ran under the strength dial's "100%".  Both are the same bug - a fixed offset
// inside a row whose contents vary - and both are arithmetic, so both are checked here for
// every band count the product can produce (1..8) and every window size it allows.
//
// What this does NOT check: whether a string is pretty.  It checks that the strings a cell
// draws have room between the 12 px paddings, that the footer's sentence box does not reach
// the dial group, and that the dial group does not reach the mode keys.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ui/Layout.h"
#include "ui/PanelCopy.h"

namespace {

int passed = 0, failed = 0;

void check (bool ok, const char* what) {
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) ++passed; else ++failed;
}

// A stand-in for aeq::ui::measure: the advance of a string at the panel's real type sizes,
// summed from per-character widths measured on the shipped window (Cairo SemiBold 26 px for
// the numbers, Cairo 15 px for the micro line).  It is wider than the truth on purpose - a
// layout that fits this also fits the font.
float tightNumberWidth (const std::string& s) { return 15.0f * float (s.size()); }
float tightMicroWidth (const std::string& s) { return 8.6f * float (s.size()); }
// ... and 14 px per glyph for the 26 px values in the footer, which is wider than Cairo's
// tabular digits at that size.
float tightValueWidth (const std::string& s) { return 14.0f * float (s.size()); }

} // namespace

int main() {
    using namespace aeq::ui::layout;

    const float rows[] = { 940.0f, 1024.0f, 1180.0f, 1240.0f, 1440.0f };
    const int bands[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const float gapPx = 10.0f;
    const float margin = 22.0f;
    // The panel's own design box and margins.  A test that invents these is a test of its own
    // arithmetic: the first version of this file used 22 px margins and design widths with
    // scale 1 and passed a footer that had 18 px of sentence at the window's smallest size.
    const float kDesignW = 1180.0f, kDesignH = 700.0f, kMargin = 22.0f;
    // the real band card, at the design window: 96 design px (CHIPS 536..632).  The old
    // 80 was this test's own invention and it hid a card that could not hold its rows.
    const float cellH = 96.0f;
    const float typeScale = 1.0f;

    // ---- 0. a card's rows stay out of each other's way ------------------------
    {
        bool fits = true;
        float worstCell = 1e9f, worstHeader = 0.0f;
        for (const float rowW : rows)
            for (const int n : bands) {
                const float cellW = ((rowW - 2.0f * margin) - gapPx * float (n - 1)) / float (n);
                if (cellW < worstCell) worstCell = cellW;
                const float inner = cellW - 2.0f * (cellW < 150.0f ? 9.0f : 12.0f);
                const float need = tightMicroWidth ("01") + 8.0f + tightMicroWidth ("STATIC");
                if (need - inner > worstHeader) worstHeader = need - inner;
                const auto r = chipRows (cellW, cellH, tightMicroWidth ("01"),
                                         tightMicroWidth ("STATIC"),
                                         tightMicroWidth ("371 Hz  \xc2\xb7  Q 0.90"),
                                         tightMicroWidth ("371 Hz"), typeScale);
                fits = fits && chipFits (r, cellH);
            }
        (void) worstCell;
        check (fits, "a card's header, value and micro rows never overlap (5 widths x 8 counts)");
        // the header is 15 px type on both ends and it does not fit at 8 bands; the card must
        // then drop to the index alone rather than print two words on top of each other
        // ... and when a card really is too narrow for its header, chipFits() says so instead
        // of the paint code printing two words on top of each other
        {
            const auto tiny = chipRows (80.0f, cellH, tightMicroWidth ("01"),
                                        tightMicroWidth ("STATIC"), 200.0f, 60.0f);
            check (! chipFits (tiny, cellH),
                   "a card too narrow for its header is reported unfittable, not overprinted");
        }
    }

    // ---- 0a. the measurement walks UTF-8, not bytes ---------------------------
    // `advance()` is what every arithmetic gate in this repo measures text with, and it used to
    // read one *byte* at a time.  The separator in "1.25 kHz  \xc2\xb7  Q 1.40" is two bytes of
    // UTF-8, so it was charged the 0.55 em fallback twice instead of Cairo's 0.219 em: a
    // 114.09 px line measured 127.30 px, 13 px too wide, and the seven-band card that drew
    // correctly was reported as over its own edge.  The numbers below are Cairo's own, read out
    // of the shipped TTF with fontTools - not this test's arithmetic.
    {
        struct { const char* text; float em; } truth[] = {
            { "a \xc2\xb7 b", 0.502f + 0.220f + 0.219f + 0.220f + 0.527f },   // 25.32 px at 15
            { "1.25 kHz  \xc2\xb7  Q 1.40", 7.606f },                          // 114.09
            { "105 Hz  \xc2\xb7  Q 1.10", 6.907f },                            // 103.61
        };
        bool ok = true;
        std::string worst;
        for (const auto& t : truth) {
            const float want = t.em * 15.0f;
            const float got  = aeq::ui::metrics::advance (t.text, 15.0f);
            if (std::abs (got - want) > 0.15f && ok) {
                ok = false;
                char buf[160];
                std::snprintf (buf, sizeof buf, "%s measured %.2f, Cairo says %.2f",
                               t.text, got, want);
                worst = buf;
            }
        }
        check (ok, (std::string ("the measurement agrees with the font it draws with, separator "
                                 "and all") + (ok ? "" : " - " + worst)).c_str());
        // ... and the separator is *one* glyph, not two bytes' worth: the defect reads 8.25 px
        // high on this string, an order of magnitude above the tolerance above
        const float sep = aeq::ui::metrics::advance ("a \xc2\xb7 b", 15.0f);
        check (std::abs (sep - 25.32f) < 0.15f,
               "a UTF-8 separator is one glyph wide, not one fallback per byte");
    }

    // ---- 0c. a form clears the card by a margin, not by epsilon ----------------
    // The 1.2.72 failure on the Windows runner was a seven-band card whose caption fitted its
    // box by **0.91 px**.  It drew correctly; it was not *decided* correctly, because 0.9 px
    // is inside the disagreement between two shapers and two hinting engines.  `chipRows()`
    // now asks for `kChipFitSlack` of room before it will draw a form at all, and this walks
    // the full matrix to prove the margin is real rather than the epsilon being renamed.
    {
        struct Sized { int bands; float rowW, winH; };
        float worst = 1e9f;
        std::string where;
        int forms[4] = { 0, 0, 0, 0 };
        for (int n = 1; n <= 8; ++n)
            for (const float rowW : rows)
                for (const float winH : { 620.0f, 660.0f, 700.0f, 800.0f, 930.0f, 1160.0f }) {
                    const float sy = winH / 700.0f;
                    const float sx = rowW / 1180.0f;
                    const float cardH = cellH * sy;
                    const float scale = cardH / kChipsDesignH;
                    const float cellW = ((rowW - 2.0f * margin) - gapPx * float (n - 1))
                                        / float (n);
                    const float full = tightMicroWidth ("9.40 kHz  \xc2\xb7  Q 3.20") * sy;
                    const float freq = tightMicroWidth ("9.40 kHz") * sy;
                    const auto r = chipRows (cellW, cardH, tightMicroWidth ("01") * sy,
                                             tightMicroWidth ("STATIC") * sy, full, freq, scale);
                    (void) sx;
                    ++forms[r.microForm];
                    const float inner = cellW - 2.0f * r.padX;
                    const float draw = r.microForm == 0 ? full
                                     : (r.microForm == 1 ? freq : 0.0f);
                    if (r.microForm >= 2) continue;          // nothing is drawn: no margin to test
                    const float room = inner - draw;
                    if (room < worst) {
                        worst = room;
                        char buf[128];
                        std::snprintf (buf, sizeof buf, "%d bands, %.0f px row, h %.0f",
                                       n, rowW, winH);
                        where = buf;
                    }
                }
        // The floor is the constant itself, not a number that happens to be below the worst
        // margin the shipped geometry gives: a gate that asserts `worst >= 1.0` passes with
        // `kChipFitSlack = 0` (the epsilon rule, worst 1.142 px) and certifies the very defect
        // it was written for.  Building the floor from the constant makes the constant the
        // subject of the test.
        check (kChipFitSlack >= 4.0f && worst >= kChipFitSlack - 0.01f,
               (std::string ("every form the card draws clears its card by the fit margin, not "
                             "by epsilon") + " - worst " + std::to_string (worst).substr (0, 5)
                + " px at " + where).c_str());
        // ... and the margin is not bought by drawing nothing: the full form is still the
        // common case at the sizes the panel actually ships, which is what the shot shows
        check (forms[0] > forms[1] + forms[2] + forms[3],
               "the full kHz/Q caption is still the form most cards draw, not the exception");
    }

    // ---- 1. the micro line has a form at every width ---------------------------
    {
        bool ok = true;
        int forms[3] = { 0, 0, 0 };
        for (const float rowW : rows)
            for (const int n : bands) {
                const float cellW = ((rowW - 2.0f * margin) - gapPx * float (n - 1)) / float (n);
                const auto r = chipRows (cellW, cellH, 18.0f, 62.0f,
                                         tightMicroWidth ("371 Hz  \xc2\xb7  Q 0.90"),
                                         tightMicroWidth ("371 Hz"), typeScale);
                ++forms[r.microForm];
                ok = ok && r.microForm >= 0 && r.microForm <= 2;
            }
        check (ok, "every card picks a micro form: full, frequency only, or none");
        check (forms[0] > 0 && forms[1] > 0,
               "both usable forms occur across the widths the plugin allows");
        {
            const auto none = chipRows (70.0f, cellH, 18.0f, 62.0f, 200.0f, 60.0f);
            check (none.microForm == 2, "and a card with no room left prints no micro line");
        }
    }

    // ---- 2. the footer's sentence box -----------------------------------------
    // Modelled as *windows*, not as design widths with scale 1: the panel lays the footer out
    // at its own sx/sy, and the dial group scales with sy while the footer's width scales with
    // sx.  Checking design widths alone (what this did before 1.2.68) is how a sentence box
    // that had shrunk to 18 px at 1000x660 passed every assertion here and broke on the real
    // panel in CI.
    {
        struct Win { float w, h; };
        const Win wins[] = { { 1000, 660 }, { 1024, 700 }, { 1240, 800 }, { 1440, 930 },
                             { 1800, 1160 }, { 1000, 1160 }, { 1800, 660 } };
        bool fits = true;
        float worst = 1e9f;
        for (const auto& win : wins) {
            // the panel's real design box and its real margins - from the editor, not guessed
            const float sx = win.w / kDesignW, sy = win.h / kDesignH;
            const float rowL = kMargin * sx, rowR = win.w - kMargin * sx;
            // two mode keys, 90 design px each, plus the editor's 4 px gap (modeSegDraw)
            const auto f = footerCopy (rowL, rowR, 2.0f * 90.0f * sx + 4.0f * sx, std::min (sx, sy));
            const float width = f.right - f.left;
            if (width < worst) worst = width;
            fits = fits && f.fits && width > 0.0f;
        }
        std::printf ("        worst sentence width %.0f px (needs 120)\n", worst);
        check (fits, "the footer's sentence keeps >= 120 px at every window size the panel "
                     "allows, including the narrow-tall and wide-short corners of its range");
    }

    // ---- 3. a dial's label, value and sub-readout -----------------------------
    {
        bool ok = true;
        for (const float rowW : rows) {
            for (int i = 0; i < kDialColumns; ++i) {
                const auto c = dialColumn (i, rowW - margin, 640.0f, 46.0f, 1.0f);
                // the dial itself: 62 px, must be inside its column, clear of the corner
                const float dia = 62.0f;
                const bool dialInside = c.centreX - dia * 0.5f >= c.left
                                        && c.centreX + dia * 0.5f <= c.right
                                        && c.right <= rowW - margin - 16.0f;
                // label above the value, and the value's box wide enough for "300%"
                const bool stacked = c.labelY + 20.0f <= c.valueY
                                     && c.valueBoxW >= tightValueWidth ("300%");
                ok = ok && dialInside && stacked;
            }
        }
        check (ok, "each dial is inside its column and clears the panel's corner");
        check (ok, "a dial's label sits above its value, and the value's box fits 300%");
    }

    // ---- 3b. the labels fit the column they are drawn in ----------------------
    // The 1.2.67 CI run is why this exists: every check in this file passed while the panel
    // failed "each footer dial clears its neighbour's label", because nothing here ever
    // compared a *label's* width with the box it was drawn in.  A label beside the value
    // needs column width for the dial AND the value AND the word; stacked, it needs the
    // width of the widest word only - and "stabiliser" (82 px at Cairo 15) is the widest
    // word on the row, so it is the one that decides.
    {
        bool ok = true;
        float worstSlack = 1e9f;
        for (const float rowW : rows)
            for (int i = 0; i < kDialColumns; ++i) {
                const auto c = dialColumn (i, rowW - margin, 640.0f, 46.0f, 1.0f);
                const float labelW = tightMicroWidth ("stabiliser");
                const float slack = c.labelBoxW - labelW;
                if (slack < worstSlack) worstSlack = slack;
                // the label's box must be inside the column, left of the dial
                ok = ok && slack >= 0.0f;
                ok = ok && c.centreX - 31.0f - c.labelBoxW >= c.left;
            }
        std::printf ("        worst label slack %.1f px (the widest word on the row)\n", worstSlack);
        check (ok, "the widest dial label fits the column it is drawn in, and stays left of "
                   "its dial - at every window width");
    }

    // ---- 4. the three columns do not overlap ---------------------------------
    {
        float prevLeft = 1e9f;
        bool ok = true;
        for (int i = kDialColumns - 1; i >= 0; --i) {
            const auto c = dialColumn (i, 1240.0f - margin, 640.0f, 46.0f, 1.0f);
            ok = ok && c.right <= prevLeft + 0.5f;
            prevLeft = c.left;
        }
        check (ok, "the three dial columns are disjoint, right to left");
        check (dialGroupLeft (1240.0f - margin, 1.0f)
                   == 1240.0f - margin - kDialCornerInset - 3.0f * kDialColW,
               "the dial group's left edge is the corner inset plus its three columns");
    }

    // ---- 5. the footer's sentence, measured with the font's own advances -------
    // This is the check this file should have had from the start, and the reason it did not:
    // measuring text needs a font, and this gate has no JUCE.  Cairo's advances are now in
    // ui/FontMetrics.h (extracted from the shipped .ttf), so the sentence can be measured
    // here for the first time.  Three releases shipped a footer whose sentence ran through
    // the dial labels while every arithmetic gate in the project was green.
    {
        // The sentences come from `aeq::copy` itself, not from a list typed in here.  The
        // list used to be hand-written, and by 1.2.72 it had gone stale: the panel said
        // "Hearing the whole take...", and this gate was still measuring "Listening against
        // Amir's 15 reference takes..." - twelve strings that no longer existed anywhere in
        // the product.  A gate that measures copies of the copy certifies nothing.
        auto factsFor = [] (int state) {
            aeq::copy::Facts f;
            switch (state) {
                case 0:                                     // nothing heard yet
                    f.haveData = false; break;
                case 1:                                     // hearing, no threshold (the default)
                    f.haveData = true; f.trustSeconds = 0.0; f.heardSeconds = 130.0; break;
                case 2:                                     // hearing, with a threshold
                    f.haveData = true; f.trustSeconds = 45.0; f.heardSeconds = 20.0; break;
                case 3:                                     // heard it all, not yet sealed
                    f.haveData = true; f.settled = true; f.trustSeconds = 0.0;
                    f.heardSeconds = 214.0; break;
                case 4:                                     // sealed, nothing to correct
                    f.haveData = true; f.settled = true; f.frozen = true; f.committed = true;
                    f.bands = 0; break;
                case 5:                                     // sealed before the threshold
                    f.haveData = true; f.settled = false; f.frozen = true; f.committed = true;
                    f.bands = 3; f.heardSeconds = 21.0; f.trustSeconds = 45.0; break;
                case 6:                                     // sealed after it
                    f.haveData = true; f.settled = true; f.frozen = true; f.committed = true;
                    f.bands = 7; f.trustSeconds = 0.0; break;
                case 7:                                     // reopened
                    f.haveData = true; f.settled = false; f.frozen = false; f.committed = true;
                    f.bands = 4; f.trustSeconds = 0.0; break;
                default: break;
            }
            return f;
        };
        struct Win { float w, h; };
        const Win wins[] = { { 1000, 660 }, { 1024, 700 }, { 1240, 800 }, { 1440, 930 },
                             { 1800, 1160 }, { 1000, 1160 }, { 1800, 660 } };
        bool ok = true;
        int worstState = 0;
        for (const auto& win : wins) {
            const float sx = win.w / kDesignW, sy = win.h / kDesignH;
            const float ds = std::min (sx, sy);
            const float boxL = kMargin * sx + 2.0f * 90.0f * sx + 4.0f * sx + 20.0f * ds;
            const float boxR = win.w - kMargin * sx - 16.0f * ds - 3.0f * kDialColW * ds - 16.0f * ds;
            const float boxH = 46.0f * sy;
            // the drawn sentence size: kCap (17 design px) scaled by the window's height
            const float size = 17.0f * ds;   // the size it is drawn at (kCap * min(sx,sy))
            // every state must be representable in at least one of its two written forms
            for (int i = 0; i < 8; ++i) {
                const auto f = factsFor (i);
                const auto longF  = wrapSentence (aeq::copy::verdict (f).c_str(),
                                                  size, boxR - boxL, boxH);
                const auto shortF = wrapSentence (aeq::copy::verdictCompact (f).c_str(),
                                                  size, boxR - boxL, boxH);
                if (! longF.fits && ! shortF.fits) {
                    ok = false;
                    std::printf ("        %gx%g state %d: neither form fits "
                                 "(long %d lines %.0f/%.0f px, short %d lines %.0f/%.0f px)\n",
                                 win.w, win.h, i, longF.lines, longF.needed, boxH,
                                 shortF.lines, shortF.needed, boxH);
                } else {
                    worstState = std::max (worstState, 1);
                }
            }
            // ... and the box itself never collapses
            ok = ok && (boxR - boxL) >= 120.0f && boxH >= 8.0f;
        }
        (void) worstState;
        check (ok, "every footer sentence fits the row it is drawn in, at every window size - "
                   "measured with Cairo's own advances, in one of its two written forms");
    }

    // ---- 6. the caption row: the sentence and the legend do not reach each other ----
    // The 1.2.70 shot of the seven-band window printed "13-TAKE CONSENSUS" through
    // "reference", with a legend dot on top of the word, because the legend was placed after
    // the sentence and from the window's edge.  This walks real window sizes with the
    // strip's own measured width and the legend's, both from FontMetrics.
    {
        struct W { float w, h; };
        const W wins[] = { { 1000, 660 }, { 1024, 700 }, { 1240, 800 }, { 1440, 930 },
                           { 1800, 1160 }, { 1000, 1160 }, { 1800, 660 } };
        bool ok = true;
        float worst = 1e9f;
        for (const auto& win : wins) {
            const float sx = win.w / kDesignW, sy = win.h / kDesignH;
            const float ds = std::min (sx, sy);
            const float rowL = kMargin * sx, rowR = win.w - kMargin * sx;
            // what the strip and the legend actually measure (SemiBold for the strip,
            // the 17 px cap for the legend words)
            const float stripInk = aeq::ui::metrics::advance ("SCANNED 21 s OF 45 s  \xc2\xb7  COARSE",
                                                     15.0f * ds) * 1.10f;
            const float legendInk = (aeq::ui::metrics::advance ("reference", 17.0f * ds)
                                     + aeq::ui::metrics::advance ("measured", 17.0f * ds)
                                     + aeq::ui::metrics::advance ("decision", 17.0f * ds))
                                    + 3.0f * 30.0f * ds + 16.0f * ds;
            const auto row = stripLine (rowL, rowR, stripInk, legendInk, ds);
            const float room = row.textRight - row.textLeft;
            if (room < worst) worst = room;
            // two facts always have to fit: "7 BANDS  ·  13-TAKE CONSENSUS"
            const float need = aeq::ui::metrics::advance ("7 BANDS  \xc2\xb7  13-TAKE CONSENSUS", 15.0f * ds)
                               * 1.10f;
            // The legend's left edge, worked out here from the row's own right edge and the
            // legend's measured ink - NOT read back from stripLine().  Without this the gate
            // passed against a stripLine() that gave the sentence the whole row and let it
            // run under the legend, which is exactly the bug the screenshot showed.
            const float legendLeft = rowR - legendInk;
            ok = ok && row.legendLeft <= legendLeft + 0.5f          // the legend fits the row
                    && row.textRight <= legendLeft - 11.0f * ds     // and the sentence stops first
                    && need <= room + 0.5f                          // the two facts still fit
                    && row.textLeft >= rowL + stripInk;             // and start after the readout
        }
        std::printf ("        worst room for the strip sentence %.0f px\n", worst);
        check (ok, "the strip's sentence fits between the scan readout and the legend, and "
                   "the legend never starts left of it - at every window size");
    }

    std::printf ("layout: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
