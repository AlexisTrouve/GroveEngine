/**
 * Integration Test IT_021: UIModule reflow-on-resize E2E (UI framework slice 1.1).
 *
 * Proves that publishing ui:resize {width,height} re-lays-out the tree against the NEW
 * viewport, so a widget sized relative to the screen MOVES. We never read widget coords
 * (the module is behind IModule) — we prove geometry changed by the CLICK, exactly like
 * IT_018: the SAME screen point hits a different button before vs after the resize.
 *
 * Fixture (root fills the viewport via widthPercent/heightPercent=1.0; horizontal layout;
 * two flex-1 buttons split the width):
 *   window 800 wide -> btn_a [0,400], btn_b [400,800]; click (500,300) hits btn_b -> "test:b".
 *   resize to 1200  -> btn_a [0,600], btn_b [600,1200]; click (500,300) now hits btn_a -> "test:a".
 *
 * Before slice 1.1, ui:resize is unhandled: the root keeps its 800-wide JSON size and (500,300)
 * keeps hitting btn_b, so the post-resize REQUIRE(test:a) fails. This locks the resize plumbing
 * (host publishes ui:resize, UIModule consumes it) + the root tracking the viewport.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_021: ui:resize reflows the layout (UI slice 1.1)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("rfl_host");   // stands in for the window host (input + resize)
    auto uiIO     = mgr.createInstance("rfl_ui");
    auto observer = mgr.createInstance("rfl_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "rfl_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_layout_reflow.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastAction;
    int actions = 0;
    observer->subscribe("ui:action", [&](const Message& m) {
        actions++;
        lastAction = m.data->getString("action", "");
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
        hostPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d));
    };
    auto click = [&](double x, double y) {
        sendMove(x, y);    pump();
        sendButton(true);  pump();
        sendButton(false); pump();
    };

    pump();  // settle: let the layout pass populate absX before we interact

    // --- Before resize: at 800 wide, (500,300) is in the RIGHT button (btn_b). ---
    lastAction.clear();
    click(500, 300);
    INFO("pre-resize action='" << lastAction << "'");
    REQUIRE(lastAction == "test:b");

    // --- Resize the viewport to 1200 wide. The root fills it, so the split moves to x=600. ---
    {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("width", 1200.0);
        d->setDouble("height", 600.0);
        hostPub->publish("ui:resize", std::move(d));   // host-published; UIModule consumes
    }
    pump();

    // --- After resize: (500,300) is now in the LEFT button (btn_a). Proves the reflow. ---
    lastAction.clear();
    click(500, 300);
    INFO("post-resize action='" << lastAction << "' actions=" << actions);
    REQUIRE(lastAction == "test:a");

    uiModule->shutdown();
}
