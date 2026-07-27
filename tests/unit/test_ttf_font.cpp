/**
 * Unit Test: BitmapFont::loadTTF — the lift off the 8x8 bitmap font.
 *
 * WHAT  : bakes a real TrueType file into the glyph atlas and checks the metrics that DEFINE the jump.
 *
 * WHY   : "the text looks nicer" is not a testable claim, and a screenshot would not catch a silently
 *         broken bake. What IS testable — and what the 8x8 monospace could never satisfy — is that the
 *         advances become PROPORTIONAL: an 'i' must be narrower than an 'M'. That single assertion
 *         fails on the old font by construction, so it cannot pass by accident.
 *         Also locked: the Latin-1 accents this engine advertises survive the bake, and a failed load
 *         leaves the previous font intact rather than a half-built atlas.
 *
 * HOW   : MockRHIDevice, so the bake runs headless — no GPU, no window. The font itself comes from the
 *         system (no TTF is vendored: shipping one is a content/licensing decision, not an engine one),
 *         so the test SKIPS cleanly when it finds none rather than failing on a machine without it.
 */

#include <catch2/catch_test_macros.hpp>

#include "Text/BitmapFont.h"
#include "../mocks/MockRHIDevice.h"

#include <string>
#include <vector>

using namespace grove;

namespace {
// A few common system faces; the first that loads wins.
const char* kCandidates[] = {
    "C:/Windows/Fonts/arial.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/calibri.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
};
}

TEST_CASE("TTF font: a real face bakes with PROPORTIONAL advances", "[text][unit][ttf]") {
    test::MockRHIDevice device;
    BitmapFont font;

    bool loaded = false;
    std::string used;
    for (const char* p : kCandidates) {
        if (font.loadTTF(device, p, 32.0f)) { loaded = true; used = p; break; }
    }
    if (!loaded) {
        WARN("no system TTF found — skipping the TTF bake check");
        return;
    }
    INFO("font: " << used);

    REQUIRE(font.isValid());
    REQUIRE(font.getBaseSize() == 32.0f);
    REQUIRE(font.getLineHeight() > 0.0f);

    // THE assertion: proportional metrics. The 8x8 bitmap gives every glyph the same advance, so this
    // is exactly the property that separates a real font from the one we are replacing.
    const float iAdv = font.getGlyph('i').advance;
    const float mAdv = font.getGlyph('M').advance;
    INFO("advance i=" << iAdv << " M=" << mAdv);
    REQUIRE(iAdv > 0.0f);
    REQUIRE(mAdv > iAdv);

    // ...and it must show up in measurement too, since that is what alignment and the ellipsis fitter
    // consume. A monospace font would make these two equal.
    REQUIRE(font.measureWidth("MMM") > font.measureWidth("iii"));

    // French accents (Latin-1) survive the bake — the engine advertises them, so they are not optional.
    REQUIRE(font.getGlyph(0x00E9).advance > 0.0f);   // é
    REQUIRE(font.getGlyph(0x00E7).advance > 0.0f);   // ç
}

TEST_CASE("TTF font: a failed load leaves the previous font intact", "[text][unit][ttf]") {
    // A half-initialised atlas would be worse than the fallback: text would vanish or draw garbage.
    // Loading nonsense must be a no-op, not a downgrade.
    test::MockRHIDevice device;
    BitmapFont font;
    REQUIRE(font.initDefault(device));
    const float baseBefore = font.getBaseSize();
    const float advBefore = font.getGlyph('M').advance;

    REQUIRE_FALSE(font.loadTTF(device, "does/not/exist.ttf", 32.0f));

    REQUIRE(font.isValid());
    REQUIRE(font.getBaseSize() == baseBefore);
    REQUIRE(font.getGlyph('M').advance == advBefore);
}
