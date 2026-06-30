#pragma once

/**
 * grove::mapview::Coord — cell & chunk addressing (map-viewer engine, slice S0).
 *
 * WHAT  : The two integer coordinate types the whole viewer is addressed by — a CellCoord
 *         (one map cell) and a ChunkCoord (one chunk = a W×H×D box of cells). Both carry a
 *         z level, so the format is Z-aware from day one.
 *
 * WHY   : Z lives in the address from the start because retro-fitting it later is a contract
 *         rewrite across producer + format + viewer. A flat producer (Theomen) simply pins
 *         z==0 and the format degenerates exactly to the 2D case — no cost paid for the
 *         capability until a deep-world consumer (a DF-like) needs it.
 *
 * HOW   : Plain value structs, std-only, header-only. int32 for x/y (worlds far larger than
 *         a 16-bit range), int16 for z (vertical levels are few). Equality is provided for
 *         use as a map key / cache lookup; ordering is intentionally left out (a hash or an
 *         explicit comparator is the host's call when it builds a chunk cache).
 */

#include <cstdint>

namespace grove {
namespace mapview {

// A single cell's address in the world. z is the vertical level (Theomen flat: z == 0).
struct CellCoord {
    int32_t x{0};
    int32_t y{0};
    int16_t z{0};
};

// A chunk's address, in chunk units (cell address / chunkDims). Same z semantics as CellCoord.
struct ChunkCoord {
    int32_t x{0};
    int32_t y{0};
    int16_t z{0};
};

inline bool operator==(const CellCoord& a, const CellCoord& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
inline bool operator==(const ChunkCoord& a, const ChunkCoord& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace mapview
} // namespace grove
