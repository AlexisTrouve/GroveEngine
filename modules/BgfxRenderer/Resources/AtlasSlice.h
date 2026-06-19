#pragma once

// ============================================================================
// Atlas grid -> texture2DArray slicing (tilemap Slice A3.3) — PURE, GPU-free, header-only.
//
// A game tileset is a PNG GRID of fixed-size tiles. The GPU tilemap samples a texture2DArray with
// one tile per LAYER, indexed by (tile id - 1). This re-lays-out a grid image into the contiguous
// per-layer blob bgfx uploads, in row-major tile order: layer = row*cols + col (so id 1 = top-left
// tile). The pixel re-layout is the bug-prone part, so it lives here and is unit-tested on the CPU.
// ============================================================================

#include <cstdint>
#include <vector>

namespace grove { namespace atlas {

// Slice an RGBA8 grid image (imgW x imgH) of tileW x tileH tiles into a texture-array blob:
// `outLayers` layers (cols*rows, row-major), each a contiguous tileW*tileH block. Partial trailing
// tiles (when imgW/imgH aren't exact multiples) are ignored — only full tiles become layers.
inline std::vector<uint32_t> sliceToArray(const uint32_t* src, int imgW, int imgH,
                                          int tileW, int tileH, int& outLayers) {
    const int cols = (tileW > 0) ? imgW / tileW : 0;
    const int rows = (tileH > 0) ? imgH / tileH : 0;
    outLayers = cols * rows;

    std::vector<uint32_t> out(static_cast<size_t>(outLayers) * tileW * tileH);
    for (int layer = 0; layer < outLayers; ++layer) {
        const int col = layer % cols;
        const int row = layer / cols;
        const int ox = col * tileW;
        const int oy = row * tileH;
        uint32_t* dst = out.data() + static_cast<size_t>(layer) * tileW * tileH;
        for (int ty = 0; ty < tileH; ++ty) {
            const uint32_t* srcRow = src + static_cast<size_t>(oy + ty) * imgW + ox;
            uint32_t* dstRow = dst + static_cast<size_t>(ty) * tileW;
            for (int tx = 0; tx < tileW; ++tx) {
                dstRow[tx] = srcRow[tx];
            }
        }
    }
    return out;
}

}} // namespace grove::atlas
