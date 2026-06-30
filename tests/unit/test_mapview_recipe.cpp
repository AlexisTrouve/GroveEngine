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
