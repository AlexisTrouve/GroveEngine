/**
 * Integration Test IT_018: UIModule layout hit-test E2E (audit #6).
 *
 * Every other E2E fixture uses ABSOLUTE layout precisely because non-absolute
 * layouts were suspected broken: UILayout::layout() (run in UIPanel::update)
 * rewrites each child's relative x/y, but computeAbsolutePosition() — which
 * derives absX/absY used by BOTH render and hit-test — only ran once at load,
 * BEFORE any layout pass. So clicks landed at stale pre-layout coordinates.
 *
 * This test drives a real click on a button positioned purely by a vertical
 * layout (the button has no explicit x/y; its position comes entirely from
 * padding + the preceding sibling + spacing). It locks the #6 fix.
 *
 * Fixture (vertical, paddingTop=20, paddingLeft=20, spacing=10, align start):
 *   btn_a: 200x50 -> laid out at (20,20)   -> center (120,45)
 *   btn_b: 200x50 -> laid out at (20,80)   -> center (120,105)
 * Before the fix both buttons keep their load-time absX (0,0) (no explicit x/y),
 * so a click at btn_b's laid-out center (120,105) misses everything (105 > 50).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_018: clicking a button placed by a vertical layout hits it (audit #6)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("lay_input");
    auto uiIO     = mgr.createInstance("lay_ui");
    auto observer = mgr.createInstance("lay_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "lay_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_layout_vertical.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int actions = 0;
    std::string actionName;
    observer->subscribe("ui:action", [&](const Message& m) {
        actions++;
        actionName = m.data->getString("action", "");
    });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0); d->setBool("pressed", pressed);
        inputPub->publish("input:mouse:button", std::move(d));
    };

    // Settle frame: hit-test/dispatch run at the TOP of a frame using the PREVIOUS
    // frame's absX, and the layout pass runs at the bottom. One input-less frame lets
    // the layout (and the #6 recompute) populate absX before we interact.
    pump();

    // Click btn_b's laid-out center. btn_b is positioned only by the vertical layout
    // (after btn_a + spacing); its load-time absX is (0,0), so this hits only if the
    // absolute positions were recomputed post-layout.
    const double cx = 120.0, cy = 105.0;
    sendMove(cx, cy);          pump();
    sendButton(true);          pump();
    sendButton(false);         pump();

    INFO("actions=" << actions << " action='" << actionName << "'");
    REQUIRE(actions == 1);
    REQUIRE(actionName == "test:b");

    uiModule->shutdown();
}
