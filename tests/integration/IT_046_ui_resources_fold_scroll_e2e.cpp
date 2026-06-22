/**
 * Integration Test IT_046: foldable 50-resource panel inside a window — fold/unfold + scroll-on-overflow.
 *
 * The vessel screen needs "un menu folded dans la window qui affiche les stocks des 50 ressources que tu
 * peux fold unfold, et quand ça unfold c'est trop grand dans la window donc ça active le scroll". This is
 * COMPOSED from the existing grouped UIList: one collapsible group "Ressources" of 50 items, collapsed by
 * default. Verified headlessly:
 *
 *   A. FOLDED — the group header renders, but NONE of the 50 item rows do (collapsed hides them).
 *   B. UNFOLD — clicking the header fires ui:list:group:toggled{collapsed:false}; the first rows now render,
 *      but the LAST one (Res 50) does NOT — i.e. the unfolded content OVERFLOWS the list (too big).
 *   C. SCROLL — wheeling down brings Res 50 into view: the overflow scrolls. That IS "ça active le scroll".
 *
 * We accumulate every rendered label into a set and assert presence/absence at each checkpoint (robust to
 * the renderer only re-publishing changed text — once a row has rendered it's recorded for good, and the
 * absence checks run BEFORE the row could have scrolled into view).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <set>
#include <string>
#include <cstdio>

using namespace grove;

TEST_CASE("IT_046: resources panel folds/unfolds + scrolls on overflow", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("rfs_host");
    auto uiIO     = mgr.createInstance("rfs_ui");
    auto observer = mgr.createInstance("rfs_obs");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "rfs_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 1280);
    cfg.setInt("windowHeight", 720);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_resources.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    // Accumulate every rendered label (never cleared) + the latest group-toggle state.
    std::set<std::string> seen;
    bool toggledFired = false; bool toggledCollapsed = true;
    auto cap = [&](const Message& m){ seen.insert(m.data->getString("text","")); };
    observer->subscribe("render:text:add", cap);
    observer->subscribe("render:text:update", cap);
    observer->subscribe("ui:list:group:toggled", [&](const Message& m){
        toggledFired = true; toggledCollapsed = m.data->getBool("collapsed", true);
    });

    auto pump = [&]{
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto has = [&](const std::string& t){ return seen.count(t) > 0; };
    auto move = [&](double x,double y){ auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",x); d->setDouble("y",y); hostPub->publish("input:mouse:move", std::move(d)); pump(); };
    auto btn  = [&](bool p){ auto d=std::make_unique<JsonDataNode>("d"); d->setInt("button",0); d->setBool("pressed",p); hostPub->publish("input:mouse:button", std::move(d)); pump(); };
    auto wheel= [&](double dl){ auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("delta",dl); hostPub->publish("input:mouse:wheel", std::move(d)); pump(); };

    pump();

    // Push the 50 resources as ONE collapsible group, COLLAPSED by default (the "menu folded").
    {
        json items = json::array();
        for (int i = 1; i <= 50; ++i) {
            char id[16];  std::snprintf(id,  sizeof id,  "res%02d", i);
            char lbl[16]; std::snprintf(lbl, sizeof lbl, "Res %02d", i);
            items.push_back({ {"id", id}, {"label", lbl}, {"subtitle", "stock"} });
        }
        json groups = json::array({ { {"id","stock"}, {"label","Ressources"}, {"collapsed", true}, {"items", items} } });
        hostPub->publish("ui:list:set_groups", std::make_unique<JsonDataNode>("d", json{ {"id","resources"}, {"groups", groups} }));
    }
    pump(); pump();

    // --- A. FOLDED: header renders, items hidden. ---
    REQUIRE(( has("> Ressources") || has("v Ressources") ));   // group header rendered (marker-prefixed)
    REQUIRE_FALSE(has("Res 01"));                              // collapsed -> no item rows

    // --- B. UNFOLD: click the header. Fixture: window (300,100) + titlebar 28 -> content (300,128);
    //     list child (10,10) -> abs (310,138); header row 0 center ~ (470, 156). ---
    move(470, 156); btn(true); btn(false);
    INFO("toggled fired=" << toggledFired << " collapsed=" << toggledCollapsed);
    REQUIRE(toggledFired);
    REQUIRE_FALSE(toggledCollapsed);          // now expanded
    pump(); pump();
    REQUIRE(has("Res 01"));                   // first rows now visible
    REQUIRE_FALSE(has("Res 50"));             // ...but the last overflows the list (too big) -> not yet drawn

    // --- C. SCROLL: wheel down (negative delta) with the cursor over the list -> Res 50 scrolls into view. ---
    move(470, 300);                           // hover inside the list so the wheel routes to it
    wheel(-100); wheel(-100);                 // delta*20 px each; clamps to the bottom
    pump(); pump();
    REQUIRE(has("Res 50"));                   // overflow scrolled -> the last resource is now visible

    uiModule->shutdown();
}
