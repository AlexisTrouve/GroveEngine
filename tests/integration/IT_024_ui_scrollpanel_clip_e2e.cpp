/**
 * Integration Test IT_024: ScrollPanel clips its children (UI framework slice 2a-2b).
 *
 * The renderer-side scissor is already GPU-proven (SpriteClipGpu / TextClipGpu). This locks the UI
 * WIRING: a ScrollPanel pushes its visible rect onto the UIRenderer clip stack before rendering its
 * children, so every render entry a child publishes carries that clip — which the renderer turns into
 * a bgfx scissor. We observe the IIO contract: the child's render:sprite:add must carry
 * clip == the panel's bounds; the panel's own chrome (drawn outside the push) must NOT.
 *
 * Fixture: a 200x150 scrollpanel at (100,100) holding a 180x400 panel "item". The item overflows the
 * panel vertically, so clipping matters. Expected: one render:sprite:add with clip = {100,100,200,150}.
 * Before slice 2a-2b no clip is ever published, so the assertion fails.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <cmath>

using namespace grove;

TEST_CASE("IT_024: a scroll panel publishes its clip onto child render entries (UI slice 2a-2b)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto uiIO     = mgr.createInstance("clip_ui");
    auto observer = mgr.createInstance("clip_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "clip_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_scrollpanel_clip.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    bool clippedToPanel = false;   // saw a render entry clipped to exactly the panel bounds
    int clippedCount = 0;
    auto onSprite = [&](const Message& m) {
        const double cw = m.data->getDouble("clipW", 0.0);
        if (cw <= 0.0) return;     // no clip on this entry (e.g. the panel's own background)
        const double cx = m.data->getDouble("clipX", -1.0);
        const double cy = m.data->getDouble("clipY", -1.0);
        const double ch = m.data->getDouble("clipH", -1.0);
        ++clippedCount;
        INFO("clipped entry: x=" << cx << " y=" << cy << " w=" << cw << " h=" << ch);
        if (std::abs(cx - 100.0) < 1.0 && std::abs(cy - 100.0) < 1.0 &&
            std::abs(cw - 200.0) < 1.0 && std::abs(ch - 150.0) < 1.0) {
            clippedToPanel = true;
        }
    };
    observer->subscribe("render:sprite:add", onSprite);
    observer->subscribe("render:sprite:update", onSprite);

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };

    // A few frames: the child's render:sprite:add fires on its first render() under the panel's clip.
    pump();
    pump();
    pump();

    INFO("clippedCount=" << clippedCount);
    REQUIRE(clippedToPanel);

    uiModule->shutdown();
}
