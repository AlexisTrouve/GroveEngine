# GroveEngine - Session Successor Prompt

## Context
GroveEngine is a C++17 hot-reload game engine with a 2D bgfx-based renderer module.

## Current State - BgfxRenderer (27 Nov 2025)

### Completed Phases

| Phase | Feature | Status |
|-------|---------|--------|
| 5 | IIO Pipeline (messages → SceneCollector → RenderGraph) | Done |
| 5.5 | Sprite shader with GPU instancing (80-byte SpriteInstance) | Done |
| 6 | Texture loading via stb_image | Done |
| 6.5 | Debug overlay (FPS/stats via bgfx debug text) | Done |
| 7 | Text rendering with embedded 8x8 bitmap font | Done |
| 8A | Multi-texture support (sorted batching by textureId) | Done |
| 8B | Tilemap rendering (TilemapPass with instanced tiles) | Done |

### Key Files
```
modules/BgfxRenderer/
├── BgfxRendererModule.cpp       # Main module entry
├── Shaders/
│   ├── vs_sprite.sc, fs_sprite.sc  # Instanced sprite shader
│   ├── varying.def.sc              # Shader inputs/outputs
│   └── *.bin.h                     # Compiled shader bytecode
├── Passes/
│   ├── ClearPass.cpp            # Clear framebuffer
│   ├── TilemapPass.cpp          # Tilemap grid rendering
│   ├── SpritePass.cpp           # Instanced sprite rendering (sorted by texture)
│   ├── TextPass.cpp             # Text rendering (glyph quads)
│   └── DebugPass.cpp            # Debug lines/shapes
├── Text/
│   └── BitmapFont.h/.cpp        # Embedded 8x8 CP437-style font
├── Resources/
│   ├── TextureLoader.cpp        # stb_image PNG/JPG loading
│   └── ResourceCache.cpp        # Texture cache with numeric IDs
├── Scene/
│   └── SceneCollector.cpp       # IIO message parsing (render:*)
├── Debug/
│   └── DebugOverlay.cpp         # FPS/stats display
└── Frame/
    └── FramePacket.h            # SpriteInstance, TextCommand, TilemapChunk
```

### ResourceCache - Texture ID System
```cpp
// Load texture and get numeric ID
uint16_t texId = resourceCache->loadTextureWithId(device, "path/to/image.png");

// Get texture by ID (for sprite rendering)
rhi::TextureHandle tex = resourceCache->getTextureById(texId);
```

### IIO Message Formats

**Sprite** (`render:sprite`)
```cpp
{ "x": 100.0, "y": 50.0, "scaleX": 32.0, "scaleY": 32.0,
  "rotation": 0.0, "u0": 0.0, "v0": 0.0, "u1": 1.0, "v1": 1.0,
  "textureId": 1, "color": 0xFFFFFFFF, "layer": 0 }
```

**Text** (`render:text`)
```cpp
{ "x": 10.0, "y": 10.0, "text": "Hello",
  "fontSize": 16, "color": 0xFFFFFFFF, "layer": 100 }
```

**Tilemap** (`render:tilemap`)
```cpp
{ "x": 0.0, "y": 0.0, "width": 10, "height": 10,
  "tileW": 16, "tileH": 16, "textureId": 0,
  "tileData": "1,0,1,0,1,0,..." }  // comma-separated tile indices
```

## Next Task: Phase 9 - Choose One

### Option A: Layer Sorting
- Currently sprites are sorted by textureId only
- Add proper layer sorting (render back-to-front)
- Sort key: `(layer << 16) | textureId` for efficient batching

### Option B: Dynamic Texture Loading via IIO
- `render:texture:load` message to load textures at runtime
- Returns textureId that can be used in sprites
- Useful for dynamically loaded assets

### Option C: Particle System
- ParticlePass for particle effects
- GPU instanced particles with lifetime/velocity
- FramePacket already has ParticleInstance struct

### Option D: Camera Features
- Zoom and pan support (already in ViewInfo)
- Screen shake effects
- Smooth camera following

### Build & Test
```bash
cd build-bgfx
cmake --build . -j4
cd tests && ./test_23_bgfx_sprites_visual
```

## Notes
- Shaders are pre-compiled (embedded in .bin.h)
- shaderc at: `build-bgfx/_deps/bgfx-build/cmake/bgfx/shaderc`
- All passes reuse sprite shader (same instancing layout)
- TilemapPass: tile index 0 = empty, 1+ = actual tiles
- SpritePass: stable_sort by textureId preserves layer order within same texture
