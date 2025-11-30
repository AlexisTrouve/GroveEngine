# InputModule - Plan d'implémentation

## Vue d'ensemble

Module de capture et conversion d'événements d'entrée (clavier, souris, gamepad) vers le système IIO de GroveEngine. Permet un découplage complet entre la source d'input (SDL, GLFW, Windows, etc.) et les modules consommateurs (UI, Game Logic, etc.).

## Objectifs

- ✅ **Découplage** - Séparer la capture d'events de leur consommation
- ✅ **Réutilisabilité** - Utilisable pour tests ET production
- ✅ **Hot-reload** - Supporte le rechargement dynamique avec préservation de l'état
- ✅ **Multi-backend** - Support SDL d'abord, extensible à GLFW/Win32/etc.
- ✅ **Thread-safe** - Injection d'events depuis la main loop, traitement dans process()
- ✅ **Production-ready** - Performance, logging, monitoring

## Architecture

```
modules/InputModule/
├── InputModule.cpp/h           # Module principal IModule
├── Core/
│   ├── InputState.cpp/h        # État des inputs (touches pressées, position souris)
│   └── InputConverter.cpp/h    # Conversion events natifs → IIO messages
└── Backends/
    └── SDLBackend.cpp/h        # Backend SDL (SDL_Event → InputEvent)
```

## Topics IIO publiés

### Input Mouse
| Topic | Payload | Description |
|-------|---------|-------------|
| `input:mouse:move` | `{x, y}` | Position souris (coordonnées écran) |
| `input:mouse:button` | `{button, pressed, x, y}` | Click souris (button: 0=left, 1=middle, 2=right) |
| `input:mouse:wheel` | `{delta}` | Molette souris (delta: + = haut, - = bas) |

### Input Keyboard
| Topic | Payload | Description |
|-------|---------|-------------|
| `input:keyboard:key` | `{key, pressed, repeat, modifiers}` | Touche clavier (scancode) |
| `input:keyboard:text` | `{text}` | Saisie texte UTF-8 (pour TextInput) |

### Input Gamepad (Phase 2)
| Topic | Payload | Description |
|-------|---------|-------------|
| `input:gamepad:button` | `{id, button, pressed}` | Bouton gamepad |
| `input:gamepad:axis` | `{id, axis, value}` | Axe analogique (-1.0 à 1.0) |
| `input:gamepad:connected` | `{id, name}` | Gamepad connecté/déconnecté |

## Phases d'implémentation

### Phase 1: Core InputModule + SDL Backend ⭐

**Objectif:** Module fonctionnel avec support souris + clavier via SDL

#### 1.1 Structure de base

**Fichiers à créer:**
```cpp
// InputModule.h/cpp - IModule principal
class InputModule : public IModule {
public:
    InputModule();
    ~InputModule() override;

    // IModule interface
    void setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) override;
    void process(const IDataNode& input) override;
    void shutdown() override;

    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;

    std::string getType() const override { return "input_module"; }
    bool isIdle() const override { return true; }

    // API spécifique InputModule
    void feedEvent(const void* nativeEvent);  // Injection depuis main loop

private:
    IIO* m_io = nullptr;
    std::unique_ptr<InputState> m_state;
    std::unique_ptr<InputConverter> m_converter;
    std::unique_ptr<SDLBackend> m_backend;

    // Event buffer (thread-safe)
    std::vector<SDL_Event> m_eventBuffer;
    std::mutex m_bufferMutex;

    // Config
    std::string m_backend = "sdl";  // "sdl", "glfw", "win32", etc.
    bool m_enableMouse = true;
    bool m_enableKeyboard = true;
    bool m_enableGamepad = false;

    // Stats
    uint64_t m_frameCount = 0;
    uint64_t m_eventsProcessed = 0;
};
```

**Topics IIO:**
- Publish: `input:mouse:move`, `input:mouse:button`, `input:keyboard:key`, `input:keyboard:text`
- Subscribe: (aucun pour Phase 1)

#### 1.2 InputState - État des inputs

```cpp
// InputState.h/cpp - État courant des inputs
class InputState {
public:
    // Mouse state
    int mouseX = 0;
    int mouseY = 0;
    bool mouseButtons[3] = {false, false, false};  // L, M, R

    // Keyboard state
    std::unordered_set<int> keysPressed;  // Scancodes pressés

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
```

#### 1.3 SDLBackend - Conversion SDL → Generic

```cpp
// SDLBackend.h/cpp - Convertit SDL_Event en événements génériques
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
        int mouseX, mouseY;
        int button;  // 0=left, 1=middle, 2=right
        bool pressed;
        float wheelDelta;

        // Keyboard data
        int scancode;
        bool repeat;
        std::string text;  // UTF-8

        // Modifiers
        bool shift, ctrl, alt;
    };

    // Convertit SDL_Event → InputEvent
    static bool convert(const SDL_Event& sdlEvent, InputEvent& outEvent);
};
```

**Conversion SDL → Generic:**
```cpp
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
            outEvent.button = sdlEvent.button.button - 1;  // SDL: 1-based
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
            return false;  // Event non supporté
    }
}
```

#### 1.4 InputConverter - Generic → IIO

```cpp
// InputConverter.h/cpp - Convertit InputEvent → IIO messages
class InputConverter {
public:
    InputConverter(IIO* io);

    void publishMouseMove(int x, int y);
    void publishMouseButton(int button, bool pressed, int x, int y);
    void publishMouseWheel(float delta);
    void publishKeyboardKey(int scancode, bool pressed, bool repeat, bool shift, bool ctrl, bool alt);
    void publishKeyboardText(const std::string& text);

private:
    IIO* m_io;
};
```

**Implémentation:**
```cpp
void InputConverter::publishMouseMove(int x, int y) {
    auto msg = std::make_unique<JsonDataNode>("mouse_move");
    msg->setInt("x", x);
    msg->setInt("y", y);
    m_io->publish("input:mouse:move", std::move(msg));
}

void InputConverter::publishMouseButton(int button, bool pressed, int x, int y) {
    auto msg = std::make_unique<JsonDataNode>("mouse_button");
    msg->setInt("button", button);
    msg->setBool("pressed", pressed);
    msg->setInt("x", x);
    msg->setInt("y", y);
    m_io->publish("input:mouse:button", std::move(msg));
}

void InputConverter::publishKeyboardKey(int scancode, bool pressed, bool repeat,
                                        bool shift, bool ctrl, bool alt) {
    auto msg = std::make_unique<JsonDataNode>("keyboard_key");
    msg->setInt("scancode", scancode);
    msg->setBool("pressed", pressed);
    msg->setBool("repeat", repeat);
    msg->setBool("shift", shift);
    msg->setBool("ctrl", ctrl);
    msg->setBool("alt", alt);
    m_io->publish("input:keyboard:key", std::move(msg));
}

void InputConverter::publishKeyboardText(const std::string& text) {
    auto msg = std::make_unique<JsonDataNode>("keyboard_text");
    msg->setString("text", text);
    m_io->publish("input:keyboard:text", std::move(msg));
}
```

#### 1.5 InputModule::process() - Pipeline complet

```cpp
void InputModule::process(const IDataNode& input) {
    m_frameCount++;

    // 1. Lock et récupère les events du buffer
    std::vector<SDL_Event> events;
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        events = std::move(m_eventBuffer);
        m_eventBuffer.clear();
    }

    // 2. Convertit SDL → Generic → IIO
    for (const auto& sdlEvent : events) {
        SDLBackend::InputEvent genericEvent;

        if (!SDLBackend::convert(sdlEvent, genericEvent)) {
            continue;  // Event non supporté, skip
        }

        // 3. Update state
        switch (genericEvent.type) {
            case SDLBackend::InputEvent::MouseMove:
                m_state->setMousePosition(genericEvent.mouseX, genericEvent.mouseY);
                m_converter->publishMouseMove(genericEvent.mouseX, genericEvent.mouseY);
                break;

            case SDLBackend::InputEvent::MouseButton:
                m_state->setMouseButton(genericEvent.button, genericEvent.pressed);
                m_converter->publishMouseButton(genericEvent.button, genericEvent.pressed,
                                                 genericEvent.mouseX, genericEvent.mouseY);
                break;

            case SDLBackend::InputEvent::MouseWheel:
                m_converter->publishMouseWheel(genericEvent.wheelDelta);
                break;

            case SDLBackend::InputEvent::KeyboardKey:
                m_state->setKey(genericEvent.scancode, genericEvent.pressed);
                m_state->updateModifiers(genericEvent.shift, genericEvent.ctrl, genericEvent.alt);
                m_converter->publishKeyboardKey(genericEvent.scancode, genericEvent.pressed,
                                                 genericEvent.repeat, genericEvent.shift,
                                                 genericEvent.ctrl, genericEvent.alt);
                break;

            case SDLBackend::InputEvent::KeyboardText:
                m_converter->publishKeyboardText(genericEvent.text);
                break;
        }

        m_eventsProcessed++;
    }
}
```

#### 1.6 feedEvent() - Injection thread-safe

```cpp
void InputModule::feedEvent(const void* nativeEvent) {
    const SDL_Event* sdlEvent = static_cast<const SDL_Event*>(nativeEvent);

    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_eventBuffer.push_back(*sdlEvent);
}
```

#### 1.7 Configuration JSON

```json
{
  "backend": "sdl",
  "enableMouse": true,
  "enableKeyboard": true,
  "enableGamepad": false,
  "logLevel": "info"
}
```

#### 1.8 CMakeLists.txt

```cmake
# modules/InputModule/CMakeLists.txt

add_library(InputModule SHARED
    InputModule.cpp
    Core/InputState.cpp
    Core/InputConverter.cpp
    Backends/SDLBackend.cpp
)

target_include_directories(InputModule
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(InputModule
    PRIVATE
        GroveEngine::impl
        SDL2::SDL2
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

# Install
install(TARGETS InputModule
    LIBRARY DESTINATION modules
    RUNTIME DESTINATION modules
)
```

#### 1.9 Test Phase 1

**Créer:** `tests/visual/test_30_input_module.cpp`

```cpp
// Test basique : Afficher les events dans la console
int main() {
    // Setup SDL + modules
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow(...);

    auto& ioManager = IntraIOManager::getInstance();
    auto inputIO = ioManager.createInstance("input_module");
    auto testIO = ioManager.createInstance("test_controller");

    // Load InputModule
    ModuleLoader inputLoader;
    auto inputModule = inputLoader.load("../modules/InputModule.dll", "input_module");

    JsonDataNode config("config");
    config.setString("backend", "sdl");
    inputModule->setConfiguration(config, inputIO.get(), nullptr);

    // Subscribe to all input events
    testIO->subscribe("input:mouse:move");
    testIO->subscribe("input:mouse:button");
    testIO->subscribe("input:keyboard:key");

    // Main loop
    bool running = true;
    while (running) {
        // 1. Poll SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;

            // 2. Feed to InputModule
            inputModule->feedEvent(&event);  // ← API spéciale
        }

        // 3. Process InputModule
        JsonDataNode input("input");
        inputModule->process(input);

        // 4. Check IIO messages
        while (testIO->hasMessages() > 0) {
            auto msg = testIO->pullMessage();
            std::cout << "Event: " << msg.topic << "\n";

            if (msg.topic == "input:mouse:move") {
                int x = msg.data->getInt("x", 0);
                int y = msg.data->getInt("y", 0);
                std::cout << "  Mouse: " << x << ", " << y << "\n";
            }
        }

        SDL_Delay(16);  // ~60fps
    }

    inputModule->shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}
```

**Résultat attendu:**
```
Event: input:mouse:move
  Mouse: 320, 240
Event: input:mouse:button
  Button: 0, Pressed: true
Event: input:keyboard:key
  Scancode: 44 (Space), Pressed: true
```

---

### Phase 2: Gamepad Support (Optionnel)

**Fichiers:**
- `Backends/SDLGamepadBackend.cpp/h`

**Topics:**
- `input:gamepad:button`
- `input:gamepad:axis`
- `input:gamepad:connected`

**Test:** `test_31_input_gamepad.cpp`

---

### Phase 3: Integration avec UIModule ✅

**Test:** `tests/integration/IT_015_input_ui_integration.cpp`

**Objectif:** Valider l'intégration complète de la chaîne input → UI → render

**Pipeline testé:**
```
SDL_Event → InputModule → IIO → UIModule → IIO → BgfxRenderer
```

**Scénarios de test:**

1. **Mouse Input Flow**
   - Simule `SDL_MOUSEMOTION` → Vérifie `input:mouse:move` publié
   - Simule `SDL_MOUSEBUTTONDOWN/UP` → Vérifie `input:mouse:button` publié
   - Vérifie que UIModule détecte le hover (`ui:hover`)
   - Vérifie que UIModule détecte le click (`ui:click`, `ui:action`)

2. **Keyboard Input Flow**
   - Simule `SDL_KEYDOWN/UP` → Vérifie `input:keyboard:key` publié
   - Vérifie que UIModule peut recevoir les événements clavier

3. **End-to-End Verification**
   - InputModule publie correctement les events IIO
   - UIModule consomme les events et génère des events UI
   - BgfxRenderer (mode headless) reçoit les commandes de rendu
   - Pas de perte d'événements dans le pipeline

**Métriques vérifiées:**
- Nombre d'événements input publiés (mouse moves, clicks, keys)
- Nombre d'événements UI générés (clicks, hovers, actions)
- Health status de l'InputModule (events processed, frames)

**Usage:**
```bash
# Run integration test
cd build
ctest -R InputUIIntegration --output-on-failure

# Or run directly
./IT_015_input_ui_integration
```

**Résultat attendu:**
```
✅ InputModule correctly published input events
✅ UIModule correctly processed input events
✅ IT_015: Integration test PASSED
```

---

## Dépendances

- **GroveEngine Core** - `IModule`, `IIO`, `IDataNode`
- **SDL2** - Pour la Phase 1 (backend SDL)
- **nlohmann/json** - Parsing JSON config
- **spdlog** - Logging

---

## Tests

| Test | Description | Phase |
|------|-------------|-------|
| `test_30_input_module` | Test basique InputModule seul | 1 |
| `test_31_input_gamepad` | Test gamepad | 2 |
| `IT_015_input_ui_integration` | InputModule + UIModule + BgfxRenderer | 3 |

---

## Hot-Reload Support

### getState()
```cpp
std::unique_ptr<IDataNode> InputModule::getState() {
    auto state = std::make_unique<JsonDataNode>("state");

    // Mouse state
    state->setInt("mouseX", m_state->mouseX);
    state->setInt("mouseY", m_state->mouseY);

    // Buffered events (important pour pas perdre des events pendant reload)
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    state->setInt("bufferedEventCount", m_eventBuffer.size());

    return state;
}
```

### setState()
```cpp
void InputModule::setState(const IDataNode& state) {
    m_state->mouseX = state.getInt("mouseX", 0);
    m_state->mouseY = state.getInt("mouseY", 0);

    // Note: On ne peut pas restaurer le buffer d'events (SDL_Event non sérialisable)
    // C'est acceptable car on perd au max 1 frame d'events
}
```

---

## Performance

**Objectifs:**
- < 0.1ms par frame pour process() (100 events/frame max)
- 0 allocation dynamique dans process() (sauf IIO messages)
- Thread-safe feedEvent() avec lock minimal

**Profiling:**
```cpp
std::unique_ptr<IDataNode> InputModule::getHealthStatus() {
    auto health = std::make_unique<JsonDataNode>("health");
    health->setString("status", "healthy");
    health->setInt("frameCount", m_frameCount);
    health->setInt("eventsProcessed", m_eventsProcessed);
    health->setDouble("eventsPerFrame", m_eventsProcessed / (double)m_frameCount);
    return health;
}
```

---

## Usage dans un vrai jeu

```cpp
// Game main.cpp
int main() {
    // Setup modules
    auto moduleSystem = ModuleSystemFactory::create("sequential");
    auto& ioManager = IntraIOManager::getInstance();

    // Load modules
    auto inputModule = loadModule("InputModule.dll");
    auto uiModule = loadModule("UIModule.dll");
    auto gameModule = loadModule("MyGameLogic.dll");
    auto rendererModule = loadModule("BgfxRenderer.dll");

    // Register (ordre important!)
    moduleSystem->registerModule("input", std::move(inputModule));    // 1er
    moduleSystem->registerModule("ui", std::move(uiModule));          // 2ème
    moduleSystem->registerModule("game", std::move(gameModule));      // 3ème
    moduleSystem->registerModule("renderer", std::move(rendererModule)); // 4ème

    // Get raw pointer to InputModule (pour feedEvent)
    InputModule* inputModulePtr = /* ... via queryModule ou autre ... */;

    // Main loop
    while (running) {
        // 1. Poll inputs
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            inputModulePtr->feedEvent(&event);
        }

        // 2. Process all modules (ordre garanti)
        moduleSystem->processModules(deltaTime);

        // InputModule publie → UIModule consomme → Renderer affiche
    }
}
```

---

## Fichiers à créer

```
modules/InputModule/
├── CMakeLists.txt                 # Build configuration
├── InputModule.h                  # Module principal header
├── InputModule.cpp                # Module principal implementation
├── Core/
│   ├── InputState.h               # État des inputs
│   ├── InputState.cpp
│   ├── InputConverter.h           # Generic → IIO
│   └── InputConverter.cpp
└── Backends/
    ├── SDLBackend.h               # SDL → Generic
    └── SDLBackend.cpp

tests/visual/
└── test_30_input_module.cpp       # Test basique

tests/integration/
└── IT_015_input_ui_integration.cpp  # Test avec UIModule
```

---

## Estimation

| Phase | Complexité | Temps estimé |
|-------|------------|--------------|
| 1.1-1.3 | Moyenne | 2-3h (structure + backend) |
| 1.4-1.5 | Facile | 1-2h (converter + process) |
| 1.6-1.9 | Facile | 1-2h (config + test) |
| **Total Phase 1** | **4-7h** | **InputModule production-ready** |
| Phase 2 | Moyenne | 2-3h (gamepad) |
| Phase 3 | Facile | 1h (integration test) |

---

## Ordre recommandé

1. ✅ **Créer structure** (CMakeLists, headers vides)
2. ✅ **InputState** (simple, pas de dépendances)
3. ✅ **SDLBackend** (conversion SDL → Generic)
4. ✅ **InputConverter** (conversion Generic → IIO)
5. ✅ **InputModule::process()** (pipeline complet)
6. ✅ **InputModule::feedEvent()** (thread-safe buffer)
7. ✅ **Test basique** (test_30_input_module.cpp)
8. ✅ **Test integration** (avec UIModule)

---

## On commence ?

Prêt à implémenter la Phase 1 ! 🚀
