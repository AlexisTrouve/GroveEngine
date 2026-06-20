#pragma once

#include <cstdint>
#include <cstddef>

namespace grove {

class FrameAllocator;

// ============================================================================
// Sprite Instance Data
// ============================================================================

// GPU Instance data - must match shader layout (5 x vec4 = 80 bytes)
// i_data0: x, y, scaleX, scaleY
// i_data1: rotation, u0, v0, u1
// i_data2: v1, textureId (as float), layer (as float), padding
// i_data3: reserved
// i_data4: r, g, b, a (color as floats 0-1)
struct SpriteInstance {
    // i_data0
    float x, y;           // Position
    float scaleX, scaleY; // Scale
    // i_data1
    float rotation;       // Radians
    float u0, v0, u1;     // UV start + end x
    // i_data2
    float v1;             // UV end y
    float textureId;      // As float for GPU compatibility
    float layer;          // Z-order as float
    float padding0;
    // i_data3
    float reserved[4];    // Reserved for future use
    // i_data4
    float r, g, b, a;     // Color as floats (0-1)
};
static_assert(sizeof(SpriteInstance) == 80, "SpriteInstance must be 80 bytes for GPU instancing");

// ============================================================================
// Tilemap Chunk Data
// ============================================================================

struct TilemapChunk {
    float x, y;           // Chunk position
    uint16_t width, height;
    uint16_t tileWidth, tileHeight;
    uint16_t textureId;
    const uint16_t* tiles; // Tile indices in tileset
    size_t tileCount;
    // Per-tile fog/visibility (Slice fog): width*height bytes, 0 = hidden (drawn dark) .. 255 =
    // fully visible. nullptr = no fog (everything visible). Sampled mipped, so it dims correctly at
    // every zoom; revealed incrementally via the A4.2 partial-update path.
    const uint8_t* fog = nullptr;
    // Retained-mode identity (Slice A4). id == 0 -> ephemeral chunk (re-sent every frame, always
    // uploaded). id != 0 -> retained chunk (render:tilemap:add/update/remove by this id); the pass
    // caches its index texture by id and uploads only when `dirty` is set (the frame it was
    // added/updated), so a static retained chunk uploads exactly once.
    uint32_t id = 0;
    bool dirty = true;
    // Dirty sub-rectangle for PARTIAL retained updates (Slice A4.2). When dirty && dirtyW>0, only
    // [dirtyX, dirtyY, +dirtyW, +dirtyH] of the index texture is re-uploaded (fog reveal, terrain
    // edits) instead of the whole grid. dirtyW==0 (with dirty) = full-grid upload (add / full update).
    uint16_t dirtyX = 0, dirtyY = 0, dirtyW = 0, dirtyH = 0;
};

// ============================================================================
// Text Command Data
// ============================================================================

struct TextCommand {
    float x, y;
    const char* text;     // Null-terminated, allocated in FrameAllocator
    uint16_t fontId;
    uint16_t fontSize;
    uint32_t color;
    uint16_t layer;
};

// ============================================================================
// Particle Instance Data
// ============================================================================

struct ParticleInstance {
    float x, y;
    float vx, vy;
    float size;
    float life;           // 0-1, remaining time
    uint32_t color;
    uint16_t textureId;
};

// ============================================================================
// Debug Shape Data
// ============================================================================

struct DebugLine {
    float x1, y1, x2, y2;
    uint32_t color;
};

struct DebugRect {
    float x, y, w, h;
    uint32_t color;
    bool filled;
};

// ============================================================================
// Camera/View Info
// ============================================================================

struct ViewInfo {
    float viewMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};  // Identity matrix
    float projMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};  // Identity matrix
    float positionX = 0.0f, positionY = 0.0f;
    float zoom = 1.0f;
    float rotation = 0.0f;   // radians; rotates the view around the screen-centre pivot (0 = none)
    uint16_t viewportX = 0, viewportY = 0;
    uint16_t viewportW = 1280, viewportH = 720;
};

// ============================================================================
// Frame Packet - IMMUTABLE after construction
// ============================================================================

struct FramePacket {
    uint64_t frameNumber = 0;
    float deltaTime = 0.016f;

    // Collected data (read-only for passes)
    const SpriteInstance* sprites = nullptr;
    size_t spriteCount = 0;

    const TilemapChunk* tilemaps = nullptr;
    size_t tilemapCount = 0;

    const TextCommand* texts = nullptr;
    size_t textCount = 0;

    const ParticleInstance* particles = nullptr;
    size_t particleCount = 0;

    const DebugLine* debugLines = nullptr;
    size_t debugLineCount = 0;

    const DebugRect* debugRects = nullptr;
    size_t debugRectCount = 0;

    // HUD / screen-space buckets — sprites & texts published with space:"screen". Drawn on
    // hudView (a fixed screen-space transform), AFTER the world, so the HUD does NOT zoom or
    // pan with the world camera. Reuse the same SpriteInstance/TextCommand layout as world.
    const SpriteInstance* hudSprites = nullptr;
    size_t hudSpriteCount = 0;

    const TextCommand* hudTexts = nullptr;
    size_t hudTextCount = 0;

    // Main view (initialized to identity transforms)
    ViewInfo mainView = {};

    // HUD overlay view: screen-space ortho (1px = 1 unit, top-left origin), zoom always 1,
    // no pan. Independent of render:camera — it only tracks the viewport size.
    ViewInfo hudView = {};

    // Clear color (default dark gray)
    uint32_t clearColor = 0x303030FF;

    // Allocator for temporary pass data
    FrameAllocator* allocator = nullptr;
};

} // namespace grove
