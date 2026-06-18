/**
 * Unit Tests: RHI TextureDesc — integer index format + sampler control (tilemap Slice A0).
 *
 * WHAT  : The GPU tilemap path needs textures the current RHI cannot describe: an R16UI
 *         integer INDEX texture (1 texel = 1 tile id) sampled with POINT + CLAMP (no
 *         filtering, no wrap). This test locks the RHI CONTRACT — TextureDesc carries
 *         format + filter + wrap, and the device receives them unchanged — plus the
 *         invariant that defaults stay exactly as before.
 *
 * WHY    : indices are not colors. Bilinear-filtering or wrapping a tile id is meaningless
 *         and corrupts the lookup (blends neighbouring ids, wraps at the chunk edge — the
 *         classic tile-boundary off-by-one). The renderer only had RGBA8 / linear / wrap
 *         textures; A0 adds the description surface the tilemap shader will rely on.
 *         Defaults MUST remain RGBA8 / Linear / Repeat so every existing texture (sprites,
 *         fonts, atlas) keeps its current behavior bit-for-bit — surgical, no regressions.
 *
 * HOW    : MockRHIDevice records each TextureDesc handed to createTexture (no bgfx / GPU
 *         needed). We assert the new fields round-trip and that an un-customized desc still
 *         defaults to the legacy values. The bgfx mapping itself (R16UI -> R16U, the sampler
 *         flag bits) lives in BgfxDevice and is exercised visually at A2 — there is no
 *         headless GPU path to assert it here, by the same design as the rest of BgfxDevice.
 */

#include <catch2/catch_test_macros.hpp>

#include "RHI/RHITypes.h"
#include "../mocks/MockRHIDevice.h"

using namespace grove;

TEST_CASE("TextureDesc defaults are unchanged (RGBA8 / Linear / Repeat)", "[rhi][tilemap][unit]") {
    // A freshly-defaulted desc must behave like every texture created before A0 existed.
    rhi::TextureDesc desc;
    REQUIRE(desc.format == rhi::TextureDesc::RGBA8);
    REQUIRE(desc.filter == rhi::TextureDesc::Linear);
    REQUIRE(desc.wrap   == rhi::TextureDesc::Repeat);
}

TEST_CASE("TextureDesc can describe an R16UI POINT/CLAMP index texture", "[rhi][tilemap][unit]") {
    test::MockRHIDevice device;

    // The tile-index texture: integer ids, no filtering, no wrap.
    rhi::TextureDesc desc;
    desc.width  = 16;
    desc.height = 16;
    desc.format = rhi::TextureDesc::R16UI;   // integer tile-index format
    desc.filter = rhi::TextureDesc::Point;   // no bilinear blend across tile ids
    desc.wrap   = rhi::TextureDesc::Clamp;   // no wrap at the chunk edge

    device.createTexture(desc);

    // The contract: the device receives exactly what we described.
    REQUIRE(device.textureDescs.size() == 1);
    const rhi::TextureDesc& got = device.textureDescs.back();
    REQUIRE(got.format == rhi::TextureDesc::R16UI);
    REQUIRE(got.filter == rhi::TextureDesc::Point);
    REQUIRE(got.wrap   == rhi::TextureDesc::Clamp);
    REQUIRE(got.width  == 16);
    REQUIRE(got.height == 16);
}

TEST_CASE("updateTexture can patch a sub-region without a full re-upload (Slice A1)", "[rhi][tilemap][unit]") {
    // The retained index grid must be patched a few texels at a time (a tile flips, fog reveals)
    // instead of re-uploading the whole 720 KB grid. The region overload carries x/y/w/h.
    test::MockRHIDevice device;

    rhi::TextureDesc desc;
    desc.width  = 64;
    desc.height = 64;
    desc.format = rhi::TextureDesc::R16UI;
    rhi::TextureHandle tex = device.createTexture(desc);

    const uint16_t patch[4] = {1, 2, 3, 4};  // a 2x2 block of tile ids
    device.updateTexture(tex, patch, sizeof(patch), /*x*/10, /*y*/20, /*w*/2, /*h*/2);

    REQUIRE(device.textureRegionUpdates.size() == 1);
    const auto& u = device.textureRegionUpdates.back();
    REQUIRE(u.x == 10);
    REQUIRE(u.y == 20);
    REQUIRE(u.w == 2);
    REQUIRE(u.h == 2);
    REQUIRE(u.size == sizeof(patch));
}

TEST_CASE("Full-texture updateTexture still exists alongside the region overload", "[rhi][tilemap][unit]") {
    // The legacy 3-arg overload (full image) must remain callable — sprites/atlas use it.
    test::MockRHIDevice device;
    rhi::TextureDesc desc; desc.width = 8; desc.height = 8;
    rhi::TextureHandle tex = device.createTexture(desc);

    const uint8_t pixels[8 * 8 * 4] = {};
    device.updateTexture(tex, pixels, sizeof(pixels));   // resolves to the full overload
    REQUIRE(device.updateTextureCount == 1);
    REQUIRE(device.textureRegionUpdates.empty());        // not counted as a region update
}

TEST_CASE("Adding R16UI did not renumber the existing formats", "[rhi][tilemap][unit]") {
    // R16UI was appended to the enum; the pre-existing formats must keep their ordinals so
    // any serialized/compared value (and BgfxDevice's switch) stays correct.
    REQUIRE(static_cast<int>(rhi::TextureDesc::RGBA8) == 0);
    REQUIRE(static_cast<int>(rhi::TextureDesc::RGB8)  == 1);
    REQUIRE(static_cast<int>(rhi::TextureDesc::R8)    == 2);
    REQUIRE(static_cast<int>(rhi::TextureDesc::DXT1)  == 3);
    REQUIRE(static_cast<int>(rhi::TextureDesc::DXT5)  == 4);
    REQUIRE(static_cast<int>(rhi::TextureDesc::R16UI) == 5);
}
