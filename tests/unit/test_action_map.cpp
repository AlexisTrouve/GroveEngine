/**
 * Unit Tests: grove::input::ActionMap — semantic input bindings (engine help).
 *
 * WHAT  : Locks the action-mapping helper that lets a game bind PHYSICAL inputs (keyboard
 *         scancodes, mouse buttons) to NAMED actions ("zoom_in"), query their state
 *         (held / just-pressed / just-released), and remap at runtime — instead of
 *         hardcoding key constants in every consumer.
 *
 * WHY    : A showcase bound zoom-out to a CHARACTER keycode (SDLK_MINUS); on an AZERTY layout
 *          that key isn't where QWERTY puts it, so dezoom silently did nothing. Binding by
 *          SCANCODE (physical position) is layout-proof. This helper centralizes that pattern,
 *          pure and headless-testable like grove::camera. v1 scope: digital sources only.
 *
 * HOW    : No SDL / no IIO — scancodes and button indices are plain ints; the game feeds raw
 *          events (onKey/onMouseButton) and queries. beginFrame() clears the per-frame edges.
 */

#include <catch2/catch_test_macros.hpp>

#include "ActionMap.h"

using namespace grove::input;

// A couple of arbitrary scancodes (values mirror SDL_SCANCODE_* but the helper is SDL-agnostic).
static constexpr int SC_MINUS = 45;   // SDL_SCANCODE_MINUS
static constexpr int SC_KP_MINUS = 86;
static constexpr int SC_EQUALS = 46;

TEST_CASE("ActionMap - key press activates the action with a one-frame justPressed edge", "[action_map][unit]") {
    ActionMap map;
    map.bindKey("zoom_in", SC_EQUALS);

    map.beginFrame();
    map.onKey(SC_EQUALS, true);
    REQUIRE(map.isActive("zoom_in"));
    REQUIRE(map.justPressed("zoom_in"));
    REQUIRE_FALSE(map.justReleased("zoom_in"));

    // Next frame: still held, but the edge is gone.
    map.beginFrame();
    REQUIRE(map.isActive("zoom_in"));
    REQUIRE_FALSE(map.justPressed("zoom_in"));
}

TEST_CASE("ActionMap - release deactivates with a justReleased edge", "[action_map][unit]") {
    ActionMap map;
    map.bindKey("fire", SC_EQUALS);

    map.beginFrame();
    map.onKey(SC_EQUALS, true);

    map.beginFrame();
    map.onKey(SC_EQUALS, false);
    REQUIRE_FALSE(map.isActive("fire"));
    REQUIRE(map.justReleased("fire"));
    REQUIRE_FALSE(map.justPressed("fire"));
}

TEST_CASE("ActionMap - key repeat does not re-fire justPressed", "[action_map][unit]") {
    ActionMap map;
    map.bindKey("jump", SC_EQUALS);

    map.beginFrame();
    map.onKey(SC_EQUALS, true);
    REQUIRE(map.justPressed("jump"));

    // Same frame OS key-repeat (pressed=true again) must not produce a second edge.
    map.onKey(SC_EQUALS, true);
    REQUIRE(map.isActive("jump"));
    // still exactly one logical press — re-pressing an already-held binding is idempotent
}

TEST_CASE("ActionMap - multiple bindings on one action (layout-proof: main row + numpad)", "[action_map][unit]") {
    ActionMap map;
    // The AZERTY fix in practice: bind BOTH plausible physical keys to the same action.
    map.bindKey("zoom_out", SC_MINUS);
    map.bindKey("zoom_out", SC_KP_MINUS);

    map.beginFrame();
    map.onKey(SC_MINUS, true);            // press main-row minus
    REQUIRE(map.isActive("zoom_out"));
    REQUIRE(map.justPressed("zoom_out"));

    map.beginFrame();
    map.onKey(SC_KP_MINUS, true);         // also hold numpad minus
    REQUIRE(map.isActive("zoom_out"));
    REQUIRE_FALSE(map.justPressed("zoom_out"));  // already active -> no new edge

    map.beginFrame();
    map.onKey(SC_MINUS, false);           // release one; the other still holds it
    REQUIRE(map.isActive("zoom_out"));
    REQUIRE_FALSE(map.justReleased("zoom_out"));

    map.beginFrame();
    map.onKey(SC_KP_MINUS, false);        // release the last -> now released
    REQUIRE_FALSE(map.isActive("zoom_out"));
    REQUIRE(map.justReleased("zoom_out"));
}

TEST_CASE("ActionMap - one key bound to several actions triggers all", "[action_map][unit]") {
    ActionMap map;
    map.bindKey("zoom_out", SC_MINUS);
    map.bindKey("shrink_ui", SC_MINUS);

    map.beginFrame();
    map.onKey(SC_MINUS, true);
    REQUIRE(map.isActive("zoom_out"));
    REQUIRE(map.isActive("shrink_ui"));
}

TEST_CASE("ActionMap - mouse button bindings", "[action_map][unit]") {
    ActionMap map;
    map.bindMouseButton("select", 0);   // left button

    map.beginFrame();
    map.onMouseButton(0, true);
    REQUIRE(map.isActive("select"));
    REQUIRE(map.justPressed("select"));

    map.beginFrame();
    map.onMouseButton(0, false);
    REQUIRE_FALSE(map.isActive("select"));
    REQUIRE(map.justReleased("select"));
}

TEST_CASE("ActionMap - remap: clearAction drops old bindings", "[action_map][unit]") {
    ActionMap map;
    map.bindKey("zoom_out", SC_MINUS);

    map.clearAction("zoom_out");
    map.bindKey("zoom_out", SC_KP_MINUS);   // remapped

    map.beginFrame();
    map.onKey(SC_MINUS, true);              // old binding must no longer fire
    REQUIRE_FALSE(map.isActive("zoom_out"));

    map.beginFrame();
    map.onKey(SC_KP_MINUS, true);           // new binding fires
    REQUIRE(map.isActive("zoom_out"));
}

TEST_CASE("ActionMap - unknown action and unbound key are harmless", "[action_map][unit]") {
    ActionMap map;
    map.bindKey("zoom_in", SC_EQUALS);

    map.beginFrame();
    map.onKey(SC_MINUS, true);              // unbound key
    REQUIRE_FALSE(map.isActive("zoom_in"));
    REQUIRE_FALSE(map.isActive("never_bound"));   // unknown action queries are false, not a crash
    REQUIRE_FALSE(map.justPressed("never_bound"));
}
