/**
 * Unit Tests: render-side view culling in SpritePass / TilemapPass.
 *
 * WHAT  : World-space draws outside the camera view must not be uploaded/drawn. The passes
 *         build the visible world bounds from FramePacket::mainView and skip instances whose
 *         AABB falls outside. HUD (screen-space, view 1) is NEVER culled.
 *
 * WHY    : the renderer drew everything submitted — wasteful for big tilemaps/scenes. Culling
 *         bounds per-frame work to roughly a screenful (the big tilemap-perf win) and reuses the
 *         grove::camera visibility math.
 *
 * HOW    : MockRHIDevice + real RHICommandBuffer; we sum DrawInstanced.instanceCount to count
 *         how many instances actually reached the GPU.
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

TEST_CASE("TilemapPass culls tiles outside the camera view", "[culling][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    TilemapPass pass(shader);
    pass.setup(device);

    // 100x1 row of 32px tiles from x=0; viewport is 1280 wide -> ~40 tiles visible, 60 off-screen.
    std::vector<uint16_t> tiles(100, static_cast<uint16_t>(1));  // all non-empty
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

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, device, cmd);

    const uint32_t drawn = drawnInstances(cmd);
    REQUIRE(drawn < 100);    // off-screen tail culled
    REQUIRE(drawn >= 38);    // the ~40 visible tiles kept (not over-culled)
    REQUIRE(drawn <= 44);
    pass.shutdown(device);
}

TEST_CASE("TilemapPass on a huge map draws only the visible window (O(visible), not O(map))", "[culling][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    TilemapPass pass(shader);
    pass.setup(device);

    // 600x600 = 360,000 tiles, all non-empty. Default viewport 1280x720 @ 32px tiles -> only a
    // ~41x23 window (~940 tiles) is visible. The pass must touch only the window, never 360k.
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

    const uint32_t drawn = drawnInstances(cmd);
    REQUIRE(drawn >= 850);     // ~941 visible tiles drawn (a screenful)
    REQUIRE(drawn <= 1050);
    REQUIRE(drawn < 5000);     // emphatically NOT the 360k map
    pass.shutdown(device);
}
