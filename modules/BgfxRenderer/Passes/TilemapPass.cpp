#include "TilemapPass.h"
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"
#include "../Frame/FramePacket.h"
#include "../Resources/ResourceCache.h"
#include "../Scene/Camera.h"

namespace grove {

// ============================================================================
// GPU tilemap pass (Slice A2). The tile grid lives in a GPU R16UI INDEX texture; one quad is drawn
// per chunk and the fragment shader resolves each pixel's tile via texelFetch. Cost is independent
// of tile count (1 draw/chunk) — replaces the old per-tile SpriteInstance generation entirely.
// ============================================================================

TilemapPass::TilemapPass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
}

void TilemapPass::setup(rhi::IRHIDevice& device) {
    // Unit quad (0..1). The tilemap vertex shader scales it to each chunk's pixel rectangle and
    // places it at the chunk's world origin; the quad's color channel is unused by vs_tilemap.
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

    uint16_t quadIndices[] = { 0, 1, 2, 0, 2, 3 };
    rhi::BufferDesc ibDesc;
    ibDesc.type = rhi::BufferDesc::Index;
    ibDesc.size = sizeof(quadIndices);
    ibDesc.data = quadIndices;
    ibDesc.dynamic = false;
    m_quadIB = device.createBuffer(ibDesc);

    // Shader uniforms + samplers.
    m_paramsUniform = device.createUniform("u_tilemapParams", 1);
    m_gridUniform   = device.createUniform("u_tilemapGrid", 1);
    m_indexSampler  = device.createUniform("s_index", 1);
    m_atlasSampler  = device.createUniform("s_atlas", 1);

    // 1x1 white fallback atlas (used when a chunk has no valid tileset bound).
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
    device.destroy(m_paramsUniform);
    device.destroy(m_gridUniform);
    device.destroy(m_indexSampler);
    device.destroy(m_atlasSampler);
    device.destroy(m_defaultTexture);
    for (auto& it : m_indexTextures) {
        if (it.handle.isValid()) device.destroy(it.handle);
    }
    m_indexTextures.clear();
}

void TilemapPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    if (frame.tilemapCount == 0) {
        return;
    }

    // Render state for tilemaps: alpha blend, no depth. Emitted PER chunk below — bgfx consumes the
    // state at each submit, so setting it once would only cover the first chunk.
    rhi::RenderState state;
    state.blend = rhi::BlendMode::Alpha;
    state.cull = rhi::CullMode::None;
    state.depthTest = false;
    state.depthWrite = false;

    // Visible world bounds for chunk-level culling: a chunk whose AABB is fully off-screen is
    // skipped entirely (no texture upload, no draw). Per-PIXEL tile work happens on the GPU.
    const camera::CameraView view{frame.mainView.positionX, frame.mainView.positionY,
                                  frame.mainView.zoom,
                                  static_cast<float>(frame.mainView.viewportW),
                                  static_cast<float>(frame.mainView.viewportH)};
    const camera::WorldBounds bounds = camera::visibleWorldBounds(view);

    for (size_t i = 0; i < frame.tilemapCount; ++i) {
        const TilemapChunk& chunk = frame.tilemaps[i];

        if (!chunk.tiles || chunk.tileCount == 0 || chunk.width == 0 || chunk.height == 0) {
            continue;
        }

        const float tw = static_cast<float>(chunk.tileWidth);
        const float th = static_cast<float>(chunk.tileHeight);
        const float chunkW = static_cast<float>(chunk.width) * tw;
        const float chunkH = static_cast<float>(chunk.height) * th;

        // Chunk-level cull (rotation-free: tilemaps are axis-aligned).
        if (!camera::isVisible(bounds, chunk.x, chunk.y, chunkW, chunkH)) {
            continue;
        }

        // Ensure a resident R16UI index texture for this chunk slot, (re)creating only on a size
        // change. POINT/CLAMP: tile ids must never be filtered or wrapped.
        if (i >= m_indexTextures.size()) {
            m_indexTextures.resize(i + 1);
        }
        IndexTexture& idx = m_indexTextures[i];
        if (!idx.handle.isValid() || idx.width != chunk.width || idx.height != chunk.height) {
            if (idx.handle.isValid()) device.destroy(idx.handle);
            rhi::TextureDesc d;
            d.width = chunk.width;
            d.height = chunk.height;
            d.format = rhi::TextureDesc::R16UI;
            d.filter = rhi::TextureDesc::Point;
            d.wrap   = rhi::TextureDesc::Clamp;
            idx.handle = device.createTexture(d);
            idx.width = chunk.width;
            idx.height = chunk.height;
        }

        // Upload the tile ids. The region overload covers the full grid but avoids the full-update
        // m_width/m_height bug. A2 re-uploads each frame (immediate mode); A4 will upload once and
        // patch only dirty texels.
        const uint32_t bytes = static_cast<uint32_t>(chunk.width) * chunk.height * sizeof(uint16_t);
        device.updateTexture(idx.handle, chunk.tiles, bytes, 0, 0, chunk.width, chunk.height);

        // Resolve the atlas (tileset) texture.
        rhi::TextureHandle tileset;
        if (chunk.textureId > 0 && m_resourceCache) {
            tileset = m_resourceCache->getTextureById(chunk.textureId);
        }
        if (!tileset.isValid()) {
            tileset = m_defaultTileset.isValid() ? m_defaultTileset : m_defaultTexture;
        }

        // Per-chunk draw: state, uniforms, two textures (index + atlas), one quad.
        cmd.setState(state);

        float params[4] = { chunk.x, chunk.y, tw, th };
        float grid[4]   = { static_cast<float>(chunk.width), static_cast<float>(chunk.height),
                            static_cast<float>(m_tilesPerRow), static_cast<float>(m_tilesPerCol) };
        cmd.setUniform(m_paramsUniform, params, 1);
        cmd.setUniform(m_gridUniform, grid, 1);

        cmd.setTexture(0, idx.handle, m_indexSampler);
        cmd.setTexture(1, tileset, m_atlasSampler);
        cmd.setVertexBuffer(m_quadVB);
        cmd.setIndexBuffer(m_quadIB);
        cmd.drawIndexed(6);
        cmd.submit(0, m_shader, 0);
    }
}

} // namespace grove
