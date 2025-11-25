#pragma once

#include <cstdint>
#include <cstddef>

namespace grove {

class FrameAllocator;

// ============================================================================
// Sprite Instance Data
// ============================================================================

struct SpriteInstance {
    float x, y;           // Position
    float scaleX, scaleY; // Scale
    float rotation;       // Radians
    float u0, v0, u1, v1; // UVs in atlas
    uint32_t color;       // RGBA packed
    uint16_t textureId;   // Index in texture array
    uint16_t layer;       // Z-order
};

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
