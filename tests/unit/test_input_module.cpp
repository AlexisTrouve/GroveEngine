/**
 * Unit/integration test: InputModule static-host path (engine help B1).
 *
 * Proves the path a static-link game (Drifterra) uses: instantiate InputModule directly,
 * feed it native SDL events via feedEvent() (the host polls SDL, the module never does),
 * call process(), and observe the input:* topics it publishes. Headless — SDL events are
 * plain structs (SDLBackend::convert is pure), so NO window / SDL_Init is needed.
 *
 * The published topics (input:mouse:move / :button, input:keyboard:key / :text) are
 * EXACTLY what UIModule subscribes to (see fix #5) — so this locks the producer end of
 * the input → UI chain that IT_016/IT_017 lock on the consumer end.
 */

#include <catch2/catch_test_macros.hpp>

#include "InputModule/InputModule.h"
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <SDL.h>
#include <cstring>
#include <string>

using namespace grove;

TEST_CASE("InputModule (static host): feedEvent -> input:* topics", "[input][static][b1]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputIO  = mgr.createInstance("b1_input");
    auto observer = mgr.createInstance("b1_observer");

    // Static-host style: instantiate the module directly, no DLL / createModule().
    InputModule input;
    JsonDataNode cfg("config");  // defaults: mouse + keyboard enabled
    input.setConfiguration(cfg, inputIO.get(), nullptr);

    int moves = 0, buttons = 0, keys = 0, texts = 0;
    int moveX = -1, moveY = -1;
    int btn = -1; bool btnPressed = false; int btnX = -1, btnY = -1;
    int scancode = -1; bool keyPressed = false;
    std::string text;

    observer->subscribe("input:mouse:move", [&](const Message& m) {
        moves++; moveX = m.data->getInt("x", -1); moveY = m.data->getInt("y", -1);
    });
    observer->subscribe("input:mouse:button", [&](const Message& m) {
        buttons++; btn = m.data->getInt("button", -1); btnPressed = m.data->getBool("pressed", false);
        btnX = m.data->getInt("x", -1); btnY = m.data->getInt("y", -1);
    });
    observer->subscribe("input:keyboard:key", [&](const Message& m) {
        keys++; scancode = m.data->getInt("scancode", -1); keyPressed = m.data->getBool("pressed", false);
    });
    observer->subscribe("input:keyboard:text", [&](const Message& m) {
        texts++; text = m.data->getString("text", "");
    });

    auto pump = [&] {
        JsonDataNode in("input");
        in.setDouble("deltaTime", 0.016);
        input.process(in);  // drains the fed-event buffer, converts, publishes
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };

    // Mouse move (100,200).
    { SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = 100; e.motion.y = 200; input.feedEvent(&e); }
    pump();
    REQUIRE(moves == 1);
    REQUIRE(moveX == 100);
    REQUIRE(moveY == 200);

    // Left mouse button down at (50,60) — SDL buttons are 1-based, published 0-based.
    { SDL_Event e{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = 50; e.button.y = 60; input.feedEvent(&e); }
    pump();
    REQUIRE(buttons == 1);
    REQUIRE(btn == 0);
    REQUIRE(btnPressed == true);
    REQUIRE(btnX == 50);
    REQUIRE(btnY == 60);

    // Keyboard key down (scancode A).
    { SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.scancode = SDL_SCANCODE_A; e.key.repeat = 0; e.key.keysym.mod = 0; input.feedEvent(&e); }
    pump();
    REQUIRE(keys == 1);
    REQUIRE(scancode == static_cast<int>(SDL_SCANCODE_A));
    REQUIRE(keyPressed == true);

    // Text input "A".
    { SDL_Event e{}; e.type = SDL_TEXTINPUT; std::strcpy(e.text.text, "A"); input.feedEvent(&e); }
    pump();
    REQUIRE(texts == 1);
    REQUIRE(text == "A");

    input.shutdown();
    mgr.removeInstance("b1_input");
    mgr.removeInstance("b1_observer");
}
