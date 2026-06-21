#pragma once

// ============================================================================
// grove::tex — generic RGBA8 mip-chain box-filter for minification anti-aliasing. PURE, header-only.
//
// WHAT : build the contiguous mip chain (mip0 || mip1 || ... || 1x1) of an RGBA8 image by 2x2 box-
//        filtering, so a texture created with mipLevels = mipCount(w,h) is GPU-trilinear-sampled and
//        stops shimmering/aliasing when minified (strong zoom-out). NPOT-safe: each level =
//        floor(prev/2) (min 1), odd dims clamp the extra row/col — matching bgfx's own mip dimensions
//        so the uploaded blob lines up level-for-level.
//
// WHY  : TextureLoader created sprite textures with mipLevels=1; free unit sprites (world-float coords)
//        layered over the tilemap then aliased under zoom-out. This is the SAME proven pattern as the
//        tilemap LOD color (Passes/LodColor.h::buildLodMipChain) — pulled out GENERIC for arbitrary
//        pixels (LodColor starts from a tile-id palette; here we start from the decoded image). Pure /
//        GPU-free so the custom part (the box-filter) is unit-tested headless — the coarsest mip of a
//        2-colour checkerboard MUST equal the average of those colours — and the GPU side is just
//        trusted-HW trilinear.
//
// NOTE : mipCount + the 2x2 box-average duplicate LodColor.h's avg4/mipCount (~12 lines). Kept SELF-
//        CONTAINED on purpose (Modularité #1): a generic texture helper shouldn't depend on the tilemap-
//        LOD header. LodColor is locked by tests and left untouched.
// ============================================================================

#include <cstdint>
#include <vector>

namespace grove { namespace tex {

// Full mip count down to 1x1 for a w x h image (same convention as bgfx: floor halving, min 1).
inline int mipCount(int w, int h) {
    int mips = 1;
    for (int d = (w > h ? w : h); d > 1; d >>= 1) ++mips;
    return mips;
}

// Average four RGBA8 texels component-wise (2x2 box filter), little-endian 0xAABBGGRR.
inline uint32_t boxAvg4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const uint32_t sum = ((a >> shift) & 0xFFu) + ((b >> shift) & 0xFFu)
                           + ((c >> shift) & 0xFFu) + ((d >> shift) & 0xFFu);
        out |= ((sum >> 2) & 0xFFu) << shift;
    }
    return out;
}

// Build the contiguous mip chain (mip0 || mip1 || ... || 1x1) from RGBA8 pixels (one uint32 per texel).
// `outMips` receives the level count. The result is exactly what bgfx wants uploaded for a mipLevels =
// outMips texture (bgfx::copy of this buffer).
inline std::vector<uint32_t> buildRgba8MipChain(const uint32_t* mip0, int w0, int h0, int& outMips) {
    outMips = mipCount(w0, h0);

    std::vector<uint32_t> buf;
    std::vector<uint32_t> prev(mip0, mip0 + static_cast<size_t>(w0) * h0);
    buf.insert(buf.end(), prev.begin(), prev.end());      // mip0 verbatim

    int pw = w0, ph = h0;
    for (int m = 1; m < outMips; ++m) {                   // each finer level = 2x2 box-filter of prev
        const int nw = pw > 1 ? pw >> 1 : 1;
        const int nh = ph > 1 ? ph >> 1 : 1;
        std::vector<uint32_t> cur(static_cast<size_t>(nw) * nh);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                const int x0 = x * 2, y0 = y * 2;
                const int x1 = (x0 + 1 < pw) ? x0 + 1 : x0;   // clamp the extra col/row for odd dims
                const int y1 = (y0 + 1 < ph) ? y0 + 1 : y0;
                cur[static_cast<size_t>(y) * nw + x] = boxAvg4(
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

}} // namespace grove::tex
