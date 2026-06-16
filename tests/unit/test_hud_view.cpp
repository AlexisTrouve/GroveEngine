/**
 * Unit Tests: HUD overlay view routing (SpritePass / TextPass -> bgfx view 1).
 *
 * WHAT  : Locks the MECHANISM behind the screen-space HUD: world-space draws go to view 0
 *         (the zoomable world camera), HUD draws (FramePacket::hudSprites / hudTexts) go to
 *         view 1 (the fixed screen-space overlay). The renderer module configures view 1
 *         with hudView's transform, so anything submitted to view 1 ignores the world zoom.
 *
 * WHY    : SceneCollector bucketing (test_scene_collector.cpp) proves the DATA splits world
 *         vs HUD. This proves the passes actually SUBMIT each bucket to the right bgfx view —
 *         without that, HUD sprites/text would still ride the world camera and zoom with it.
 *
 * HOW    : MockRHIDevice (no GPU), real RHICommandBuffer. The command buffer records each
 *         Submit with its ViewId, so we run a pass over a packet holding both a world and a
 *         HUD element and assert both view 0 and view 1 appear among the submits.
 */

#include <catch2/catch_test_macros.hpp>

#include "Passes/SpritePass.h"
#include "Passes/TextPass.h"
#include "Frame/FramePacket.h"
#include "RHI/RHICommandBuffer.h"
#include "../mocks/MockRHIDevice.h"

#include <algorithm>
#include <vector>

using namespace grove;
using namespace grove::test;

// Collect the ViewId of every Submit command recorded in a command buffer.
static std::vector<rhi::ViewId> submitViews(const rhi::RHICommandBuffer& cmd) {
    std::vector<rhi::ViewId> views;
    for (const auto& c : cmd.getCommands()) {
        if (c.type == rhi::CommandType::Submit) {
            views.push_back(c.submit.view);
        }
    }
    return views;
}

static bool hasView(const std::vector<rhi::ViewId>& views, rhi::ViewId v) {
    return std::find(views.begin(), views.end(), v) != views.end();
}

TEST_CASE("SpritePass routes HUD sprites to view 1, world sprites to view 0", "[hud_view][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    REQUIRE(shader.isValid());

    SpritePass pass(shader);
    pass.setup(device);

    SpriteInstance world{};  world.scaleX = 10.0f; world.scaleY = 10.0f;
    SpriteInstance hud{};    hud.scaleX = 20.0f;   hud.scaleY = 8.0f;

    FramePacket frame;
    frame.sprites = &world;    frame.spriteCount = 1;
    frame.hudSprites = &hud;   frame.hudSpriteCount = 1;

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, device, cmd);

    auto views = submitViews(cmd);
    INFO("submit views recorded: " << views.size());
    REQUIRE(hasView(views, 0));   // world -> view 0
    REQUIRE(hasView(views, 1));   // HUD   -> view 1

    pass.shutdown(device);
}

TEST_CASE("SpritePass with only world sprites never submits to view 1", "[hud_view][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    SpritePass pass(shader);
    pass.setup(device);

    SpriteInstance world{}; world.scaleX = 10.0f; world.scaleY = 10.0f;
    FramePacket frame;
    frame.sprites = &world; frame.spriteCount = 1;  // no hudSprites

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, device, cmd);

    auto views = submitViews(cmd);
    REQUIRE(hasView(views, 0));
    REQUIRE_FALSE(hasView(views, 1));   // nothing on the HUD view

    pass.shutdown(device);
}

TEST_CASE("TextPass routes HUD text to view 1, world text to view 0", "[hud_view][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    TextPass pass(shader);
    pass.setup(device);
    REQUIRE(pass.getFont().isValid());   // bitmap font must init headless or the test is moot

    TextCommand world{}; world.text = "W"; world.fontSize = 16; world.color = 0xFFFFFFFFu; world.layer = 0;
    TextCommand hud{};   hud.text   = "H"; hud.fontSize   = 16; hud.color   = 0xFFFFFFFFu; hud.layer = 0;

    FramePacket frame;
    frame.texts = &world;     frame.textCount = 1;
    frame.hudTexts = &hud;    frame.hudTextCount = 1;

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, device, cmd);

    auto views = submitViews(cmd);
    INFO("submit views recorded: " << views.size());
    REQUIRE(hasView(views, 0));   // world text -> view 0
    REQUIRE(hasView(views, 1));   // HUD text   -> view 1

    pass.shutdown(device);
}
