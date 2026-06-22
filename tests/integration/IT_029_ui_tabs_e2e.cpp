/**
 * Integration Test IT_029: tabbed container switches pages (UI framework slice 5c).
 *
 * Both pages put their button at the SAME screen position (160,155). Page 0 is active first, so the
 * click hits its button. Clicking tab "Two" switches to page 1 (publishing ui:tab:changed{index:1}),
 * after which the SAME click hits page 1's button — the active page flipped, and the inactive page's
 * content is gone (hidden + purged).
 *
 * tabs (100,100,300,200) barHeight 30 -> 2 tabs of width 150: tab0 [100,250], tab1 [250,400] (y 100..130).
 * content origin (100,130); each page's button -> abs [110,210]x[140,170] (center 160,155).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_029: clicking a tab switches the active page (UI slice 5c)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("tab_host");
    auto uiIO     = mgr.createInstance("tab_ui");
    auto observer = mgr.createInstance("tab_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "tab_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_tabs.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastAction;
    int tabChangedIndex = -1;
    observer->subscribe("ui:action", [&](const Message& m) { lastAction = m.data->getString("action", ""); });
    observer->subscribe("ui:tab:changed", [&](const Message& m) { tabChangedIndex = m.data->getInt("index", -1); });

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

    // Page 0 is active -> its button responds.
    lastAction.clear();
    click(160, 155);
    INFO("page0 action='" << lastAction << "'");
    REQUIRE(lastAction == "page0");

    // Click the second tab -> switch to page 1 (ui:tab:changed{1}); no ui:action from the tab itself.
    lastAction.clear();
    click(300, 115);
    INFO("tab action='" << lastAction << "' tabChangedIndex=" << tabChangedIndex);
    REQUIRE(tabChangedIndex == 1);
    REQUIRE(lastAction.empty());

    // The SAME content click now hits page 1's button (page 0 is hidden + purged).
    lastAction.clear();
    click(160, 155);
    INFO("page1 action='" << lastAction << "'");
    REQUIRE(lastAction == "page1");

    uiModule->shutdown();
}
