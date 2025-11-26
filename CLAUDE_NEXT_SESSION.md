# GroveEngine - Session Successor Prompt

## Contexte Rapide

GroveEngine est un moteur de jeu C++17 avec hot-reload de modules. On développe actuellement le module **BgfxRenderer** pour le rendu 2D.

## État Actuel (26 Nov 2025)

### Phases Complétées ✅

**Phase 1** - Squelette du module
- `libBgfxRenderer.so` compilé et chargeable dynamiquement

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

### Fichiers Clés

```
modules/BgfxRenderer/
├── BgfxRendererModule.cpp   # Point d'entrée module
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
│   ├── SpritePass.cpp       # Rendu sprites (à compléter)
│   └── DebugPass.cpp        # Debug shapes
├── Frame/
│   ├── FramePacket.h        # Données immutables par frame
│   └── FrameAllocator.cpp   # Allocateur bump
└── Scene/
    └── SceneCollector.cpp   # Collecte messages IIO
```

## Prochaine Phase : Phase 4

### Objectif
Intégrer le ShaderManager dans le module principal et rendre le SpritePass fonctionnel.

### Tâches

1. **Mettre à jour BgfxRendererModule.cpp** :
   - Ajouter `ShaderManager` comme membre
   - Initialiser les shaders dans `setConfiguration()`
   - Passer le program aux passes

2. **Compléter SpritePass.cpp** :
   - Utiliser le shader "sprite" du ShaderManager
   - Implémenter l'update du instance buffer avec les données FramePacket
   - Soumettre les draw calls instancés

3. **Test d'intégration** :
   - Créer un test qui charge le module via `ModuleLoader`
   - Envoyer des sprites via IIO
   - Vérifier le rendu

### Build & Test

```bash
# Build avec BgfxRenderer
cmake -DGROVE_BUILD_BGFX_RENDERER=ON -B build-bgfx
cmake --build build-bgfx -j4

# Tests RHI
./build-bgfx/tests/test_20_bgfx_rhi

# Test visuel triangle
./build-bgfx/tests/test_21_bgfx_triangle
```

## Notes Importantes

- **WSL2** : Le rendu fonctionne via Vulkan (pas OpenGL)
- **Shaders** : Pré-compilés, pas besoin de shaderc à runtime
- **Thread Safety** : Voir `docs/coding_guidelines.md` pour les patterns mutex

## Commit Actuel

```
1443c12 feat(BgfxRenderer): Complete Phase 2-3 with shaders and triangle rendering
```
