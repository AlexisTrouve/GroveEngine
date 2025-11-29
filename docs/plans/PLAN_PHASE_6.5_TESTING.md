# Phase 6.5 - BgfxRenderer Testing Suite

## Vue d'ensemble

Plan complet de tests pour valider toutes les composantes du BgfxRenderer avant Phase 7.

## État actuel des tests

### Tests existants ✅
- `test_20_bgfx_rhi.cpp` - Tests unitaires RHI (CommandBuffer, FrameAllocator)
- `test_22_bgfx_sprites_headless.cpp` - Tests headless sprites + IIO
- `test_23_bgfx_sprites_visual.cpp` - Tests visuels sprites
- `test_bgfx_triangle.cpp` - Test visuel triangle basique
- `test_bgfx_sprites.cpp` - Tests visuels sprites (legacy)

### Tests manquants 🔴
Identifiés dans le plan Phase 6.5 original :
- TU ShaderManager
- TU RenderGraph (compilation, ordre d'exécution)
- TU FrameAllocator (coverage complet)
- TU RHICommandBuffer (tous les types de commandes)
- TI SceneCollector (parsing complet de tous les messages IIO)
- TI ResourceCache (thread-safety, double-loading)
- TI TextureLoader (formats, erreurs)
- TI Pipeline complet headless (mock device)

---

## Plan de tests détaillé

### A. Tests Unitaires (TU) - Headless, pas de GPU

#### A1. FrameAllocator (complément de test_20)
**Fichier** : `tests/unit/test_frame_allocator.cpp`

**Tests** :
1. `allocation_basic` - Alloc simple, vérifier pointeur non-null
2. `allocation_aligned` - Vérifier alignement 16-byte
3. `allocation_typed` - Template `allocate<T>()`
4. `allocation_array` - `allocateArray<T>(count)`
5. `allocation_overflow` - Dépasser capacité → nullptr
6. `reset_clears_offset` - `reset()` remet offset à 0
7. `concurrent_allocations` - Thread-safety (lancer 4 threads qui alloc en //)
8. `stats_accurate` - `getUsed()` / `getCapacity()` corrects
9. `alignment_various` - Test 1, 4, 8, 16, 32 byte alignments

**Durée estimée** : ~0.1s (avec threads)

---

#### A2. RHICommandBuffer (complément de test_20)
**Fichier** : `tests/unit/test_rhi_command_buffer.cpp`

**Tests** :
1. `record_setState` - Vérifier command.type = SetState
2. `record_setTexture` - Slot, handle, sampler
3. `record_setUniform` - Data copié correctement (vec4)
4. `record_setVertexBuffer` - Buffer + offset
5. `record_setIndexBuffer` - Buffer + offset + 32bit flag
6. `record_setInstanceBuffer` - Start + count
7. `record_setScissor` - x, y, w, h
8. `record_draw` - vertexCount + startVertex
9. `record_drawIndexed` - indexCount + startIndex
10. `record_drawInstanced` - indexCount + instanceCount
11. `record_submit` - viewId + shader + depth
12. `clear_empties_buffer` - `clear()` puis `size() == 0`
13. `move_semantics` - `std::move(cmd)` fonctionne

**Durée estimée** : ~0.01s

---

#### A3. RenderGraph
**Fichier** : `tests/unit/test_render_graph.cpp`

**Tests** :
1. `add_single_pass` - Ajouter une passe, compile OK
2. `add_multiple_passes_no_deps` - 3 passes sans dépendances
3. `compile_topological_sort` - Vérifier ordre selon `getSortOrder()`
4. `compile_with_dependencies` - PassB dépend de PassA → ordre respecté
5. `compile_cycle_detection` - PassA → PassB → PassA doit échouer (TODO: si implémenté)
6. `setup_calls_all_passes` - Mock device, vérifier setup() appelé
7. `shutdown_calls_all_passes` - Vérifier shutdown() appelé
8. `build_tasks_creates_tasks` - `buildTasks()` génère les tasks dans TaskGraph (si TaskGraph existe)

**Mock RenderPass** :
```cpp
class MockPass : public RenderPass {
    static int s_setupCount;
    static int s_shutdownCount;
    static int s_executeCount;

    void setup(IRHIDevice&) override { s_setupCount++; }
    void shutdown(IRHIDevice&) override { s_shutdownCount++; }
    void execute(const FramePacket&, RHICommandBuffer&) override { s_executeCount++; }
};
```

**Durée estimée** : ~0.05s

---

#### A4. ShaderManager
**Fichier** : `tests/unit/test_shader_manager.cpp`

**Tests** :
1. `init_creates_default_shaders` - Vérifier sprite/color programs créés
2. `getProgram_sprite` - Retourne handle valide
3. `getProgram_color` - Retourne handle valide
4. `getProgram_invalid` - Programme inexistant → handle invalid
5. `shutdown_destroys_programs` - Cleanup propre (nécessite mock device)

**Mock IRHIDevice** :
```cpp
class MockRHIDevice : public IRHIDevice {
    std::vector<ShaderHandle> created;

    ShaderHandle createShader(const ShaderDesc& desc) override {
        ShaderHandle h;
        h.id = created.size() + 1;
        created.push_back(h);
        return h;
    }

    void destroy(ShaderHandle h) override {
        // Track destroy calls
    }

    // Stub other methods...
};
```

**Durée estimée** : ~0.01s

---

### B. Tests d'Intégration (TI) - Headless, interactions multi-composants

#### B1. SceneCollector (complément de test_22)
**Fichier** : `tests/integration/test_scene_collector.cpp`

**Tests** :
1. `parse_sprite_full` - Tous les champs (x, y, scale, rotation, UVs, color, textureId, layer)
2. `parse_sprite_batch` - Array de sprites
3. `parse_tilemap_with_tiles` - Chunk + array de tiles
4. `parse_text_with_string` - TextCommand avec string alloué
5. `parse_particle` - Tous les champs particule
6. `parse_camera_matrices` - Vérifier viewMatrix et projMatrix calculés
7. `parse_clear_color` - clearColor stocké
8. `parse_debug_line` - x1, y1, x2, y2, color
9. `parse_debug_rect_filled` - x, y, w, h, color, filled=true
10. `parse_debug_rect_outline` - filled=false
11. `finalize_copies_to_allocator` - Vérifier que sprites/texts copiés dans FrameAllocator
12. `finalize_string_pointers_valid` - Pointeurs de texte valides après finalize
13. `clear_empties_collections` - `clear()` vide tous les vectors
14. `collect_from_iio_mock` - Créer mock IIO, publish messages, collecter
15. `multiple_frames` - Collect → finalize → clear → repeat (3 cycles)

**Durée estimée** : ~0.1s

---

#### B2. ResourceCache (thread-safety critical)
**Fichier** : `tests/integration/test_resource_cache.cpp`

**Tests** :
1. `load_texture_once` - Charger texture, vérifier handle valide
2. `load_texture_twice_same_handle` - Double load retourne même handle
3. `get_texture_by_path` - Lookup après load
4. `get_texture_by_id` - Lookup numérique
5. `get_texture_id_from_path` - Path → ID mapping
6. `load_shader_once` - Charger shader
7. `load_shader_twice_same_handle` - Éviter duplication
8. `has_texture_true` - `hasTexture()` après load
9. `has_texture_false` - Avant load
10. `concurrent_texture_loads` - 4 threads load same texture → 1 seul handle créé
11. `concurrent_different_textures` - 4 threads load différentes textures → 4 handles
12. `clear_destroys_all` - `clear()` destroy tous les handles
13. `stats_accurate` - `getTextureCount()`, `getShaderCount()`

**Mock device** :
```cpp
class MockRHIDevice : public IRHIDevice {
    std::atomic<int> textureCreateCount{0};
    std::atomic<int> shaderCreateCount{0};

    TextureHandle createTexture(const TextureDesc&) override {
        textureCreateCount++;
        TextureHandle h;
        h.id = textureCreateCount.load();
        return h;
    }

    // Similar for shaders...
};
```

**Durée estimée** : ~0.2s (threads)

---

#### B3. TextureLoader
**Fichier** : `tests/integration/test_texture_loader.cpp`

**Tests** :
1. `load_png_success` - Charger PNG valide (créer test asset 16x16)
2. `load_jpg_success` - Charger JPG valide
3. `load_nonexistent_fail` - Fichier inexistant → success=false
4. `load_invalid_format_fail` - Fichier corrompu → success=false
5. `load_from_memory` - Charger depuis buffer mémoire
6. `load_result_dimensions` - Vérifier width/height corrects
7. `load_result_handle_valid` - Handle valide si success=true

**Assets de test** :
Créer `tests/assets/textures/` avec :
- `white_16x16.png` - Texture blanche 16x16
- `checker_32x32.png` - Damier 32x32
- `invalid.png` - Fichier corrompu (quelques bytes random)

**Note** : Nécessite mock IRHIDevice qui accepte TextureDesc sans vraiment créer GPU texture.

**Durée estimée** : ~0.05s

---

#### B4. Pipeline complet headless
**Fichier** : `tests/integration/test_pipeline_headless.cpp`

**Description** : Test du flux complet sans GPU :
```
IIO messages → SceneCollector → FramePacket → RenderGraph → CommandBuffer
```

**Tests** :
1. `full_pipeline_single_sprite` - 1 sprite IIO → 1 sprite dans FramePacket → SpritePass génère commands
2. `full_pipeline_batch_sprites` - Batch de 100 sprites
3. `full_pipeline_with_camera` - Camera message → projMatrix utilisée
4. `full_pipeline_clear_color` - Clear message → clearColor dans packet
5. `full_pipeline_all_passes` - Clear + Sprite + Debug → ordre d'exécution correct
6. `full_pipeline_multiple_frames` - 10 frames consécutives

**Mock components** :
- MockRHIDevice (stub toutes les méthodes)
- Mock IIO (IntraIO suffit, déjà fonctionnel)

**Validation** :
- Vérifier nombre de commands dans CommandBuffer
- Vérifier ordre des passes (Clear avant Sprite)
- Vérifier données dans FramePacket (spriteCount, etc.)

**Durée estimée** : ~0.5s

---

### C. Tests Visuels (existants à conserver)

Ces tests nécessitent une fenêtre/GPU, déjà implémentés :

1. `test_bgfx_triangle.cpp` - Triangle coloré basique
2. `test_bgfx_sprites.cpp` / `test_23_bgfx_sprites_visual.cpp` - Rendu sprites
3. Future : `test_text_rendering.cpp` - Rendu texte (Phase 7)
4. Future : `test_particles.cpp` - Particules (Phase 7)

---

## Structure des fichiers de tests

```
tests/
├── unit/                          # TU purs, 0 dépendances externes
│   ├── test_frame_allocator.cpp
│   ├── test_rhi_command_buffer.cpp
│   ├── test_render_graph.cpp
│   └── test_shader_manager.cpp
│
├── integration/                   # TI avec mocks, headless
│   ├── test_20_bgfx_rhi.cpp      ✅ Existant
│   ├── test_22_bgfx_sprites_headless.cpp ✅ Existant
│   ├── test_scene_collector.cpp
│   ├── test_resource_cache.cpp
│   ├── test_texture_loader.cpp
│   └── test_pipeline_headless.cpp
│
├── visual/                        # Tests avec GPU
│   ├── test_bgfx_triangle.cpp    ✅ Existant
│   ├── test_23_bgfx_sprites_visual.cpp ✅ Existant
│   └── test_bgfx_sprites.cpp     ✅ Existant (legacy)
│
└── assets/                        # Assets de test
    └── textures/
        ├── white_16x16.png
        ├── checker_32x32.png
        └── invalid.png
```

---

## Mocks & Utilities

### Mock IRHIDevice (partagé entre tests)
**Fichier** : `tests/mocks/MockRHIDevice.h`

```cpp
#pragma once
#include "grove/rhi/RHIDevice.h"
#include <atomic>
#include <vector>

namespace grove::test {

class MockRHIDevice : public rhi::IRHIDevice {
public:
    // Counters
    std::atomic<int> textureCreateCount{0};
    std::atomic<int> bufferCreateCount{0};
    std::atomic<int> shaderCreateCount{0};
    std::atomic<int> textureDestroyCount{0};
    std::atomic<int> bufferDestroyCount{0};
    std::atomic<int> shaderDestroyCount{0};

    // Handles
    std::vector<rhi::TextureHandle> textures;
    std::vector<rhi::BufferHandle> buffers;
    std::vector<rhi::ShaderHandle> shaders;

    // IRHIDevice implementation (all stubbed)
    bool init(void*, uint16_t, uint16_t) override { return true; }
    void shutdown() override {}
    void reset(uint16_t, uint16_t) override {}

    rhi::TextureHandle createTexture(const rhi::TextureDesc&) override {
        rhi::TextureHandle h;
        h.id = textureCreateCount++;
        textures.push_back(h);
        return h;
    }

    rhi::BufferHandle createBuffer(const rhi::BufferDesc&) override {
        rhi::BufferHandle h;
        h.id = bufferCreateCount++;
        buffers.push_back(h);
        return h;
    }

    rhi::ShaderHandle createShader(const rhi::ShaderDesc&) override {
        rhi::ShaderHandle h;
        h.id = shaderCreateCount++;
        shaders.push_back(h);
        return h;
    }

    void destroy(rhi::TextureHandle) override { textureDestroyCount++; }
    void destroy(rhi::BufferHandle) override { bufferDestroyCount++; }
    void destroy(rhi::ShaderHandle) override { shaderDestroyCount++; }

    // Autres méthodes stubbed...
    void updateBuffer(rhi::BufferHandle, const void*, uint32_t) override {}
    void frame() override {}
    // etc.
};

} // namespace grove::test
```

---

## Plan d'exécution (ordre recommandé)

### Sprint 1 : Fondations (TU)
1. ✅ `test_frame_allocator.cpp` (complément)
2. ✅ `test_rhi_command_buffer.cpp` (complément)
3. ✅ `test_shader_manager.cpp` (nouveau)
4. ✅ `test_render_graph.cpp` (nouveau)

**Durée estimée** : 2-3h (avec mocks)

### Sprint 2 : Intégration (TI)
5. ✅ `test_scene_collector.cpp` (complément massif)
6. ✅ `test_resource_cache.cpp` (thread-safety critical)
7. ✅ `test_texture_loader.cpp` (avec assets)

**Durée estimée** : 3-4h (assets + thread tests)

### Sprint 3 : Pipeline complet
8. ✅ `test_pipeline_headless.cpp` (end-to-end)
9. ✅ Créer `MockRHIDevice.h` partagé
10. ✅ Créer assets de test (PNG/JPG)

**Durée estimée** : 2-3h

---

## Résumé des livrables

### Code
- 8 nouveaux fichiers de tests (4 TU + 4 TI)
- 1 fichier mock partagé (`MockRHIDevice.h`)
- 3 assets de test (textures PNG)

### Couverture
- **FrameAllocator** : 9 tests (basic, aligned, overflow, concurrent, stats)
- **RHICommandBuffer** : 13 tests (tous les types de commandes + move)
- **RenderGraph** : 8 tests (compile, sort, deps, setup/shutdown)
- **ShaderManager** : 5 tests (init, get, invalid, shutdown)
- **SceneCollector** : 15 tests (tous les types de messages + finalize + IIO)
- **ResourceCache** : 13 tests (load, get, thread-safety, stats)
- **TextureLoader** : 7 tests (formats, errors, dimensions)
- **Pipeline headless** : 6 tests (end-to-end flow)

**Total** : 76 tests unitaires/intégration headless

---

## Critères de succès Phase 6.5

✅ Tous les tests passent (0 failures)
✅ Aucun leak mémoire (valgrind clean sur tests)
✅ Thread-safety validée (ResourceCache concurrent tests OK)
✅ Coverage > 80% sur composants core (FrameAllocator, CommandBuffer, RenderGraph)
✅ Pipeline headless fonctionnel (IIO → FramePacket → Commands)
✅ Tests exécutent en < 5s total (headless)

---

## Post Phase 6.5

Une fois tous les tests passés, on peut :
- **Phase 7** : Implémenter passes manquantes (Text, Tilemap, Particles) avec TDD
- **Phase 8** : Polish (hot-reload, profiling, documentation)

---

**Durée totale estimée Phase 6.5** : 7-10h développement + tests
**Date cible** : À définir selon disponibilité

---

## Notes de développement

### Catch2 vs Custom Framework
Tests actuels utilisent :
- `test_20_bgfx_rhi.cpp` → Custom macros (TEST, ASSERT)
- `test_22_bgfx_sprites_headless.cpp` → Catch2

**Recommandation** : Uniformiser sur **Catch2** pour tous les nouveaux tests (meilleur reporting, fixtures, matchers).

### Assets de test
Générer programmatiquement pour éviter bloat :
```cpp
// GenerateTestTexture.h
namespace grove::test {
    std::vector<uint8_t> generateWhite16x16PNG();
    std::vector<uint8_t> generateChecker32x32PNG();
}
```

### CI/CD
Ajouter `.github/workflows/tests.yml` :
```yaml
- name: Run BgfxRenderer tests (headless)
  run: |
    cd build
    ctest -R "test_(frame_allocator|rhi_command|render_graph|shader_manager|scene_collector|resource_cache|texture_loader|pipeline_headless)" --output-on-failure
```

---

**Statut** : Plan complet prêt pour exécution
**Prochaine étape** : Implémenter Sprint 1 (TU Fondations)
