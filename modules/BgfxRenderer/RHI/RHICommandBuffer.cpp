#include "RHICommandBuffer.h"

namespace grove::rhi {

void RHICommandBuffer::setState(const RenderState& state) {
    Command cmd;
    cmd.type = CommandType::SetState;
    cmd.setState.state = state;
    m_commands.push_back(cmd);
}

void RHICommandBuffer::setTexture(uint8_t slot, TextureHandle tex, UniformHandle sampler) {
    Command cmd;
    cmd.type = CommandType::SetTexture;
    cmd.setTexture.slot = slot;
    cmd.setTexture.texture = tex;
    cmd.setTexture.sampler = sampler;
    m_commands.push_back(cmd);
}

void RHICommandBuffer::setUniform(UniformHandle uniform, const float* data, uint8_t numVec4s) {
    Command cmd;
    cmd.type = CommandType::SetUniform;
    cmd.setUniform.uniform = uniform;
    cmd.setUniform.numVec4s = numVec4s;
    std::memcpy(cmd.setUniform.data, data, static_cast<size_t>(numVec4s) * 16);
    m_commands.push_back(cmd);
}

void RHICommandBuffer::setVertexBuffer(BufferHandle buffer, uint32_t offset) {
    Command cmd;
    cmd.type = CommandType::SetVertexBuffer;
    cmd.setVertexBuffer.buffer = buffer;
    cmd.setVertexBuffer.offset = offset;
    m_commands.push_back(cmd);
}

void RHICommandBuffer::setIndexBuffer(BufferHandle buffer, uint32_t offset, bool is32Bit) {
    Command cmd;
    cmd.type = CommandType::SetIndexBuffer;
    cmd.setIndexBuffer.buffer = buffer;
    cmd.setIndexBuffer.offset = offset;
    cmd.setIndexBuffer.is32Bit = is32Bit;
    m_commands.push_back(cmd);
}

void RHICommandBuffer::setInstanceBuffer(BufferHandle buffer, uint32_t start, uint32_t count) {
    Command cmd;
    cmd.type = CommandType::SetInstanceBuffer;
    cmd.setInstanceBuffer.buffer = buffer;
    cmd.setInstanceBuffer.start = start;
    cmd.setInstanceBuffer.count = count;
    m_commands.push_back(cmd);
}

void RHICommandBuffer::setTransientInstanceBuffer(const TransientInstanceBuffer& buffer, uint32_t start, uint32_t count) {
    Command cmd;
    cmd.type = CommandType::SetTransientInstanceBuffer;
    cmd.setTransientInstanceBuffer.poolIndex = buffer.poolIndex;
    cmd.setTransientInstanceBuffer.start = start;
    cmd.setTransientInstanceBuffer.count = count;
    m_commands.push_back(cmd);
}

void RHICommandBuffer::setScissor(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    Command cmd;
    cmd.type = CommandType::SetScissor;
    cmd.setScissor.x = x;
    cmd.setScissor.y = y;
    cmd.setScissor.w = w;
    cmd.setScissor.h = h;
    m_commands.push_back(cmd);
}

void RHICommandBuffer::draw(uint32_t vertexCount, uint32_t startVertex) {
    Command cmd;
    cmd.type = CommandType::Draw;
    cmd.draw.vertexCount = vertexCount;
    cmd.draw.startVertex = startVertex;
    m_commands.push_back(cmd);
}

void RHICommandBuffer::drawIndexed(uint32_t indexCount, uint32_t startIndex) {
    Command cmd;
    cmd.type = CommandType::DrawIndexed;
    cmd.drawIndexed.indexCount = indexCount;
    cmd.drawIndexed.startIndex = startIndex;
    m_commands.push_back(cmd);
}

void RHICommandBuffer::drawInstanced(uint32_t indexCount, uint32_t instanceCount) {
    Command cmd;
    cmd.type = CommandType::DrawInstanced;
    cmd.drawInstanced.indexCount = indexCount;
    cmd.drawInstanced.instanceCount = instanceCount;
    m_commands.push_back(cmd);
}

void RHICommandBuffer::submit(ViewId view, ShaderHandle shader, uint32_t depth) {
    Command cmd;
    cmd.type = CommandType::Submit;
    cmd.submit.view = view;
    cmd.submit.shader = shader;
    cmd.submit.depth = depth;
    m_commands.push_back(cmd);
}

} // namespace grove::rhi
