#pragma once

#include <string>
#include <SDL.h>

namespace grove {

class SDLBackend {
public:
    struct InputEvent {
        enum Type {
            MouseMove,
            MouseButton,
            MouseWheel,
            KeyboardKey,
            KeyboardText
        };

        Type type;

        // Mouse data
        int mouseX = 0;
        int mouseY = 0;
        int button = 0;  // 0=left, 1=middle, 2=right
        bool pressed = false;
        float wheelDelta = 0.0f;

        // Keyboard data
        int scancode = 0;
        bool repeat = false;
        std::string text;  // UTF-8

        // Modifiers
        bool shift = false;
        bool ctrl = false;
        bool alt = false;
    };

    // Convert SDL_Event → InputEvent
    static bool convert(const SDL_Event& sdlEvent, InputEvent& outEvent);
};

} // namespace grove
