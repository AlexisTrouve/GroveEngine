#pragma once

// ============================================================================
// Text truncation with an ellipsis — PURE, GPU-free, font-agnostic, header-only.
//
// WHAT : given a line and a width budget, how many BYTES of it fit, and does it need an ellipsis?
//
// WHY  : a long label used to run straight out of a narrow button and over whatever sat beside it.
//        Clipping it mid-glyph reads as a rendering bug; an ellipsis reads as "there is more text",
//        which is the truth. Doing it here rather than in the widget means EVERY text consumer gets
//        it (game HUD text too), not just UI widgets.
//
// HOW  : the advance lookup is a CALLABLE, so this file knows nothing about the font, the atlas or
//        the GPU — which is what makes it unit-testable headlessly with a fake metric. It is also
//        what keeps the eventual swap to a real TTF/MSDF font contained: only the callable changes.
//        The ellipsis width is passed in for the same reason.
//
//        Budget rule: a truncated line must fit the ellipsis TOO, so the text part is fitted against
//        (maxWidth - ellipsisWidth). We never emit a partial glyph — the last one that does not fit
//        entirely is dropped.
// ============================================================================

#include "Utf8.h"

#include <cstddef>
#include <cstdint>

namespace grove { namespace text {

struct FitResult {
    size_t bytes = 0;        // bytes of the input to draw (always a whole number of codepoints)
    bool ellipsis = false;   // draw the ellipsis after those bytes?
    float width = 0.0f;      // resulting drawn width, ellipsis INCLUDED — use it to align the line
};

// `advanceOf(codepoint) -> float` gives a glyph's advance in the SAME units as maxWidth (i.e. already
// scaled to the on-screen font size). A line ends at '\n' or '\0'; neither is consumed.
// maxWidth <= 0 means "no limit" — the whole line, no ellipsis (the default for every existing caller).
template <class AdvanceFn>
inline FitResult fitLine(const char* line, float maxWidth, AdvanceFn advanceOf, float ellipsisWidth) {
    FitResult out;
    if (line == nullptr) return out;

    // Measure the full line first: most lines fit, and then there is nothing to decide.
    float full = 0.0f;
    const char* p = line;
    while (*p && *p != '\n') {
        const char* before = p;
        const uint32_t c = decodeUtf8(p);
        full += advanceOf(c);
        (void)before;
    }
    const size_t fullBytes = static_cast<size_t>(p - line);

    if (maxWidth <= 0.0f || full <= maxWidth) {
        out.bytes = fullBytes;
        out.width = full;
        return out;
    }

    // Truncating: reserve the ellipsis, then take whole glyphs while they fit. A budget too small for
    // the ellipsis itself yields an empty text part — we still report the ellipsis, because showing
    // "…" in a sliver is more honest than showing a random first letter.
    const float budget = maxWidth - ellipsisWidth;
    float used = 0.0f;
    p = line;
    const char* lastGood = line;
    while (*p && *p != '\n') {
        const char* before = p;
        const uint32_t c = decodeUtf8(p);
        const float adv = advanceOf(c);
        if (used + adv > budget) { p = before; break; }
        used += adv;
        lastGood = p;
    }

    out.bytes = static_cast<size_t>(lastGood - line);
    out.ellipsis = true;
    out.width = used + ellipsisWidth;
    return out;
}

}} // namespace grove::text
