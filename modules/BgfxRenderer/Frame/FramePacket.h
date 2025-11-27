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
    float viewMatrix[16];
    float projMatrix[16];
    float positionX, positionY;
    float zoom;
    uint16_t viewportX, viewportY;
    uint16_t viewportW, viewportH;
};

// ============================================================================
// Frame Packet - IMMUTABLE after construction
// ============================================================================

struct FramePacket {
    uint64_t frameNumber;
    float deltaTime;

    // Collected data (read-only for passes)
    const SpriteInstance* sprites;
    size_t spriteCount;

    const TilemapChunk* tilemaps;
    size_t tilemapCount;

    const TextCommand* texts;
    size_t textCount;

    const ParticleInstance* particles;
    size_t particleCount;

    const DebugLine* debugLines;
    size_t debugLineCount;

    const DebugRect* debugRects;
    size_t debugRectCount;

    // Main view
    ViewInfo mainView;

    // Clear color
    uint32_t clearColor;

    // Allocator for temporary pass data
    FrameAllocator* allocator;
};

} // namespace grove
