#include "SpritePass.h"
#include "../RHI/RHIDevice.h"
#include "../Frame/FramePacket.h"
#include "../Resources/ResourceCache.h"
#include <algorithm>
#include <cstring>

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

    // Build sorted indices by layer (primary) and textureId (secondary) for batching
    m_sortedIndices.clear();
    m_sortedIndices.reserve(frame.spriteCount);
    for (size_t i = 0; i < frame.spriteCount; ++i) {
        m_sortedIndices.push_back(static_cast<uint32_t>(i));
    }

    // Sort by layer first (ascending: layer 0 = background, rendered first)
    // Then by textureId to batch sprites on the same layer
    std::sort(m_sortedIndices.begin(), m_sortedIndices.end(),
        [&frame](uint32_t a, uint32_t b) {
            const SpriteInstance& sa = frame.sprites[a];
            const SpriteInstance& sb = frame.sprites[b];
            if (sa.layer != sb.layer) {
                return sa.layer < sb.layer;
            }
            return sa.textureId < sb.textureId;
        });

    // Process sprites in batches by texture
    // Use transient buffers for proper multi-batch rendering
    uint32_t batchStart = 0;
    while (batchStart < frame.spriteCount) {
        // Find the end of current batch (same texture)
        uint16_t currentTexId = static_cast<uint16_t>(frame.sprites[m_sortedIndices[batchStart]].textureId);
        uint32_t batchEnd = batchStart + 1;

        while (batchEnd < frame.spriteCount) {
            uint16_t nextTexId = static_cast<uint16_t>(frame.sprites[m_sortedIndices[batchEnd]].textureId);
            if (nextTexId != currentTexId) {
                break;  // Texture changed, flush this batch
            }
            ++batchEnd;
        }

        uint32_t batchCount = batchEnd - batchStart;

        // Resolve texture handle for this batch
        rhi::TextureHandle batchTexture;
        if (currentTexId == 0 || !m_resourceCache) {
            batchTexture = m_activeTexture.isValid() ? m_activeTexture : m_defaultTexture;
        } else {
            batchTexture = m_resourceCache->getTextureById(currentTexId);
            if (!batchTexture.isValid()) {
                batchTexture = m_activeTexture.isValid() ? m_activeTexture : m_defaultTexture;
            }
        }

        // Allocate transient instance buffer for this batch
        rhi::TransientInstanceBuffer transientBuffer = device.allocTransientInstanceBuffer(batchCount);

        if (transientBuffer.isValid()) {
            // Copy sprite data to transient buffer
            SpriteInstance* dest = static_cast<SpriteInstance*>(transientBuffer.data);
            for (uint32_t i = 0; i < batchCount; ++i) {
                dest[i] = frame.sprites[m_sortedIndices[batchStart + i]];
            }

            // Re-set state for each batch to ensure clean state
            cmd.setState(state);

            // Set buffers and draw
            cmd.setVertexBuffer(m_quadVB);
            cmd.setIndexBuffer(m_quadIB);
            cmd.setTransientInstanceBuffer(transientBuffer, 0, batchCount);
            cmd.setTexture(0, batchTexture, m_textureSampler);
            cmd.drawInstanced(6, batchCount);
            cmd.submit(0, m_shader, 0);
        } else {
            // Fallback: use dynamic buffer (may have issues with multiple batches)
            // This should only happen if GPU runs out of transient memory
            std::vector<SpriteInstance> batchData;
            batchData.reserve(batchCount);
            for (uint32_t i = 0; i < batchCount; ++i) {
                batchData.push_back(frame.sprites[m_sortedIndices[batchStart + i]]);
            }
            device.updateBuffer(m_instanceBuffer, batchData.data(),
                               static_cast<uint32_t>(batchData.size() * sizeof(SpriteInstance)));
            cmd.setInstanceBuffer(m_instanceBuffer, 0, batchCount);
            flushBatch(device, cmd, batchTexture, batchCount);
        }

        batchStart = batchEnd;
    }
}

} // namespace grove
