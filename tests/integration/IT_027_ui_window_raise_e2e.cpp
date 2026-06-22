/**
 * Integration Test IT_027: in-app window raise-on-click / z-order (UI framework slice 3b-2).
 *
 * Two overlapping windows each have a content button at the SAME screen point (185,198). Whichever
 * window is on top owns that point. Clicking the back window's title bar raises it (bringToFront),
 * so the SAME click that hit W2's button now hits W1's — the z-order flipped.
 *
 * w1 (100,100,200,150): btnA at content (50,50) -> abs [150,210]x[178,208]  ("winA")
 * w2 (160,140,200,150): btnB at content (0,20)  -> abs [160,220]x[188,218]  ("winB")  [on top initially]
 * overlap point (185,198) is in both buttons. w1's title bar [100,300]x[100,128] is exposed (w2 starts y=140).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_027: clicking a window raises it above the others (UI slice 3b-2)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("wr_host");
    auto uiIO     = mgr.createInstance("wr_ui");
    auto observer = mgr.createInstance("wr_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "wr_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_window_raise.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastAction;
    observer->subscribe("ui:action", [&](const Message& m) { lastAction = m.data->getString("action", ""); });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d));
    };
    auto click = [&](double x, double y) { sendMove(x, y); pump(); sendButton(true); pump(); sendButton(false); pump(); };

    pump();  // settle

    // W2 is on top initially -> the shared point hits its button.
    lastAction.clear();
    click(185, 198);
    INFO("initial action='" << lastAction << "'");
    REQUIRE(lastAction == "winB");

    // Click W1's exposed title bar -> raises W1 (absorbed, no content action).
    lastAction.clear();
    click(120, 114);
    INFO("titlebar action='" << lastAction << "'");
    REQUIRE(lastAction.empty());

    // The SAME shared point now hits W1's button -> z-order flipped.
    lastAction.clear();
    click(185, 198);
    INFO("after-raise action='" << lastAction << "'");
    REQUIRE(lastAction == "winA");

    uiModule->shutdown();
}
