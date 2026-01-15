# InputModule - Phase 2: Gamepad Support

## Vue d'ensemble

Extension de l'InputModule pour supporter les manettes de jeu (gamepad/controller) via SDL2. Cette phase ajoute le support complet des boutons, axes analogiques, et gestion de la connexion/déconnexion de manettes.

## Prérequis

- ✅ Phase 1 complétée (souris + clavier)
- ✅ SDL2 installé et fonctionnel
- ✅ InputModule compilé et testé

## Objectifs

- 🎮 Support des boutons de gamepad (face buttons, shoulder buttons, etc.)
- 🕹️ Support des axes analogiques (joysticks, triggers)
- 🔌 Détection de connexion/déconnexion de manettes
- 🎯 Support multi-manettes (jusqu'à 4 joueurs)
- 🔄 Hot-reload avec préservation de l'état des manettes
- 📊 Deadzone configurable pour les axes analogiques

## Topics IIO publiés

### Gamepad Buttons
| Topic | Payload | Description |
|-------|---------|-------------|
| `input:gamepad:button` | `{id, button, pressed}` | Bouton de manette (id=manette 0-3, button=index) |

**Boutons SDL2 (SDL_GameControllerButton):**
- 0-3: A, B, X, Y (face buttons)
- 4-5: Back, Guide, Start
- 6-7: Left Stick Click, Right Stick Click
- 8-11: D-Pad Up, Down, Left, Right
- 12-13: Left Shoulder, Right Shoulder

### Gamepad Axes
| Topic | Payload | Description |
|-------|---------|-------------|
| `input:gamepad:axis` | `{id, axis, value}` | Axe analogique (value: -1.0 à 1.0) |

**Axes SDL2 (SDL_GameControllerAxis):**
- 0-1: Left Stick X, Left Stick Y
- 2-3: Right Stick X, Right Stick Y
- 4-5: Left Trigger, Right Trigger

### Gamepad Connection
| Topic | Payload | Description |
|-------|---------|-------------|
| `input:gamepad:connected` | `{id, name, connected}` | Connexion/déconnexion de manette |

**Payload example:**
```json
{
  "id": 0,
  "name": "Xbox 360 Controller",
  "connected": true
}
```

## Architecture

### Fichiers à créer

```
modules/InputModule/
├── Backends/
│   ├── SDLGamepadBackend.h       # NEW - Conversion SDL gamepad → Generic
│   └── SDLGamepadBackend.cpp     # NEW
└── Core/
    └── GamepadState.h/cpp        # NEW - État des manettes connectées
```

### Modifications aux fichiers existants

**InputModule.h** - Ajouter membres privés :
```cpp
std::unique_ptr<GamepadState> m_gamepadState;
std::array<SDL_GameController*, 4> m_controllers;  // Max 4 manettes
```

**InputModule.cpp** - Ajouter dans `process()` :
```cpp
case SDLBackend::InputEvent::GamepadButton:
    if (m_enableGamepad) {
        m_gamepadState->setButton(genericEvent.gamepadId,
                                   genericEvent.button,
                                   genericEvent.pressed);
        m_converter->publishGamepadButton(...);
    }
    break;

case SDLBackend::InputEvent::GamepadAxis:
    if (m_enableGamepad) {
        float value = applyDeadzone(genericEvent.axisValue, m_axisDeadzone);
        m_gamepadState->setAxis(genericEvent.gamepadId,
                                genericEvent.axis,
                                value);
        m_converter->publishGamepadAxis(...);
    }
    break;
```

## Implémentation détaillée

### 1. GamepadState.h/cpp

```cpp
// GamepadState.h
#pragma once

#include <array>
#include <string>

namespace grove {

class GamepadState {
public:
    static constexpr int MAX_GAMEPADS = 4;
    static constexpr int MAX_BUTTONS = 16;
    static constexpr int MAX_AXES = 6;

    struct Gamepad {
        bool connected = false;
        std::string name;
        std::array<bool, MAX_BUTTONS> buttons = {};
        std::array<float, MAX_AXES> axes = {};
    };

    GamepadState() = default;
    ~GamepadState() = default;

    // Connection
    void connect(int id, const std::string& name);
    void disconnect(int id);
    bool isConnected(int id) const;

    // Buttons
    void setButton(int id, int button, bool pressed);
    bool isButtonPressed(int id, int button) const;

    // Axes
    void setAxis(int id, int axis, float value);
    float getAxisValue(int id, int axis) const;

    // Query
    const Gamepad& getGamepad(int id) const;
    int getConnectedCount() const;

private:
    std::array<Gamepad, MAX_GAMEPADS> m_gamepads;
};

} // namespace grove
```

### 2. SDLGamepadBackend.h

```cpp
// SDLGamepadBackend.h
#pragma once

#include "SDLBackend.h"
#include <SDL.h>

namespace grove {

class SDLGamepadBackend {
public:
    // Extend InputEvent with gamepad fields
    static bool convertGamepad(const SDL_Event& sdlEvent,
                               SDLBackend::InputEvent& outEvent);

    // Helper: Apply deadzone to axis value
    static float applyDeadzone(float value, float deadzone);

    // Helper: Get gamepad name from SDL_GameController
    static const char* getGamepadName(SDL_GameController* controller);
};

} // namespace grove
```

### 3. SDLGamepadBackend.cpp

```cpp
// SDLGamepadBackend.cpp
#include "SDLGamepadBackend.h"
#include <cmath>

namespace grove {

bool SDLGamepadBackend::convertGamepad(const SDL_Event& sdlEvent,
                                       SDLBackend::InputEvent& outEvent) {
    switch (sdlEvent.type) {
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            outEvent.type = SDLBackend::InputEvent::GamepadButton;
            outEvent.gamepadId = sdlEvent.cbutton.which;
            outEvent.button = sdlEvent.cbutton.button;
            outEvent.pressed = (sdlEvent.type == SDL_CONTROLLERBUTTONDOWN);
            return true;

        case SDL_CONTROLLERAXISMOTION:
            outEvent.type = SDLBackend::InputEvent::GamepadAxis;
            outEvent.gamepadId = sdlEvent.caxis.which;
            outEvent.axis = sdlEvent.caxis.axis;
            // Convert SDL int16 (-32768 to 32767) to float (-1.0 to 1.0)
            outEvent.axisValue = sdlEvent.caxis.value / 32768.0f;
            return true;

        case SDL_CONTROLLERDEVICEADDED:
            outEvent.type = SDLBackend::InputEvent::GamepadConnected;
            outEvent.gamepadId = sdlEvent.cdevice.which;
            outEvent.gamepadConnected = true;
            return true;

        case SDL_CONTROLLERDEVICEREMOVED:
            outEvent.type = SDLBackend::InputEvent::GamepadConnected;
            outEvent.gamepadId = sdlEvent.cdevice.which;
            outEvent.gamepadConnected = false;
            return true;

        default:
            return false;
    }
}

float SDLGamepadBackend::applyDeadzone(float value, float deadzone) {
    if (std::abs(value) < deadzone) {
        return 0.0f;
    }

    // Rescale to maintain smooth transition
    float sign = (value > 0.0f) ? 1.0f : -1.0f;
    float absValue = std::abs(value);
    return sign * ((absValue - deadzone) / (1.0f - deadzone));
}

const char* SDLGamepadBackend::getGamepadName(SDL_GameController* controller) {
    return SDL_GameControllerName(controller);
}

} // namespace grove
```

### 4. Extend SDLBackend::InputEvent

**Dans SDLBackend.h**, ajouter au enum Type :
```cpp
enum Type {
    MouseMove,
    MouseButton,
    MouseWheel,
    KeyboardKey,
    KeyboardText,
    GamepadButton,      // NEW
    GamepadAxis,        // NEW
    GamepadConnected    // NEW
};
```

Ajouter les champs :
```cpp
// Gamepad data
int gamepadId = 0;         // 0-3
int axis = 0;              // 0-5
float axisValue = 0.0f;    // -1.0 to 1.0
bool gamepadConnected = false;
std::string gamepadName;
```

### 5. Extend InputConverter

**InputConverter.h** - Ajouter méthodes :
```cpp
void publishGamepadButton(int id, int button, bool pressed);
void publishGamepadAxis(int id, int axis, float value);
void publishGamepadConnected(int id, const std::string& name, bool connected);
```

**InputConverter.cpp** - Implémentation :
```cpp
void InputConverter::publishGamepadButton(int id, int button, bool pressed) {
    auto msg = std::make_unique<JsonDataNode>("gamepad_button");
    msg->setInt("id", id);
    msg->setInt("button", button);
    msg->setBool("pressed", pressed);
    m_io->publish("input:gamepad:button", std::move(msg));
}

void InputConverter::publishGamepadAxis(int id, int axis, float value) {
    auto msg = std::make_unique<JsonDataNode>("gamepad_axis");
    msg->setInt("id", id);
    msg->setInt("axis", axis);
    msg->setDouble("value", static_cast<double>(value));
    m_io->publish("input:gamepad:axis", std::move(msg));
}

void InputConverter::publishGamepadConnected(int id, const std::string& name,
                                             bool connected) {
    auto msg = std::make_unique<JsonDataNode>("gamepad_connected");
    msg->setInt("id", id);
    msg->setString("name", name);
    msg->setBool("connected", connected);
    m_io->publish("input:gamepad:connected", std::move(msg));
}
```

### 6. Configuration JSON

```json
{
  "backend": "sdl",
  "enableMouse": true,
  "enableKeyboard": true,
  "enableGamepad": true,
  "gamepad": {
    "deadzone": 0.15,
    "maxGamepads": 4,
    "autoConnect": true
  }
}
```

### 7. Hot-Reload Support

**getState()** - Ajouter sérialisation gamepad :
```cpp
// Gamepad state
auto gamepads = std::make_unique<JsonDataNode>("gamepads");
for (int i = 0; i < 4; i++) {
    if (m_gamepadState->isConnected(i)) {
        auto gp = std::make_unique<JsonDataNode>("gamepad");
        gp->setBool("connected", true);
        gp->setString("name", m_gamepadState->getGamepad(i).name);
        // Save button/axis state si nécessaire
        gamepads->setChild(std::to_string(i), std::move(gp));
    }
}
state->setChild("gamepads", std::move(gamepads));
```

**setState()** - Ajouter restauration gamepad :
```cpp
// Restore gamepad state
if (state.hasChild("gamepads")) {
    auto& gamepads = state.getChild("gamepads");
    // Restore connections et state
}
```

## Test Phase 2

### test_31_input_gamepad.cpp

```cpp
/**
 * Test: InputModule Gamepad Test
 *
 * Instructions:
 * - Connect a gamepad/controller
 * - Press buttons to see gamepad:button events
 * - Move joysticks to see gamepad:axis events
 * - Disconnect/reconnect to test gamepad:connected events
 * - Press ESC to exit
 */

#include <SDL.h>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include "modules/InputModule/InputModule.h"

#include <iostream>
#include <iomanip>

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "InputModule Gamepad Test\n";
    std::cout << "========================================\n\n";

    // Initialize SDL with gamepad support
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Gamepad Test - Press ESC to exit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600, SDL_WINDOW_SHOWN
    );

    // Setup IIO
    auto& ioManager = grove::IntraIOManager::getInstance();
    auto inputIO = ioManager.createInstance("input_module");
    auto testIO = ioManager.createInstance("test_controller");

    // Load InputModule
    grove::ModuleLoader inputLoader;
    auto inputModule = inputLoader.load("../modules/InputModule.dll", "input_module");

    grove::JsonDataNode config("config");
    config.setBool("enableGamepad", true);
    config.setDouble("gamepad.deadzone", 0.15);

    inputModule->setConfiguration(config, inputIO.get(), nullptr);

    // Subscribe to gamepad events
    testIO->subscribe("input:gamepad:button");
    testIO->subscribe("input:gamepad:axis");
    testIO->subscribe("input:gamepad:connected");

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN &&
                event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }

            inputModule->feedEvent(&event);
        }

        grove::JsonDataNode input("input");
        inputModule->process(input);

        // Display gamepad events
        while (testIO->hasMessages() > 0) {
            auto msg = testIO->pullMessage();

            if (msg.topic == "input:gamepad:button") {
                int id = msg.data->getInt("id", 0);
                int button = msg.data->getInt("button", 0);
                bool pressed = msg.data->getBool("pressed", false);

                const char* buttonNames[] = {
                    "A", "B", "X", "Y",
                    "BACK", "GUIDE", "START",
                    "L3", "R3",
                    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
                    "LB", "RB"
                };

                std::cout << "[GAMEPAD " << id << "] Button "
                          << buttonNames[button]
                          << " " << (pressed ? "PRESSED" : "RELEASED") << "\n";
            }
            else if (msg.topic == "input:gamepad:axis") {
                int id = msg.data->getInt("id", 0);
                int axis = msg.data->getInt("axis", 0);
                float value = msg.data->getDouble("value", 0.0);

                const char* axisNames[] = {
                    "LEFT_X", "LEFT_Y",
                    "RIGHT_X", "RIGHT_Y",
                    "LT", "RT"
                };

                // Only print if value is significant
                if (std::abs(value) > 0.01f) {
                    std::cout << "[GAMEPAD " << id << "] Axis "
                              << axisNames[axis]
                              << " = " << std::fixed << std::setprecision(2)
                              << value << "\n";
                }
            }
            else if (msg.topic == "input:gamepad:connected") {
                int id = msg.data->getInt("id", 0);
                std::string name = msg.data->getString("name", "Unknown");
                bool connected = msg.data->getBool("connected", false);

                std::cout << "[GAMEPAD " << id << "] "
                          << (connected ? "CONNECTED" : "DISCONNECTED")
                          << " - " << name << "\n";
            }
        }

        SDL_Delay(16);
    }

    inputModule->shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
```

## Configuration CMakeLists.txt

Ajouter dans `modules/InputModule/CMakeLists.txt` :
```cmake
# Phase 2 files (gamepad support)
if(GROVE_INPUT_MODULE_GAMEPAD)
    target_sources(InputModule PRIVATE
        Core/GamepadState.cpp
        Backends/SDLGamepadBackend.cpp
    )
    target_compile_definitions(InputModule PRIVATE GROVE_GAMEPAD_SUPPORT)
endif()
```

Ajouter dans `tests/CMakeLists.txt` :
```cmake
# Test 31: InputModule Gamepad Test
if(GROVE_BUILD_INPUT_MODULE AND GROVE_INPUT_MODULE_GAMEPAD)
    add_executable(test_31_input_gamepad
        visual/test_31_input_gamepad.cpp
    )

    target_link_libraries(test_31_input_gamepad PRIVATE
        GroveEngine::impl
        SDL2
        pthread
        dl
        X11
    )

    message(STATUS "Visual test 'test_31_input_gamepad' enabled (run manually)")
endif()
```

## Estimation

| Tâche | Complexité | Temps estimé |
|-------|------------|--------------|
| GamepadState.h/cpp | Facile | 30min |
| SDLGamepadBackend.h/cpp | Moyenne | 1h |
| Extend InputConverter | Facile | 30min |
| Modifications InputModule | Facile | 30min |
| test_31_input_gamepad.cpp | Moyenne | 1h |
| Debug & Polish | Moyenne | 30min |
| **Total Phase 2** | **4h** | **Support gamepad complet** |

## Ordre d'implémentation recommandé

1. ✅ Créer GamepadState.h/cpp
2. ✅ Créer SDLGamepadBackend.h/cpp
3. ✅ Extend SDLBackend::InputEvent enum + fields
4. ✅ Extend InputConverter (3 nouvelles méthodes)
5. ✅ Modifier InputModule.h (membres privés)
6. ✅ Modifier InputModule.cpp (process() + init/shutdown)
7. ✅ Tester avec test_31_input_gamepad.cpp
8. ✅ Valider hot-reload avec gamepad connecté

## Notes techniques

### Deadzone
La deadzone par défaut (0.15 = 15%) évite les micro-mouvements indésirables des joysticks au repos. Configurable via JSON.

### Multi-gamepad
SDL2 supporte jusqu'à 16 manettes, mais on limite à 4 pour des raisons pratiques (local multiplayer classique).

### Hot-reload
Pendant un hot-reload, les manettes connectées restent actives (SDL_GameController* restent valides). On peut restaurer l'état des boutons/axes si nécessaire.

### Performance
- Événements gamepad = ~20-50 events/frame max (2 joysticks + triggers + boutons)
- Overhead négligeable vs souris/clavier

---

**Status:** 📋 Planifié - Implémentation future

**Dépendances:** Phase 1 complétée, SDL2 avec support gamecontroller
