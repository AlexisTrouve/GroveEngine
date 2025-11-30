#include "SDLBackend.h"

namespace grove {

bool SDLBackend::convert(const SDL_Event& sdlEvent, InputEvent& outEvent) {
    switch (sdlEvent.type) {
        case SDL_MOUSEMOTION:
            outEvent.type = InputEvent::MouseMove;
            outEvent.mouseX = sdlEvent.motion.x;
            outEvent.mouseY = sdlEvent.motion.y;
            return true;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            outEvent.type = InputEvent::MouseButton;
            outEvent.button = sdlEvent.button.button - 1;  // SDL: 1-based, we want 0-based
            outEvent.pressed = (sdlEvent.type == SDL_MOUSEBUTTONDOWN);
            outEvent.mouseX = sdlEvent.button.x;
            outEvent.mouseY = sdlEvent.button.y;
            return true;

        case SDL_MOUSEWHEEL:
            outEvent.type = InputEvent::MouseWheel;
            outEvent.wheelDelta = static_cast<float>(sdlEvent.wheel.y);
            return true;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
            outEvent.type = InputEvent::KeyboardKey;
            outEvent.scancode = sdlEvent.key.keysym.scancode;
            outEvent.pressed = (sdlEvent.type == SDL_KEYDOWN);
            outEvent.repeat = (sdlEvent.key.repeat != 0);
            outEvent.shift = (sdlEvent.key.keysym.mod & KMOD_SHIFT) != 0;
            outEvent.ctrl = (sdlEvent.key.keysym.mod & KMOD_CTRL) != 0;
            outEvent.alt = (sdlEvent.key.keysym.mod & KMOD_ALT) != 0;
            return true;

        case SDL_TEXTINPUT:
            outEvent.type = InputEvent::KeyboardText;
            outEvent.text = sdlEvent.text.text;
            return true;

        default:
            return false;  // Event not supported
    }
}

} // namespace grove
