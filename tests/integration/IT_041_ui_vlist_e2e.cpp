/**
 * Integration Test IT_041: VIRTUALIZED template list — the list = a virtualized repeater (engine step 6).
 *
 * A `list` with "repeat":"{{fleet}}" + a widget-subtree "template" renders ONLY the visible rows as pooled
 * template instances, recycled on scroll. Proves the three load-bearing properties on a 100-ship fleet:
 *
 *   A. BOUNDED — only ~viewport-many rows are instantiated/rendered (Ship0.. a handful), NOT all 100.
 *   B. PER-ITEM event — clicking a row publishes fleet:pick with THAT row's item id.
 *   C. SCROLL RE-BIND — scrolling to the bottom recycles the pool: the far rows now render (Ship99) and the
 *      top ones don't (Ship0); the SAME top-of-list click now picks the item scrolled to the top.
 *
 * Fixture: list (100,100) 220x200, rowHeight 40 -> 5 rows fit (pool ~6). Full-row button -> a click anywhere
 *   in a row hits it. 100 ships -> maxScroll = 100*40-200 = 3800 -> firstVisible at bottom = 95.
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

TEST_CASE("IT_041: virtualized template list — bounded rows, per-item events, scroll re-bind", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("vl_host");
    auto uiIO     = mgr.createInstance("vl_ui");
    auto observer = mgr.createInstance("vl_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "vl_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_vlist.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::vector<std::string> texts;
    std::string pickId;
    auto cap = [&](const Message& m) { texts.push_back(m.data->getString("text", "")); };
    observer->subscribe("render:text:add", cap);
    observer->subscribe("render:text:update", cap);
    observer->subscribe("fleet:pick", [&](const Message& m) { pickId = m.data->getString("id", ""); });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto saw = [&](const std::string& t) { return std::find(texts.begin(), texts.end(), t) != texts.end(); };
    auto shipCount = [&] {
        int n = 0; for (auto& t : texts) if (t.rfind("Ship", 0) == 0) ++n; return n;
    };
    auto move = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d)); pump();
    };
    auto btn = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d)); pump();
    };
    auto wheel = [&](double delta) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("delta", delta);
        hostPub->publish("input:mouse:wheel", std::move(d)); pump();
    };
    auto click = [&](double x, double y) { move(x, y); btn(true); btn(false); };

    pump();  // settle

    // Push a 100-ship fleet.
    {
        json arr = json::array();
        for (int i = 0; i < 100; ++i) arr.push_back({ {"id", "s" + std::to_string(i)}, {"name", "Ship" + std::to_string(i)} });
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"fleet", arr} }));
    }
    texts.clear(); pump(); pump();

    // --- A. BOUNDED: only the visible handful render, not 100. ---
    INFO("ship rows rendered: " << shipCount());
    REQUIRE(saw("Ship0"));
    REQUIRE_FALSE(saw("Ship99"));
    REQUIRE(shipCount() <= 10);        // ~6 visible, never ~100

    // --- B. PER-ITEM event: top row -> ship 0. ---
    pickId.clear(); click(210, 120);   // top row
    INFO("top pick (unscrolled)='" << pickId << "'");
    REQUIRE(pickId == "s0");

    // --- C. SCROLL RE-BIND: slam to the bottom; the pool recycles to the far rows. ---
    move(210, 150);                    // hover the list so the wheel routes to it
    texts.clear();                     // clear BEFORE the scroll — the wheel pump re-renders the far rows
    wheel(-1000.0);                    // huge scroll down -> clamps to maxScroll (3800), firstVisible 95
    pump();                            // settle
    INFO("after scroll, ship rows: " << shipCount());
    REQUIRE(saw("Ship99"));
    REQUIRE_FALSE(saw("Ship0"));
    REQUIRE(shipCount() <= 10);        // still bounded after scrolling

    // The SAME top-of-list click now picks the item scrolled to the top (item 95).
    pickId.clear(); click(210, 120);
    INFO("top pick (scrolled)='" << pickId << "'");
    REQUIRE(pickId == "s95");

    uiModule->shutdown();
}
