#pragma once

#include <unordered_set>

namespace grove {

class InputState {
public:
    InputState() = default;
    ~InputState() = default;

    // Mouse state
    int mouseX = 0;
    int mouseY = 0;
    bool mouseButtons[3] = {false, false, false};  // L, M, R

    // Keyboard state
    std::unordered_set<int> keysPressed;  // Scancodes pressed

    // Modifiers
    struct Modifiers {
        bool shift = false;
        bool ctrl = false;
        bool alt = false;
    } modifiers;

    // Methods
    void setMousePosition(int x, int y);
    void setMouseButton(int button, bool pressed);
    void setKey(int scancode, bool pressed);
    void updateModifiers(bool shift, bool ctrl, bool alt);

    // Query
    bool isMouseButtonPressed(int button) const;
    bool isKeyPressed(int scancode) const;
};

} // namespace grove
