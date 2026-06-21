/**
 * Integration Test IT_020: action-wheel (radial menu) click E2E.
 *
 * Per the doctrine ("a UI without an E2E test that really clicks it does not exist"),
 * this drives a REAL pointer onto a wedge of the radial widget and asserts the right
 * ui:action fires — and that the center dead-zone and off-wheel clicks fire nothing.
 *
 * Fixture (assets/ui/test_e2e_radial.json): wheel centered at (400,300), band [40,160],
 * 4 wedges (top/right/bottom/left = act:move/act:attack/act:build/act:scan). Selection
 * is ANGULAR, so we click at a known DIRECTION from the center, not at a rect.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <string>
#include <vector>

using namespace grove;

TEST_CASE("IT_020: clicking a wedge emits ui:action for that DIRECTION", "[integration][ui][e2e][radial]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("rad_input");
    auto uiIO     = mgr.createInstance("rad_ui");
    auto observer = mgr.createInstance("rad_obs");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "rad_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_radial.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::vector<std::string> actions;
    std::vector<int> indices;
    observer->subscribe("ui:action", [&](const Message& m) {
        actions.push_back(m.data->getString("action", ""));
        indices.push_back(m.data->getInt("index", -1));
    });
    // The wheel AUTO-CLOSES on selection: it hides AND purges its retained render entries (the
    // ghost-rect fix). Rects publish as render:sprite (textureId 0), labels as render:text.
    int removes = 0;
    observer->subscribe("render:sprite:remove", [&](const Message&) { removes++; });
    observer->subscribe("render:text:remove",   [&](const Message&) { removes++; });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed, double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0); d->setBool("pressed", pressed);
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:button", std::move(d));
    };

    auto setVisible = [&](const std::string& id, bool vis) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setString("id", id); d->setBool("visible", vis);
        inputPub->publish("ui:set_visible", std::move(d));
    };

    // Wheel center (400,300), band [40,160]. First render registers the wheel's entries (bg + 4 rects
    // + 4 labels = 9 retained entries).
    // RIGHT wedge: offset (+100, 0) -> index 1 -> "act:attack", then the wheel AUTO-CLOSES.
    sendMove(500.0, 300.0);          pump();   // hover the right wedge (renders -> registers entries)
    sendButton(true,  500.0, 300.0); pump();
    removes = 0;                               // count only the auto-close purge
    sendButton(false, 500.0, 300.0); pump();   // confirm -> ui:action act:attack + auto-close

    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0] == "act:attack"); REQUIRE(indices[0] == 1);
    INFO("removes after auto-close=" << removes);
    REQUIRE(removes >= 4);                      // the 4 retained item labels are purged (the bg disc +
                                                // wedges are ephemeral sectors -> they vanish on their own)

    // The wheel is now HIDDEN: a click where it used to be does nothing (not routed to a hidden widget).
    sendButton(true,  400.0, 180.0); pump();
    sendButton(false, 400.0, 180.0); pump();
    REQUIRE(actions.size() == 1);              // still 1 -> the hidden wheel ignored the click

    // Re-show it -> render() re-registers fresh entries; selection works again (no stale ids).
    setVisible("wheel", true);       pump();
    // TOP wedge: offset (0, -120) -> index 0 -> "act:move". Proves selection follows the DIRECTION.
    sendMove(400.0, 180.0);          pump();
    sendButton(true,  400.0, 180.0); pump();
    sendButton(false, 400.0, 180.0); pump();   // confirm -> ui:action act:move + auto-close again

    INFO("actions.size=" << actions.size());
    REQUIRE(actions.size() == 2);
    REQUIRE(actions[1] == "act:move");   REQUIRE(indices[1] == 0);

    uiModule->shutdown();
}

TEST_CASE("IT_020: releasing in the center dead-zone CANCELS (no ui:action)", "[integration][ui][e2e][radial]") {
    // Discrimination: a click that lands on the wheel disc but inside the inner radius
    // must produce a ui:click (the disc WAS hit) but NO ui:action (nothing selected).
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("rad_input_dz");
    auto uiIO     = mgr.createInstance("rad_ui_dz");
    auto observer = mgr.createInstance("rad_obs_dz");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "rad_ui_dz"));

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_radial.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int clicks = 0, actions = 0;
    observer->subscribe("ui:click",  [&](const Message&) { clicks++; });
    observer->subscribe("ui:action", [&](const Message&) { actions++; });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed, double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0); d->setBool("pressed", pressed);
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:button", std::move(d));
    };

    // Center (400,300) is inside the inner radius (40) -> dead-zone.
    sendMove(400.0, 300.0);          pump();
    sendButton(true,  400.0, 300.0); pump();
    sendButton(false, 400.0, 300.0); pump();

    INFO("clicks=" << clicks << " actions=" << actions);
    REQUIRE(clicks  >= 1);   // the wheel disc WAS hit -> real cancel, not a miss
    REQUIRE(actions == 0);   // ...but nothing was selected

    uiModule->shutdown();
}

TEST_CASE("IT_020: clicking OUTSIDE the wheel emits nothing", "[integration][ui][e2e][radial]") {
    // A press+release beyond the outer radius isn't routed to the wheel at all.
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("rad_input_miss");
    auto uiIO     = mgr.createInstance("rad_ui_miss");
    auto observer = mgr.createInstance("rad_obs_miss");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "rad_ui_miss"));

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_radial.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int clicks = 0, actions = 0;
    observer->subscribe("ui:click",  [&](const Message&) { clicks++; });
    observer->subscribe("ui:action", [&](const Message&) { actions++; });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sendButton = [&](bool pressed, double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0); d->setBool("pressed", pressed);
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:button", std::move(d));
    };

    // (400,480): 180px below center > outer radius 160 -> off the wheel entirely.
    sendButton(true,  400.0, 480.0); pump();
    sendButton(false, 400.0, 480.0); pump();

    INFO("clicks=" << clicks << " actions=" << actions);
    REQUIRE(clicks  == 0);
    REQUIRE(actions == 0);

    uiModule->shutdown();
}
