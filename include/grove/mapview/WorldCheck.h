#pragma once

/**
 * grove::mapview::WorldCheck — a semantic validator for a world-document (slice S3 support tool).
 *
 * WHAT  : Pure diagnostic passes over a parsed Manifest and its decoded chunks. checkManifest()
 *         inspects the coordinate system + field schema; checkChunk() inspects one decoded chunk
 *         against that schema. Both APPEND graded Diagnostics (Error/Warning) to a Report; neither
 *         touches the filesystem or compression (the disk-driving lives in WorldCheckDisk.h).
 *
 * WHY   : A producer (Theomen, future games) writes a `.world` and today the only way to know it is
 *         correct is to launch the GPU viewer and eyeball pixels — which the doctrine forbids as proof
 *         ("reading/looking is not a test"). This gives the producer a DETERMINISTIC, headless answer:
 *         run the checker, get the exact line that is wrong. It is the format's fail-franc philosophy
 *         turned into a diagnostic tool — it does NOT re-implement the reader's guards (a corrupt blob,
 *         an unknown encoding, a bit-width disagreement already throw at read); it catches the SEMANTIC
 *         mistakes the reader accepts silently: an inverted bounds box, a chunk whose cell count does
 *         not match chunkDims, a field declared with bits=0, values that decode to NaN, dead chunks
 *         outside the world. Every check here is something the reader/codec does NOT already reject.
 *
 * HOW   : Header-only, std-only (+ Manifest/WorldDocument, which the caller has already). Each check is
 *         one branch that pushes a Diagnostic. Severity is defined by consumer impact: Error = the viewer
 *         will fail or draw wrong (non-zero exit); Warning = suspicious but tolerated. Products are
 *         computed in uint64 with an explicit "invalid dims" guard so a bad chunkDims can't wrap. Cross-
 *         checks that the reader already enforces (present-field ∈ schema, packed byte-width) are
 *         deliberately NOT repeated — validating twice would just rot out of sync with the codec.
 */

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "grove/mapview/Coord.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/Manifest.h"
#include "grove/mapview/WorldDocument.h"

namespace grove {
namespace mapview {

// Consumer impact of a finding. POURQUOI graded, not boolean: a producer needs to know "this WILL break
// the viewer" (Error) apart from "this is probably a mistake but renders" (Warning) so it can triage.
enum class Severity : uint8_t { Info, Warning, Error };

// One finding: what/where/how-bad. `where` is a human location ("coordinate.bounds", "chunk (2,0,0)",
// field name) so the producer can jump straight to the offending declaration.
struct Diagnostic {
    Severity    severity{Severity::Error};
    std::string category;   // "manifest" | "field" | "chunk" — coarse bucket for filtering
    std::string where;      // precise location within that bucket
    std::string message;    // one-line, human-readable
};

// The accumulated verdict + summary stats over one world-document.
struct Report {
    std::vector<Diagnostic> diagnostics;
    // Summary (filled by the disk driver; the pure checks leave these at 0 unless told otherwise).
    std::size_t chunkCount{0};
    std::size_t fieldCount{0};
    std::uint64_t totalCells{0};

    std::size_t errors() const {
        std::size_t n = 0;
        for (const auto& d : diagnostics) if (d.severity == Severity::Error) ++n;
        return n;
    }
    std::size_t warnings() const {
        std::size_t n = 0;
        for (const auto& d : diagnostics) if (d.severity == Severity::Warning) ++n;
        return n;
    }
    bool ok() const { return errors() == 0; }                       // no errors (warnings tolerated)
    bool clean() const { return errors() == 0 && warnings() == 0; } // spotless

    // Append helper — keeps the check bodies terse.
    void add(Severity sev, std::string category, std::string where, std::string message) {
        diagnostics.push_back(Diagnostic{sev, std::move(category), std::move(where), std::move(message)});
    }
};

namespace detail {

// Cells per chunk, or 0 if any dim is non-positive (an invalid chunkDims — flagged separately by
// checkManifest — must not silently wrap the product). uint64 so 128×128×128 can't overflow.
inline std::uint64_t cellsPerChunk(const Coordinate& c) {
    if (c.chunkDims[0] <= 0 || c.chunkDims[1] <= 0 || c.chunkDims[2] <= 0) return 0;
    return static_cast<std::uint64_t>(c.chunkDims[0]) * static_cast<std::uint64_t>(c.chunkDims[1]) *
           static_cast<std::uint64_t>(c.chunkDims[2]);
}

// Floor division for b>0 (correct for negative a, unlike C++ truncation toward zero). Used to map a
// cell bound to the chunk index that contains it, so we can tell if a chunk lies outside the world.
inline std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b, r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
}

// Look up a field declaration by name in the schema (nullptr if absent).
inline const FieldDecl* findField(const Manifest& m, const std::string& name) {
    for (const auto& f : m.fields) if (f.name == name) return &f;
    return nullptr;
}

inline std::string coordStr(std::int32_t x, std::int32_t y, std::int16_t z) {
    return "(" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ")";
}

} // namespace detail

// --- Manifest-level checks --------------------------------------------------------------------------
// Validate the coordinate system + field schema. These are SEMANTIC checks parseManifest does not do
// (it only fail-francs an unknown encoding / unsupported version). Appends to `out`.
inline void checkManifest(const Manifest& m, Report& out) {
    const Coordinate& c = m.coordinate;

    // M1 — topology: the engine implements only SquareLayout. Any other value parses fine but the
    // viewer cannot lay it out → the world is unrenderable. Error.
    if (c.topology != "square") {
        out.add(Severity::Error, "manifest", "coordinate.topology",
                "topology '" + c.topology + "' is not supported (engine implements only 'square')");
    }

    // M2 — cell size must be strictly positive (world units per cell); 0/negative is a degenerate grid.
    for (int a = 0; a < 2; ++a) {
        if (!(c.cellSize[a] > 0.0)) {  // also catches NaN
            out.add(Severity::Error, "manifest", "coordinate.cellSize",
                    std::string("cellSize.") + (a == 0 ? "x" : "y") + " must be > 0 (got " +
                    std::to_string(c.cellSize[a]) + ")");
        }
    }

    // M3 — bounds must be a non-empty box: min <= max on every axis. Inverted bounds = an empty world
    // (nothing in range) or a producer that swapped its corners. Error.
    static const char* axis[3] = {"x", "y", "z"};
    for (int a = 0; a < 3; ++a) {
        if (c.boundsMin[a] > c.boundsMax[a]) {
            out.add(Severity::Error, "manifest", "coordinate.bounds",
                    std::string("bounds.min.") + axis[a] + " (" + std::to_string(c.boundsMin[a]) +
                    ") exceeds bounds.max." + axis[a] + " (" + std::to_string(c.boundsMax[a]) +
                    ") — empty/inverted world");
        }
    }

    // M4 — chunk dims must be strictly positive; 0/negative makes cellsPerChunk meaningless and the
    // chunk-grid tiling undefined. Error.
    for (int a = 0; a < 3; ++a) {
        if (c.chunkDims[a] <= 0) {
            out.add(Severity::Error, "manifest", "coordinate.chunkDims",
                    std::string("chunkDims.") + axis[a] + " must be > 0 (got " +
                    std::to_string(c.chunkDims[a]) + ")");
        }
    }

    // M5 — a schema with no fields is legal but carries no data; the viewer would draw nothing. Warning.
    if (m.fields.empty()) {
        out.add(Severity::Warning, "manifest", "fields",
                "no fields declared — the world carries no data to render");
    }

    // Per-field schema checks.
    std::unordered_set<std::string> seen;
    for (std::size_t i = 0; i < m.fields.size(); ++i) {
        const FieldDecl& f = m.fields[i];
        const std::string loc = f.name.empty() ? ("fields[" + std::to_string(i) + "]") : ("field '" + f.name + "'");

        // M6 — a field with no name cannot be addressed (presence is keyed by name). Error.
        if (f.name.empty()) {
            out.add(Severity::Error, "manifest", "fields[" + std::to_string(i) + "]",
                    "field #" + std::to_string(i) + " has an empty name");
        }
        // M7 — duplicate names: the presence mask + chunk.has() are name-keyed, so two same-named
        // fields are ambiguous (which one is present?). Error.
        else if (!seen.insert(f.name).second) {
            out.add(Severity::Error, "manifest", loc, "duplicate field name '" + f.name + "'");
        }

        // M8 — variable-width integers must declare a valid width. bits=0 is the classic "declared
        // Uint/Int but forgot to set bits" bug (storageBits()==0 → packBits throws at write). Error.
        if (f.encoding == Encoding::Uint || f.encoding == Encoding::Int) {
            if (f.bits < 1 || f.bits > 32) {
                out.add(Severity::Error, "manifest", loc,
                        "encoding " + encodingToString(f.encoding) + " requires bits in [1,32] (got " +
                        std::to_string(static_cast<int>(f.bits)) + ")");
            }
        }
        // M9 — fixed-width encodings derive their width; a stray `bits` is ignored but misleading
        // (the producer may think it took effect). Warning.
        else if (f.bits != 0) {
            out.add(Severity::Warning, "manifest", loc,
                    "encoding " + encodingToString(f.encoding) + " derives its width; bits=" +
                    std::to_string(static_cast<int>(f.bits)) + " is ignored");
        }

        // M10 — scale=0 collapses every raw value to the constant `offset`; almost always a mistake
        // (a flat, information-free layer). Warning.
        if (f.scale == 0.0) {
            out.add(Severity::Warning, "manifest", loc,
                    "scale=0 — all values decode to the constant offset (" + std::to_string(f.offset) + ")");
        }
    }
}

// --- Chunk-level checks -----------------------------------------------------------------------------
// Validate one DECODED chunk against the manifest. `declaredCoord` is the coord the transport used to
// locate the chunk (e.g. parsed from its filename) — cross-checked against the coord the blob carries.
// These catch producer mistakes the reader accepts silently. Appends to `out`.
inline void checkChunk(const Manifest& m, ChunkCoord declaredCoord, const ChunkData& chunk, Report& out) {
    const Coordinate& c = m.coordinate;
    const std::string here = "chunk " + detail::coordStr(declaredCoord.x, declaredCoord.y, declaredCoord.z);

    // C1 — the coord inside the blob must match the coord the transport used to find it; a mismatch
    // means a mislabeled/renamed file (the viewer would place its cells at the wrong world position). Error.
    if (!(chunk.coord == declaredCoord)) {
        out.add(Severity::Error, "chunk", here,
                "blob carries coord " + detail::coordStr(chunk.coord.x, chunk.coord.y, chunk.coord.z) +
                " but is stored as " + detail::coordStr(declaredCoord.x, declaredCoord.y, declaredCoord.z));
    }

    // C2 — cellCount must equal the chunkDims product; the viewer indexes cells by chunkDims, so a
    // chunk with a different count is malformed (a fill-loop off-by-one). Skipped if chunkDims invalid
    // (M4 owns that). Error.
    const std::uint64_t expect = detail::cellsPerChunk(c);
    if (expect != 0 && chunk.cellCount != expect) {
        out.add(Severity::Error, "chunk", here,
                "cellCount=" + std::to_string(chunk.cellCount) + " but chunkDims imply " +
                std::to_string(expect) + " cells");
    }

    // C3 — a chunk whose index lies outside the bounds-implied chunk grid never overlaps the world and
    // is dead weight (or a coord bug). Only checked when chunkDims are valid. Warning.
    if (expect != 0) {
        bool outside = false;
        const std::int32_t idx[3]    = {declaredCoord.x, declaredCoord.y, declaredCoord.z};
        for (int a = 0; a < 3; ++a) {
            const std::int64_t lo = detail::floorDiv(c.boundsMin[a], c.chunkDims[a]);
            const std::int64_t hi = detail::floorDiv(c.boundsMax[a], c.chunkDims[a]);
            if (idx[a] < lo || idx[a] > hi) { outside = true; break; }
        }
        if (outside) {
            out.add(Severity::Warning, "chunk", here,
                    "lies outside the bounds-implied chunk grid — it will never be rendered");
        }
    }

    // C4 — a chunk present on disk but with no fields draws nothing; usually an accidental empty write. Warning.
    if (chunk.fields.empty()) {
        out.add(Severity::Warning, "chunk", here, "has no present fields — nothing to draw");
    }

    // C6 — Float32 fields can carry NaN/Inf bit patterns (uninitialized floats); these decode to NaN/Inf
    // and the palette silently substitutes its fallback, hiding the hole. The integer/unorm encodings
    // cannot produce NaN, so only Float32 fields are scanned. Warning (counted, not per-cell spammed).
    for (const auto& pf : chunk.fields) {
        const FieldDecl* d = detail::findField(m, pf.first);
        if (!d || d->encoding != Encoding::Float32) continue;
        std::size_t bad = 0;
        for (std::uint32_t raw : pf.second) {
            const double v = decodePhysical(*d, raw);
            if (std::isnan(v) || std::isinf(v)) ++bad;
        }
        if (bad != 0) {
            out.add(Severity::Warning, "chunk", here,
                    "field '" + pf.first + "' has " + std::to_string(bad) + " NaN/Inf value(s)");
        }
    }
}

} // namespace mapview
} // namespace grove
