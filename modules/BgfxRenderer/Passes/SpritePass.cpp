#include "SpritePass.h"
#include "../RHI/RHIDevice.h"
#include "../Frame/FramePacket.h"

namespace grove {

SpritePass::SpritePass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
}

void SpritePass::setup(rhi::IRHIDevice& device) {
    // Create quad vertex buffer (unit quad, instanced)
    // Layout must match shader: a_position (vec3) + a_color0 (vec4)
    // Note: Color is white (1,1,1,1) - actual color comes from instance data
    float quadVertices[] = {
        // pos.x, pos.y, pos.z,    r,    g,    b,    a
        0.0f, 0.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // bottom-left
        1.0f, 0.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // bottom-right
        1.0f, 1.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // top-right
        0.0f, 1.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // top-left
    };

    rhi::BufferDesc vbDesc;
    vbDesc.type = rhi::BufferDesc::Vertex;
    vbDesc.size = sizeof(quadVertices);
    vbDesc.data = quadVertices;
    vbDesc.dynamic = false;
    vbDesc.layout = rhi::BufferDesc::PosColor;  // Match shader: a_position + a_color0
    m_quadVB = device.createBuffer(vbDesc);

    // Create index buffer
    uint16_t quadIndices[] = {
        0, 1, 2,  // first triangle
        0, 2, 3   // second triangle
    };

    rhi::BufferDesc ibDesc;
    ibDesc.type = rhi::BufferDesc::Index;
    ibDesc.size = sizeof(quadIndices);
    ibDesc.data = quadIndices;
    ibDesc.dynamic = false;
    m_quadIB = device.createBuffer(ibDesc);

    // Create dynamic instance buffer
    rhi::BufferDesc instDesc;
    instDesc.type = rhi::BufferDesc::Instance;
    instDesc.size = MAX_SPRITES_PER_BATCH * sizeof(SpriteInstance);
    instDesc.data = nullptr;
    instDesc.dynamic = true;
    m_instanceBuffer = device.createBuffer(instDesc);

    // Create texture sampler uniform (must match shader: s_texColor)
    m_textureSampler = device.createUniform("s_texColor", 1);

    // Create default white 1x1 texture (used when no texture is bound)
    uint32_t whitePixel = 0xFFFFFFFF;  // RGBA white
    rhi::TextureDesc texDesc;
    texDesc.width = 1;
    texDesc.height = 1;
    texDesc.format = rhi::TextureDesc::RGBA8;
    texDesc.data = &whitePixel;
    texDesc.dataSize = sizeof(whitePixel);
    m_defaultTexture = device.createTexture(texDesc);
}

void SpritePass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_instanceBuffer);
    device.destroy(m_textureSampler);
    device.destroy(m_defaultTexture);
    // Note: m_shader is owned by ShaderManager, not destroyed here
}

void SpritePass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    if (frame.spriteCount == 0) {
        return;
    }

    // Set render state for sprites (alpha blending, no depth)
    rhi::RenderState state;
    state.blend = rhi::BlendMode::Alpha;
    state.cull = rhi::CullMode::None;
    state.depthTest = false;
    state.depthWrite = false;
    cmd.setState(state);

    // Process sprites in batches
    size_t remaining = frame.spriteCount;
    size_t offset = 0;

    while (remaining > 0) {
        size_t batchSize = (remaining > MAX_SPRITES_PER_BATCH)
            ? MAX_SPRITES_PER_BATCH : remaining;

        // Update instance buffer with sprite data
        // The SpriteInstance struct matches what we send to GPU
        const SpriteInstance* batchData = frame.sprites + offset;
        device.updateBuffer(m_instanceBuffer, batchData,
                           static_cast<uint32_t>(batchSize * sizeof(SpriteInstance)));

        cmd.setVertexBuffer(m_quadVB);
        cmd.setIndexBuffer(m_quadIB);
        cmd.setInstanceBuffer(m_instanceBuffer, 0, static_cast<uint32_t>(batchSize));

        // Bind texture (use active texture if set, otherwise default white)
        rhi::TextureHandle texToUse = m_activeTexture.isValid() ? m_activeTexture : m_defaultTexture;
        cmd.setTexture(0, texToUse, m_textureSampler);

        // Submit draw call
        cmd.drawInstanced(6, static_cast<uint32_t>(batchSize)); // 6 indices per quad
        cmd.submit(0, m_shader, 0);

        offset += batchSize;
        remaining -= batchSize;
    }
}

} // namespace grove
