#pragma once

/**
 * grove::input::ActionMap — semantic input bindings (header-only).
 *
 * WHAT  : Maps PHYSICAL inputs (keyboard scancodes, mouse buttons) to NAMED actions
 *         ("zoom_in", "fire", "pan_left") and tracks their state per frame: held
 *         (isActive), and the one-frame edges justPressed / justReleased. Supports
 *         multiple bindings per action and runtime remap.
 *
 * WHY   : Consumers were hardcoding key constants (e.g. SDLK_MINUS) directly in their event
 *         loops. Two problems: (1) not remappable, and (2) keycodes are CHARACTER-based, so a
 *         binding made on QWERTY breaks on AZERTY — that is the exact bug behind "dezoom does
 *         nothing". Binding by SCANCODE (physical key position) is layout-proof. Centralizing
 *         the map here, in the engine, gives every game the same robust, remappable layer —
 *         the input-side sibling of grove::camera.
 *
 * HOW   : Pure C++/std — no SDL, no IIO, no bgfx. Scancodes and button indices are plain ints
 *         (use SDL_SCANCODE_* values, but nothing here depends on SDL). The host feeds raw
 *         events each frame (onKey/onMouseButton) between beginFrame() and the queries.
 *         State is tracked as the SET of currently-held bindings per action, so an action
 *         held via several keys only releases when the last one lifts, and OS key-repeat is
 *         idempotent (re-inserting an already-held binding produces no spurious edge).
 *
 * SCOPE : v1 is DIGITAL only (key / mouse button, pressed/released). Deferred: analog axes &
 *         deadzones, gamepad, chords (Ctrl+S), input contexts/layers, and JSON (de)serialize —
 *         a game can build a bind table from any config and call bind*() itself.
 *
 * Locked by tests/unit/test_action_map.cpp.
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace grove {
namespace input {

// Digital input sources understood in v1.
enum class Source : uint8_t {
    Key,          // keyboard scancode (physical position) — layout-proof
    MouseButton   // mouse button index (0=left, 1=middle, 2=right, ...)
};

class ActionMap {
public:
    // ------------------------------------------------------------------
    // Configuration / remap
    // ------------------------------------------------------------------

    // Bind a physical input to an action. An action may have several bindings; a binding may
    // map to several actions. Re-adding the same binding is a no-op (deduplicated).
    void bind(const std::string& action, Source source, int code) {
        const uint64_t key = encode(source, code);
        auto& actions = m_bindingToActions[key];
        for (const auto& a : actions) {
            if (a == action) return;  // already bound
        }
        actions.push_back(action);
        m_actions.try_emplace(action);  // ensure the action exists (inactive)
    }

    void bindKey(const std::string& action, int scancode) { bind(action, Source::Key, scancode); }
    void bindMouseButton(const std::string& action, int button) { bind(action, Source::MouseButton, button); }

    // Drop ALL bindings of an action (for remap: clearAction then bind the new keys). Also
    // clears any held state so a stale "held" can't survive a remap.
    void clearAction(const std::string& action) {
        for (auto it = m_bindingToActions.begin(); it != m_bindingToActions.end(); ) {
            auto& actions = it->second;
            for (size_t i = 0; i < actions.size(); ) {
                if (actions[i] == action) actions.erase(actions.begin() + i);
                else ++i;
            }
            if (actions.empty()) it = m_bindingToActions.erase(it);
            else ++it;
        }
        auto sIt = m_actions.find(action);
        if (sIt != m_actions.end()) sIt->second = ActionState{};
    }

    // Remove every binding and action state.
    void clear() {
        m_bindingToActions.clear();
        m_actions.clear();
    }

    // ------------------------------------------------------------------
    // Per-frame feed
    // ------------------------------------------------------------------

    // Call once at the start of each frame: clears the justPressed/justReleased edges while
    // preserving held state. (Held state only changes via onKey/onMouseButton.)
    void beginFrame() {
        for (auto& [name, state] : m_actions) {
            state.justPressed = false;
            state.justReleased = false;
        }
    }

    // Feed a raw keyboard event (scancode = physical key). Idempotent under OS key-repeat.
    void onKey(int scancode, bool pressed) { onInput(Source::Key, scancode, pressed); }

    // Feed a raw mouse-button event.
    void onMouseButton(int button, bool pressed) { onInput(Source::MouseButton, button, pressed); }

    // ------------------------------------------------------------------
    // Query
    // ------------------------------------------------------------------

    // Currently held (at least one of its bindings is down).
    bool isActive(const std::string& action) const {
        auto it = m_actions.find(action);
        return it != m_actions.end() && !it->second.held.empty();
    }

    // Became active THIS frame (rising edge). Unknown action -> false.
    bool justPressed(const std::string& action) const {
        auto it = m_actions.find(action);
        return it != m_actions.end() && it->second.justPressed;
    }

    // Became inactive THIS frame (falling edge). Unknown action -> false.
    bool justReleased(const std::string& action) const {
        auto it = m_actions.find(action);
        return it != m_actions.end() && it->second.justReleased;
    }

private:
    struct ActionState {
        // Set of (source,code) bindings currently held down. Active iff non-empty; using a set
        // lets several keys hold one action and makes repeats idempotent.
        std::unordered_set<uint64_t> held;
        bool justPressed = false;
        bool justReleased = false;
    };

    // Pack a source + code into one key for the binding lookup table.
    static uint64_t encode(Source source, int code) {
        return (static_cast<uint64_t>(source) << 32) | static_cast<uint32_t>(code);
    }

    void onInput(Source source, int code, bool pressed) {
        const uint64_t key = encode(source, code);
        auto bIt = m_bindingToActions.find(key);
        if (bIt == m_bindingToActions.end()) return;  // unbound input — ignore

        for (const auto& action : bIt->second) {
            ActionState& state = m_actions[action];
            const bool wasActive = !state.held.empty();
            if (pressed) {
                state.held.insert(key);
                if (!wasActive) state.justPressed = true;  // empty -> held
            } else {
                state.held.erase(key);
                if (wasActive && state.held.empty()) state.justReleased = true;  // held -> empty
            }
        }
    }

    std::unordered_map<uint64_t, std::vector<std::string>> m_bindingToActions;  // (source,code) -> actions
    std::unordered_map<std::string, ActionState> m_actions;                     // action -> state
};

} // namespace input
} // namespace grove
