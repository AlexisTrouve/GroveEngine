#pragma once

// ============================================================================
// Per-layer average colour of a tileset array (tilemap LOD) — PURE, GPU-free, header-only.
//
// WHAT : reduce each layer of a texture2DArray blob (the output of AtlasSlice::sliceToArray) to ONE
//        RGBA8 colour — what that tile type reads as when it is too small to see. The resulting
//        table fills the zoom-out LOD colour texture, so a tileset's dezoomed colours ARE its art.
//
// WHY  : the zoom-out band CANNOT sample the atlas — the tile-index texture is R16UI POINT/CLAMP and
//        tile ids may never be filtered (the average of "rock" and "water" is not "sand"). So the
//        LOD colour has to be precomputed per tile type. Deriving it from the tileset (rather than
//        having the game hand over a parallel palette) removes the second source of truth that would
//        have to be kept in sync with the art by hand.
//
// HOW  : alpha-WEIGHTED mean. RGB = sum(rgb*a)/sum(a), A = mean(a). A tile that is 80% transparent
//        must read as "faint red" (red hue, low coverage), NOT "dark red": a straight mean would
//        darken the RGB, and then compositing against the background would darken it a SECOND time.
//        sum(a) == 0 -> transparent black (no hue is recoverable from invisible texels).
//        NOTE the deliberate divergence from LodColor.h's avg4, which averages straight: that one
//        operates on already-per-tile colours where the effect is second-order, and changing it
//        would alter the existing LOD mip chain (a regression).
// ============================================================================

#include <cstdint>
#include <vector>

namespace grove { namespace atlas {

// Average each of `layers` contiguous tileW*tileH RGBA8 blocks of `arr` into one colour per layer.
// Colour convention (engine-wide): bytes [R,G,B,A] little-endian == 0xAABBGGRR, alpha = high byte.
// `arr` may be null when layers == 0 (the failed-slice path: tile size larger than the image).
inline std::vector<uint32_t> averageLayers(const uint32_t* arr, int tileW, int tileH, int layers) {
    std::vector<uint32_t> out;
    if (layers <= 0 || tileW <= 0 || tileH <= 0 || arr == nullptr) return out;

    const size_t texels = static_cast<size_t>(tileW) * tileH;
    out.reserve(static_cast<size_t>(layers));

    for (int layer = 0; layer < layers; ++layer) {
        const uint32_t* src = arr + static_cast<size_t>(layer) * texels;

        // 64-bit accumulators: sum(channel * alpha) peaks at 255*255*texels, which overflows 32 bits
        // for tiles of ~256x256 and up.
        uint64_t wr = 0, wg = 0, wb = 0;   // alpha-weighted colour sums
        uint64_t wsum = 0;                 // sum of alpha (the weights)
        for (size_t i = 0; i < texels; ++i) {
            const uint32_t px = src[i];
            const uint64_t a = (px >> 24) & 0xFFu;
            wr += ((px >>  0) & 0xFFu) * a;
            wg += ((px >>  8) & 0xFFu) * a;
            wb += ((px >> 16) & 0xFFu) * a;
            wsum += a;
        }

        if (wsum == 0) {           // fully transparent layer -> no hue to recover
            out.push_back(0x00000000u);
            continue;
        }

        // Round to nearest (+ half the divisor) on every channel, so an exact mean stays exact.
        const uint64_t r = (wr + wsum / 2) / wsum;
        const uint64_t g = (wg + wsum / 2) / wsum;
        const uint64_t b = (wb + wsum / 2) / wsum;
        const uint64_t a = (wsum + texels / 2) / texels;   // coverage = plain mean of alpha

        out.push_back(static_cast<uint32_t>((a << 24) | (b << 16) | (g << 8) | r));
    }
    return out;
}

}} // namespace grove::atlas
