#include "ParticlePass.h"
#include "../RHI/RHIDevice.h"
#include "../Resources/ResourceCache.h"
#include <cstring>

namespace grove {

ParticlePass::ParticlePass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
    m_particleInstances.reserve(MAX_PARTICLES_PER_BATCH);
}

void ParticlePass::setup(rhi::IRHIDevice& device) {
    // Create quad vertex buffer (unit quad centered at origin for particles)
    float quadVertices[] = {
        // pos.x, pos.y, pos.z,    r,    g,    b,    a
        -0.5f, -0.5f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // bottom-left
         0.5f, -0.5f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // bottom-right
         0.5f,  0.5f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // top-right
        -0.5f,  0.5f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // top-left
    };

    rhi::BufferDesc vbDesc;
    vbDesc.type = rhi::BufferDesc::Vertex;
    vbDesc.size = sizeof(quadVertices);
    vbDesc.data = quadVertices;
    vbDesc.dynamic = false;
    vbDesc.layout = rhi::BufferDesc::PosColor;
    m_quadVB = device.createBuffer(vbDesc);

    // Create index buffer
    uint16_t quadIndices[] = {
        0, 1, 2,
        0, 2, 3
    };

    rhi::BufferDesc ibDesc;
    ibDesc.type = rhi::BufferDesc::Index;
    ibDesc.size = sizeof(quadIndices);
    ibDesc.data = quadIndices;
    ibDesc.dynamic = false;
    m_quadIB = device.createBuffer(ibDesc);

    // Fallback dynamic instance buffer (only used if transient allocation fails)
    rhi::BufferDesc instDesc;
    instDesc.type = rhi::BufferDesc::Instance;
    instDesc.size = MAX_PARTICLES_PER_BATCH * sizeof(SpriteInstance);
    instDesc.data = nullptr;
    instDesc.dynamic = true;
    m_instanceBuffer = device.createBuffer(instDesc);

    // Create texture sampler uniform
    m_textureSampler = device.createUniform("s_texColor", 1);

    // Create default white texture for untextured particles
    uint32_t whitePixel = 0xFFFFFFFF;
    rhi::TextureDesc texDesc;
    texDesc.width = 1;
    texDesc.height = 1;
    texDesc.format = rhi::TextureDesc::RGBA8;
    texDesc.data = &whitePixel;
    texDesc.dataSize = sizeof(whitePixel);
    m_defaultTexture = device.createTexture(texDesc);
}

void ParticlePass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_instanceBuffer);
    device.destroy(m_textureSampler);
    device.destroy(m_defaultTexture);
}

void ParticlePass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    if (frame.particleCount == 0) {
        return;
    }

    // Set render state for particles
    rhi::RenderState state;
    state.blend = m_additiveBlending ? rhi::BlendMode::Additive : rhi::BlendMode::Alpha;
    state.cull = rhi::CullMode::None;
    state.depthTest = false;
    state.depthWrite = false;
    cmd.setState(state);

    m_particleInstances.clear();

    // Current texture for batching
    uint16_t currentTextureId = UINT16_MAX;
    rhi::TextureHandle currentTexture = m_defaultTexture;

    for (size_t i = 0; i < frame.particleCount; ++i) {
        const ParticleInstance& particle = frame.particles[i];

        // Skip dead particles
        if (particle.life <= 0.0f) {
            continue;
        }

        // Check if texture changed - flush batch
        if (particle.textureId != currentTextureId && !m_particleInstances.empty()) {
            flushBatch(device, cmd, currentTexture, static_cast<uint32_t>(m_particleInstances.size()));
            m_particleInstances.clear();
        }

        // Update current texture if needed
        if (particle.textureId != currentTextureId) {
            currentTextureId = particle.textureId;
            if (particle.textureId > 0 && m_resourceCache) {
                currentTexture = m_resourceCache->getTextureById(particle.textureId);
            }
            if (!currentTexture.isValid()) {
                currentTexture = m_defaultTexture;
            }
        }

        // Convert ParticleInstance to GPU-aligned SpriteInstance
        SpriteInstance inst;

        // Position (particle position is center)
        inst.x = particle.x;
        inst.y = particle.y;

        // Scale by particle size
        inst.scaleX = particle.size;
        inst.scaleY = particle.size;

        // No rotation (could add spin later)
        inst.rotation = 0.0f;

        // Full UV (use entire texture)
        inst.u0 = 0.0f;
        inst.v0 = 0.0f;
        inst.u1 = 1.0f;
        inst.v1 = 1.0f;

        // Texture ID
        inst.textureId = static_cast<float>(particle.textureId);

        // Layer (particles render at high layer by default)
        inst.layer = 200.0f;

        // Padding/reserved
        inst.padding0 = 0.0f;
        inst.reserved[0] = 0.0f;
        inst.reserved[1] = 0.0f;
        inst.reserved[2] = 0.0f;
        inst.reserved[3] = 0.0f;

        // Color with life-based alpha fade
        uint32_t color = particle.color;
        inst.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
        inst.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
        inst.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
        // Alpha fades with life
        float baseAlpha = static_cast<float>(color & 0xFF) / 255.0f;
        inst.a = baseAlpha * particle.life;

        m_particleInstances.push_back(inst);

        // Flush if batch is full
        if (m_particleInstances.size() >= MAX_PARTICLES_PER_BATCH) {
            flushBatch(device, cmd, currentTexture, static_cast<uint32_t>(m_particleInstances.size()));
            m_particleInstances.clear();
        }
    }

    // Flush remaining particles
    if (!m_particleInstances.empty()) {
        flushBatch(device, cmd, currentTexture, static_cast<uint32_t>(m_particleInstances.size()));
    }
}

void ParticlePass::flushBatch(rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd,
                               rhi::TextureHandle texture, uint32_t count) {
    if (count == 0) return;

    // Try to use transient buffer for multi-batch support
    rhi::TransientInstanceBuffer transientBuffer = device.allocTransientInstanceBuffer(count);

    if (transientBuffer.isValid()) {
        // Copy particle data to transient buffer
        std::memcpy(transientBuffer.data, m_particleInstances.data(), count * sizeof(SpriteInstance));

        // Set buffers
        cmd.setVertexBuffer(m_quadVB);
        cmd.setIndexBuffer(m_quadIB);
        cmd.setTransientInstanceBuffer(transientBuffer, 0, count);
        cmd.setTexture(0, texture, m_textureSampler);
        cmd.drawInstanced(6, count);
        cmd.submit(0, m_shader, 0);
    } else {
        // Fallback to dynamic buffer (single batch per frame limitation)
        device.updateBuffer(m_instanceBuffer, m_particleInstances.data(),
                            count * sizeof(SpriteInstance));

        cmd.setVertexBuffer(m_quadVB);
        cmd.setIndexBuffer(m_quadIB);
        cmd.setInstanceBuffer(m_instanceBuffer, 0, count);
        cmd.setTexture(0, texture, m_textureSampler);
        cmd.drawInstanced(6, count);
        cmd.submit(0, m_shader, 0);
    }
}

} // namespace grove
