/**
 * Integration Test IT_017: UIModule checkbox + slider E2E — inject real input, assert events.
 *
 * Extends the E2E harness established by IT_016 (button/textinput) to the two
 * value-emitting widgets. Per doctrine ("a UI without an E2E test that really
 * clicks it does not exist"), each case drives input through the IIO bus and
 * asserts the ui:* events the widget is supposed to emit — headless, no window.
 *
 * Cases:
 *  1. Checkbox: press+release over it toggles checked, emits ui:value_changed{checked};
 *     a second press+release toggles back (proves the toggle direction, not a stuck bit).
 *  2. Slider click: pressing at 75% of the track sets value to ~75 and emits
 *     ui:value_changed{value} (proves the slider responds to a point click).
 *  3. Slider DRAG (audit H2): grab at 0%, move to 50%, move to 100%, release.
 *     A live slider must emit ui:value_changed for the intermediate position too,
 *     not only at grab and release — otherwise a volume/brightness slider gives no
 *     feedback while the user drags. This case locks the H2 fix.
 *
 * Fixtures use absolute layout so hit-test positions are known (sidesteps the
 * layout-vs-absX bug #6, same as IT_016).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <vector>

using namespace grove;

namespace {
// Resolve the UIModule shared library path for the current platform.
std::string uiModulePath() {
#ifdef _WIN32
    return "../modules/libUIModule.dll";
#else
    return "../modules/libUIModule.so";
#endif
}
} // namespace

TEST_CASE("IT_017: clicking a checkbox toggles it and emits ui:value_changed", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("cb_input");
    auto uiIO     = mgr.createInstance("cb_ui");
    auto observer = mgr.createInstance("cb_observer");

    ModuleLoader uiLoader;
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiModulePath(), "cb_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_checkbox.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int toggles = 0;
    bool lastChecked = false;
    observer->subscribe("ui:value_changed", [&](const Message& m) {
        toggles++;
        lastChecked = m.data->getBool("checked", false);
    });

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
    auto sendButton = [&](bool pressed) {
        // NB: the button handler uses the CURRENT mouse position (set by move),
        // not coordinates on the button message — so a prior move is required.
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0); d->setBool("pressed", pressed);
        inputPub->publish("input:mouse:button", std::move(d));
    };

    // Checkbox rect is [100..300]x[100..140] -> center (200,120).
    sendMove(200.0, 120.0); pump();

    // First click: unchecked -> checked.
    sendButton(true);  pump();
    sendButton(false); pump();
    INFO("after click 1: toggles=" << toggles << " checked=" << lastChecked);
    REQUIRE(toggles == 1);
    REQUIRE(lastChecked == true);

    // Second click: checked -> unchecked (toggle, not a stuck "true").
    sendButton(true);  pump();
    sendButton(false); pump();
    INFO("after click 2: toggles=" << toggles << " checked=" << lastChecked);
    REQUIRE(toggles == 2);
    REQUIRE(lastChecked == false);

    uiModule->shutdown();
}

TEST_CASE("IT_017: clicking a slider at 75% sets value and emits ui:value_changed", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("sld_input");
    auto uiIO     = mgr.createInstance("sld_ui");
    auto observer = mgr.createInstance("sld_observer");

    ModuleLoader uiLoader;
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiModulePath(), "sld_ui"));

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_slider.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::vector<double> values;
    observer->subscribe("ui:value_changed", [&](const Message& m) {
        values.push_back(m.data->getDouble("value", -1.0));
    });

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
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0); d->setBool("pressed", pressed);
        inputPub->publish("input:mouse:button", std::move(d));
    };

    // Track is [100..400] wide, range 0..100. x=325 -> t=0.75 -> value=75.
    sendMove(325.0, 115.0);  pump();
    sendButton(true);        pump();   // press at 75%
    sendButton(false);       pump();   // release

    REQUIRE(!values.empty());
    INFO("emitted values count=" << values.size() << " last=" << values.back());
    REQUIRE(values.back() >= 73.0);
    REQUIRE(values.back() <= 77.0);

    uiModule->shutdown();
}

TEST_CASE("IT_017: dragging a slider emits ui:value_changed live (audit H2)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("drag_input");
    auto uiIO     = mgr.createInstance("drag_ui");
    auto observer = mgr.createInstance("drag_observer");

    ModuleLoader uiLoader;
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiModulePath(), "drag_ui"));

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_slider.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::vector<double> values;
    observer->subscribe("ui:value_changed", [&](const Message& m) {
        values.push_back(m.data->getDouble("value", -1.0));
    });

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
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0); d->setBool("pressed", pressed);
        inputPub->publish("input:mouse:button", std::move(d));
    };

    // Track [100..400], range 0..100. Grab at 0% (x=100), drag through 50% (x=250),
    // then 100% (x=400), then release. Each move is a frame with NO button edge —
    // exactly the drag path that UISlider::update() handles outside the dispatch.
    sendMove(100.0, 115.0); sendButton(true); pump();   // grab @ 0%   -> value ~0
    sendMove(250.0, 115.0);                   pump();   // drag @ 50%  -> value ~50
    sendMove(400.0, 115.0);                   pump();   // drag @ 100% -> value ~100
    sendButton(false);                        pump();   // release

    REQUIRE(!values.empty());
    INFO("emitted values count=" << values.size());

    // The live-feedback contract: an intermediate drag position (~50) must have been
    // emitted. Before the H2 fix, only grab (~0) and release (~100) were published,
    // so no value in (40,60) ever appeared -> this REQUIRE is the red line.
    bool sawMidDrag = false;
    for (double v : values) {
        if (v > 40.0 && v < 60.0) { sawMidDrag = true; break; }
    }
    REQUIRE(sawMidDrag);

    // ...and the drag must have ended at the final position.
    REQUIRE(values.back() >= 95.0);

    uiModule->shutdown();
}
