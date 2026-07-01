#pragma once

/**
 * grove::mapview::TileMapper — field value -> tile id (T3, the tiling counterpart of Palette).
 *
 * WHAT  : One data-driven mapping from a (decoded, physical) field value to a discrete TILE ID: a uint16
 *         index into a tileset (1..N = a texture layer, 0 = transparent / "no tile"). Where a Palette gives a
 *         continuous colour for the per-cell bulk-sprite path, a TileMapper gives a tile index for the
 *         retained TILEMAP path — the SAME field, drawn as textured tiles instead of flat-colour quads.
 *
 * WHY   : Tiling was demo-local (a hand-written elevToTile band function inside capture_mapview_tiles). Alexi:
 *         "je parlais du tilling" -> tiling should be a first-class, recipe-driven, testable core output, not a
 *         hack in one demo. Mirroring Palette::banded keeps ONE mental model: a producer names bands, the
 *         viewer is a dumb tile-index emitter. Kept a separate brick from Palette because the output type
 *         (a discrete tileset id) and the render path (retained tilemap upload) are different from a colour.
 *
 * HOW   : Header-only, std-only. `banded` mirrors Palette::banded exactly: each (upperBound, tileId) covers
 *         values < upperBound (ascending); a value >= the last bound takes the last id; a nodata value (NaN)
 *         or an empty set takes `fallback` (default 0 = transparent). map() is a pure value -> uint16.
 */

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace grove {
namespace mapview {

class TileMapper {
public:
    TileMapper() = default;

    // Discrete tile bands: each (upperBound, tileId) covers values < upperBound (ascending). Values >= the
    // last bound take the last id; a nodata value (NaN) / empty set takes `fallback` (0 = transparent tile).
    static TileMapper banded(std::vector<std::pair<double, uint16_t>> bands, uint16_t fallback = 0) {
        TileMapper t;
        t.bands_ = std::move(bands);
        t.fallback_ = fallback;
        return t;
    }

    // Map a (decoded) field value to a tileset id. Same band semantics as Palette::banded (see HOW).
    uint16_t map(double value) const {
        if (std::isnan(value)) return fallback_;   // nodata -> transparent (consistent with Palette)
        if (bands_.empty()) return fallback_;
        for (const auto& b : bands_) {
            if (value < b.first) return b.second;
        }
        return bands_.back().second;               // above the last upper bound
    }

    bool empty() const { return bands_.empty(); }
    uint16_t fallback() const { return fallback_; }

private:
    std::vector<std::pair<double, uint16_t>> bands_;  // (upperBound, tileId), ascending
    uint16_t fallback_{0};                            // nodata / empty -> this id (0 = transparent)
};

} // namespace mapview
} // namespace grove
