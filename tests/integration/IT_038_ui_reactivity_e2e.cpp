/**
 * Integration Test IT_038: JSON-UI reactivity — partial data updates (engine step 3, "be solid").
 *
 * A label binds "Ship: {{ship.name}} | {{credits}}". The game updates the model incrementally and the
 * label re-renders each time (observed on render:text:*):
 *
 *   A. ui:data {<full model>}        -> baseline render.
 *   B. ui:data:set {path, value}     -> deep path set (ship.name) leaves the rest intact.
 *   C. ui:data:merge {<partial>}     -> deep merge (credits) leaves the rest intact.
 *   D. ui:data:merge {<nested>}      -> deep merge into a sub-object (ship.name) — RFC 7386.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <vector>
#include <algorithm>

using namespace grove;

TEST_CASE("IT_038: partial data updates re-resolve bindings (set / merge)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("react_host");
    auto uiIO     = mgr.createInstance("react_ui");
    auto observer = mgr.createInstance("react_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "react_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_reactivity.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::vector<std::string> texts;
    auto cap = [&](const Message& m) { texts.push_back(m.data->getString("text", "")); };
    observer->subscribe("render:text:add", cap);
    observer->subscribe("render:text:update", cap);

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto saw = [&](const std::string& t) { return std::find(texts.begin(), texts.end(), t) != texts.end(); };
    auto publishJson = [&](const std::string& topic, json j) {
        hostPub->publish(topic, std::make_unique<JsonDataNode>("d", std::move(j)));
    };

    pump();  // settle

    // --- A. Full model. ---
    publishJson("ui:data", json{ {"ship", {{"name", "Aurora"}}}, {"credits", 1000} });
    texts.clear(); pump();
    REQUIRE(saw("Ship: Aurora | 1000"));

    // --- B. ui:data:set a deep path — only ship.name changes, credits preserved. ---
    publishJson("ui:data:set", json{ {"path", "ship.name"}, {"value", "Borealis"} });
    texts.clear(); pump();
    INFO("after set ship.name");
    REQUIRE(saw("Ship: Borealis | 1000"));

    // --- C. ui:data:merge a top-level field — credits changes, ship preserved. ---
    publishJson("ui:data:merge", json{ {"credits", 1500} });
    texts.clear(); pump();
    REQUIRE(saw("Ship: Borealis | 1500"));

    // --- D. ui:data:merge nested (deep merge / RFC 7386) — ship.name changes, credits preserved. ---
    publishJson("ui:data:merge", json{ {"ship", {{"name", "Cygnus"}}} });
    texts.clear(); pump();
    REQUIRE(saw("Ship: Cygnus | 1500"));

    uiModule->shutdown();
}
