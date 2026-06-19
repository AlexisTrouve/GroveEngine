/**
 * Unit Tests: render-side view culling in SpritePass / TilemapPass.
 *
 * WHAT  : World-space draws outside the camera view must not be uploaded/drawn. SpritePass skips
 *         off-screen sprite instances; the GPU TilemapPass draws ONE quad per visible chunk (the
 *         fragment shader resolves every tile) and skips chunks whose AABB is off-screen. HUD
 *         (screen-space, view 1) is NEVER culled.
 *
 * WHY    : the renderer drew everything submitted — wasteful for big tilemaps/scenes. Sprite culling
 *         bounds per-frame work to a screenful; the tilemap's index-texture path makes the draw cost
 *         independent of tile count (1 draw/chunk), so a 360k-tile chunk is still a single draw.
 *
 * HOW    : MockRHIDevice + real RHICommandBuffer; we sum DrawInstanced.instanceCount (sprites) and
 *         count DrawIndexed commands (tilemap quads) that actually reached the GPU.
 */

#include <catch2/catch_test_macros.hpp>

#include "Passes/SpritePass.h"
#include "Passes/TilemapPass.h"
#include "Frame/FramePacket.h"
#include "RHI/RHICommandBuffer.h"
#include "../mocks/MockRHIDevice.h"

#include <vector>

using namespace grove;
using namespace grove::test;

// Total instances submitted = sum of DrawInstanced.instanceCount recorded in the buffer.
static uint32_t drawnInstances(const rhi::RHICommandBuffer& cmd) {
    uint32_t n = 0;
    for (const auto& c : cmd.getCommands()) {
        if (c.type == rhi::CommandType::DrawInstanced) n += c.drawInstanced.instanceCount;
    }
    return n;
}

// Number of DrawIndexed commands = quads drawn. The GPU tilemap draws exactly one quad per VISIBLE
// chunk (the fragment shader resolves every tile), so this counts visible chunks.
static uint32_t drawIndexedCount(const rhi::RHICommandBuffer& cmd) {
    uint32_t n = 0;
    for (const auto& c : cmd.getCommands()) {
        if (c.type == rhi::CommandType::DrawIndexed) ++n;
    }
    return n;
}

TEST_CASE("SpritePass culls world sprites outside the camera view", "[culling][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    SpritePass pass(shader);
    pass.setup(device);

    // Default mainView: pos (0,0), zoom 1, viewport 1280x720 -> visible world [0,1280]x[0,720].
    SpriteInstance inView{};  inView.x = 100.0f; inView.y = 100.0f; inView.scaleX = 32.0f; inView.scaleY = 32.0f;
    SpriteInstance offView{}; offView.x = 5000.0f; offView.y = 5000.0f; offView.scaleX = 32.0f; offView.scaleY = 32.0f;
    SpriteInstance arr[2] = {inView, offView};

    FramePacket frame;
    frame.sprites = arr;
    frame.spriteCount = 2;

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, device, cmd);

    REQUIRE(drawnInstances(cmd) == 1);   // only the in-view sprite reached the GPU
    pass.shutdown(device);
}

TEST_CASE("SpritePass does NOT cull HUD sprites (screen-space)", "[culling][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    SpritePass pass(shader);
    pass.setup(device);

    // A HUD sprite at coords that WOULD be culled in world space — but HUD is screen-space.
    SpriteInstance hud{}; hud.x = 5000.0f; hud.y = 5000.0f; hud.scaleX = 32.0f; hud.scaleY = 32.0f;
    SpriteInstance arr[1] = {hud};

    FramePacket frame;
    frame.hudSprites = arr;
    frame.hudSpriteCount = 1;

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, device, cmd);

    REQUIRE(drawnInstances(cmd) == 1);   // HUD never culled
    pass.shutdown(device);
}

TEST_CASE("TilemapPass draws one quad per visible chunk and culls off-screen chunks", "[culling][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    TilemapPass pass(shader);
    pass.setup(device);

    std::vector<uint16_t> tiles(100, static_cast<uint16_t>(1));  // 100x1 row of 32px tiles
    TilemapChunk chunk{};
    chunk.x = 0.0f; chunk.y = 0.0f;
    chunk.width = 100; chunk.height = 1;
    chunk.tileWidth = 32; chunk.tileHeight = 32;
    chunk.textureId = 0;
    chunk.tiles = tiles.data();
    chunk.tileCount = tiles.size();

    FramePacket frame;
    frame.tilemaps = &chunk;
    frame.tilemapCount = 1;

    SECTION("visible chunk: one quad, one R16UI POINT/CLAMP index texture, one region upload") {
        rhi::RHICommandBuffer cmd;
        pass.execute(frame, device, cmd);

        REQUIRE(drawIndexedCount(cmd) == 1);   // ONE quad covers the whole chunk
        REQUIRE(drawnInstances(cmd) == 0);     // the old per-tile instanced path is gone

        // An R16UI POINT/CLAMP index texture sized to the grid was created.
        bool foundIndex = false;
        for (const auto& d : device.textureDescs) {
            if (d.format == rhi::TextureDesc::R16UI) {
                foundIndex = true;
                REQUIRE(d.filter == rhi::TextureDesc::Point);
                REQUIRE(d.wrap   == rhi::TextureDesc::Clamp);
                REQUIRE(d.width  == 100);
                REQUIRE(d.height == 1);
            }
        }
        REQUIRE(foundIndex);

        // The grid was uploaded once via the region overload, at the grid's size.
        REQUIRE(device.textureRegionUpdates.size() == 1);
        REQUIRE(device.textureRegionUpdates.back().w == 100);
        REQUIRE(device.textureRegionUpdates.back().h == 1);
    }

    SECTION("off-screen chunk: no draw, no upload (chunk-level cull)") {
        chunk.x = 100000.0f;   // far outside the [0,1280]x[0,720] view
        rhi::RHICommandBuffer cmd;
        pass.execute(frame, device, cmd);

        REQUIRE(drawIndexedCount(cmd) == 0);
        REQUIRE(device.textureRegionUpdates.empty());
    }

    pass.shutdown(device);
}

TEST_CASE("GPU tilemap draw cost is independent of tile count (1 draw for a 360k-tile chunk)", "[culling][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    TilemapPass pass(shader);
    pass.setup(device);

    // 600x600 = 360,000 tiles. The old path generated ~940 instances; the index-texture path
    // generates ZERO per-tile CPU work — a single quad + a single index upload, full stop.
    const int W = 600, H = 600;
    std::vector<uint16_t> tiles(static_cast<size_t>(W) * H, static_cast<uint16_t>(1));
    TilemapChunk chunk{};
    chunk.x = 0.0f; chunk.y = 0.0f;
    chunk.width = static_cast<uint16_t>(W); chunk.height = static_cast<uint16_t>(H);
    chunk.tileWidth = 32; chunk.tileHeight = 32;
    chunk.textureId = 0;
    chunk.tiles = tiles.data();
    chunk.tileCount = tiles.size();

    FramePacket frame;
    frame.tilemaps = &chunk;
    frame.tilemapCount = 1;

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, device, cmd);

    REQUIRE(drawIndexedCount(cmd) == 1);                       // ONE draw, not O(map)
    REQUIRE(drawnInstances(cmd) == 0);                         // zero per-tile instances
    REQUIRE(device.textureRegionUpdates.size() == 1);          // one upload for the whole grid
    REQUIRE(device.textureRegionUpdates.back().w == 600);
    REQUIRE(device.textureRegionUpdates.back().h == 600);
    pass.shutdown(device);
}

TEST_CASE("Retained tilemap uploads once: dirty frame uploads, clean frame skips (Slice A4.1)", "[culling][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    TilemapPass pass(shader);
    pass.setup(device);

    std::vector<uint16_t> tiles(100, static_cast<uint16_t>(1));
    TilemapChunk chunk{};
    chunk.x = 0.0f; chunk.y = 0.0f;
    chunk.width = 10; chunk.height = 10;
    chunk.tileWidth = 32; chunk.tileHeight = 32;
    chunk.tiles = tiles.data();
    chunk.tileCount = tiles.size();
    chunk.id = 42;          // retained chunk
    chunk.dirty = true;     // freshly added

    FramePacket frame;
    frame.tilemaps = &chunk;
    frame.tilemapCount = 1;

    // Frame 1 (dirty): uploads + draws.
    { rhi::RHICommandBuffer cmd; pass.execute(frame, device, cmd); REQUIRE(drawIndexedCount(cmd) == 1); }
    REQUIRE(device.textureRegionUpdates.size() == 1);

    // Frame 2 (clean): still drawn, but the resident texture is REUSED — no upload. This is the win.
    chunk.dirty = false;
    { rhi::RHICommandBuffer cmd; pass.execute(frame, device, cmd); REQUIRE(drawIndexedCount(cmd) == 1); }
    REQUIRE(device.textureRegionUpdates.size() == 1);   // STILL 1 — upload-once

    // Frame 3 (dirty again = updated): re-uploads.
    chunk.dirty = true;
    { rhi::RHICommandBuffer cmd; pass.execute(frame, device, cmd); }
    REQUIRE(device.textureRegionUpdates.size() == 2);

    pass.shutdown(device);
}

TEST_CASE("Tilemap bakes a mipped LOD color texture per chunk (Slice B1)", "[culling][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    TilemapPass pass(shader);
    pass.setup(device);

    std::vector<uint16_t> tiles(64, static_cast<uint16_t>(1));   // 8x8 chunk
    TilemapChunk chunk{};
    chunk.x = 0.0f; chunk.y = 0.0f;
    chunk.width = 8; chunk.height = 8;
    chunk.tileWidth = 32; chunk.tileHeight = 32;
    chunk.tiles = tiles.data();
    chunk.tileCount = tiles.size();

    FramePacket frame;
    frame.tilemaps = &chunk;
    frame.tilemapCount = 1;

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, device, cmd);

    // A mipped RGBA8 LOD color texture sized to the grid must have been created. The procedural
    // atlas array (created in setup) is also RGBA8 8x8 — distinguish by layers==1 + a full mip chain.
    bool foundLod = false;
    for (const auto& d : device.textureDescs) {
        if (d.format == rhi::TextureDesc::RGBA8 && d.layers == 1 && d.mipLevels > 1
            && d.width == 8 && d.height == 8) {
            foundLod = true;
            REQUIRE(d.mipLevels == 4);                       // 8 -> 4 -> 2 -> 1
            REQUIRE(d.filter == rhi::TextureDesc::Linear);   // trilinear over the mip chain
            REQUIRE(d.wrap   == rhi::TextureDesc::Clamp);
        }
    }
    REQUIRE(foundLod);
    pass.shutdown(device);
}

TEST_CASE("Retained tilemap partial update uploads only the dirty sub-rect (Slice A4.2)", "[culling][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    TilemapPass pass(shader);
    pass.setup(device);

    std::vector<uint16_t> tiles(100, static_cast<uint16_t>(1));   // 10x10
    TilemapChunk chunk{};
    chunk.x = 0; chunk.y = 0; chunk.width = 10; chunk.height = 10;
    chunk.tileWidth = 32; chunk.tileHeight = 32;
    chunk.tiles = tiles.data(); chunk.tileCount = tiles.size();
    chunk.id = 1; chunk.dirty = true;     // frame 1: full (dirtyW == 0)

    FramePacket frame;
    frame.tilemaps = &chunk;
    frame.tilemapCount = 1;

    // Frame 1: texture created -> full upload.
    { rhi::RHICommandBuffer cmd; pass.execute(frame, device, cmd); }
    REQUIRE(device.textureRegionUpdates.size() == 1);
    REQUIRE(device.textureRegionUpdates.back().w == 10);
    REQUIRE(device.textureRegionUpdates.back().h == 10);

    // Frame 2: partial dirty rect -> only that sub-rect is uploaded (texture already resident).
    chunk.dirty = true;
    chunk.dirtyX = 2; chunk.dirtyY = 3; chunk.dirtyW = 4; chunk.dirtyH = 5;
    { rhi::RHICommandBuffer cmd; pass.execute(frame, device, cmd); }
    REQUIRE(device.textureRegionUpdates.size() == 2);
    const auto& u = device.textureRegionUpdates.back();
    REQUIRE(u.x == 2);
    REQUIRE(u.y == 3);
    REQUIRE(u.w == 4);
    REQUIRE(u.h == 5);

    pass.shutdown(device);
}
