/**
 * Integration Test IT_047: off-screen fleet drawer — "le menu de vaisseau caché hors screen".
 *
 * The vessel screen's ship menu is an edge DRAWER that is hidden off-screen and slides in. It holds a
 * data-driven repeater of ship VIGNETTES (one clickable button per ship, caption bound to {{name}}). Verified
 * headlessly:
 *
 *   A. HIDDEN — at rest the drawer is fully closed / off-screen, so NO vignette renders (purged).
 *   B. SLIDES IN — ui:drawer:set{open:true} -> after the slide, the vignettes render (bound names appear).
 *   C. CLICKABLE — clicking a vignette fires vessel:open carrying that ship's id (from the repeater scope),
 *      which is what slice 4 will use to open the big inspector.
 *
 * Accumulate-and-check: a name absent before the open and present after proves it was hidden then revealed.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <set>
#include <string>

using namespace grove;

TEST_CASE("IT_047: fleet drawer is hidden off-screen, slides in, vignettes are clickable", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("fd_host");
    auto uiIO     = mgr.createInstance("fd_ui");
    auto observer = mgr.createInstance("fd_obs");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "fd_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 1280);
    cfg.setInt("windowHeight", 720);
    cfg.setString("layoutFile", "../../assets/ui/demo_vessel_screen.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::set<std::string> seen;
    std::string openedId;
    observer->subscribe("render:text:add",    [&](const Message& m){ seen.insert(m.data->getString("text","")); });
    observer->subscribe("render:text:update", [&](const Message& m){ seen.insert(m.data->getString("text","")); });
    observer->subscribe("vessel:open",        [&](const Message& m){ openedId = m.data->getString("id",""); });

    auto pump = [&]{
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto has = [&](const std::string& t){ return seen.count(t) > 0; };
    auto move = [&](double x,double y){ auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",x); d->setDouble("y",y); hostPub->publish("input:mouse:move", std::move(d)); pump(); };
    auto btn  = [&](bool p){ auto d=std::make_unique<JsonDataNode>("d"); d->setInt("button",0); d->setBool("pressed",p); hostPub->publish("input:mouse:button", std::move(d)); pump(); };
    auto drawer = [&](bool open){ auto d=std::make_unique<JsonDataNode>("d"); d->setString("id","fleetDrawer"); d->setBool("open",open); hostPub->publish("ui:drawer:set", std::move(d)); pump(); };

    // Push the fleet — each ship a vignette with an explicit slot position (host-computed, like blueprint parts).
    {
        const char* names[6] = {"S.S. Aurora","S.S. Borealis","S.S. Cygnus","S.S. Draco","S.S. Equinox","S.S. Falcon"};
        json fleet = json::array();
        for (int i = 0; i < 6; ++i)
            fleet.push_back({ {"id", "ship" + std::to_string(i)}, {"name", names[i]},
                              {"vx", 14}, {"vy", 8 + i*120}, {"vw", 272}, {"vh", 110} });
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"fleet", fleet} }));
    }
    pump(); pump();

    // --- A. HIDDEN: drawer closed -> the vignette is off-screen, not rendered. ---
    REQUIRE_FALSE(has("S.S. Aurora"));

    // --- B. SLIDES IN: open the drawer + let it slide (slideDuration 0.22 / 0.016 ~ 14 frames). ---
    drawer(true);
    for (int i = 0; i < 25; ++i) pump();
    REQUIRE(has("S.S. Aurora"));          // vignette name (bound {{name}}) now visible
    REQUIRE(has("S.S. Falcon"));          // ...the whole repeater rendered

    // --- C. CLICKABLE: vignette 0 abs rect = drawer(0,0) + panel(0,48) + slot(14,8) -> (14,56) 272x110.
    //     center ~ (150, 111). Click it -> vessel:open{id:"ship0"}. ---
    move(150, 111); btn(true); btn(false);
    INFO("opened id = " << openedId);
    REQUIRE(openedId == "ship0");

    uiModule->shutdown();
}
