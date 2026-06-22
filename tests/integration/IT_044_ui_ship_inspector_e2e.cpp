/**
 * Integration Test IT_044: ship inspector — data-driven blueprint, sprites-as-UI, clickable parts.
 *
 * Loads demo_ship_inspector.json, pushes a ship whose parts[] mix colour BLOCKS (tex 0) and SPRITES
 * (tex 1..4). Verifies headlessly:
 *
 *   A. SPRITES-AS-UI — the sprite parts emit render:sprite:* (the UI draws textures, not just rects).
 *   B. CLICKABLE PART carries its data — clicking a part fires ship:part with that part's id (resolved
 *      against its repeater scope), proving per-part events on a data-driven blueprint.
 *   C. REACTIVE INFO — relaying the click (merge selectedPart) renders the part's label in the info panel,
 *      and the `if {{noPart}}` hint flips off.
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

TEST_CASE("IT_044: ship inspector blueprint — sprites-as-UI + clickable data-driven parts", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("insp_host");
    auto uiIO     = mgr.createInstance("insp_ui");
    auto game     = mgr.createInstance("insp_game");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "insp_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 1280);
    cfg.setInt("windowHeight", 720);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_blueprint.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int spriteAdds = 0;
    std::vector<std::string> texts;
    std::string partId, partLabel;
    game->subscribe("render:sprite:add",    [&](const Message&) { ++spriteAdds; });
    game->subscribe("render:text:add",       [&](const Message& m){ texts.push_back(m.data->getString("text","")); });
    game->subscribe("render:text:update",    [&](const Message& m){ texts.push_back(m.data->getString("text","")); });
    game->subscribe("ship:part",             [&](const Message& m){ partId = m.data->getString("id",""); partLabel = m.data->getString("label",""); });

    auto pump = [&] {
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (game->hasMessages() > 0) game->pullAndDispatch();
    };
    auto saw = [&](const std::string& t){ return std::find(texts.begin(), texts.end(), t) != texts.end(); };
    auto move = [&](double x, double y){ auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",x); d->setDouble("y",y); hostPub->publish("input:mouse:move", std::move(d)); pump(); };
    auto btn  = [&](bool p){ auto d=std::make_unique<JsonDataNode>("d"); d->setInt("button",0); d->setBool("pressed",p); hostPub->publish("input:mouse:button", std::move(d)); pump(); };
    auto click = [&](double x, double y){ move(x,y); btn(true); btn(false); };

    pump();

    // Push the ship: parts as blocks (tex 0) + sprites (tex 1..4 from the SVG->PNG art).
    auto part = [](const char* id,int x,int y,int w,int h,const char* col,int tex,const char* lbl){
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
    spriteAdds = 0; texts.clear(); pump(); pump();

    // --- A. sprites-as-UI: the 4 sprite parts (cockpit/gun/reactor/engine) emit render:sprite. ---
    INFO("sprite adds = " << spriteAdds);
    REQUIRE(spriteAdds >= 4);
    REQUIRE(saw("S.S. Aurora"));                      // bound title rendered
    REQUIRE(saw("Clique une piece de la maquette"));  // if {{noPart}} hint shown

    // --- B. clickable part carries its id: the cockpit (blueprint 160,10 -> abs ~442,192). ---
    // window (230,70) + titlebar 28 -> content (230,98); blueprint (12,44) -> (242,142); part (160,10) -> (402,152) w80h80.
    partId.clear(); click(442, 192);
    INFO("part='" << partId << "' label='" << partLabel << "'");
    REQUIRE(partId == "cockpit");
    REQUIRE(partLabel == "Cockpit");

    // --- C. reactive info: relay the click -> merge selectedPart -> label renders + hint flips off. ---
    {
        json patch = { {"noPart", false}, {"selectedPart", {{"label","Cockpit"},{"stat","PV 120"}}} };
        hostPub->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", std::move(patch)));
    }
    texts.clear(); pump(); pump();
    REQUIRE(saw("Cockpit"));      // selectedPart.label in the info panel

    uiModule->shutdown();
}
