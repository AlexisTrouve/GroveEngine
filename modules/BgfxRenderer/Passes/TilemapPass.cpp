#include "TilemapPass.h"
#include "../RHI/RHIDevice.h"
#include "../Frame/FramePacket.h"
#include "../Resources/ResourceCache.h"
#include "../Scene/Camera.h"
#include <cmath>

namespace grove {

TilemapPass::TilemapPass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
    m_tileInstances.reserve(MAX_TILES_PER_BATCH);
}

void TilemapPass::setup(rhi::IRHIDevice& device) {
    // Create quad vertex buffer (unit quad, instanced) - same as SpritePass
    float quadVertices[] = {
        // pos.x, pos.y, pos.z,    r,    g,    b,    a
        0.0f, 0.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,
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

    // Create dynamic instance buffer
    rhi::BufferDesc instDesc;
    instDesc.type = rhi::BufferDesc::Instance;
    instDesc.size = MAX_TILES_PER_BATCH * sizeof(SpriteInstance);
    instDesc.data = nullptr;
    instDesc.dynamic = true;
    m_instanceBuffer = device.createBuffer(instDesc);

    // Create texture sampler uniform
    m_textureSampler = device.createUniform("s_texColor", 1);

    // Create default white texture
    uint32_t whitePixel = 0xFFFFFFFF;
    rhi::TextureDesc texDesc;
    texDesc.width = 1;
    texDesc.height = 1;
    texDesc.format = rhi::TextureDesc::RGBA8;
    texDesc.data = &whitePixel;
    texDesc.dataSize = sizeof(whitePixel);
    m_defaultTexture = device.createTexture(texDesc);
}

void TilemapPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_instanceBuffer);
    device.destroy(m_textureSampler);
    device.destroy(m_defaultTexture);
}

void TilemapPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    if (frame.tilemapCount == 0) {
        return;
    }

    // Set render state for tilemaps (alpha blending, no depth)
    rhi::RenderState state;
    state.blend = rhi::BlendMode::Alpha;
    state.cull = rhi::CullMode::None;
    state.depthTest = false;
    state.depthWrite = false;
    cmd.setState(state);

    // Visible world bounds for per-tile culling (big maps: only a screenful of tiles is built).
    const camera::CameraView view{frame.mainView.positionX, frame.mainView.positionY,
                                  frame.mainView.zoom,
                                  static_cast<float>(frame.mainView.viewportW),
                                  static_cast<float>(frame.mainView.viewportH)};
    const camera::WorldBounds bounds = camera::visibleWorldBounds(view);

    // Process each tilemap chunk
    for (size_t i = 0; i < frame.tilemapCount; ++i) {
        const TilemapChunk& chunk = frame.tilemaps[i];

        if (!chunk.tiles || chunk.tileCount == 0) {
            continue;
        }

        // Get tileset texture
        rhi::TextureHandle tileset;
        if (chunk.textureId > 0 && m_resourceCache) {
            tileset = m_resourceCache->getTextureById(chunk.textureId);
        }
        if (!tileset.isValid()) {
            tileset = m_defaultTileset.isValid() ? m_defaultTileset : m_defaultTexture;
        }

        // Calculate UV size per tile in tileset
        float tileU = 1.0f / m_tilesPerRow;
        float tileV = 1.0f / m_tilesPerCol;

        m_tileInstances.clear();

        // Iterate ONLY the visible tile window. The chunk is a regular grid, so the on-screen
        // tile indices follow directly from the camera bounds — this is O(visible tiles), NOT
        // O(map). Critical at tens of thousands of tiles: a per-tile cull loop would still visit
        // every tile every frame; computing the index window skips that entirely.
        const float tw = static_cast<float>(chunk.tileWidth);
        const float th = static_cast<float>(chunk.tileHeight);
        int minTX = static_cast<int>(std::floor((bounds.minX - chunk.x) / tw));
        int maxTX = static_cast<int>(std::floor((bounds.maxX - chunk.x) / tw));
        int minTY = static_cast<int>(std::floor((bounds.minY - chunk.y) / th));
        int maxTY = static_cast<int>(std::floor((bounds.maxY - chunk.y) / th));
        if (minTX < 0) minTX = 0;
        if (minTY < 0) minTY = 0;
        if (maxTX > static_cast<int>(chunk.width) - 1)  maxTX = static_cast<int>(chunk.width) - 1;
        if (maxTY > static_cast<int>(chunk.height) - 1) maxTY = static_cast<int>(chunk.height) - 1;

        // Generate sprite instances for each VISIBLE tile.
        for (int tileY = minTY; tileY <= maxTY; ++tileY) {
        for (int tileX = minTX; tileX <= maxTX; ++tileX) {
            const size_t t = static_cast<size_t>(tileY) * chunk.width + static_cast<size_t>(tileX);
            if (t >= chunk.tileCount) continue;
            uint16_t tileIndex = chunk.tiles[t];
            if (tileIndex == 0) continue;   // empty tile

            const float worldX = chunk.x + (static_cast<float>(tileX) + 0.5f) * tw;
            const float worldY = chunk.y + (static_cast<float>(tileY) + 0.5f) * th;

            // Calculate UV coords from tile index
            // tileIndex-1 because 0 is empty, actual tiles start at 1
            uint16_t actualIndex = tileIndex - 1;
            uint16_t tileCol = actualIndex % m_tilesPerRow;
            uint16_t tileRow = actualIndex / m_tilesPerRow;

            float u0 = tileCol * tileU;
            float v0 = tileRow * tileV;
            float u1 = u0 + tileU;
            float v1 = v0 + tileV;

            // Create sprite instance for this tile
            SpriteInstance inst;
            inst.x = worldX;
            inst.y = worldY;
            inst.scaleX = static_cast<float>(chunk.tileWidth);
            inst.scaleY = static_cast<float>(chunk.tileHeight);
            inst.rotation = 0.0f;
            inst.u0 = u0;
            inst.v0 = v0;
            inst.u1 = u1;
            inst.v1 = v1;
            inst.textureId = 0.0f;  // Using tileset bound directly
            inst.layer = -100.0f;   // Tilemaps render behind sprites
            inst.padding0 = 0.0f;
            inst.reserved[0] = 0.0f;
            inst.reserved[1] = 0.0f;
            inst.reserved[2] = 0.0f;
            inst.reserved[3] = 0.0f;
            inst.r = 1.0f;
            inst.g = 1.0f;
            inst.b = 1.0f;
            inst.a = 1.0f;

            m_tileInstances.push_back(inst);

            // Flush batch if full
            if (m_tileInstances.size() >= MAX_TILES_PER_BATCH) {
                device.updateBuffer(m_instanceBuffer, m_tileInstances.data(),
                                   static_cast<uint32_t>(m_tileInstances.size() * sizeof(SpriteInstance)));

                cmd.setVertexBuffer(m_quadVB);
                cmd.setIndexBuffer(m_quadIB);
                cmd.setInstanceBuffer(m_instanceBuffer, 0, static_cast<uint32_t>(m_tileInstances.size()));
                cmd.setTexture(0, tileset, m_textureSampler);
                cmd.drawInstanced(6, static_cast<uint32_t>(m_tileInstances.size()));
                cmd.submit(0, m_shader, 0);

                m_tileInstances.clear();
            }
        }
        }

        // Flush remaining tiles for this chunk
        if (!m_tileInstances.empty()) {
            device.updateBuffer(m_instanceBuffer, m_tileInstances.data(),
                               static_cast<uint32_t>(m_tileInstances.size() * sizeof(SpriteInstance)));

            cmd.setVertexBuffer(m_quadVB);
            cmd.setIndexBuffer(m_quadIB);
            cmd.setInstanceBuffer(m_instanceBuffer, 0, static_cast<uint32_t>(m_tileInstances.size()));
            cmd.setTexture(0, tileset, m_textureSampler);
            cmd.drawInstanced(6, static_cast<uint32_t>(m_tileInstances.size()));
            cmd.submit(0, m_shader, 0);

            m_tileInstances.clear();
        }
    }
}

} // namespace grove
