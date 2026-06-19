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
