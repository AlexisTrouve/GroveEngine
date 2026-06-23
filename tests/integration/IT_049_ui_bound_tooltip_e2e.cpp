/**
 * Integration Test IT_049: DATA-DRIVEN tooltip. A fleet ship icon shows NO name (icons only), but hovering it
 * pops a tooltip with the ship's name — and that tooltip text is BOUND ("tooltip":"{{name}}"), resolved per
 * repeater item. Verified headlessly:
 *
 *   - The icon has no caption, so the ship name "Aurora" renders nowhere... until you hover the icon for the
 *     tooltip delay (0.5s), at which point the tooltip draws "Aurora" — proving the bound tooltip resolved.
 *
 * Locks the engine bit: UIWidget::applyBoundProp now binds "tooltip" (so {{...}} in a tooltip is data-driven).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <set>
#include <string>

using namespace grove;

TEST_CASE("IT_049: a fleet icon's tooltip is data-bound and shows the ship name on hover", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("tt_host");
    auto uiIO     = mgr.createInstance("tt_ui");
    auto observer = mgr.createInstance("tt_obs");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "tt_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 1280);
    cfg.setInt("windowHeight", 720);
    cfg.setString("layoutFile", "../../assets/ui/demo_vessel_screen.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::set<std::string> texts;
    auto cap = [&](const Message& m){ texts.insert(m.data->getString("text","")); };
    observer->subscribe("render:text:add",    cap);
    observer->subscribe("render:text:update", cap);
    observer->subscribe("render:text",        cap);   // immediate-mode (the tooltip draws via drawText)

    auto pump = [&]{
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto move = [&](double x,double y){ auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",x); d->setDouble("y",y); hostPub->publish("input:mouse:move", std::move(d)); pump(); };
    auto show = [&](const char* id,bool v){ auto d=std::make_unique<JsonDataNode>("d"); d->setString("id",id); d->setBool("visible",v); hostPub->publish("ui:set_visible", std::move(d)); pump(); };

    // Fleet: slot 0 named "Aurora". The icon template binds tooltip:"{{name}}" but shows NO caption.
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
                slots.push_back({ {"id","ship"+std::to_string(idx)}, {"name", ships[idx]}, {"ix", k*46}, {"iy", g*64+20}, {"icon", 1+(idx%4)} });
                ++idx;
            }
        }
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"groups", groups}, {"slots", slots} }));
    }
    show("fleetPanel", true);
    pump();

    // The ship name is never a caption -> it must NOT render before the tooltip pops.
    REQUIRE_FALSE(texts.count("Aurora") > 0);

    // Hover icon 0 (group 0, col 0): fleetPanel(12,52)+icons(10,8) -> (22,60); slot (0,20) -> (22,80) 40x40,
    // center ~ (42,100). Hold the hover past the 0.5s delay (~40 frames @16ms) -> the tooltip pops "Aurora".
    move(42, 100);
    for (int i = 0; i < 40; ++i) pump();
    REQUIRE(texts.count("Aurora") > 0);   // the bound tooltip resolved {{name}} and drew it

    uiModule->shutdown();
}
