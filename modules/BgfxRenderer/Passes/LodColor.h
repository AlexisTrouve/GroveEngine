#pragma once

// ============================================================================
// Tilemap LOD color generation (Slice B) — PURE, GPU-free, header-only.
//
// The zoom-out band samples a mipped RGBA8 "LOD color" texture: mip0[t] = paletteColor(tile id),
// finer mips are 2x2 box-filters down to 1x1. The math lives here (no RHI/bgfx) so it is unit-
// testable on the CPU: the coarsest mip of a checkerboard MUST equal the average of its colors —
// an exact, eye-free oracle for "the LOD averages correctly".
// ============================================================================

#include <cstdint>
#include <vector>

namespace grove { namespace lod {

// Tile palette: id -> color. id 0 = empty -> transparent (so empty/solid edges fade in alpha when
// box-filtered into coarser mips). RGBA8 bytes [R,G,B,A] -> little-endian literal 0xAABBGGRR.
static constexpr int kPaletteSize = 8;
inline uint32_t paletteColor(uint16_t id) {
    static const uint32_t kColors[kPaletteSize] = {
        0xFFC8C8C8u, // 1: light grey
        0xFF50C83Cu, // 2: green
        0xFFE68246u, // 3: blue
        0xFF3CD2E6u, // 4: yellow
        0xFFC84CB4u, // 5: magenta
        0xFF00B4C8u, // 6: amber
        0xFFF0781Eu, // 7: cyan-blue
        0xFFFFFFFFu, // 8: white
    };
    if (id == 0) return 0x00000000u;
    return kColors[(id - 1) % kPaletteSize];
}

// Resolve a tile id to its LOD colour, honouring an optional caller-supplied TABLE.
//
// WHY a table: the built-in palette above wraps modulo 8, so a game with a real tileset sees its own
// art zoomed in and unrelated colours zoomed out. A table (either the per-layer average of the bound
// tileset, or a palette the game pushed explicitly) makes both bands agree. `palette == nullptr` =>
// the historical 8 colours, byte-identical — that is the non-regression guarantee.
//
// Indexing convention: table[i] is the colour of tile id i+1 — the SAME id->layer mapping as the
// atlas array (id 1 = layer 0), so a tileset's average table can be used verbatim.
//
// An id BEYOND the table is TRANSPARENT, deliberately not wrapped: that tile has no atlas layer
// either, so a visible hole is the honest signal. Wrapping would invent a plausible wrong colour.
inline uint32_t lodColor(uint16_t id, const uint32_t* palette, int paletteSize) {
    if (id == 0) return 0x00000000u;                       // empty tile
    if (palette == nullptr || paletteSize <= 0) return paletteColor(id);
    const int idx = static_cast<int>(id) - 1;
    if (idx >= paletteSize) return 0x00000000u;            // no such tile type -> hole, not a guess
    return palette[idx];
}

// Decode a raw RGBA8 byte blob (as carried by render:tilemap:palette) into a LOD colour table.
// 4 bytes per entry in R,G,B,A order -> the engine's 0xAABBGGRR literal, so the wire format matches
// what an image/pixel buffer already holds and needs no swizzling game-side. A trailing partial
// entry is ignored (a truncated colour has no meaningful value). Pure so the parse — the bug-prone
// half of the topic handler — is unit-testable without a GPU or an IIO bus.
inline std::vector<uint32_t> paletteFromBytes(const uint8_t* bytes, size_t n) {
    std::vector<uint32_t> out;
    if (bytes == nullptr) return out;
    const size_t count = n / 4;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* p = bytes + i * 4;
        out.push_back(static_cast<uint32_t>(p[0])              // R -> low byte
                    | (static_cast<uint32_t>(p[1]) << 8)       // G
                    | (static_cast<uint32_t>(p[2]) << 16)      // B
                    | (static_cast<uint32_t>(p[3]) << 24));    // A -> high byte
    }
    return out;
}

// Average four RGBA8 texels component-wise (2x2 box filter).
inline uint32_t avg4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const uint32_t sum = ((a >> shift) & 0xFFu) + ((b >> shift) & 0xFFu)
                           + ((c >> shift) & 0xFFu) + ((d >> shift) & 0xFFu);
        out |= ((sum >> 2) & 0xFFu) << shift;
    }
    return out;
}

// Full mip count down to 1x1 for a w x h image.
inline int mipCount(int w, int h) {
    int mips = 1;
    for (int d = (w > h ? w : h); d > 1; d >>= 1) ++mips;
    return mips;
}

// Build the contiguous mip chain (mip0 || mip1 || ... || 1x1) of the LOD color texture from tile
// ids. mip0[t] = lodColor(tiles[t], ...); each finer level is a 2x2 box-filter (odd dims clamp).
// `outMips` receives the level count. This is exactly what the pass uploads to bgfx.
// `palette`/`paletteSize` are OPTIONAL (see lodColor): omitted => the historical built-in colours.
inline std::vector<uint32_t> buildLodMipChain(int w0, int h0, const uint16_t* tiles, int& outMips,
                                              const uint32_t* palette = nullptr, int paletteSize = 0) {
    outMips = mipCount(w0, h0);

    std::vector<uint32_t> buf;
    std::vector<uint32_t> prev(static_cast<size_t>(w0) * h0);
    for (size_t i = 0; i < prev.size(); ++i) {
        prev[i] = lodColor(tiles[i], palette, paletteSize);
    }
    buf.insert(buf.end(), prev.begin(), prev.end());

    int pw = w0, ph = h0;
    for (int m = 1; m < outMips; ++m) {
        const int nw = pw > 1 ? pw >> 1 : 1;
        const int nh = ph > 1 ? ph >> 1 : 1;
        std::vector<uint32_t> cur(static_cast<size_t>(nw) * nh);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                const int x0 = x * 2, y0 = y * 2;
                const int x1 = (x0 + 1 < pw) ? x0 + 1 : x0;   // clamp for odd dims
                const int y1 = (y0 + 1 < ph) ? y0 + 1 : y0;
                cur[static_cast<size_t>(y) * nw + x] = avg4(
                    prev[static_cast<size_t>(y0) * pw + x0], prev[static_cast<size_t>(y0) * pw + x1],
                    prev[static_cast<size_t>(y1) * pw + x0], prev[static_cast<size_t>(y1) * pw + x1]);
            }
        }
        buf.insert(buf.end(), cur.begin(), cur.end());
        prev.swap(cur);
        pw = nw; ph = nh;
    }
    return buf;
}

// Build a mipped R8 chain (Slice fog): mip0 = the per-tile SCALAR bytes (e.g. visibility 0..255),
// finer mips are 2x2 box-filters down to 1x1, contiguous (bgfx layout). Encoding fog as a SCALAR is
// what makes mipping meaningful — box-filtering visibility gives soft partial-reveal edges at
// zoom-out, whereas averaging discrete enum states would be nonsense.
inline std::vector<uint8_t> buildR8MipChain(const uint8_t* src, int w0, int h0, int& outMips) {
    outMips = mipCount(w0, h0);

    std::vector<uint8_t> buf;
    std::vector<uint8_t> prev(src, src + static_cast<size_t>(w0) * h0);
    buf.insert(buf.end(), prev.begin(), prev.end());

    int pw = w0, ph = h0;
    for (int m = 1; m < outMips; ++m) {
        const int nw = pw > 1 ? pw >> 1 : 1;
        const int nh = ph > 1 ? ph >> 1 : 1;
        std::vector<uint8_t> cur(static_cast<size_t>(nw) * nh);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                const int x0 = x * 2, y0 = y * 2;
                const int x1 = (x0 + 1 < pw) ? x0 + 1 : x0;
                const int y1 = (y0 + 1 < ph) ? y0 + 1 : y0;
                const unsigned s = prev[static_cast<size_t>(y0) * pw + x0]
                                 + prev[static_cast<size_t>(y0) * pw + x1]
                                 + prev[static_cast<size_t>(y1) * pw + x0]
                                 + prev[static_cast<size_t>(y1) * pw + x1];
                cur[static_cast<size_t>(y) * nw + x] = static_cast<uint8_t>(s >> 2);
            }
        }
        buf.insert(buf.end(), cur.begin(), cur.end());
        prev.swap(cur);
        pw = nw; ph = nh;
    }
    return buf;
}

}} // namespace grove::lod
