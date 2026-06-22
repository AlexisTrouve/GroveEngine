/**
 * Integration Test IT_043: the FULL demo composition (everything together) — headless verification.
 *
 * Loads the same `demo_fleet_command.json` the visual demo uses and drives it as the "game" would, checking
 * the whole stack works COMBINED (not just each feature in isolation):
 *
 *   A. BINDING — HUD + tabs render the bound data ("Crédits: 1240", "Tour 3"); the fleet list renders rows.
 *   B. PER-ITEM EVENT — clicking a fleet row fires fleet:select with that row's id.
 *   C. REACTIVE LOOP — relaying the selection (ui:data:merge selected+flags) updates the detail window
 *      ("Classe: Frigate") AND flips the `if`s (the "select a ship" hint -> the "Saborder" button).
 *   D. TURN REACTIVITY — clicking "Tour +" fires turn:advance; relaying ui:data:merge {turn} re-renders it.
 *   E. UI-CONTROL EVENTS — "Journal" fires demo:drawer (the game relays to ui:drawer:toggle).
 *
 * This is the composition's verification; the visual test_ui_full_demo is the demo (no assertions).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <vector>
#include <string>
#include <algorithm>

using namespace grove;

TEST_CASE("IT_043: full fleet-command demo composition works end to end", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("demo_host");
    auto uiIO     = mgr.createInstance("demo_ui");
    auto game     = mgr.createInstance("demo_game");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "demo_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 1280);
    cfg.setInt("windowHeight", 720);
    cfg.setString("layoutFile", "../../assets/ui/demo_fleet_command.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::vector<std::string> texts;
    std::string selectId, drawerId; bool turnAdvanced = false;
    game->subscribe("render:text:add",    [&](const Message& m) { texts.push_back(m.data->getString("text", "")); });
    game->subscribe("render:text:update", [&](const Message& m) { texts.push_back(m.data->getString("text", "")); });
    game->subscribe("fleet:select",  [&](const Message& m) { selectId = m.data->getString("id", ""); });
    game->subscribe("turn:advance",  [&](const Message&)   { turnAdvanced = true; });
    game->subscribe("demo:drawer",   [&](const Message& m) { drawerId = m.data->getString("id", ""); });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (game->hasMessages() > 0) game->pullAndDispatch();
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

    pump();

    // Initial model (as the game pushes it).
    {
        json fleet = json::array({
            {{"id","s0"},{"name","Aurora"},  {"cls","Frigate"},{"hp",0.9}},
            {{"id","s1"},{"name","Borealis"},{"cls","Hauler"}, {"hp",0.6}},
            {{"id","s2"},{"name","Cygnus"},  {"cls","Scout"},  {"hp",0.4}}
        });
        json model = { {"credits",1240},{"turn",3},{"fleetCount",3},{"hasSelection",false},{"noSelection",true},
                       {"selected", {{"id",""},{"name","—"},{"cls","—"},{"hp",0}}}, {"fleet",fleet} };
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", std::move(model)));
    }
    texts.clear(); pump(); pump();

    // --- A. binding renders the model. ---
    REQUIRE(saw("Crédits: 1240"));
    REQUIRE(saw("Tour 3"));
    REQUIRE(saw("Aurora"));                 // a fleet row label

    // --- B. per-item event: clicking row 0 (list at y=60, rowHeight 56 -> row 0 ~ y88). ---
    selectId.clear(); click(160, 88);
    INFO("select='" << selectId << "'");
    REQUIRE(selectId == "s0");

    // --- C. reactive loop: relay the selection (game) -> detail updates + `if`s flip. ---
    REQUIRE(saw("← Sélectionne un vaisseau dans la flotte"));   // hint shown while noSelection
    {
        json patch = { {"hasSelection",true},{"noSelection",false},
                       {"selected", {{"id","s0"},{"name","Aurora"},{"cls","Frigate"},{"hp",0.9}}} };
        hostPub->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", std::move(patch)));
    }
    texts.clear(); pump(); pump();
    REQUIRE(saw("Classe: Frigate"));        // detail bound to selected.cls
    REQUIRE(saw("Saborder"));               // if {{hasSelection}} now true -> button shown

    // --- D. turn reactivity: "Tour +" button (abs ~905,23) -> event -> relay -> re-render. ---
    turnAdvanced = false; click(905, 23);
    REQUIRE(turnAdvanced);
    {
        json patch = { {"turn",4} };
        hostPub->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", std::move(patch)));
    }
    texts.clear(); pump();
    REQUIRE(saw("Tour 4"));

    // --- E. UI-control event: "Journal" button (abs ~1010,23) -> demo:drawer {journal}. ---
    drawerId.clear(); click(1010, 23);
    INFO("drawer='" << drawerId << "'");
    REQUIRE(drawerId == "journal");

    uiModule->shutdown();
}
