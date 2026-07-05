/**
 * Integration Test IT_054: an animated flipbook widget advances its sprite-sheet cell (UI slice 6a).
 *
 * WHAT  : Loads UIModule with a fixture holding one `flipbook` widget (a 4-column sheet at 60 fps,
 *         textureId 5). We pump frames and observe the published render:sprite:* — the widget must
 *         cycle through distinct cell UVs as time advances, and it must do so via render:sprite:update
 *         (the retained UV-change publish path).
 * WHY   : This is the E2E proof of slice 6a. It is decisive: before the UV plumbing, u0 was hardcoded
 *         to 0 in publishSprite* and UVs were absent from change-detection, so u0 would be stuck at 0
 *         and no UV-only update would ever republish → both assertions below fail. "No E2E = it
 *         doesn't exist" (project doctrine) — this makes the animation objectively observable headless.
 * HOW   : cols=4 → cell u0 ∈ {0, 0.25, 0.5, 0.75}. We isolate the flipbook by its textureId (5), record
 *         every observed u0, and require (a) at least two distinct u0 values, (b) the specific 0.25
 *         (frame 1 → real sheet-cell math, not just "some change"), (c) a render:sprite:update fired.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <cmath>
#include <set>

using namespace grove;

TEST_CASE("IT_054: a flipbook widget cycles its sprite-sheet cell over time (UI slice 6a)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto uiIO     = mgr.createInstance("flip_ui");
    auto observer = mgr.createInstance("flip_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "flip_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_flipbook.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::set<int> distinctU0;   // observed u0 quantised to 1/100 (the flipbook's cells are 0.25 apart)
    bool sawQuarter = false;    // saw u0 ≈ 0.25 (frame index 1) — proves the real sheet-cell UV math
    bool sawUpdate  = false;    // saw a render:sprite:update — proves the retained UV-change republish

    auto onSprite = [&](const Message& m, bool isUpdate) {
        // Isolate the flipbook by its texture id (the fixture's root panel doesn't share it).
        if (m.data->getInt("textureId", -1) != 5) return;
        const double u0 = m.data->getDouble("u0", -1.0);
        if (u0 < 0.0) return;
        distinctU0.insert(static_cast<int>(std::lround(u0 * 100.0)));
        if (std::abs(u0 - 0.25) < 0.01) sawQuarter = true;
        if (isUpdate) sawUpdate = true;
    };
    observer->subscribe("render:sprite:add",    [&](const Message& m) { onSprite(m, false); });
    observer->subscribe("render:sprite:update", [&](const Message& m) { onSprite(m, true);  });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);   // ~1 sheet frame per pump at 60 fps
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };

    // A full loop of the 4-cell sheet takes ~4 pumps; pump generously to sweep several cells.
    for (int i = 0; i < 10; ++i) pump();

    INFO("distinct u0 count = " << distinctU0.size() << ", sawQuarter=" << sawQuarter
         << ", sawUpdate=" << sawUpdate);
    REQUIRE(distinctU0.size() >= 2);   // the cell advanced (not stuck on frame 0)
    REQUIRE(sawQuarter);               // frame 1's UV (0.25) is exactly the sheet-cell mapping
    REQUIRE(sawUpdate);                // republished on a UV-only change (the change-detection fix)

    uiModule->shutdown();
}
