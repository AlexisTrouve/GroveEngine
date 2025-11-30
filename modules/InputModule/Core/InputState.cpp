#include "InputState.h"

namespace grove {

void InputState::setMousePosition(int x, int y) {
    mouseX = x;
    mouseY = y;
}

void InputState::setMouseButton(int button, bool pressed) {
    if (button >= 0 && button < 3) {
        mouseButtons[button] = pressed;
    }
}

void InputState::setKey(int scancode, bool pressed) {
    if (pressed) {
        keysPressed.insert(scancode);
    } else {
        keysPressed.erase(scancode);
    }
}

void InputState::updateModifiers(bool shift, bool ctrl, bool alt) {
    modifiers.shift = shift;
    modifiers.ctrl = ctrl;
    modifiers.alt = alt;
}

bool InputState::isMouseButtonPressed(int button) const {
    if (button >= 0 && button < 3) {
        return mouseButtons[button];
    }
    return false;
}

bool InputState::isKeyPressed(int scancode) const {
    return keysPressed.find(scancode) != keysPressed.end();
}

} // namespace grove
