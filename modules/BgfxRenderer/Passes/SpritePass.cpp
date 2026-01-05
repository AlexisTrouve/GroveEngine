#include "SpritePass.h"
#include "../RHI/RHIDevice.h"
#include "../Frame/FramePacket.h"
#include "../Resources/ResourceCache.h"
#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

namespace grove {

SpritePass::SpritePass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
    m_sortedIndices.reserve(MAX_SPRITES_PER_BATCH);
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

    // Note: We no longer create a persistent instance buffer since we use transient buffers
    // But keep it for fallback if transient allocation fails
    rhi::BufferDesc instDesc;
    instDesc.type = rhi::BufferDesc::Instance;
    instDesc.size = MAX_SPRITES_PER_BATCH * sizeof(SpriteInstance);
    instDesc.data = nullptr;
    instDesc.dynamic = true;
    m_instanceBuffer = device.createBuffer(instDesc);

    // Create texture sampler uniform (must match shader: s_texColor)
    m_textureSampler = device.createUniform("s_texColor", 1);

    // Create default white 4x4 texture (used when no texture is bound)
    // Some drivers have issues with 1x1 textures
    uint32_t whitePixels[16];
    for (int i = 0; i < 16; ++i) whitePixels[i] = 0xFFFFFFFF;  // RGBA white
    rhi::TextureDesc texDesc;
    texDesc.width = 4;
    texDesc.height = 4;
    texDesc.format = rhi::TextureDesc::RGBA8;
    texDesc.data = whitePixels;
    texDesc.dataSize = sizeof(whitePixels);
    m_defaultTexture = device.createTexture(texDesc);

    spdlog::info("SpritePass: defaultTexture valid={} (4x4 white)", m_defaultTexture.isValid());
}

void SpritePass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_instanceBuffer);
    device.destroy(m_textureSampler);
    device.destroy(m_defaultTexture);
    // Note: m_shader is owned by ShaderManager, not destroyed here
}

void SpritePass::flushBatch(rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd,
                            rhi::TextureHandle texture, uint32_t count) {
    if (count == 0) return;

    cmd.setVertexBuffer(m_quadVB);
    cmd.setIndexBuffer(m_quadIB);
    // Note: Instance buffer should be set before calling this
    cmd.setTexture(0, texture, m_textureSampler);
    cmd.drawInstanced(6, count);
    cmd.submit(0, m_shader, 0);
}

void SpritePass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    if (frame.spriteCount == 0) return;

    // Set render state ONCE (like TextPass does)
    rhi::RenderState state;
    state.blend = rhi::BlendMode::Alpha;
    state.cull = rhi::CullMode::None;
    state.depthTest = false;
    state.depthWrite = false;
    cmd.setState(state);

    // Sort sprites by layer for correct draw order
    m_sortedIndices.clear();
    m_sortedIndices.reserve(frame.spriteCount);
    for (size_t i = 0; i < frame.spriteCount; ++i) {
        m_sortedIndices.push_back(static_cast<uint32_t>(i));
    }
    std::sort(m_sortedIndices.begin(), m_sortedIndices.end(),
        [&frame](uint32_t a, uint32_t b) {
            return frame.sprites[a].layer < frame.sprites[b].layer;
        });

    // Copy sorted sprites to temporary buffer (like TextPass does with glyphs)
    std::vector<SpriteInstance> sortedSprites;
    sortedSprites.reserve(frame.spriteCount);
    for (uint32_t idx : m_sortedIndices) {
        sortedSprites.push_back(frame.sprites[idx]);
    }

    // Update dynamic instance buffer with ALL sprites (like TextPass)
    device.updateBuffer(m_instanceBuffer, sortedSprites.data(),
                       static_cast<uint32_t>(sortedSprites.size() * sizeof(SpriteInstance)));

    // Set buffers and draw ALL sprites in ONE call (like TextPass)
    cmd.setVertexBuffer(m_quadVB);
    cmd.setIndexBuffer(m_quadIB);
    cmd.setInstanceBuffer(m_instanceBuffer, 0, static_cast<uint32_t>(sortedSprites.size()));
    cmd.setTexture(0, m_defaultTexture, m_textureSampler);
    cmd.drawInstanced(6, static_cast<uint32_t>(sortedSprites.size()));
    cmd.submit(0, m_shader, 0);
}

} // namespace grove
