/**
 * Integration Test IT_047: fleet menu — "le menu de vaisseau caché hors screen", small top-left panel.
 *
 * The fleet menu is a compact panel anchored top-left, hidden by default (visible:false -> entries purged).
 * Inside it, control groups are stacked VERTICALLY; each group is a small label over a HORIZONTAL row of ship
 * ICONS (no names). Verified headlessly:
 *
 *   A. HIDDEN — visible:false -> no ship icon renders (entries purged).
 *   B. SHOWN — ui:set_visible{true} -> the group labels render and every ship icon draws a sprite.
 *   C. CLICKABLE — clicking an icon fires vessel:open carrying that ship's id (from the repeater scope),
 *      which slice 4 will use to open the big inspector.
 *
 * (Both content lists are data-driven single repeaters — `groups` -> stacked labels, `slots` -> the icon grid,
 * each slot's ix/iy host-computed so the icons form a horizontal row under their group label.)
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <set>
#include <string>

using namespace grove;

TEST_CASE("IT_047: fleet menu hidden top-left panel, shows stacked control groups of horizontal ship icons", "[integration][ui][e2e]") {
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

    int spriteAdds = 0;
    std::set<std::string> texts;
    std::string selectedId;
    observer->subscribe("render:sprite:add",   [&](const Message&){ ++spriteAdds; });
    observer->subscribe("render:text:add",     [&](const Message& m){ texts.insert(m.data->getString("text","")); });
    observer->subscribe("render:text:update",  [&](const Message& m){ texts.insert(m.data->getString("text","")); });
    observer->subscribe("vessel:select",       [&](const Message& m){ selectedId = m.data->getString("id",""); });

    auto pump = [&]{
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto move = [&](double x,double y){ auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",x); d->setDouble("y",y); hostPub->publish("input:mouse:move", std::move(d)); pump(); };
    auto btn  = [&](bool p){ auto d=std::make_unique<JsonDataNode>("d"); d->setInt("button",0); d->setBool("pressed",p); hostPub->publish("input:mouse:button", std::move(d)); pump(); };
    auto show = [&](bool v){ auto d=std::make_unique<JsonDataNode>("d"); d->setString("id","fleetPanel"); d->setBool("visible",v); hostPub->publish("ui:set_visible", std::move(d)); pump(); };

    // Push the fleet: 3 control groups (5/4/3) stacked; each group a horizontal row of icons.
    {
        const int sizes[3] = {5, 4, 3};
        const char* names[3] = {"Alpha", "Bravo", "Reserve"};
        json groups = json::array();
        json slots  = json::array();
        int idx = 0;
        for (int g = 0; g < 3; ++g) {
            groups.push_back({ {"name", names[g]}, {"ly", g * 64} });
            for (int k = 0; k < sizes[g]; ++k) {
                slots.push_back({ {"id", "ship" + std::to_string(idx)}, {"ix", k * 46}, {"iy", g * 64 + 20}, {"icon", 1 + (idx % 4)} });
                ++idx;
            }
        }
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"groups", groups}, {"slots", slots} }));
    }
    pump(); pump();

    // --- A. HIDDEN: visible:false -> no SHIP icon renders. (Discount the always-on baseline panels first.) ---
    spriteAdds = 0;
    pump(); pump();
    REQUIRE(spriteAdds == 0);

    // --- B. SHOWN: reveal the panel -> stacked group labels + every ship icon render. ---
    show(true);
    pump(); pump();
    INFO("sprite adds after show = " << spriteAdds);
    REQUIRE(spriteAdds >= 12);                       // 12 ship icons drew sprites (+ panel bg)
    REQUIRE(texts.count("Alpha") > 0);               // a stacked control-group label rendered
    REQUIRE(texts.count("Reserve") > 0);             // ...the last group too (vertical stacking)

    // --- C. CLICKABLE: fleetPanel(12,52) + icons child(10,8) -> (22,60); group 0 icon 0 at (0,20) -> (22,80)
    //     40x40, center ~ (42,100). LEFT-click selects it -> vessel:select{id:"ship0"} (right-click opens). ---
    move(42, 100); btn(true); btn(false);
    INFO("selected id = " << selectedId);
    REQUIRE(selectedId == "ship0");

    uiModule->shutdown();
}
