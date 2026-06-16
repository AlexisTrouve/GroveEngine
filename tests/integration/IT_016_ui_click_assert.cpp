/**
 * Integration Test IT_016: UIModule click E2E — inject a real click, assert the response.
 *
 * This is the FIRST test that actually interacts with the UI and asserts the result
 * (per the doctrine: "a UI without an E2E test that really clicks it does not exist").
 * IT_014/IT_015 only assert "loads + healthy"; this one drives input and asserts events.
 *
 * Flow: load UIModule with an ABSOLUTE-layout fixture (a single button at a known
 * position — sidesteps the layout-vs-absX bug), then publish input as a separate
 * instance (sender-skip rule), pump the module a frame per input, and assert that
 * a press+release over the button emits ui:click and ui:action(action="test:ok").
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_016: clicking a button emits ui:click + ui:action", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("input_publisher");
    auto uiIO     = mgr.createInstance("ui_module");
    auto observer = mgr.createInstance("test_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "ui_module"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_button.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int clicks = 0;
    int actions = 0;
    std::string actionName;
    observer->subscribe("ui:click",  [&](const Message&) { clicks++; });
    observer->subscribe("ui:action", [&](const Message& m) {
        actions++;
        actionName = m.data->getString("action", "");
    });

    // Process one frame, then drain whatever the UI published to the observer.
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

    // Button is at (100,100) size 200x60 -> center (200,130).
    const double cx = 200.0, cy = 130.0;

    sendMove(cx, cy);            pump();  // hover the button
    sendButton(true,  cx, cy);  pump();  // press
    sendButton(false, cx, cy);  pump();  // release -> click + action

    INFO("clicks=" << clicks << " actions=" << actions << " action='" << actionName << "'");
    REQUIRE(clicks  >= 1);              // a real click landed on the button
    REQUIRE(actions == 1);             // the button's onClick fired exactly once
    REQUIRE(actionName == "test:ok");  // ...with the correct action payload

    uiModule->shutdown();
}

TEST_CASE("IT_016: clicking OUTSIDE the button emits nothing (discrimination)", "[integration][ui][e2e]") {
    // Proves the positive test isn't a false positive: a press+release away from any
    // widget must produce NO ui:click / ui:action.
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("input_publisher_miss");
    auto uiIO     = mgr.createInstance("ui_module_miss");
    auto observer = mgr.createInstance("test_observer_miss");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "ui_module_miss"));

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_button.json");
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

    // (700,500) is well outside the button rect [100..300]x[100..160].
    sendButton(true,  700.0, 500.0); pump();
    sendButton(false, 700.0, 500.0); pump();

    INFO("clicks=" << clicks << " actions=" << actions);
    REQUIRE(clicks  == 0);
    REQUIRE(actions == 0);

    uiModule->shutdown();
}

TEST_CASE("IT_016: typing into a focused textinput emits ui:text_changed", "[integration][ui][e2e]") {
    // Drives the REAL keyboard path: focus a textinput by clicking it, then type via the
    // topic the InputModule actually publishes (input:keyboard:text), and assert the
    // textinput received the character. This is the E2E that locks fix #5 (UIModule used
    // to subscribe only the legacy "input:keyboard", so real keyboard input was dead).
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("input_publisher_kb");
    auto uiIO     = mgr.createInstance("ui_module_kb");
    auto observer = mgr.createInstance("test_observer_kb");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "ui_module_kb"));

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_textinput.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int textChanges = 0;
    std::string lastText;
    observer->subscribe("ui:text_changed", [&](const Message& m) {
        textChanges++;
        lastText = m.data->getString("text", "");
    });

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
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:move", std::move(d));
    };

    // Textinput is at (100,100) size 300x40 -> center (250,120). Move there first
    // (the button handler uses the current mouse position), then click to focus it.
    sendMove(250.0, 120.0);          pump();
    sendButton(true,  250.0, 120.0); pump();
    sendButton(false, 250.0, 120.0); pump();

    // Type 'A' via the topic the real InputModule publishes.
    {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setString("text", "A");
        inputPub->publish("input:keyboard:text", std::move(d));
    }
    pump();

    INFO("textChanges=" << textChanges << " lastText='" << lastText << "'");
    REQUIRE(textChanges >= 1);     // the keystroke reached the focused textinput
    REQUIRE(lastText == "A");      // ...and produced the right text

    uiModule->shutdown();
}

TEST_CASE("IT_016: special keys (backspace, enter) reach a focused textinput", "[integration][ui][e2e]") {
    // Locks fix #5-suite: InputModule publishes input:keyboard:key with the raw SDL
    // scancode, but UITextInput::onKeyInput speaks JS-style codes (Backspace=8,
    // Enter=13…). Without the scancode->editkey mapping in UIModule, backspace/enter
    // were inert (only printable text via input:keyboard:text worked). Drive a real
    // edit: type "AB", backspace -> "A", then Enter -> submit (onSubmit="test:submit").
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("input_publisher_sk");
    auto uiIO     = mgr.createInstance("ui_module_sk");
    auto observer = mgr.createInstance("test_observer_sk");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "ui_module_sk"));

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_textinput.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastText;
    int submits = 0;
    std::string submitAction;
    observer->subscribe("ui:text_changed", [&](const Message& m) {
        lastText = m.data->getString("text", "");
    });
    observer->subscribe("ui:text_submit", [&](const Message&) { submits++; });
    observer->subscribe("ui:action", [&](const Message& m) {
        submitAction = m.data->getString("action", "");
    });

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
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:move", std::move(d));
    };
    auto sendText = [&](const std::string& t) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setString("text", t);
        inputPub->publish("input:keyboard:text", std::move(d));
    };
    auto sendKey = [&](int scancode) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("scancode", scancode);
        d->setBool("pressed", true);
        inputPub->publish("input:keyboard:key", std::move(d));
    };

    // Focus the textinput (center 250,120; move first so the click lands on it).
    sendMove(250.0, 120.0);          pump();
    sendButton(true,  250.0, 120.0); pump();
    sendButton(false, 250.0, 120.0); pump();

    // Type "AB".
    sendText("A"); pump();
    sendText("B"); pump();
    REQUIRE(lastText == "AB");

    // Backspace (SDL_SCANCODE_BACKSPACE = 42) -> "A".
    sendKey(42); pump();
    INFO("after backspace lastText='" << lastText << "'");
    REQUIRE(lastText == "A");

    // Enter (SDL_SCANCODE_RETURN = 40) -> submit with onSubmit action.
    sendKey(40); pump();
    INFO("submits=" << submits << " action='" << submitAction << "'");
    REQUIRE(submits >= 1);
    REQUIRE(submitAction == "test:submit");

    uiModule->shutdown();
}

TEST_CASE("IT_016: typing a multi-character string inserts all of it (C2)", "[integration][ui][e2e]") {
    // Locks fix #5/C2: a single input:keyboard:text can carry MORE than one character
    // (IME commit, paste, a multi-byte UTF-8 codepoint). The old code forwarded only the
    // first byte (keyChar is one char), so "Hello" became "H". Now the whole string is
    // inserted via the focused textinput.
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("input_publisher_mc");
    auto uiIO     = mgr.createInstance("ui_module_mc");
    auto observer = mgr.createInstance("test_observer_mc");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "ui_module_mc"));

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_textinput.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastText;
    observer->subscribe("ui:text_changed", [&](const Message& m) {
        lastText = m.data->getString("text", "");
    });

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
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:move", std::move(d));
    };
    auto sendText = [&](const std::string& t) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setString("text", t);
        inputPub->publish("input:keyboard:text", std::move(d));
    };

    // Focus the textinput, then commit a whole word in one text event.
    sendMove(250.0, 120.0);          pump();
    sendButton(true,  250.0, 120.0); pump();
    sendButton(false, 250.0, 120.0); pump();

    sendText("Hello"); pump();
    INFO("lastText='" << lastText << "'");
    REQUIRE(lastText == "Hello");   // pre-fix: "H" (only the first byte)

    uiModule->shutdown();
}
