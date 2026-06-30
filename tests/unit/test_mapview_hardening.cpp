/**
 * Hardening Tests: grove::mapview adversarial-review locks (map-viewer engine, S1 hardening).
 *
 * WHAT  : One regression lock per real bug surfaced by the S1 adversarial review (4 parallel reviewers over
 *         geometry / streaming / recipe / orchestrator). Each case reproduces a concrete defect that the
 *         corresponding header fix must turn from red to green.
 *
 * WHY    : Low-trust doctrine — a finding is only believed once it is reproduced as a failing test, and only
 *         closed once a fix makes it pass while staying in the regression suite (no throwaway tests).
 *
 * HOW    : Catch2, headless. Mocks: a throw-once provider (exception safety), an in-memory chunk provider.
 */

#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/mapview/ChunkCache.h"
#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/Cull.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/Hillshade.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Palette.h"
#include "grove/mapview/Projection.h"

using namespace grove::mapview;
using Catch::Matchers::WithinAbs;

// ------------------------------------------------------------------------------------------------
// B1 — chunksInViewport must not throw / OOM / invoke UB on a huge (zoom-out) viewport.
// ------------------------------------------------------------------------------------------------
TEST_CASE("mapview hardening B1 - cull survives an extreme zoom-out viewport", "[mapview][hardening][unit]") {
    // A legitimately wide rect: ~2e9 chunks per axis. Must not throw a length_error / OOM.
    REQUIRE_NOTHROW(chunksInViewport(-1e9, -1e9, 1e9, 1e9, 1.0, 1.0, 1, 1, 0));
    // Out-of-int-range chunk index (float->int would be UB without clamping).
    REQUIRE_NOTHROW(chunksInViewport(-16.0, -16.0, 1e12, 1e12, 1.0, 1.0, 16, 16, 0));
    // A normal viewport still returns the right set (no regression).
    REQUIRE(chunksInViewport(0.0, 0.0, 5.0, 5.0, 1.0, 1.0, 4, 4, 0).size() == 4);
}

// ------------------------------------------------------------------------------------------------
// B2 — cull guard must reject a degenerate cell size (currently only chunkW/H is guarded).
// ------------------------------------------------------------------------------------------------
TEST_CASE("mapview hardening B2 - cull rejects a degenerate cell size", "[mapview][hardening][unit]") {
    REQUIRE(chunksInViewport(0.0, 0.0, 100.0, 100.0, 0.0, 1.0, 16, 16, 0).empty());   // cellW == 0
    REQUIRE(chunksInViewport(0.0, 0.0, 100.0, 100.0, 1.0, 0.0, 16, 16, 0).empty());   // cellH == 0
    REQUIRE_NOTHROW(chunksInViewport(0.0, 0.0, 100.0, 100.0, -1.0, 1.0, 16, 16, 0));  // cellW < 0 (no crash)
    REQUIRE(chunksInViewport(0.0, 0.0, 100.0, 100.0, -1.0, 1.0, 16, 16, 0).empty());
}

// ------------------------------------------------------------------------------------------------
// B3 — ChunkCache must stay consistent when provider.load() throws (no phantom LRU node).
// ------------------------------------------------------------------------------------------------
struct ThrowOnceProvider final : IChunkProvider {
    std::unordered_set<ChunkCoord, ChunkCoordHash> avail;
    ChunkCoord throwOn{};
    int throwsLeft{0};
    bool has(ChunkCoord c) const override { return avail.find(c) != avail.end(); }
    ChunkData load(ChunkCoord c) override {
        if (c == throwOn && throwsLeft > 0) { --throwsLeft; throw std::runtime_error("load boom"); }
        ChunkData d; d.coord = c; d.cellCount = 1; d.fields.emplace_back("v", std::vector<uint32_t>{1});
        return d;
    }
};

TEST_CASE("mapview hardening B3 - a throwing load doesn't corrupt the cache budget", "[mapview][hardening][unit]") {
    const ChunkCoord A{0, 0, 0}, B{1, 1, 0}, C{2, 2, 0};
    ThrowOnceProvider p;
    p.avail = {A, B, C};
    p.throwOn = B;
    p.throwsLeft = 1;  // B fails the first load, succeeds after
    ChunkCache cache(p, 2);

    REQUIRE_THROWS(cache.ensureResident({B}));   // B's load throws — must leave the cache consistent
    cache.ensureResident({A, C});                // resident {A,C} (budget 2)
    cache.ensureResident({B});                   // B now loads -> over budget -> evict a non-visible one

    // With a phantom B node (the bug), eviction breaks at the back (phantom B matches visible B) and the
    // budget is never enforced -> residentCount == 3. The fix (load before mutate) leaves no phantom -> 2.
    REQUIRE(cache.residentCount() == 2);
}

// ------------------------------------------------------------------------------------------------
// C1 — Hillshade::factor must stay finite & in [0,1] for non-finite gradients.
// ------------------------------------------------------------------------------------------------
TEST_CASE("mapview hardening C1 - hillshade factor stays finite for NaN/Inf gradients", "[mapview][hardening][unit]") {
    const Hillshade h(0, 0, 1);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (double f : {h.factor(nan, 0.0), h.factor(0.0, nan), h.factor(inf, 0.0), h.factor(inf, inf)}) {
        REQUIRE(std::isfinite(f));
        REQUIRE(f >= 0.0);
        REQUIRE(f <= 1.0);
    }
}

// ------------------------------------------------------------------------------------------------
// C5 — Palette::eval(NaN) must resolve to the fallback consistently across kinds.
// ------------------------------------------------------------------------------------------------
TEST_CASE("mapview hardening C5 - palette maps NaN to the fallback for every kind", "[mapview][hardening][unit]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const Rgba fb{0.5f, 0.25f, 0.125f, 1.0f};

    const Palette ramp = Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}, fb);
    const Palette band = Palette::banded({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}, fb);
    const Palette cat  = Palette::categorical({Rgba{1, 0, 0, 1}}, fb);
    for (const Palette* p : {&ramp, &band, &cat}) {
        const Rgba c = p->eval(nan);
        REQUIRE_THAT(c.r, WithinAbs(fb.r, 1e-6));
        REQUIRE_THAT(c.g, WithinAbs(fb.g, 1e-6));
        REQUIRE_THAT(c.b, WithinAbs(fb.b, 1e-6));
    }
}

// ------------------------------------------------------------------------------------------------
// A1 — SquareLayout: a cell's own min corner must pick back to that cell (half-open contract).
// ------------------------------------------------------------------------------------------------
TEST_CASE("mapview hardening A1 - worldToCell(cellQuad.min) round-trips for fractional cell sizes", "[mapview][hardening][unit]") {
    for (double sx : {0.1, 1.0 / 3.0, 0.7}) {
        const SquareLayout layout(sx, sx);
        for (int cx = -2000; cx < 2000; cx += 1) {
            const CellCoord c{cx, 0, 0};
            const CellCoord back = layout.worldToCell(layout.cellQuad(c).corners[0]);
            REQUIRE(back.x == c.x);
            REQUIRE(back.y == c.y);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// D1 / D2 — MapView: own the schema (no dangling FieldDecl*); guard a degenerate cell size.
// ------------------------------------------------------------------------------------------------
struct SimpleProvider final : IChunkProvider {
    std::unordered_map<ChunkCoord, ChunkData, ChunkCoordHash> chunks;
    bool has(ChunkCoord c) const override { return chunks.find(c) != chunks.end(); }
    ChunkData load(ChunkCoord c) override { return chunks.at(c); }
};

static ChunkData makeChunk(ChunkCoord coord, uint32_t cellCount,
                           std::vector<std::pair<std::string, std::vector<uint32_t>>> fields) {
    ChunkData c; c.coord = coord; c.cellCount = cellCount; c.fields = std::move(fields);
    return c;
}

static std::vector<FieldDecl> makeSchemaByValue() {
    return {FieldDecl{"elevation", Encoding::Int, 16, 1.0, 0.0}};
}

TEST_CASE("mapview hardening D1 - MapView from a temporary schema decodes correctly", "[mapview][hardening][unit]") {
    SimpleProvider provider;
    provider.chunks[ChunkCoord{0, 0, 0}] = makeChunk({0, 0, 0}, 4, {{"elevation", {10, 20, 30, 40}}});
    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;

    // The schema is a TEMPORARY destroyed at the end of this full-expression — MapView must own it,
    // not dangle a pointer into it (else decodePhysical reads freed scale/offset -> wrong colour / UAF).
    MapView mv(makeSchemaByValue(), GridSpec{2, 2, 1, 1.0, 1.0}, layout, proj, provider, 8);
    mv.setLens(Lens{"e", {Layer{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}),
                                Filter::always(), 0, 1.0f}}});
    mv.setViewport(Viewport{0.0, 0.0, 2.0, 2.0});
    mv.update();

    REQUIRE(mv.cellCount() == 4);
    // elevation 10 -> grey 0.10 (proves the FieldDecl scale/offset survived).
    const CellDraw* c0 = nullptr;
    for (const auto& d : mv.cells()) if (std::abs(d.x - 0.5) < 1e-9 && std::abs(d.y - 0.5) < 1e-9) c0 = &d;
    REQUIRE(c0 != nullptr);
    REQUIRE_THAT(c0->color.r, WithinAbs(0.10f, 1e-4));
}

TEST_CASE("mapview hardening D2 - MapView guards a degenerate cell size", "[mapview][hardening][unit]") {
    SimpleProvider provider;
    provider.chunks[ChunkCoord{0, 0, 0}] = makeChunk({0, 0, 0}, 4, {{"elevation", {10, 20, 30, 40}}});
    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    const std::vector<FieldDecl> schema = {FieldDecl{"elevation", Encoding::Int, 16, 1.0, 0.0}};

    MapView mv(schema, GridSpec{2, 2, 1, 0.0, 1.0}, layout, proj, provider, 8);  // cellW == 0
    mv.setLens(Lens{"e", {Layer{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}),
                                Filter::always(), 0, 1.0f}}});
    mv.setViewport(Viewport{0.0, 0.0, 2.0, 2.0});
    REQUIRE_NOTHROW(mv.update());
    REQUIRE(mv.cellCount() == 0);  // degenerate config -> nothing drawn, no UB
}
