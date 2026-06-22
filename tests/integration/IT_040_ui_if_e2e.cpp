/**
 * Integration Test IT_040: JSON-UI conditional `if` — data-driven show/hide that PURGES (engine step 5).
 *
 * A label declares "if":"{{showLabel}}". It renders only when the bound bool is true; when it goes false the
 * widget is hidden AND its retained render entries are released (render:text:remove — no ghost). Cycle:
 *
 *   A. show  (ui:data {showLabel:true})  -> the label renders ("Hello" on render:text:*).
 *   B. hide  (ui:data {showLabel:false}) -> no re-render AND a render:text:remove fires (purged, not ghosted).
 *   C. show again                         -> it re-registers + renders.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <vector>
#include <algorithm>

using namespace grove;

TEST_CASE("IT_040: `if` show/hide purges retained entries", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("if_host");
    auto uiIO     = mgr.createInstance("if_ui");
    auto observer = mgr.createInstance("if_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "if_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_if.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::vector<std::string> texts;
    int removeCount = 0;
    observer->subscribe("render:text:add",    [&](const Message& m) { texts.push_back(m.data->getString("text", "")); });
    observer->subscribe("render:text:update", [&](const Message& m) { texts.push_back(m.data->getString("text", "")); });
    observer->subscribe("render:text:remove", [&](const Message&) { ++removeCount; });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto saw = [&](const std::string& t) { return std::find(texts.begin(), texts.end(), t) != texts.end(); };
    auto setShow = [&](bool v) {
        json j; j["showLabel"] = v;
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", std::move(j)));
    };

    pump();  // settle (showLabel missing -> if false -> hidden)

    // --- A. show. ---
    setShow(true); texts.clear(); pump();
    REQUIRE(saw("Hello"));

    // --- B. hide -> not re-rendered AND purged (a remove fired). ---
    setShow(false); texts.clear(); removeCount = 0; pump();
    INFO("after hide: sawHello=" << saw("Hello") << " removes=" << removeCount);
    REQUIRE_FALSE(saw("Hello"));
    REQUIRE(removeCount >= 1);

    // --- C. show again -> re-registers + renders. ---
    setShow(true); texts.clear(); pump();
    REQUIRE(saw("Hello"));

    uiModule->shutdown();
}
