/**
 * Integration Test IT_051: fleet selection semantics — LEFT-click selects, RIGHT-click opens; a control-group
 * LABEL selects the whole group. Verified headlessly:
 *
 *   A. LEFT-CLICK a ship icon -> vessel:select{id} (and does NOT open the inspector).
 *   B. RIGHT-CLICK the same icon -> vessel:open{id} (the inspector trigger).
 *   C. CLICK a group label -> vessel:selectGroup{group}.
 *
 * Locks the engine bits: the click dispatch now carries the real button index, fires "rightClick" vs "click",
 * and surfaces a button on a right-press too.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <string>

using namespace grove;

TEST_CASE("IT_051: left-click selects a ship, right-click opens it, a group label selects the group", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("sel_host");
    auto uiIO     = mgr.createInstance("sel_ui");
    auto observer = mgr.createInstance("sel_obs");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "sel_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 1280);
    cfg.setInt("windowHeight", 720);
    cfg.setString("layoutFile", "../../assets/ui/demo_vessel_screen.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string selectId, openId, selectGroup;
    observer->subscribe("vessel:select",      [&](const Message& m){ selectId = m.data->getString("id",""); });
    observer->subscribe("vessel:open",        [&](const Message& m){ openId = m.data->getString("id",""); });
    observer->subscribe("vessel:selectGroup", [&](const Message& m){ selectGroup = m.data->getString("group",""); });

    auto pump = [&]{
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto move = [&](double x,double y){ auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",x); d->setDouble("y",y); hostPub->publish("input:mouse:move", std::move(d)); pump(); };
    auto press = [&](int button,bool p){ auto d=std::make_unique<JsonDataNode>("d"); d->setInt("button",button); d->setBool("pressed",p); hostPub->publish("input:mouse:button", std::move(d)); pump(); };
    auto lclick = [&](double x,double y){ move(x,y); press(0,true); press(0,false); };
    auto rclick = [&](double x,double y){ move(x,y); press(1,true); press(1,false); };
    auto show = [&](const char* id,bool v){ auto d=std::make_unique<JsonDataNode>("d"); d->setString("id",id); d->setBool("visible",v); hostPub->publish("ui:set_visible", std::move(d)); pump(); };

    // Fleet: 3 groups; each slot carries its group + a (resting) border colour.
    {
        const int sizes[3] = {5, 4, 3};
        const char* gnames[3] = {"Alpha","Bravo","Reserve"};
        const char* ships[12] = {"Aurora","Borealis","Cygnus","Draco","Equinox","Falcon",
                                 "Gemini","Helios","Icarus","Juno","Kestrel","Lyra"};
        json groups = json::array(), slots = json::array();
        int idx = 0;
        for (int g = 0; g < 3; ++g) {
            groups.push_back({ {"name", gnames[g]}, {"ly", g*64} });
            for (int k = 0; k < sizes[g]; ++k) {
                slots.push_back({ {"id","ship"+std::to_string(idx)}, {"name", ships[idx]}, {"group", gnames[g]},
                                  {"border","0x2a3650FF"}, {"ix", k*46}, {"iy", g*64+20}, {"icon", 1+(idx%4)} });
                ++idx;
            }
        }
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"groups", groups}, {"slots", slots} }));
    }
    show("fleetPanel", true);
    pump();

    // --- A. LEFT-CLICK icon 0 (center ~42,100) -> select, NOT open. ---
    selectId.clear(); openId.clear();
    lclick(42, 100);
    INFO("select='" << selectId << "' open='" << openId << "'");
    REQUIRE(selectId == "ship0");
    REQUIRE(openId.empty());

    // --- B. RIGHT-CLICK the same icon -> open. ---
    openId.clear();
    rclick(42, 100);
    INFO("open='" << openId << "'");
    REQUIRE(openId == "ship0");

    // --- C. CLICK the "Alpha" group label (at ly 0 -> abs (22,60) 120x16, center ~82,68) -> select group. ---
    selectGroup.clear();
    lclick(82, 68);
    INFO("group='" << selectGroup << "'");
    REQUIRE(selectGroup == "Alpha");

    uiModule->shutdown();
}
