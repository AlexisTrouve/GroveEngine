/**
 * Integration Test IT_053: the WHOLE UI runs HOSTED BY THE ENGINE — not hand-wired.
 *
 * Same scenario as IT_044 (data-driven ship blueprint: sprites-as-UI + clickable parts +
 * reactive info), but UIModule is driven ENTIRELY through DebugEngine:
 *   - hosted via IEngine::registerStaticModule() (no .so, no manual setConfiguration),
 *   - driven by engine.step() ONLY (no hand-wired uiModule->process(), no manual IIO pump).
 *
 * This is the acceptance lock for "the UI system runs on the engine": a real click travels
 * input -> UIModule -> ship:part across frames, delivered by the engine's own loop, exactly
 * as a static-linked game would consume it. If the UI only worked hand-wired (the old
 * bypass), this fails.
 *
 * NOTE on ordering (why this passes): UIModule self-drains its inbox INSIDE process()
 * (beginFrame resets input edges, processInput pulls + sets them, updateUI consumes them).
 * The engine processes modules, THEN pumps — so the injected input is consumed inside
 * process() with correct edge timing, and the post-process pump never races it.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/DebugEngine.h>
#include <grove/IModuleSystem.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include "UIModule.h"   // construct UIModule statically (UIModule_static)
#include <vector>
#include <string>
#include <algorithm>

using namespace grove;

TEST_CASE("IT_053: UIModule runs hosted on the engine — a click flows through engine.step()", "[integration][ui][engine][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub = mgr.createInstance("e_host");   // the "game" side: injects input + ui:data
    auto game    = mgr.createInstance("e_game");   // observes UIModule's output topics

    DebugEngine engine;
    engine.initialize();

    // Config forwarded to the module by registerStaticModule -> setConfiguration.
    auto cfg = std::make_unique<JsonDataNode>("config");
    cfg->setInt("windowWidth", 1280);
    cfg->setInt("windowHeight", 720);
    cfg->setString("layoutFile", "../../assets/ui/test_e2e_blueprint.json");
    cfg->setInt("baseLayer", 1000);

    // HOST the UI on the engine: a live UIModule, no .so, no hand-wiring.
    engine.registerStaticModule("e_ui", std::make_unique<UIModule>(),
                                ModuleSystemType::SEQUENTIAL, std::move(cfg));

    int spriteAdds = 0;
    std::vector<std::string> texts;
    std::string partId, partLabel;
    game->subscribe("render:sprite:add", [&](const Message&)   { ++spriteAdds; });
    game->subscribe("render:text:add",    [&](const Message& m){ texts.push_back(m.data->getString("text", "")); });
    game->subscribe("render:text:update", [&](const Message& m){ texts.push_back(m.data->getString("text", "")); });
    game->subscribe("ship:part",          [&](const Message& m){ partId = m.data->getString("id", ""); partLabel = m.data->getString("label", ""); });

    // engine.step() is the ONLY engine call; then drain the external observer (the
    // observer is the game side, not a hosted module, so the engine doesn't pump it).
    auto step  = [&]{ engine.step(1.0f / 60.0f); while (game->hasMessages() > 0) game->pullAndDispatch(); };
    auto saw   = [&](const std::string& t){ return std::find(texts.begin(), texts.end(), t) != texts.end(); };
    auto move  = [&](double x, double y){ auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y); hostPub->publish("input:mouse:move", std::move(d)); step(); };
    auto btn   = [&](bool p){ auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", p); hostPub->publish("input:mouse:button", std::move(d)); step(); };
    auto click = [&](double x, double y){ move(x, y); btn(true); btn(false); };

    step();  // settle

    // Push the ship: parts as blocks (tex 0) + sprites (tex 1..4) — identical to IT_044.
    auto part = [](const char* id, int x, int y, int w, int h, const char* col, int tex, const char* lbl){
        return json{ {"id",id},{"x",x},{"y",y},{"w",w},{"h",h},{"color",col},{"tex",tex},{"label",lbl},{"stat","..."} };
    };
    {
        json parts = json::array({
            part("cockpit",160,10,80,80,  "0xFFFFFFFF",1,"Cockpit"),
            part("hullA",  140,90,120,44, "0x3a4a63FF",0,"Coque avant"),
            part("gunL",   66,100,64,64,  "0xFFFFFFFF",4,"Canon babord"),
            part("reactor",160,190,80,80, "0xFFFFFFFF",2,"Reacteur"),
            part("engL",   108,312,70,92, "0xFFFFFFFF",3,"Moteur babord")
        });
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{
            {"ship", {{"name","S.S. Aurora"},{"parts",parts}}}, {"noPart", true},
            {"selectedPart", {{"label",""},{"stat",""}}} }));
    }
    spriteAdds = 0; texts.clear(); step(); step();

    // --- A. sprites-as-UI emitted THROUGH the engine. ---
    INFO("sprite adds = " << spriteAdds);
    REQUIRE(spriteAdds >= 4);
    REQUIRE(saw("S.S. Aurora"));
    REQUIRE(saw("Clique une piece de la maquette"));

    // --- B. a real click travels input -> UIModule (hosted) -> ship:part, all via engine.step(). ---
    partId.clear(); click(442, 192);   // cockpit (same coords as IT_044)
    INFO("part='" << partId << "' label='" << partLabel << "'");
    REQUIRE(partId == "cockpit");
    REQUIRE(partLabel == "Cockpit");

    // --- C. reactive info: relay the click -> merge selectedPart -> label renders. ---
    {
        json patch = { {"noPart", false}, {"selectedPart", {{"label","Cockpit"},{"stat","PV 120"}}} };
        hostPub->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", std::move(patch)));
    }
    texts.clear(); step(); step();
    REQUIRE(saw("Cockpit"));

    engine.shutdown();
    mgr.removeInstance("e_host");
    mgr.removeInstance("e_game");
}
