#pragma once

#include "RHITypes.h"
#include <vector>
#include <cstring>

namespace grove::rhi {

// ============================================================================
// Command Types - POD for serialization
// ============================================================================

enum class CommandType : uint8_t {
    SetState,
    SetTexture,
    SetUniform,
    SetVertexBuffer,
    SetIndexBuffer,
    SetInstanceBuffer,
    SetTransientInstanceBuffer,  // For frame-local multi-batch rendering
    SetScissor,
    Draw,
    DrawIndexed,
    DrawInstanced,
    Submit
};

struct Command {
    CommandType type;
    union {
        struct { RenderState state; } setState;
        struct { uint8_t slot; TextureHandle texture; UniformHandle sampler; } setTexture;
        struct { UniformHandle uniform; float data[16]; uint8_t numVec4s; } setUniform;
        struct { BufferHandle buffer; uint32_t offset; } setVertexBuffer;
        struct { BufferHandle buffer; uint32_t offset; bool is32Bit; } setIndexBuffer;
        struct { BufferHandle buffer; uint32_t start; uint32_t count; } setInstanceBuffer;
        struct { uint16_t poolIndex; uint32_t start; uint32_t count; } setTransientInstanceBuffer;
        struct { uint16_t x, y, w, h; } setScissor;
        struct { uint32_t vertexCount; uint32_t startVertex; } draw;
        struct { uint32_t indexCount; uint32_t startIndex; } drawIndexed;
        struct { uint32_t indexCount; uint32_t instanceCount; } drawInstanced;
        struct { ViewId view; ShaderHandle shader; uint32_t depth; } submit;
    };

    // Default constructor required because union contains non-trivial types
    Command() : type(CommandType::SetState) { std::memset(&setState, 0, sizeof(setState)); }
    ~Command() = default;
    Command(const Command&) = default;
    Command& operator=(const Command&) = default;
};

// ============================================================================
// Command Buffer - One per thread, write-only during recording
// ============================================================================

class RHICommandBuffer {
public:
    RHICommandBuffer() = default;

    // Non-copyable, movable
    RHICommandBuffer(const RHICommandBuffer&) = delete;
    RHICommandBuffer& operator=(const RHICommandBuffer&) = delete;
    RHICommandBuffer(RHICommandBuffer&&) = default;
    RHICommandBuffer& operator=(RHICommandBuffer&&) = default;

    // Command recording
    void setState(const RenderState& state);
    void setTexture(uint8_t slot, TextureHandle tex, UniformHandle sampler);
    void setUniform(UniformHandle uniform, const float* data, uint8_t numVec4s);
    void setVertexBuffer(BufferHandle buffer, uint32_t offset = 0);
    void setIndexBuffer(BufferHandle buffer, uint32_t offset = 0, bool is32Bit = false);
    void setInstanceBuffer(BufferHandle buffer, uint32_t start, uint32_t count);
    void setTransientInstanceBuffer(const TransientInstanceBuffer& buffer, uint32_t start, uint32_t count);
    void setScissor(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void draw(uint32_t vertexCount, uint32_t startVertex = 0);
    void drawIndexed(uint32_t indexCount, uint32_t startIndex = 0);
    void drawInstanced(uint32_t indexCount, uint32_t instanceCount);
    void submit(ViewId view, ShaderHandle shader, uint32_t depth = 0);

    // Read-only access for execution
    const std::vector<Command>& getCommands() const { return m_commands; }

    void clear() { m_commands.clear(); }
    size_t size() const { return m_commands.size(); }

private:
    std::vector<Command> m_commands;
};

} // namespace grove::rhi
