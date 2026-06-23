/**
 * Integration Test IT_048: the vessel-screen JOINTURE — click a fleet icon -> the inspector opens for THAT
 * ship. This is feature C of the spec ("quand on clique dessus ça doit show une window avec la maquette en
 * gros"). The inspector window lives hidden in the layout; the game reacts to vessel:open by pushing that
 * ship's data and revealing the window. Verified headlessly:
 *
 *   A. CLOSED — at rest the inspector is hidden, so its title doesn't render.
 *   B. OPEN ON CLICK — clicking a fleet icon fires vessel:open{id,name}; the host (this test) pushes the
 *      ship's blueprint + reveals the inspector -> the window title AND the ship name render, and the
 *      blueprint's sprite parts draw.
 *
 * The vessel:open -> (push data + show window) wiring is what the real demo does; here the test plays host.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <set>
#include <string>

using namespace grove;

TEST_CASE("IT_048: clicking a fleet icon opens the inspector for that ship", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("vo_host");
    auto uiIO     = mgr.createInstance("vo_ui");
    auto observer = mgr.createInstance("vo_obs");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "vo_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 1280);
    cfg.setInt("windowHeight", 720);
    cfg.setString("layoutFile", "../../assets/ui/demo_vessel_screen.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::set<std::string> texts;
    int spriteAdds = 0;
    double titleX = -1.0;   // rendered x of the window title — must be CENTERED, not at the top-left (0)
    auto onText = [&](const Message& m){
        const std::string t = m.data->getString("text","");
        texts.insert(t);
        if (t == "Inspecteur vaisseau") titleX = m.data->getDouble("x", -1.0);
    };
    observer->subscribe("render:text:add",    onText);
    observer->subscribe("render:text:update", onText);
    observer->subscribe("render:sprite:add",  [&](const Message&){ ++spriteAdds; });

    // HOST: react to a fleet-icon click by pushing that ship's blueprint + revealing the inspector window.
    observer->subscribe("vessel:open", [&](const Message& m){
        const std::string name = m.data->getString("name", "");
        json parts = json::array({
            { {"id","cockpit"},{"x",150},{"y",10}, {"w",80},{"h",80}, {"color","0xFFFFFFFF"},{"tex",1},{"label","Cockpit"},{"stat","PV 120"} },
            { {"id","hull"},   {"x",130},{"y",90}, {"w",120},{"h",60},{"color","0x46587aFF"},{"tex",0},{"label","Coque"},{"stat","PV 140"} },
            { {"id","gun"},    {"x",60}, {"y",96}, {"w",64},{"h",64}, {"color","0xFFFFFFFF"},{"tex",4},{"label","Canon"},{"stat","Degats 14"} },
            { {"id","reactor"},{"x",150},{"y",160},{"w",80},{"h",80}, {"color","0xFFFFFFFF"},{"tex",2},{"label","Reacteur"},{"stat","Energie +60"} },
            { {"id","engine"}, {"x",110},{"y",250},{"w",70},{"h",90}, {"color","0xFFFFFFFF"},{"tex",3},{"label","Moteur"},{"stat","Poussee +35"} }
        });
        // MERGE so the fleet data (groups/slots) survives the open (ui:data would replace the whole tree).
        hostPub->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", json{
            {"ship", {{"name", name}, {"parts", parts}}},
            {"noPart", true}, {"selectedPart", {{"label","-"},{"stat",""}}} }));
        { auto d=std::make_unique<JsonDataNode>("d"); d->setString("id","inspector"); d->setBool("visible", true);
          hostPub->publish("ui:set_visible", std::move(d)); }
    });

    auto pump = [&]{
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto move = [&](double x,double y){ auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",x); d->setDouble("y",y); hostPub->publish("input:mouse:move", std::move(d)); pump(); };
    auto btn  = [&](bool p){ auto d=std::make_unique<JsonDataNode>("d"); d->setInt("button",1); d->setBool("pressed",p); hostPub->publish("input:mouse:button", std::move(d)); pump(); };   // RIGHT button = open
    auto show = [&](const char* id,bool v){ auto d=std::make_unique<JsonDataNode>("d"); d->setString("id",id); d->setBool("visible",v); hostPub->publish("ui:set_visible", std::move(d)); pump(); };

    // Push the fleet (groups + slots, slot 0 named "Aurora") and reveal the fleet panel.
    {
        const int sizes[3] = {5, 4, 3};
        const char* names[3] = {"Alpha", "Bravo", "Reserve"};
        const char* ships[12] = {"Aurora","Borealis","Cygnus","Draco","Equinox","Falcon",
                                 "Gemini","Helios","Icarus","Juno","Kestrel","Lyra"};
        json groups = json::array(), slots = json::array();
        int idx = 0;
        for (int g = 0; g < 3; ++g) {
            groups.push_back({ {"name", names[g]}, {"ly", g * 64} });
            for (int k = 0; k < sizes[g]; ++k) {
                slots.push_back({ {"id","ship"+std::to_string(idx)}, {"name", ships[idx]}, {"ix", k*46}, {"iy", g*64+20}, {"icon", 1+(idx%4)} });
                ++idx;
            }
        }
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"groups", groups}, {"slots", slots} }));
    }
    show("fleetPanel", true);
    pump();

    // --- A. CLOSED: the inspector is hidden -> its title is not rendered. ---
    REQUIRE_FALSE(texts.count("Inspecteur vaisseau") > 0);

    // --- B. OPEN ON RIGHT-CLICK: right-click fleet icon 0 ("Aurora") at (42,100) -> vessel:open -> host shows
    //     the inspector (left-click would only select). ---
    spriteAdds = 0;
    move(42, 100); btn(true); btn(false);
    for (int i = 0; i < 4; ++i) pump();   // let the host relay (data + show) apply + render
    INFO("sprites after open = " << spriteAdds);
    REQUIRE(texts.count("Inspecteur vaisseau") > 0);   // the window opened
    REQUIRE(texts.count("Aurora") > 0);                // ...showing THAT ship (bound {{ship.name}})
    REQUIRE(spriteAdds >= 3);                           // the blueprint's sprite parts drew
    // The window is 62%-wide + anchor:center, so revealing it must LAY IT OUT — its title sits near the
    // centered window's left edge (~243), NOT at the top-left (0) as it would if shown without a relayout.
    INFO("title x = " << titleX);
    REQUIRE(titleX > 100.0);

    uiModule->shutdown();
}
