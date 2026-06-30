/**
 * Unit Tests: grove::mapview recipe — Palette + Filter (map-viewer engine, slice S1c).
 *
 * WHAT  : Locks the two pure bricks of the recipe system (§5): Palette (value -> Rgba: ramp / banded /
 *         categorical) and Filter (a minimal composable predicate value -> bool). These are the modular
 *         colour + filter the viewer is built on.
 *
 * WHY    : A wrong ramp interpolation or band boundary mis-colours every cell; a wrong predicate shows the
 *         wrong cells. Both are pure value functions, so they lock exactly and cheaply here.
 *
 * HOW    : Catch2; colour channels checked with WithinAbs.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/mapview/Filter.h"
#include "grove/mapview/Palette.h"

using namespace grove::mapview;
using Catch::Matchers::WithinAbs;

static const Rgba kBlack{0, 0, 0, 1};
static const Rgba kWhite{1, 1, 1, 1};
static const Rgba kRed{1, 0, 0, 1};
static const Rgba kGreen{0, 1, 0, 1};
static const Rgba kBlue{0, 0, 1, 1};

TEST_CASE("mapview S1c - ramp palette clamps ends and lerps the middle", "[mapview][recipe][unit]") {
    const Palette p = Palette::ramp({{0.0, kBlack}, {100.0, kWhite}});

    // Below/above the range clamp to the end stops.
    REQUIRE_THAT(p.eval(-10.0).r, WithinAbs(0.0f, 1e-6));
    REQUIRE_THAT(p.eval(200.0).r, WithinAbs(1.0f, 1e-6));
    // Midpoint is grey (0.5).
    const Rgba mid = p.eval(50.0);
    REQUIRE_THAT(mid.r, WithinAbs(0.5f, 1e-6));
    REQUIRE_THAT(mid.g, WithinAbs(0.5f, 1e-6));
    REQUIRE_THAT(mid.b, WithinAbs(0.5f, 1e-6));
}

TEST_CASE("mapview S1c - ramp interpolates within the correct segment", "[mapview][recipe][unit]") {
    // black @0 -> red @50 -> blue @100
    const Palette p = Palette::ramp({{0.0, kBlack}, {50.0, kRed}, {100.0, kBlue}});
    const Rgba q = p.eval(25.0);   // halfway in the first segment -> half red
    REQUIRE_THAT(q.r, WithinAbs(0.5f, 1e-6));
    REQUIRE_THAT(q.b, WithinAbs(0.0f, 1e-6));
    const Rgba q2 = p.eval(75.0);  // halfway in the second segment -> half red + half blue
    REQUIRE_THAT(q2.r, WithinAbs(0.5f, 1e-6));
    REQUIRE_THAT(q2.b, WithinAbs(0.5f, 1e-6));
}

TEST_CASE("mapview S1c - banded palette buckets by ascending upper bound", "[mapview][recipe][unit]") {
    // value < 0 -> blue (ocean) ; < 100 -> green (land) ; else white (peak)
    const Palette p = Palette::banded({{0.0, kBlue}, {100.0, kGreen}, {100000.0, kWhite}});
    REQUIRE(p.eval(-5.0).b == 1.0f);     // blue
    REQUIRE(p.eval(50.0).g == 1.0f);     // green
    REQUIRE(p.eval(5000.0).r == 1.0f);   // white
    // Exactly on a boundary goes to the next band (strict <).
    REQUIRE(p.eval(0.0).g == 1.0f);
}

TEST_CASE("mapview S1c - categorical palette indexes a table with a fallback", "[mapview][recipe][unit]") {
    const Palette p = Palette::categorical({kRed, kGreen, kBlue}, kBlack);
    REQUIRE(p.eval(0.0).r == 1.0f);
    REQUIRE(p.eval(1.0).g == 1.0f);
    REQUIRE(p.eval(2.0).b == 1.0f);
    REQUIRE(p.eval(2.4).b == 1.0f);   // rounds to 2
    // Out of range -> fallback (black).
    REQUIRE(p.eval(9.0).r == 0.0f);
    REQUIRE(p.eval(-1.0).r == 0.0f);
}

TEST_CASE("mapview S1c - filter leaf comparisons", "[mapview][recipe][unit]") {
    REQUIRE(Filter::cmp(Filter::Op::Gt, 0.0).eval(1.0));
    REQUIRE_FALSE(Filter::cmp(Filter::Op::Gt, 0.0).eval(0.0));
    REQUIRE(Filter::cmp(Filter::Op::Ge, 0.0).eval(0.0));
    REQUIRE(Filter::cmp(Filter::Op::Lt, 5.0).eval(4.0));
    REQUIRE(Filter::cmp(Filter::Op::Le, 5.0).eval(5.0));
    REQUIRE(Filter::cmp(Filter::Op::Eq, 3.0).eval(3.0));
    REQUIRE(Filter::cmp(Filter::Op::Ne, 3.0).eval(4.0));
    REQUIRE(Filter::always().eval(-999.0));
}

TEST_CASE("mapview S1c - filter AND/OR/NOT composition", "[mapview][recipe][unit]") {
    // Land band: elevation > 0 AND elevation < 1000
    const Filter land = Filter::all({Filter::cmp(Filter::Op::Gt, 0.0),
                                     Filter::cmp(Filter::Op::Lt, 1000.0)});
    REQUIRE(land.eval(500.0));
    REQUIRE_FALSE(land.eval(-5.0));
    REQUIRE_FALSE(land.eval(2000.0));

    // Extreme: elevation < 0 OR elevation > 1000
    const Filter extreme = Filter::any({Filter::cmp(Filter::Op::Lt, 0.0),
                                        Filter::cmp(Filter::Op::Gt, 1000.0)});
    REQUIRE(extreme.eval(-5.0));
    REQUIRE(extreme.eval(2000.0));
    REQUIRE_FALSE(extreme.eval(500.0));

    // NOT(land) == extreme-or-boundary
    const Filter notLand = Filter::negate(land);
    REQUIRE(notLand.eval(-5.0));
    REQUIRE_FALSE(notLand.eval(500.0));
}

TEST_CASE("mapview S1g - cross-field filter resolves named fields via a sampler", "[mapview][recipe][unit]") {
    // land = elevation > 0 (primary) AND biome == 2 (named field).
    const Filter f = Filter::all({Filter::cmp(Filter::Op::Gt, 0.0),
                                  Filter::cmpField("biome", Filter::Op::Eq, 2.0)});

    auto biomeIs2 = [](const std::string& field, double& out) {
        if (field == "biome") { out = 2.0; return true; }
        return false;
    };
    REQUIRE(f.eval(biomeIs2, 50.0));         // elevation 50 > 0 AND biome 2 == 2
    REQUIRE_FALSE(f.eval(biomeIs2, -5.0));   // elevation fails

    // A different biome value fails the named leaf.
    auto biomeIs3 = [](const std::string& field, double& out) {
        if (field == "biome") { out = 3.0; return true; }
        return false;
    };
    REQUIRE_FALSE(f.eval(biomeIs3, 50.0));

    // biome ABSENT at this cell -> named leaf fails franc -> the whole AND is false.
    auto noBiome = [](const std::string&, double&) { return false; };
    REQUIRE_FALSE(f.eval(noBiome, 50.0));
}

TEST_CASE("mapview S1g - primary-only filters still evaluate without a sampler", "[mapview][recipe][unit]") {
    // Backward compatibility: cmp() leaves use the primary value; eval(value) needs no sampler.
    REQUIRE(Filter::cmp(Filter::Op::Ge, 10.0).eval(10.0));
    REQUIRE(Filter::all({Filter::cmp(Filter::Op::Gt, 0.0), Filter::cmp(Filter::Op::Lt, 100.0)}).eval(50.0));
}

TEST_CASE("mapview S1f - diverging palette pivots low->mid->high", "[mapview][recipe][unit]") {
    // blue (deep) -> white (sea level, pivot 0) -> green (high), over [-100, 0, 100].
    const Palette p = Palette::diverging(kBlue, kWhite, kGreen, -100.0, 0.0, 100.0);
    REQUIRE(p.eval(0.0).r == 1.0f);                 // pivot = white
    REQUIRE(p.eval(0.0).g == 1.0f);
    REQUIRE(p.eval(0.0).b == 1.0f);
    REQUIRE_THAT(p.eval(-100.0).b, WithinAbs(1.0f, 1e-6));  // low end = blue
    REQUIRE_THAT(p.eval(100.0).g, WithinAbs(1.0f, 1e-6));   // high end = green
    // Halfway below the pivot: between blue and white -> blue stays 1, red/green rise to 0.5.
    const Rgba lo = p.eval(-50.0);
    REQUIRE_THAT(lo.b, WithinAbs(1.0f, 1e-6));
    REQUIRE_THAT(lo.r, WithinAbs(0.5f, 1e-6));
    // Ends clamp.
    REQUIRE_THAT(p.eval(-999.0).b, WithinAbs(1.0f, 1e-6));
    REQUIRE_THAT(p.eval(999.0).g, WithinAbs(1.0f, 1e-6));
}

TEST_CASE("mapview S1f - stepped palette quantizes a range into equal bands", "[mapview][recipe][unit]") {
    // [0,300] into 3 flat bands: red [0,100), green [100,200), blue [200,300].
    const Palette p = Palette::stepped(0.0, 300.0, {kRed, kGreen, kBlue});
    REQUIRE(p.eval(50.0).r == 1.0f);    // band 0
    REQUIRE(p.eval(150.0).g == 1.0f);   // band 1
    REQUIRE(p.eval(250.0).b == 1.0f);   // band 2
    // Boundaries go to the upper band (strict <).
    REQUIRE(p.eval(100.0).g == 1.0f);
    REQUIRE(p.eval(200.0).b == 1.0f);
    // Below / above the range clamp to the first / last band.
    REQUIRE(p.eval(-50.0).r == 1.0f);
    REQUIRE(p.eval(9999.0).b == 1.0f);
}
