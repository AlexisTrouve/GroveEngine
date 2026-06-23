/**
 * Unit tests for grove::assets::shelfPack (atlas bin-packing, phase 2b). Pure algorithm — no images/GPU.
 * Verifies packed rects stay inside the atlas, never overlap, and that an over-wide rect fails.
 */

#include <catch2/catch_test_macros.hpp>
#include "Assets/ShelfPacker.h"
#include <vector>
#include <cstddef>

using namespace grove::assets;

namespace {
bool overlap(const PackRect& a, const PackRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
void assertNoOverlap(const std::vector<PackRect>& rs, const PackResult& r) {
    for (std::size_t i = 0; i < rs.size(); ++i) {
        REQUIRE(rs[i].x >= 0); REQUIRE(rs[i].y >= 0);
        REQUIRE(rs[i].x + rs[i].w <= r.width);
        REQUIRE(rs[i].y + rs[i].h <= r.height);
        for (std::size_t j = i + 1; j < rs.size(); ++j) REQUIRE_FALSE(overlap(rs[i], rs[j]));
    }
}
} // namespace

TEST_CASE("shelfPack: mixed rects fit the atlas and never overlap", "[assets][unit]") {
    std::vector<PackRect> rects = { {32,32},{64,16},{16,64},{48,48},{8,8},{100,20} };
    PackResult r = shelfPack(rects, 128, 1);
    REQUIRE(r.ok);
    REQUIRE(r.width <= 128);
    assertNoOverlap(rects, r);
}

TEST_CASE("shelfPack: a rect wider than the atlas fails", "[assets][unit]") {
    std::vector<PackRect> rects = { {200,10} };
    REQUIRE_FALSE(shelfPack(rects, 128, 1).ok);
}

TEST_CASE("shelfPack: many same-size icons pack into multiple rows", "[assets][unit]") {
    std::vector<PackRect> rects(20, PackRect{50,50});
    PackResult r = shelfPack(rects, 256, 2);   // ~4 per row -> 20 icons -> ~5 rows
    REQUIRE(r.ok);
    REQUIRE(r.height >= 5 * 50);
    assertNoOverlap(rects, r);
}
