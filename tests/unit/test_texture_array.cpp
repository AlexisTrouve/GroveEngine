/**
 * Unit Test: TextureLoader array load (tilemap Slice A3.3c) — PNG grid -> texture2DArray.
 *
 * WHAT  : loadArrayFromFile decodes a PNG, slices it into tileW x tileH tiles (AtlasSlice, already
 *         unit-tested) and uploads a texture2DArray. This checks the I/O glue: it decodes a real
 *         asset and hands the device an RGBA8 array desc with the right per-tile size + layer count.
 *
 * WHY    : the slice math is locked by AtlasSliceUnit and the GPU sample by TilemapLodGpu; this only
 *         covers the file->slice->array wiring. No GPU needed (MockRHIDevice records the desc). Skips
 *         cleanly if the asset isn't found from the test's working dir.
 */

#include <catch2/catch_test_macros.hpp>

#include "Resources/TextureLoader.h"
#include "../mocks/MockRHIDevice.h"

using namespace grove;

TEST_CASE("loadArrayFromFile slices a PNG into a multi-layer RGBA8 array", "[atlas][tilemap][unit]") {
    test::MockRHIDevice device;

    // The test runs from build/tests; try a couple of cwd-relative paths to a known asset.
    const char* candidates[] = {
        "../../assets/textures/1f440.png",
        "../assets/textures/1f440.png",
        "assets/textures/1f440.png",
    };
    TextureLoader::LoadResult r;
    for (const char* p : candidates) {
        r = TextureLoader::loadArrayFromFile(device, p, 16, 16);
        if (r.success) break;
    }
    if (!r.success) {
        WARN("atlas asset not found from cwd — skipping array-load check (" << r.error << ")");
        return;
    }

    // 16px tiles -> the image (>= 32x32) slices into several layers.
    REQUIRE(r.layers > 1);
    REQUIRE(r.width == 16);
    REQUIRE(r.height == 16);

    REQUIRE(device.textureDescs.size() == 1);
    const rhi::TextureDesc& d = device.textureDescs.back();
    REQUIRE(d.format == rhi::TextureDesc::RGBA8);
    REQUIRE(d.layers == r.layers);    // uploaded as an array, one layer per tile
    REQUIRE(d.width == 16);
    REQUIRE(d.height == 16);
}

// ============================================================================
// RAW-PIXEL tileset load. A game that GENERATES its tileset at startup (colours straight from its
// own data) had to encode a PNG and write it to disk just so the engine could read and decode it
// back. This path takes the pixels directly. Same slice + upload + average as the file path, minus
// the codec — so it is fully headless-testable with an exact oracle (no asset, no skip).
// ============================================================================

TEST_CASE("loadArrayFromPixels slices a raw RGBA8 buffer into array layers", "[atlas][tilemap][unit]") {
    test::MockRHIDevice device;

    // 4x2 px image of 2x2 tiles -> 2 cols x 1 row = 2 layers. Left tile solid RED, right solid BLUE.
    // Bytes are R,G,B,A per texel — the same wire layout as render:texture:upload and the LOD palette.
    const uint8_t R[4] = { 255, 0, 0, 255 };
    const uint8_t B[4] = { 0, 0, 255, 255 };
    std::vector<uint8_t> px;
    for (int y = 0; y < 2; ++y) {                       // row-major: R R B B  /  R R B B
        for (int x = 0; x < 4; ++x) {
            const uint8_t* c = (x < 2) ? R : B;
            px.insert(px.end(), c, c + 4);
        }
    }
    REQUIRE(px.size() == 4u * 2u * 4u);

    TextureLoader::LoadResult r =
        TextureLoader::loadArrayFromPixels(device, px.data(), px.size(), 4, 2, 2, 2);

    REQUIRE(r.success);
    REQUIRE(r.layers == 2);
    REQUIRE(r.width == 2);        // per-TILE size, not the image size
    REQUIRE(r.height == 2);

    REQUIRE(device.textureDescs.size() == 1);
    const rhi::TextureDesc& d = device.textureDescs.back();
    REQUIRE(d.format == rhi::TextureDesc::RGBA8);
    REQUIRE(d.layers == 2);
    REQUIRE(d.width == 2);
    REQUIRE(d.height == 2);

    // The zoom-out colour table rides along for free — solid tiles average to themselves.
    REQUIRE(r.layerColors.size() == 2u);
    REQUIRE(r.layerColors[0] == 0xFF0000FFu);   // red  (0xAABBGGRR)
    REQUIRE(r.layerColors[1] == 0xFFFF0000u);   // blue
}

TEST_CASE("loadArrayFromPixels rejects a buffer that does not match imgW*imgH*4", "[atlas][tilemap][unit]") {
    // A short buffer would slice garbage (or read out of bounds). Fail FRANKLY — no truncation, no
    // "best effort" partial atlas that would show as mystery tiles later.
    test::MockRHIDevice device;
    std::vector<uint8_t> tooShort(4u * 2u * 4u - 4u);

    TextureLoader::LoadResult r =
        TextureLoader::loadArrayFromPixels(device, tooShort.data(), tooShort.size(), 4, 2, 2, 2);

    REQUIRE_FALSE(r.success);
    REQUIRE_FALSE(r.error.empty());
    REQUIRE(device.textureDescs.empty());        // nothing was uploaded
}

TEST_CASE("loadArrayFromPixels rejects degenerate dimensions", "[atlas][tilemap][unit]") {
    test::MockRHIDevice device;
    std::vector<uint8_t> px(4u * 4u);

    REQUIRE_FALSE(TextureLoader::loadArrayFromPixels(device, px.data(), px.size(), 2, 2, 0, 2).success);
    REQUIRE_FALSE(TextureLoader::loadArrayFromPixels(device, px.data(), px.size(), 0, 2, 2, 2).success);
    REQUIRE_FALSE(TextureLoader::loadArrayFromPixels(device, nullptr, 0, 2, 2, 2, 2).success);
    // Tile larger than the image -> 0 layers, which is an error, not an empty success.
    REQUIRE_FALSE(TextureLoader::loadArrayFromPixels(device, px.data(), px.size(), 2, 2, 4, 4).success);
    REQUIRE(device.textureDescs.empty());
}
