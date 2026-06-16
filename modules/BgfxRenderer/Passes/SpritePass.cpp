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

    // Create default white 4x4 texture (restored to white)
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
    // World sprites on view 0 (zoomable world camera), then HUD sprites on view 1 (fixed
    // screen-space overlay configured by the renderer module from FramePacket::hudView).
    // Order matters: view 1 draws after view 0, so the HUD sits on top.
    renderSpriteSet(device, cmd, frame.sprites, frame.spriteCount, 0);
    renderSpriteSet(device, cmd, frame.hudSprites, frame.hudSpriteCount, 1);
}

void SpritePass::renderSpriteSet(rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd,
                                 const SpriteInstance* sprites, size_t count, rhi::ViewId viewId) {
    if (count == 0) return;

    // Prepare render state (will be set before each batch)
    rhi::RenderState state;
    state.blend = rhi::BlendMode::Alpha;
    state.cull = rhi::CullMode::None;
    state.depthTest = false;
    state.depthWrite = false;

    // Sort sprites by layer first (for correct draw order), then by texture (for batching)
    m_sortedIndices.clear();
    m_sortedIndices.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        m_sortedIndices.push_back(static_cast<uint32_t>(i));
    }
    std::sort(m_sortedIndices.begin(), m_sortedIndices.end(),
        [sprites](uint32_t a, uint32_t b) {
            // Sort by layer first, then by textureId for batching
            if (sprites[a].layer != sprites[b].layer) {
                return sprites[a].layer < sprites[b].layer;
            }
            return sprites[a].textureId < sprites[b].textureId;
        });

    // Batch sprites by texture
    std::vector<SpriteInstance> batchSprites;
    batchSprites.reserve(count);

    uint16_t currentTextureId = 0;
    bool firstBatch = true;

    static int spriteLogCount = 0;
    for (size_t i = 0; i < m_sortedIndices.size(); ++i) {
        uint32_t idx = m_sortedIndices[i];
        const SpriteInstance& sprite = sprites[idx];
        uint16_t spriteTexId = static_cast<uint16_t>(sprite.textureId);

        // Log first few textured sprites
        if (spriteLogCount < 10 && spriteTexId > 0) {
            spdlog::info("🎨 [SpritePass] Processing sprite #{}: textureId={}, pos=({:.1f},{:.1f}), scale={}x{}, layer={}",
                spriteLogCount++, spriteTexId, sprite.x, sprite.y, sprite.scaleX, sprite.scaleY, (int)sprite.layer);
        }

        // Start new batch if texture changes
        if (!firstBatch && spriteTexId != currentTextureId) {
            // Flush previous batch using TRANSIENT BUFFER (one per batch)
            uint32_t batchSize = static_cast<uint32_t>(batchSprites.size());
            rhi::TransientInstanceBuffer transientBuffer = device.allocTransientInstanceBuffer(batchSize);

            // CRITICAL: Set render state before EACH batch (consumed by submit)
            cmd.setState(state);

            // Get texture handle from ResourceCache
            rhi::TextureHandle texHandle = m_defaultTexture;
            if (m_resourceCache && currentTextureId > 0) {
                auto cachedTex = m_resourceCache->getTextureById(currentTextureId);
                if (cachedTex.isValid()) {
                    texHandle = cachedTex;
                    static int batchNum = 0;
                    spdlog::info("[Batch #{}] SpritePass flushing batch: textureId={}, handle={}, size={}",
                        batchNum++, currentTextureId, texHandle.id, batchSprites.size());
                }
            }

            if (transientBuffer.isValid()) {
                // Copy sprite data to transient buffer (frame-local, won't be overwritten)
                std::memcpy(transientBuffer.data, batchSprites.data(), batchSize * sizeof(SpriteInstance));

                cmd.setVertexBuffer(m_quadVB);
                cmd.setIndexBuffer(m_quadIB);
                cmd.setTransientInstanceBuffer(transientBuffer, 0, batchSize);
                cmd.setTexture(0, texHandle, m_textureSampler);
                cmd.drawInstanced(6, batchSize);
                cmd.submit(viewId, m_shader, 0);
            } else {
                // Fallback to dynamic buffer (single batch limitation - data will be overwritten!)
                device.updateBuffer(m_instanceBuffer, batchSprites.data(), batchSize * sizeof(SpriteInstance));

                cmd.setVertexBuffer(m_quadVB);
                cmd.setIndexBuffer(m_quadIB);
                cmd.setInstanceBuffer(m_instanceBuffer, 0, batchSize);
                cmd.setTexture(0, texHandle, m_textureSampler);
                cmd.drawInstanced(6, batchSize);
                cmd.submit(viewId, m_shader, 0);
            }

            // Start new batch
            batchSprites.clear();
        }

        batchSprites.push_back(sprite);
        currentTextureId = spriteTexId;
        firstBatch = false;
    }

    // Flush final batch
    if (!batchSprites.empty()) {
        // Use TRANSIENT BUFFER for final batch too
        uint32_t batchSize = static_cast<uint32_t>(batchSprites.size());
        rhi::TransientInstanceBuffer transientBuffer = device.allocTransientInstanceBuffer(batchSize);

        // CRITICAL: Set render state before EACH batch (consumed by submit)
        cmd.setState(state);

        // Get texture handle from ResourceCache
        rhi::TextureHandle texHandle = m_defaultTexture;
        if (m_resourceCache && currentTextureId > 0) {
            auto cachedTex = m_resourceCache->getTextureById(currentTextureId);
            if (cachedTex.isValid()) {
                texHandle = cachedTex;
                static int finalBatchNum = 0;
                spdlog::info("[Final Batch #{}] SpritePass flushing final batch: textureId={}, handle={}, size={}",
                    finalBatchNum++, currentTextureId, texHandle.id, batchSprites.size());
            } else {
                static bool warnLogged = false;
                if (!warnLogged) {
                    spdlog::warn("SpritePass: Texture ID {} not found in cache, using default", currentTextureId);
                    warnLogged = true;
                }
            }
        }

        if (transientBuffer.isValid()) {
            // Copy sprite data to transient buffer (frame-local, won't be overwritten)
            std::memcpy(transientBuffer.data, batchSprites.data(), batchSize * sizeof(SpriteInstance));

            cmd.setVertexBuffer(m_quadVB);
            cmd.setIndexBuffer(m_quadIB);
            cmd.setTransientInstanceBuffer(transientBuffer, 0, batchSize);
            cmd.setTexture(0, texHandle, m_textureSampler);
            cmd.drawInstanced(6, batchSize);
            cmd.submit(viewId, m_shader, 0);
        } else {
            // Fallback to dynamic buffer (single batch limitation - data will be overwritten!)
            device.updateBuffer(m_instanceBuffer, batchSprites.data(), batchSize * sizeof(SpriteInstance));

            cmd.setVertexBuffer(m_quadVB);
            cmd.setIndexBuffer(m_quadIB);
            cmd.setInstanceBuffer(m_instanceBuffer, 0, batchSize);
            cmd.setTexture(0, texHandle, m_textureSampler);
            cmd.drawInstanced(6, batchSize);
            cmd.submit(viewId, m_shader, 0);
        }
    }
}

} // namespace grove
