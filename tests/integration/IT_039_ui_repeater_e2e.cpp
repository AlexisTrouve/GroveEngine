/**
 * Integration Test IT_039: JSON-UI repeater — template x data array, per-item scope (engine step 4).
 *
 * A host panel with "repeat":"{{fleet}}" + a "template" instantiates the template once per fleet element,
 * each element becoming the row's data SCOPE. This proves the killer property — both directions resolve
 * against the ITEM:
 *
 *   A. BINDING-IN per item — each row's label "{{name}}" renders its own item's name (Aurora + Borealis).
 *   B. EVENTS-OUT per item — each row's button carries its item: clicking row 0 -> fleet:recall {shipId:s1},
 *      clicking row 1 -> {shipId:s2}. (The "in-row routing", now general + free.)
 *   C. RE-EXPANSION — pushing a new, larger fleet rebuilds the rows; the new row renders + clicks correctly.
 *
 * Fixture: host (100,100); template height 40 -> row i at abs y = 100 + i*40. Button (160,4,50,32) ->
 *   abs (260, 104 + i*40), center ~(285, 120 + i*40).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <vector>
#include <algorithm>

using namespace grove;

TEST_CASE("IT_039: repeater instantiates a template per item with per-item scope", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("rep_host");
    auto uiIO     = mgr.createInstance("rep_ui");
    auto observer = mgr.createInstance("rep_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "rep_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_repeater.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::vector<std::string> texts;
    std::string recallId;
    observer->subscribe("render:text:add",    [&](const Message& m) { texts.push_back(m.data->getString("text", "")); });
    observer->subscribe("render:text:update", [&](const Message& m) { texts.push_back(m.data->getString("text", "")); });
    observer->subscribe("fleet:recall",        [&](const Message& m) { recallId = m.data->getString("shipId", ""); });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto saw = [&](const std::string& t) { return std::find(texts.begin(), texts.end(), t) != texts.end(); };
    auto move = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d)); pump();
    };
    auto btn = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d)); pump();
    };
    auto click = [&](double x, double y) { move(x, y); btn(true); btn(false); };
    auto pushData = [&](json j) { hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", std::move(j))); };

    pump();  // settle

    // --- Push a 2-ship fleet. ---
    pushData(json{ {"fleet", json::array({ {{"id","s1"},{"name","Aurora"}}, {{"id","s2"},{"name","Borealis"}} })} });
    texts.clear(); pump();

    // A. Each row binds its OWN item's name.
    REQUIRE(saw("Aurora"));
    REQUIRE(saw("Borealis"));

    // B. Each row's button carries its OWN item id.
    recallId.clear(); click(285, 120);   // row 0
    INFO("row0 recall='" << recallId << "'");
    REQUIRE(recallId == "s1");
    recallId.clear(); click(285, 160);   // row 1
    INFO("row1 recall='" << recallId << "'");
    REQUIRE(recallId == "s2");

    // C. Re-expansion: a new 3-ship fleet rebuilds the rows; the new row renders + clicks right.
    pushData(json{ {"fleet", json::array({
        {{"id","s1"},{"name","Aurora"}}, {{"id","s2"},{"name","Borealis"}}, {{"id","s3"},{"name","Cygnus"}} })} });
    texts.clear(); pump();
    REQUIRE(saw("Cygnus"));
    recallId.clear(); click(285, 200);   // row 2 (y = 100 + 2*40 + ... center ~200)
    INFO("row2 recall='" << recallId << "'");
    REQUIRE(recallId == "s3");

    uiModule->shutdown();
}
