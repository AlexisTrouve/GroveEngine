/**
 * Unit Tests: text truncation with an ellipsis (grove::text::fitLine) — pure, GPU-free, font-free.
 *
 * WHAT  : how many bytes of a line fit a width budget, and whether an ellipsis is needed.
 *
 * WHY   : a long label ran out of a narrow button and over its neighbours. The fix has to decide where
 *         to cut, and off-by-one there is invisible in a screenshot but obvious in a test. The advance
 *         lookup is a callable, so this exercises the real decision logic with a FAKE metric — no font
 *         atlas, no GPU, no window. That is also what makes the eventual TTF swap safe: this oracle
 *         still holds, only the callable changes.
 *
 * HOW   : a fixed 10px-per-ASCII-glyph metric, so every expected value is countable by hand.
 */

#include <catch2/catch_test_macros.hpp>

#include "Text/TextFit.h"

#include <cstring>
#include <string>

using namespace grove::text;

namespace {
// Every ASCII glyph is 10 wide; the ellipsis is 3 dots = 30.
auto tenPx = [](uint32_t) { return 10.0f; };
constexpr float kEllipsis = 30.0f;

std::string drawn(const char* line, float maxWidth) {
    const FitResult r = fitLine(line, maxWidth, tenPx, kEllipsis);
    std::string s(line, r.bytes);
    if (r.ellipsis) s += "...";
    return s;
}
}

TEST_CASE("Text fit: a line that fits is untouched, no ellipsis", "[text][unit][fit]") {
    const FitResult r = fitLine("ABCD", 100.0f, tenPx, kEllipsis);
    REQUIRE(r.bytes == 4u);
    REQUIRE_FALSE(r.ellipsis);
    REQUIRE(r.width == 40.0f);
}

TEST_CASE("Text fit: exactly filling the budget is NOT truncated", "[text][unit][fit]") {
    // 4 glyphs x 10 = 40 == maxWidth. An off-by-one here would ellipsise text that fits perfectly —
    // the most visible possible bug, on every label whose text happens to match its box.
    const FitResult r = fitLine("ABCD", 40.0f, tenPx, kEllipsis);
    REQUIRE(r.bytes == 4u);
    REQUIRE_FALSE(r.ellipsis);
}

TEST_CASE("Text fit: an overflowing line keeps whole glyphs plus an ellipsis", "[text][unit][fit]") {
    // 12 glyphs = 120 > budget 100. Text budget = 100 - 30 (ellipsis) = 70 -> 7 glyphs.
    // (A 10-glyph string would measure exactly 100 and FIT — see the exact-fit case above.)
    REQUIRE(drawn("ABCDEFGHIJKL", 100.0f) == "ABCDEFG...");
    const FitResult r = fitLine("ABCDEFGHIJKL", 100.0f, tenPx, kEllipsis);
    REQUIRE(r.ellipsis);
    REQUIRE(r.width == 100.0f);        // 70 of text + 30 of ellipsis, i.e. exactly the budget
}

TEST_CASE("Text fit: a partial glyph is never emitted", "[text][unit][fit]") {
    // Budget 95 -> 65 for text: 6 glyphs (60) fit, the 7th (70) does not. Never draw half a letter.
    REQUIRE(drawn("ABCDEFGHIJ", 95.0f) == "ABCDEF...");
}

TEST_CASE("Text fit: a budget too small for the ellipsis shows the ellipsis alone", "[text][unit][fit]") {
    // More honest than a lone random first letter: "there is text here, and it does not fit".
    const FitResult r = fitLine("ABCDEFGHIJ", 20.0f, tenPx, kEllipsis);
    REQUIRE(r.bytes == 0u);
    REQUIRE(r.ellipsis);
}

TEST_CASE("Text fit: maxWidth <= 0 means no limit (the unchanged default)", "[text][unit][fit]") {
    // Every existing caller passes no budget; they must be byte-identical.
    const FitResult r = fitLine("ABCDEFGHIJ", 0.0f, tenPx, kEllipsis);
    REQUIRE(r.bytes == 10u);
    REQUIRE_FALSE(r.ellipsis);
    REQUIRE(r.width == 100.0f);
}

TEST_CASE("Text fit: a line stops at the newline and never consumes it", "[text][unit][fit]") {
    // TextPass calls this per line; swallowing the '\n' would silently join two lines.
    const FitResult r = fitLine("AB\nCD", 0.0f, tenPx, kEllipsis);
    REQUIRE(r.bytes == 2u);
    REQUIRE(r.width == 20.0f);
}

TEST_CASE("Text fit: multi-byte UTF-8 is cut on a codepoint, never mid-sequence", "[text][unit][fit]") {
    // 5x "é" = 5 codepoints / 10 bytes / 50 wide. 50 > 45, so it truncates: text budget = 45 - 30 = 15
    // -> ONE glyph, which is 2 BYTES. Cutting at 1 byte would emit half a sequence and render as
    // garbage — the French accents this engine supports make that a real case, not a theoretical one.
    // (2 "é" would measure 20 and simply FIT — with a 30-wide ellipsis, truncating to a single glyph
    //  is only reachable with a longer string.)
    const FitResult r = fitLine("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 45.0f, tenPx, kEllipsis);
    REQUIRE(r.bytes == 2u);
    REQUIRE(r.ellipsis);
}
