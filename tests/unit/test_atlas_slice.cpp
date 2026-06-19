/**
 * Unit Tests: atlas grid -> texture2DArray slicing (tilemap Slice A3.3) — pure, GPU-free.
 *
 * WHAT  : A game tileset is a PNG GRID of fixed tiles. The tilemap shader samples a texture2DArray
 *         (one tile per layer). This splits the grid image into contiguous per-layer blocks, in the
 *         tile-id order the shader expects (row-major: layer = row*cols + col, id = layer + 1).
 *
 * WHY    : the slice (pixel re-layout) is the bug-prone part; pin it down with an exact oracle here
 *         so the GPU upload is just createTexture(layers, data). Mirrors the LodColor approach.
 *
 * HOW    : feed a known RGBA8 grid, assert each output layer is the right tile, contiguous.
 */

#include <catch2/catch_test_macros.hpp>

#include "Resources/AtlasSlice.h"

#include <cstdint>
#include <vector>

using namespace grove::atlas;

TEST_CASE("Atlas slice: a 2x2-tile grid splits into 4 row-major layers", "[atlas][tilemap][unit]") {
    // 2x2 image of 1x1 tiles. Row-major image: row0 = [TL, TR], row1 = [BL, BR].
    const uint32_t TL = 0xFF0000FFu, TR = 0xFF00FF00u, BL = 0xFFFF0000u, BR = 0xFFFFFFFFu;
    std::vector<uint32_t> img = { TL, TR, BL, BR };

    int layers = 0;
    std::vector<uint32_t> arr = sliceToArray(img.data(), 2, 2, 1, 1, layers);

    REQUIRE(layers == 4);
    REQUIRE(arr.size() == 4u);
    // layer = row*cols + col -> 0:(0,0)=TL, 1:(1,0)=TR, 2:(0,1)=BL, 3:(1,1)=BR
    REQUIRE(arr[0] == TL);
    REQUIRE(arr[1] == TR);
    REQUIRE(arr[2] == BL);
    REQUIRE(arr[3] == BR);
}

TEST_CASE("Atlas slice: each layer is a contiguous tileW*tileH block", "[atlas][tilemap][unit]") {
    // 4x2 image of 2x2 tiles -> 2 cols x 1 row = 2 layers, each 2x2 = 4 texels.
    const int W = 4, H = 2, tw = 2, th = 2;
    std::vector<uint32_t> img(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            img[static_cast<size_t>(y) * W + x] = (x < 2) ? 0xAAAAAAAAu : 0xBBBBBBBBu;

    int layers = 0;
    std::vector<uint32_t> arr = sliceToArray(img.data(), W, H, tw, th, layers);

    REQUIRE(layers == 2);
    REQUIRE(arr.size() == static_cast<size_t>(2) * tw * th);
    for (int i = 0; i < tw * th; ++i) REQUIRE(arr[i] == 0xAAAAAAAAu);                 // left tile
    for (int i = 0; i < tw * th; ++i) REQUIRE(arr[static_cast<size_t>(tw) * th + i] == 0xBBBBBBBBu);  // right
}
