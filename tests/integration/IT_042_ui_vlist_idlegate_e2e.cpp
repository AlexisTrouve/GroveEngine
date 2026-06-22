/**
 * Integration Test IT_042: virtualized-list idle gate (perf follow-on). A template list must NOT re-window
 * + re-resolve its rows on frames where nothing changed (no scroll, no data, no move). Proven via the
 * `templateWindowOps` counter in getHealthStatus:
 *
 *   - idle frames        -> counter UNCHANGED (the windowDirty gate skips the work),
 *   - a scroll           -> counter INCREMENTS (input changed),
 *   - a data push        -> counter INCREMENTS (data version bumped).
 *
 * Behaviour preservation (rows still bind/scroll correctly) is locked by IT_041; this locks the gate.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <string>

using namespace grove;

TEST_CASE("IT_042: template list skips re-windowing on idle frames", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("ig_host");
    auto uiIO     = mgr.createInstance("ig_ui");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "ig_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_vlist.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
    };
    auto windowOps = [&] {
        auto h = uiModule->getHealthStatus();
        return h->getInt("templateWindowOps", -1);
    };
    auto move = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d)); pump();
    };
    auto wheel = [&](double delta) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("delta", delta);
        hostPub->publish("input:mouse:wheel", std::move(d)); pump();
    };

    pump();

    // Populate.
    {
        json arr = json::array();
        for (int i = 0; i < 100; ++i) arr.push_back({ {"id", "s" + std::to_string(i)}, {"name", "Ship" + std::to_string(i)} });
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"fleet", arr} }));
    }
    pump(); pump();

    // --- IDLE: several frames with no input -> the gate skips re-windowing. ---
    const int before = windowOps();
    for (int i = 0; i < 5; ++i) pump();
    INFO("windowOps idle: before=" << before << " after=" << windowOps());
    REQUIRE(windowOps() == before);          // no work on idle frames

    // --- SCROLL: a wheel changes the scroll -> exactly one more window. ---
    move(210, 150);                          // hover the list (no scroll yet)
    const int beforeScroll = windowOps();
    wheel(-100.0);
    INFO("windowOps scroll: before=" << beforeScroll << " after=" << windowOps());
    REQUIRE(windowOps() > beforeScroll);     // scroll re-windows
    const int afterScroll = windowOps();
    for (int i = 0; i < 3; ++i) pump();
    REQUIRE(windowOps() == afterScroll);     // then idle again

    // --- DATA PUSH: bumps the version -> re-window. ---
    const int beforeData = windowOps();
    hostPub->publish("ui:data:set", std::make_unique<JsonDataNode>("d", json{ {"path","fleet.0.name"},{"value","Renamed"} }));
    pump();
    INFO("windowOps data: before=" << beforeData << " after=" << windowOps());
    REQUIRE(windowOps() > beforeData);

    uiModule->shutdown();
}
