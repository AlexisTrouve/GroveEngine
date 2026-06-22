/**
 * Integration Test IT_037: JSON-UI data-binding (in) + declarative events (out) — step 2 of the engine.
 *
 *   A. BINDING-IN — a label's "text":"Credits: {{credits}}" resolves against pushed data: after
 *      ui:data {credits:1240}, the label RENDERS "Credits: 1240" (observed on render:text:* — proving
 *      data -> bound prop -> render output, with no imperative ui:set_text).
 *   B. EVENTS-OUT — a button declares on:click -> {event:"act:launch", args:{who:"{{commander}}",...}}.
 *      Clicking it publishes "act:launch" with the args RESOLVED against the data scope.
 *
 * Both directions resolve {{path}} against the same data context — the symmetry the engine is built on.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <vector>
#include <algorithm>

using namespace grove;

TEST_CASE("IT_037: data-binding (in) + declarative events (out)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("bind_host");
    auto uiIO     = mgr.createInstance("bind_ui");
    auto observer = mgr.createInstance("bind_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "bind_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_binding.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::vector<std::string> renderedTexts;
    std::string evWho, evFleet; bool evFired = false;
    auto captureText = [&](const Message& m) { renderedTexts.push_back(m.data->getString("text", "")); };
    observer->subscribe("render:text:add", captureText);
    observer->subscribe("render:text:update", captureText);
    observer->subscribe("act:launch", [&](const Message& m) {
        evFired = true; evWho = m.data->getString("who", ""); evFleet = m.data->getString("fleet", "");
    });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sawText = [&](const std::string& t) {
        return std::find(renderedTexts.begin(), renderedTexts.end(), t) != renderedTexts.end();
    };
    auto move = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d)); pump();
    };
    auto button = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d)); pump();
    };

    pump();  // settle (load-time bindings resolve against empty data)

    // --- Push the data context. ---
    {
        json j; j["credits"] = 1240; j["commander"] = "Vega"; j["fleet"] = "alpha";
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", j));
    }
    renderedTexts.clear();
    pump();  // re-resolve bindings + re-render -> the label publishes its new text

    // --- A. BINDING-IN: the label rendered the bound value. ---
    INFO("rendered texts after ui:data: " << renderedTexts.size());
    REQUIRE(sawText("Credits: 1240"));

    // --- B. EVENTS-OUT: clicking the button fires the declared event with resolved args. ---
    move(70, 75);            // over goBtn (20,60,100,30 -> center ~70,75)
    button(true);
    button(false);
    INFO("evFired=" << evFired << " who='" << evWho << "' fleet='" << evFleet << "'");
    REQUIRE(evFired);
    REQUIRE(evWho == "Vega");
    REQUIRE(evFleet == "alpha");

    uiModule->shutdown();
}
