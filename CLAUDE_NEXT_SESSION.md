# GroveEngine - Session Successor Prompt

## Contexte Rapide

GroveEngine est un moteur de jeu C++17 avec hot-reload de modules. On développe le module **BgfxRenderer** pour le rendu 2D.

## État Actuel (27 Nov 2025)

### Phases Complétées ✅

**Phase 1-4** - Squelette, RHI, RenderGraph, ShaderManager
- Tout fonctionne, voir commits précédents

**Phase 5** - Pipeline IIO → Rendu ✅ (PARTIEL)
- `test_23_bgfx_sprites_visual.cpp` : test complet SDL2 + IIO + Module
- Pipeline vérifié fonctionnel :
  - Module charge avec Vulkan (~500 FPS)
  - IIO route les messages (sprites, camera, clear)
  - SceneCollector collecte et crée FramePacket
  - RenderGraph exécute les passes
- **MAIS** : Les sprites ne s'affichent PAS visuellement

### Problème à Résoudre

Le shader actuel (`vs_color`/`fs_color` de bgfx drawstress) est un shader position+couleur simple. Il ne supporte pas l'instancing nécessaire pour les sprites.

**Ce qui manque :**
1. Un shader sprite avec instancing qui lit les données d'instance (position, scale, rotation, color, UV)
2. Le vertex layout correct pour le quad (pos.xy, uv.xy)
3. L'instance layout correct pour SpriteInstance

### Fichiers Clés

```
modules/BgfxRenderer/
├── Shaders/
│   ├── ShaderManager.cpp    # Charge les shaders embedded
│   ├── vs_color.bin.h       # Shader actuel (PAS d'instancing)
│   └── fs_color.bin.h
├── Passes/
│   └── SpritePass.cpp       # Execute avec instance buffer
├── Frame/
│   └── FramePacket.h        # SpriteInstance struct
└── RHI/
    └── BgfxDevice.cpp       # createBuffer avec VertexLayout
```

### Structure SpriteInstance (à matcher dans le shader)

```cpp
struct SpriteInstance {
    float x, y;           // Position
    float scaleX, scaleY; // Scale
    float rotation;       // Rotation en radians
    float u0, v0, u1, v1; // UV coords
    uint32_t color;       // ABGR
    uint16_t textureId;   // ID texture
    uint16_t layer;       // Layer de tri
};
```

## Prochaine Étape : Shader Sprite Instancing

### Option A : Shader BGFX natif (.sc)

Créer `vs_sprite.sc` et `fs_sprite.sc` avec :
- Vertex input : position (vec2), uv (vec2)
- Instance input : transform, color, uvRect
- Compiler avec shaderc pour toutes les plateformes

### Option B : Simplifier temporairement

Dessiner chaque sprite comme un quad individuel sans instancing :
- Plus lent mais fonctionne avec le shader actuel
- Modifier SpritePass pour soumettre un draw par sprite

### Build & Test

```bash
# Build
cmake -DGROVE_BUILD_BGFX_RENDERER=ON -B build-bgfx
cmake --build build-bgfx -j4

# Tests
./build-bgfx/tests/test_21_bgfx_triangle      # Triangle coloré ✅
./build-bgfx/tests/test_23_bgfx_sprites_visual # Pipeline OK, sprites invisibles

# Tous les tests
cd build-bgfx && ctest --output-on-failure
```

## Notes Importantes

- **Cross-Platform** : Linux + Windows (MinGW)
- **WSL2** : Vulkan fonctionne, pas OpenGL
- **Shaders embedded** : Pré-compilés dans .bin.h, pas de shaderc runtime
- **100% tests passent** : 20/20

## Questions pour la prochaine session

1. Option A ou B pour les sprites ?
2. Priorité : voir quelque chose à l'écran vs architecture propre ?
