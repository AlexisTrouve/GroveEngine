# InputModule

⚠️ **Development Stage**: Phase 1-3 complete (mouse, keyboard). Gamepad support is Phase 2 (TODO). See [main README](../../README.md#current-status).

Module de capture et conversion d'événements d'entrée (clavier, souris) vers le système IIO de GroveEngine.

## Vue d'ensemble

L'InputModule permet un découplage complet entre la source d'input (SDL, GLFW, Windows, etc.) et les modules consommateurs (UI, Game Logic, etc.). Il capture les événements natifs de la plateforme, les normalise, et les publie via le système IIO pour que d'autres modules puissent y réagir.

## Architecture

```
SDL_Event (native) → InputModule.feedEvent()
                          ↓
                    [Event Buffer]
                          ↓
                   InputModule.process()
                          ↓
                  SDLBackend.convert()
                          ↓
                   [Generic InputEvent]
                          ↓
                 InputConverter.publish()
                          ↓
                      IIO Messages
```

### Composants

- **InputModule** - Module principal IModule
- **InputState** - État courant des inputs (touches pressées, position souris)
- **SDLBackend** - Conversion SDL_Event → InputEvent générique
- **InputConverter** - Conversion InputEvent → messages IIO

## Topics IIO publiés

### Mouse Events

| Topic | Payload | Description |
|-------|---------|-------------|
| `input:mouse:move` | `{x, y}` | Position souris (coordonnées écran) |
| `input:mouse:button` | `{button, pressed, x, y}` | Click souris (button: 0=left, 1=middle, 2=right) |
| `input:mouse:wheel` | `{delta}` | Molette souris (delta: + = haut, - = bas) |

### Keyboard Events

| Topic | Payload | Description |
|-------|---------|-------------|
| `input:keyboard:key` | `{scancode, pressed, repeat, shift, ctrl, alt}` | Touche clavier |
| `input:keyboard:text` | `{text}` | Saisie texte UTF-8 (pour TextInput) |

### Gamepad Events (Phase 2)

| Topic | Payload | Description |
|-------|---------|-------------|
| `input:gamepad:button` | `{id, button, pressed}` | Bouton gamepad |
| `input:gamepad:axis` | `{id, axis, value}` | Axe analogique (-1.0 à 1.0) |
| `input:gamepad:connected` | `{id, name, connected}` | Gamepad connecté/déconnecté |

## Configuration

```json
{
  "backend": "sdl",
  "enableMouse": true,
  "enableKeyboard": true,
  "enableGamepad": false,
  "logLevel": "info"
}
```

## Usage

### Dans un test ou jeu

```cpp
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include "modules/InputModule/InputModule.h"

// Setup
auto& ioManager = grove::IntraIOManager::getInstance();
auto inputIO = ioManager.createInstance("input_module");
auto gameIO = ioManager.createInstance("game_logic");

// Load module
grove::ModuleLoader inputLoader;
auto inputModule = inputLoader.load("../modules/InputModule.dll", "input_module");

// Configure
grove::JsonDataNode config("config");
config.setString("backend", "sdl");
config.setBool("enableMouse", true);
config.setBool("enableKeyboard", true);
inputModule->setConfiguration(config, inputIO.get(), nullptr);

// Subscribe to events with callback handlers
gameIO->subscribe("input:mouse:button", [this](const grove::Message& msg) {
    int button = msg.data->getInt("button", 0);
    bool pressed = msg.data->getBool("pressed", false);
    double x = msg.data->getDouble("x", 0.0);
    double y = msg.data->getDouble("y", 0.0);
    handleMouseButton(button, pressed, x, y);
});

gameIO->subscribe("input:keyboard:key", [this](const grove::Message& msg) {
    int scancode = msg.data->getInt("scancode", 0);
    bool pressed = msg.data->getBool("pressed", false);
    handleKeyboard(scancode, pressed);
});

// Main loop
while (running) {
    // 1. Poll SDL events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        inputModule->feedEvent(&event);  // Thread-safe injection
    }

    // 2. Process InputModule (converts buffered events → IIO)
    grove::JsonDataNode input("input");
    inputModule->process(input);

    // 3. Process game logic - pull and auto-dispatch to callbacks
    while (gameIO->hasMessages() > 0) {
        gameIO->pullAndDispatch();  // Callbacks invoked automatically
    }
}

// Cleanup
inputModule->shutdown();
```

### Avec SequentialModuleSystem

```cpp
auto moduleSystem = ModuleSystemFactory::create("sequential");

// Load modules in order
auto inputModule = loadModule("InputModule.dll");
auto uiModule = loadModule("UIModule.dll");
auto gameModule = loadModule("GameLogic.dll");

moduleSystem->registerModule("input", std::move(inputModule));
moduleSystem->registerModule("ui", std::move(uiModule));
moduleSystem->registerModule("game", std::move(gameModule));

// Get InputModule for feedEvent()
auto* inputPtr = /* get pointer via queryModule or similar */;

// Main loop
while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        inputPtr->feedEvent(&event);
    }

    // Process all modules in order (input → ui → game)
    moduleSystem->processModules(deltaTime);
}
```

## Hot-Reload Support

L'InputModule supporte le hot-reload avec préservation de l'état :

### État préservé
- Position souris (x, y)
- État des boutons souris (left, middle, right)
- Statistiques (frameCount, eventsProcessed)

### État non préservé
- Buffer d'événements (SDL_Event non sérialisable)
- Touches clavier actuellement pressées

**Note:** Perdre au max 1 frame d'événements pendant le reload (~16ms à 60fps).

## Tests

### Test unitaire visuel
```bash
# Compile
cmake -B build -DGROVE_BUILD_INPUT_MODULE=ON
cmake --build build --target test_30_input_module

# Run
./build/test_30_input_module
```

**Interactions:**
- Bouger la souris pour voir `input:mouse:move`
- Cliquer pour voir `input:mouse:button`
- Scroller pour voir `input:mouse:wheel`
- Taper des touches pour voir `input:keyboard:key`
- Taper du texte pour voir `input:keyboard:text`

### Test d'intégration
```bash
# Compile avec UIModule
cmake -B build -DGROVE_BUILD_INPUT_MODULE=ON -DGROVE_BUILD_UI_MODULE=ON
cmake --build build

# Run integration test
cd build
ctest -R InputUIIntegration --output-on-failure
```

## Performance

### Objectifs
- < 0.1ms par frame pour `process()` (100 events/frame max)
- 0 allocation dynamique dans `process()` (sauf IIO messages)
- Thread-safe `feedEvent()` avec lock minimal

### Monitoring

```cpp
auto health = inputModule->getHealthStatus();
std::cout << "Status: " << health->getString("status", "") << "\n";
std::cout << "Frames: " << health->getInt("frameCount", 0) << "\n";
std::cout << "Events processed: " << health->getInt("eventsProcessed", 0) << "\n";
std::cout << "Events/frame: " << health->getDouble("eventsPerFrame", 0.0) << "\n";
```

## Dépendances

- **GroveEngine Core** - IModule, IIO, IDataNode
- **SDL2** - Backend pour capture d'événements
- **nlohmann/json** - Parsing configuration JSON
- **spdlog** - Logging

## Phases d'implémentation

- ✅ **Phase 1** - Souris + Clavier (SDL Backend)
- 📋 **Phase 2** - Gamepad Support (voir `plans/later/PLAN_INPUT_MODULE_PHASE2_GAMEPAD.md`)
- ✅ **Phase 3** - Test d'intégration avec UIModule

## Fichiers

```
modules/InputModule/
├── README.md                  # Ce fichier
├── CMakeLists.txt             # Configuration build
├── InputModule.h              # Module principal
├── InputModule.cpp
├── Core/
│   ├── InputState.h           # État des inputs
│   ├── InputState.cpp
│   ├── InputConverter.h       # Generic → IIO
│   └── InputConverter.cpp
└── Backends/
    ├── SDLBackend.h           # SDL → Generic
    └── SDLBackend.cpp

tests/
├── visual/
│   └── test_30_input_module.cpp       # Test visuel interactif
└── integration/
    └── IT_015_input_ui_integration.cpp # Test intégration complet
```

## Extensibilité

Pour ajouter un nouveau backend (GLFW, Win32, etc.) :

1. Créer `Backends/YourBackend.h/cpp`
2. Implémenter `convert(NativeEvent, InputEvent&)`
3. Modifier `InputModule::process()` pour utiliser le nouveau backend
4. Configurer via `backend: "your_backend"` dans la config JSON

Le reste du système (InputConverter, IIO topics) reste inchangé ! 🚀

## Licence

Voir LICENSE à la racine du projet.
