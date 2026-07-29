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

// ============================================================================
// Additive sprites (`blend: "additive"` on render:sprite) — the batching contract.
//
// The look this exists for is a Waterfall-style engine plume: a STRETCHED quad that GLOWS, so two
// overlapping plumes brighten where they cross. `render:sprite` could already stretch and rotate a
// texture but only in ALPHA blend; `render:particle` was additive but a square billboard. Neither
// could draw "additive AND stretched", which is the whole effect.
//
// SpritePass sets ONE render state per batch, so the batch has to BREAK on a blend change exactly
// as it already breaks on a texture or clip change. Without that break, the first sprite's blend
// would silently govern every sprite batched with it — and the bug would look like "additive works
// sometimes", depending on draw order.
// ============================================================================

static int submitCount(const rhi::RHICommandBuffer& cmd) {
    int n = 0;
    for (const auto& c : cmd.getCommands()) if (c.type == rhi::CommandType::Submit) ++n;
    return n;
}

// Blend states recorded, in order, one per SetState command.
static std::vector<rhi::BlendMode> submittedBlends(const rhi::RHICommandBuffer& cmd) {
    std::vector<rhi::BlendMode> out;
    for (const auto& c : cmd.getCommands()) {
        if (c.type == rhi::CommandType::SetState) out.push_back(c.setState.state.blend);
    }
    return out;
}

static SpriteInstance makeSprite(float x, bool additive) {
    SpriteInstance s{};
    s.x = x; s.y = 0.0f;
    s.scaleX = 10.0f; s.scaleY = 10.0f;
    s.u1 = 1.0f; s.v1 = 1.0f;
    s.textureId = 0.0f;          // same texture: only the blend may split the batch
    s.layer = 0.0f;
    s.padding0 = additive ? 1.0f : 0.0f;
    s.r = s.g = s.b = s.a = 1.0f;
    return s;
}

TEST_CASE("SpritePass: sprites of the SAME blend stay in ONE batch", "[sprite][blend][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    SpritePass pass(shader);
    pass.setup(device);
    rhi::RHICommandBuffer cmd;

    SpriteInstance sprites[3] = { makeSprite(0.0f, false), makeSprite(20.0f, false), makeSprite(40.0f, false) };
    FramePacket packet;
    packet.sprites = sprites;
    packet.spriteCount = 3;
    packet.mainView.viewportW = 800; packet.mainView.viewportH = 600; packet.mainView.zoom = 1.0f;

    pass.execute(packet, device, cmd);

    // The non-regression: adding a blend field must not fragment batches that share one.
    REQUIRE(submitCount(cmd) == 1);
    pass.shutdown(device);
}

TEST_CASE("SpritePass: a blend CHANGE breaks the batch", "[sprite][blend][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    SpritePass pass(shader);
    pass.setup(device);
    rhi::RHICommandBuffer cmd;

    // Same texture, same layer — ONLY the blend differs. Before the break existed these three
    // batched together and the first one's state governed all of them.
    SpriteInstance sprites[3] = { makeSprite(0.0f, false), makeSprite(20.0f, true), makeSprite(40.0f, false) };
    FramePacket packet;
    packet.sprites = sprites;
    packet.spriteCount = 3;
    packet.mainView.viewportW = 800; packet.mainView.viewportH = 600; packet.mainView.zoom = 1.0f;

    pass.execute(packet, device, cmd);

    REQUIRE(submitCount(cmd) == 3);

    // ...and each batch carries ITS OWN blend, in draw order. Counting batches alone would pass on
    // a pass that split correctly but then submitted them all as alpha.
    const std::vector<rhi::BlendMode> blends = submittedBlends(cmd);
    REQUIRE(blends.size() == 3);
    REQUIRE(blends[0] == rhi::BlendMode::Alpha);
    REQUIRE(blends[1] == rhi::BlendMode::Additive);
    REQUIRE(blends[2] == rhi::BlendMode::Alpha);

    pass.shutdown(device);
}

TEST_CASE("SpritePass: an all-additive set is additive, in one batch", "[sprite][blend][unit]") {
    MockRHIDevice device;
    rhi::ShaderHandle shader = device.createShader(rhi::ShaderDesc{});
    SpritePass pass(shader);
    pass.setup(device);
    rhi::RHICommandBuffer cmd;

    SpriteInstance sprites[2] = { makeSprite(0.0f, true), makeSprite(20.0f, true) };
    FramePacket packet;
    packet.sprites = sprites;
    packet.spriteCount = 2;
    packet.mainView.viewportW = 800; packet.mainView.viewportH = 600; packet.mainView.zoom = 1.0f;

    pass.execute(packet, device, cmd);

    REQUIRE(submitCount(cmd) == 1);
    const std::vector<rhi::BlendMode> blends = submittedBlends(cmd);
    REQUIRE_FALSE(blends.empty());
    REQUIRE(blends[0] == rhi::BlendMode::Additive);

    pass.shutdown(device);
}
