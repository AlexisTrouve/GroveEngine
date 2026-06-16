/**
 * Unit test: BitmapFont — UTF-8 decoding + French accented glyph coverage (engine help A1).
 *
 * Drifterra (and any French/i18n game) renders labels via render:text. The bitmap font
 * only covered ASCII 32-126 AND the text path iterated the string BYTE by byte — so a
 * UTF-8 "é" (0xC3 0xA9) was read as two bytes => two wrong glyphs, double width.
 *
 * This locks two things, headless (MockRHIDevice, no GPU):
 *  1. measureWidth() decodes UTF-8 → one advance per CODEPOINT (not per byte).
 *  2. The French accented codepoints resolve to their OWN glyph (not the '?' fallback),
 *     and uppercase-accented codepoints alias to their base letter (documented 8x8 fallback).
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Text/BitmapFont.h"
#include "../mocks/MockRHIDevice.h"
#include <cmath>

using namespace grove;
using Catch::Matchers::WithinAbs;

namespace {
// A loaded default font on a mock device — no GPU needed.
struct FontFixture {
    test::MockRHIDevice device;
    BitmapFont font;
    FontFixture() { REQUIRE(font.initDefault(device)); }
};
} // namespace

TEST_CASE("BitmapFont: measureWidth counts codepoints, not UTF-8 bytes", "[font][i18n]") {
    FontFixture fx;
    const float adv = fx.font.getBaseSize();  // monospace advance = base cell size

    // "été" = é(2 bytes) t(1) é(2 bytes) = 5 bytes, but 3 codepoints.
    INFO("measureWidth(\"été\")=" << fx.font.measureWidth("été") << " expected " << (3 * adv));
    REQUIRE_THAT(fx.font.measureWidth("été"), WithinAbs(3 * adv, 0.01f));  // pre-fix: 5*adv

    // Pure ASCII unchanged.
    REQUIRE_THAT(fx.font.measureWidth("abc"), WithinAbs(3 * adv, 0.01f));

    // A 3-byte codepoint (€ = E2 82 AC) counts as ONE glyph.
    REQUIRE_THAT(fx.font.measureWidth("\xE2\x82\xAC"), WithinAbs(1 * adv, 0.01f));  // pre-fix: 3*adv
}

TEST_CASE("BitmapFont: French lowercase accents have their own glyph", "[font][i18n]") {
    FontFixture fx;

    // Two glyphs share a cell only if BOTH atlas coords match (same column => same u0,
    // so u0 alone is not enough — must also differ in v0/row).
    auto sameCell = [](const GlyphInfo& a, const GlyphInfo& b) {
        return std::abs(a.u0 - b.u0) < 0.0001f && std::abs(a.v0 - b.v0) < 0.0001f;
    };

    const GlyphInfo& q = fx.font.getGlyph('?');  // the fallback glyph
    const GlyphInfo& e = fx.font.getGlyph('e');  // base letter

    // 'é' must resolve to a DEDICATED cell — neither the '?' fallback nor plain 'e'.
    const GlyphInfo& eAcute = fx.font.getGlyph(0xE9);  // 'é' U+00E9
    REQUIRE_FALSE(sameCell(eAcute, q));
    REQUIRE_FALSE(sameCell(eAcute, e));

    // The essential French set must also be present (≠ fallback cell).
    for (uint32_t cp : {0xE8u /*è*/, 0xEAu /*ê*/, 0xE0u /*à*/, 0xE7u /*ç*/, 0xF4u /*ô*/, 0xFBu /*û*/, 0xEFu /*ï*/}) {
        INFO("codepoint " << cp);
        REQUIRE_FALSE(sameCell(fx.font.getGlyph(cp), q));
    }
}

TEST_CASE("BitmapFont: uppercase accents alias to their base letter (8x8 fallback)", "[font][i18n]") {
    FontFixture fx;
    // No room for an accent above a full-height capital in 8x8 → documented fallback:
    // À/Â/Ä→A, Ç→C, È/É/Ê/Ë→E, etc. Legible (sans accent) beats a '?' tofu.
    REQUIRE_THAT(fx.font.getGlyph(0xC0).u0, WithinAbs(fx.font.getGlyph('A').u0, 0.0001f)); // À→A
    REQUIRE_THAT(fx.font.getGlyph(0xC9).u0, WithinAbs(fx.font.getGlyph('E').u0, 0.0001f)); // É→E
    REQUIRE_THAT(fx.font.getGlyph(0xC7).u0, WithinAbs(fx.font.getGlyph('C').u0, 0.0001f)); // Ç→C
}
