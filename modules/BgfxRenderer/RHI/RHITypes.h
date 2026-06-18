#pragma once

#include <cstdint>

namespace grove::rhi {

// ============================================================================
// Typed Handles - Never expose bgfx:: outside BgfxDevice.cpp
// ============================================================================

struct TextureHandle {
    uint16_t id = UINT16_MAX;
    bool isValid() const { return id != UINT16_MAX; }
};

struct BufferHandle {
    uint16_t id = UINT16_MAX;
    bool isValid() const { return id != UINT16_MAX; }
};

struct ShaderHandle {
    uint16_t id = UINT16_MAX;
    bool isValid() const { return id != UINT16_MAX; }
};

struct UniformHandle {
    uint16_t id = UINT16_MAX;
    bool isValid() const { return id != UINT16_MAX; }
};

struct FramebufferHandle {
    uint16_t id = UINT16_MAX;
    bool isValid() const { return id != UINT16_MAX; }
};

using ViewId = uint16_t;

// ============================================================================
// Transient Instance Buffer - Frame-local allocation for multi-batch rendering
// ============================================================================

struct TransientInstanceBuffer {
    void* data = nullptr;      // CPU-side pointer for writing data
    uint32_t size = 0;         // Size in bytes
    uint32_t count = 0;        // Number of instances
    uint16_t stride = 0;       // Bytes per instance
    uint16_t poolIndex = UINT16_MAX;  // Index in device's transient pool
    bool isValid() const { return data != nullptr && poolIndex != UINT16_MAX; }
};

// ============================================================================
// Render States
// ============================================================================

enum class BlendMode : uint8_t {
    None,
    Alpha,
    Additive,
    Multiply
};

enum class CullMode : uint8_t {
    None,
    CW,
    CCW
};

enum class PrimitiveType : uint8_t {
    Triangles,
    Lines,
    Points
};

struct RenderState {
    BlendMode blend = BlendMode::Alpha;
    CullMode cull = CullMode::None;
    PrimitiveType primitive = PrimitiveType::Triangles;
    bool depthTest = false;
    bool depthWrite = false;
};

// ============================================================================
// Vertex Layout
// ============================================================================

struct VertexLayout {
    enum Attrib : uint8_t {
        Position,   // float3
        TexCoord0,  // float2
        Color0,     // uint32 RGBA
        Normal,     // float3
        Count
    };
    uint32_t stride = 0;
    uint16_t offsets[Attrib::Count] = {};
    bool has[Attrib::Count] = {};
};

// ============================================================================
// Resource Descriptors
// ============================================================================

struct TextureDesc {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t mipLevels = 1;
    // R16UI = 16-bit unsigned INTEGER, non-normalized. The GPU tilemap uses it as the tile
    // INDEX texture (1 texel = 1 tile id) read by texelFetch. Appended last so the existing
    // ordinals (RGBA8..DXT5) — which BgfxDevice's switch relies on — never shift.
    enum Format : uint8_t { RGBA8, RGB8, R8, DXT1, DXT5, R16UI } format = RGBA8;
    // Sampler control. The index texture needs Point+Clamp so a texelFetch never blends or
    // wraps tile ids (the tile-boundary off-by-one). Defaults (Linear/Repeat) reproduce the
    // previous BGFX_SAMPLER_NONE behavior bit-for-bit — existing textures are unaffected.
    enum Filter : uint8_t { Linear, Point } filter = Linear;
    enum Wrap   : uint8_t { Repeat, Clamp } wrap   = Repeat;
    const void* data = nullptr;
    uint32_t dataSize = 0;
};

struct BufferDesc {
    uint32_t size = 0;
    const void* data = nullptr;
    bool dynamic = false;
    enum Type : uint8_t { Vertex, Index, Instance } type = Vertex;
    // For Vertex buffers, specify the layout type
    enum Layout : uint8_t {
        Raw,           // Raw bytes (no layout - legacy)
        PosColor,      // vec3 position + vec4 color (for sprites)
        InstanceData   // Instance buffer (36 bytes per instance)
    } layout = Raw;
};

struct ShaderDesc {
    const void* vsData = nullptr;
    uint32_t vsSize = 0;
    const void* fsData = nullptr;
    uint32_t fsSize = 0;
};

} // namespace grove::rhi
