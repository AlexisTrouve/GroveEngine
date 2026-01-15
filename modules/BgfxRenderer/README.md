# BgfxRenderer Module

⚠️ **Development Stage**: Experimental, non-deterministic. See [main README](../../README.md#current-status) for limitations.

Module de rendu 2D pour GroveEngine, basé sur [bgfx](https://github.com/bkaradzic/bgfx).

## Features

- **Abstraction RHI** : Aucune dépendance bgfx exposée hors de `BgfxDevice.cpp`
- **Multi-backend** : DirectX 11/12, OpenGL, Vulkan, Metal (auto-détecté)
- **MT-ready** : Architecture task-based, lock-free frame allocator
- **Hot-reload** : Support complet du hot-reload GroveEngine
- **Batching** : Sprites groupés par texture pour performance

## Architecture

```
BgfxRenderer/
├── BgfxRendererModule.h/.cpp   # Point d'entrée IModule
├── RHI/                        # Render Hardware Interface
│   ├── RHITypes.h              # Handles typés, enums
│   ├── RHIDevice.h             # Interface abstraite
│   ├── RHICommandBuffer.h/.cpp # Command recording
│   └── BgfxDevice.cpp          # Implémentation bgfx
├── Frame/
│   ├── FrameAllocator.h/.cpp   # Allocateur lock-free
│   └── FramePacket.h           # Données immuables par frame
├── RenderGraph/
│   ├── RenderPass.h            # Interface pass
│   └── RenderGraph.h/.cpp      # Gestion des passes
├── Passes/
│   ├── ClearPass.h/.cpp        # Clear framebuffer
│   ├── SpritePass.h/.cpp       # Sprites + batching
│   └── DebugPass.h/.cpp        # Debug lines/shapes
├── Scene/
│   └── SceneCollector.h/.cpp   # Collecte depuis IIO
└── Resources/
    └── ResourceCache.h/.cpp    # Cache textures/shaders
```

## Build

### Windows (recommandé pour le rendu)

```powershell
cd "E:\Users\Alexis Trouvé\Documents\Projets\GroveEngine"

# Build rapide
.\build_renderer.bat

# Ou avec options
.\build_renderer.bat debug     # Build Debug
.\build_renderer.bat clean     # Clean + rebuild
.\build_renderer.bat vs        # Ouvrir Visual Studio
```

### Linux/WSL

```bash
cmake -B build -DGROVE_BUILD_BGFX_RENDERER=ON
cmake --build build -j4
```

Dépendances Linux :
```bash
sudo apt-get install libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

## Configuration

Le module est configuré via `IDataNode` dans `setConfiguration()` :

```json
{
    "windowWidth": 1280,
    "windowHeight": 720,
    "backend": "auto",
    "shaderPath": "./shaders",
    "vsync": true,
    "maxSpritesPerBatch": 10000,
    "frameAllocatorSizeMB": 16,
    "nativeWindowHandle": 0
}
```

| Paramètre | Type | Défaut | Description |
|-----------|------|--------|-------------|
| `windowWidth` | int | 1280 | Largeur fenêtre |
| `windowHeight` | int | 720 | Hauteur fenêtre |
| `backend` | string | "auto" | Backend graphique (auto, opengl, vulkan, dx11, dx12, metal) |
| `shaderPath` | string | "./shaders" | Chemin des shaders compilés |
| `vsync` | bool | true | Synchronisation verticale |
| `maxSpritesPerBatch` | int | 10000 | Sprites max par batch |
| `frameAllocatorSizeMB` | int | 16 | Taille allocateur frame (MB) |
| `nativeWindowHandle` | int | 0 | Handle fenêtre native (HWND, Window, etc.) |

## Communication IIO

Le renderer subscribe à `render:*` et traite les messages suivants :

### Sprites

```cpp
// Topic: render:sprite
auto sprite = std::make_unique<JsonDataNode>("sprite");
sprite->setDouble("x", 100.0);
sprite->setDouble("y", 200.0);
sprite->setDouble("scaleX", 1.0);
sprite->setDouble("scaleY", 1.0);
sprite->setDouble("rotation", 0.0);        // Radians
sprite->setDouble("u0", 0.0);              // UV min
sprite->setDouble("v0", 0.0);
sprite->setDouble("u1", 1.0);              // UV max
sprite->setDouble("v1", 1.0);
sprite->setInt("color", 0xFFFFFFFF);       // RGBA
sprite->setInt("textureId", 0);
sprite->setInt("layer", 0);                // Z-order
io->publish("render:sprite", std::move(sprite));
```

### Batch de sprites

```cpp
// Topic: render:sprite:batch
auto batch = std::make_unique<JsonDataNode>("batch");
auto sprites = std::make_unique<JsonDataNode>("sprites");
// Ajouter plusieurs sprites comme enfants...
batch->setChild("sprites", std::move(sprites));
io->publish("render:sprite:batch", std::move(batch));
```

### Caméra

```cpp
// Topic: render:camera
auto cam = std::make_unique<JsonDataNode>("camera");
cam->setDouble("x", 0.0);
cam->setDouble("y", 0.0);
cam->setDouble("zoom", 1.0);
cam->setInt("viewportX", 0);
cam->setInt("viewportY", 0);
cam->setInt("viewportW", 1280);
cam->setInt("viewportH", 720);
io->publish("render:camera", std::move(cam));
```

### Clear color

```cpp
// Topic: render:clear
auto clear = std::make_unique<JsonDataNode>("clear");
clear->setInt("color", 0x303030FF);        // RGBA
io->publish("render:clear", std::move(clear));
```

### Debug (lignes et rectangles)

```cpp
// Topic: render:debug:line
auto line = std::make_unique<JsonDataNode>("line");
line->setDouble("x1", 0.0);
line->setDouble("y1", 0.0);
line->setDouble("x2", 100.0);
line->setDouble("y2", 100.0);
line->setInt("color", 0xFF0000FF);         // Rouge
io->publish("render:debug:line", std::move(line));

// Topic: render:debug:rect
auto rect = std::make_unique<JsonDataNode>("rect");
rect->setDouble("x", 50.0);
rect->setDouble("y", 50.0);
rect->setDouble("w", 100.0);
rect->setDouble("h", 100.0);
rect->setInt("color", 0x00FF00FF);         // Vert
rect->setBool("filled", false);
io->publish("render:debug:rect", std::move(rect));
```

### Topics complets

| Topic | Description |
|-------|-------------|
| `render:sprite` | Un sprite |
| `render:sprite:batch` | Batch de sprites |
| `render:tilemap` | Chunk de tilemap |
| `render:text` | Texte à afficher |
| `render:particle` | Particule |
| `render:camera` | Configuration caméra |
| `render:clear` | Clear color |
| `render:debug:line` | Ligne de debug |
| `render:debug:rect` | Rectangle de debug |

## Intégration

### Exemple minimal

```cpp
#include <grove/ModuleLoader.h>
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>

int main() {
    // Créer le gestionnaire IO
    auto ioManager = std::make_unique<IntraIOManager>();
    auto io = ioManager->createIO("renderer");

    // Charger le module
    ModuleLoader loader;
    loader.load("./modules/libBgfxRenderer.dll", "renderer");

    // Configurer
    JsonDataNode config("config");
    config.setInt("windowWidth", 1920);
    config.setInt("windowHeight", 1080);
    config.setInt("nativeWindowHandle", (int)(intptr_t)hwnd);  // Ton HWND

    auto* module = loader.getModule();
    module->setConfiguration(config, io.get(), nullptr);

    // Main loop
    JsonDataNode input("input");
    while (running) {
        input.setDouble("deltaTime", deltaTime);

        // Envoyer des sprites via IIO
        auto sprite = std::make_unique<JsonDataNode>("sprite");
        sprite->setDouble("x", playerX);
        sprite->setDouble("y", playerY);
        sprite->setInt("textureId", 0);
        io->publish("render:sprite", std::move(sprite));

        // Process (collecte IIO + rendu)
        module->process(input);
    }

    module->shutdown();
    return 0;
}
```

## Règles d'architecture

| Règle | Raison |
|-------|--------|
| **Zéro `bgfx::` hors de `BgfxDevice.cpp`** | Abstraction propre, changement backend possible |
| **FramePacket const dans les passes** | Thread-safety, pas de mutation pendant render |
| **CommandBuffer par thread** | Pas de lock pendant l'encoding |
| **Handles, jamais de pointeurs raw** | Indirection = safe pour relocation |
| **Allocation via FrameAllocator** | Lock-free, reset gratuit chaque frame |

## TODO

- [ ] Chargement textures (stb_image)
- [ ] Compilation shaders (shaderc ou pré-compilés)
- [ ] TilemapPass
- [ ] TextPass + fonts (stb_truetype)
- [ ] ParticlePass
- [ ] Resize handling (window:resize)
- [ ] Multi-view support
- [ ] Render targets / post-process

## Dépendances

- **bgfx** : Téléchargé automatiquement via CMake FetchContent
- **GroveEngine::impl** : Core engine (IModule, IIO, IDataNode)
- **spdlog** : Logging
