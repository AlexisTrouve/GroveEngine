# GroveEngine - Session Successor Prompt

## Contexte Rapide

GroveEngine est un moteur de jeu C++17 avec hot-reload de modules. On développe actuellement le module **BgfxRenderer** pour le rendu 2D.

## État Actuel (27 Nov 2025)

### Portage Windows ✅

Le projet compile maintenant sur **Windows** (MinGW/Ninja) en plus de Linux :
- `ModuleFactory.cpp` et `ModuleLoader.cpp` : LoadLibrary/GetProcAddress
- `SystemUtils.cpp` : Windows process memory APIs
- `FileWatcher.h` : st_mtime au lieu de st_mtim
- `IIO.h` : ajout `#include <cstdint>`
- Tests integration (test_09, test_10, test_11) : wrappers `grove_dlopen/grove_dlsym/grove_dlclose/grove_dlerror`

### Phases Complétées ✅

**Phase 1** - Squelette du module
- `libBgfxRenderer.so/.dll` compilé et chargeable dynamiquement

**Phase 2** - RHI Layer
- `BgfxDevice` : init/shutdown/frame, création textures/buffers/shaders
- `RHICommandBuffer` : recording de commandes GPU
- `FrameAllocator` : allocateur lock-free per-frame
- `RenderGraph` : tri topologique Kahn pour ordonnancement des passes
- Tests : `test_20_bgfx_rhi` (23 tests passent)

**Phase 3** - Shaders & Triangle
- `ShaderManager` : chargement centralisé des shaders embedded
- Shaders pré-compilés : OpenGL, Vulkan, DX11, Metal
- Test visuel : `test_21_bgfx_triangle` - triangle RGB coloré (~567 FPS Vulkan)

**Phase 4** - SceneCollector & IIO Integration ✅ (NOUVEAU)
- `SceneCollector` : collecte des messages IIO pour `render:sprite`, `render:camera`, etc.
- Calcul des matrices view/proj avec support zoom dans `parseCamera()`
- Test `test_22_bgfx_sprites_headless` : 23 assertions, 5 test cases passent
  - Validation structure sprite data
  - Routing IIO inter-modules (game → renderer pattern)
  - Structure camera/clear/debug messages

### Fichiers Clés

```
modules/BgfxRenderer/
├── BgfxRendererModule.cpp   # Point d'entrée module + ShaderManager
├── RHI/
│   ├── RHIDevice.h          # Interface abstraite
│   ├── BgfxDevice.cpp       # Implémentation bgfx
│   ├── RHICommandBuffer.h   # Command buffer
│   └── RHITypes.h           # Handles, states
├── Shaders/
│   ├── ShaderManager.cpp    # Gestion shaders
│   ├── vs_color.bin.h       # Vertex shader embedded
│   └── fs_color.bin.h       # Fragment shader embedded
├── RenderGraph/
│   └── RenderGraph.cpp      # Tri topologique passes
├── Passes/
│   ├── ClearPass.cpp        # Clear screen
│   ├── SpritePass.cpp       # Rendu sprites instancié
│   └── DebugPass.cpp        # Debug shapes
├── Frame/
│   ├── FramePacket.h        # Données immutables par frame
│   └── FrameAllocator.cpp   # Allocateur bump
└── Scene/
    └── SceneCollector.cpp   # Collecte messages IIO + matrices view/proj
```

## Prochaine Phase : Phase 5

### Objectif
Test visuel complet avec sprites via IIO.

### Tâches

1. **Créer test_23_bgfx_sprites_visual.cpp** :
   - Charger le module BgfxRenderer via ModuleLoader
   - Publier des sprites via IIO depuis un "game module" simulé
   - Valider le rendu visuel (sprites affichés à l'écran)

2. **Compléter la boucle render** :
   - Appeler `SceneCollector::collect()` pour récupérer les messages IIO
   - Passer le `FramePacket` finalisé aux passes
   - S'assurer que `SpritePass::execute()` dessine les sprites

3. **Debug** :
   - Ajouter les debug shapes (lignes, rectangles) si besoin

### Build & Test

```bash
# Windows (MinGW + Ninja)
cmake -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DGROVE_BUILD_BGFX_RENDERER=ON -B build-bgfx
cmake --build build-bgfx -j4

# IMPORTANT: Sur Windows, ajouter MinGW au PATH pour ctest:
PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH" ctest -R Bgfx --output-on-failure

# Tests actuels
./build-bgfx/tests/test_20_bgfx_rhi         # 23 tests RHI
./build-bgfx/tests/test_21_bgfx_triangle    # Test visuel triangle
./build-bgfx/tests/test_22_bgfx_sprites_headless  # 5 tests IIO/structure
```

## Notes Importantes

- **Cross-Platform** : Le projet compile sur Linux ET Windows
- **Windows PATH** : Les DLLs MinGW doivent être dans le PATH pour exécuter les tests via ctest
- **WSL2** : Le rendu fonctionne via Vulkan (pas OpenGL)
- **Shaders** : Pré-compilés, pas besoin de shaderc à runtime
- **Thread Safety** : Voir `docs/coding_guidelines.md` pour les patterns mutex
- **IIO Routing** : Les messages ne sont pas routés vers l'instance émettrice, utiliser deux instances séparées (pattern game → renderer)
