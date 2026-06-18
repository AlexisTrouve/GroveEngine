#include "TilemapPass.h"
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"
#include "../Frame/FramePacket.h"
#include "../Resources/ResourceCache.h"
#include "../Scene/Camera.h"
#include <vector>
#include <unordered_set>

namespace grove {

// Create a resident R16UI tile-index texture (POINT/CLAMP — tile ids must never be filtered or
// wrapped). Shared by the ephemeral and retained paths.
static rhi::TextureHandle createIndexTexture(rhi::IRHIDevice& device, uint16_t w, uint16_t h) {
    rhi::TextureDesc d;
    d.width  = w;
    d.height = h;
    d.format = rhi::TextureDesc::R16UI;
    d.filter = rhi::TextureDesc::Point;
    d.wrap   = rhi::TextureDesc::Clamp;
    return device.createTexture(d);
}

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

    // Procedural color atlas ARRAY (Slice A3 verification): one solid color per layer, so tile id N
    // renders as a distinct color (id 1 -> layer 0, ...). RGBA8 texel bytes are [R,G,B,A]; on
    // little-endian that is the literal 0xAABBGGRR. Layers are laid out contiguously for the upload.
    constexpr int ATLAS_TILE = 8;       // px per layer; solid color -> exact size is irrelevant
    constexpr int ATLAS_LAYERS = 8;
    static const uint32_t kColors[ATLAS_LAYERS] = {
        0xFFC8C8C8u, // 1: light grey
        0xFF50C83Cu, // 2: green
        0xFFE68246u, // 3: blue
        0xFF3CD2E6u, // 4: yellow
        0xFFC84CB4u, // 5: magenta
        0xFF00B4C8u, // 6: amber
        0xFFF0781Eu, // 7: cyan-blue
        0xFFFFFFFFu, // 8: white
    };
    std::vector<uint32_t> atlasPixels(static_cast<size_t>(ATLAS_TILE) * ATLAS_TILE * ATLAS_LAYERS);
    for (int layer = 0; layer < ATLAS_LAYERS; ++layer) {
        const size_t base = static_cast<size_t>(layer) * ATLAS_TILE * ATLAS_TILE;
        for (int p = 0; p < ATLAS_TILE * ATLAS_TILE; ++p) {
            atlasPixels[base + p] = kColors[layer];
        }
    }
    rhi::TextureDesc atlasDesc;
    atlasDesc.width  = ATLAS_TILE;
    atlasDesc.height = ATLAS_TILE;
    atlasDesc.layers = ATLAS_LAYERS;
    atlasDesc.format = rhi::TextureDesc::RGBA8;
    atlasDesc.data = atlasPixels.data();
    atlasDesc.dataSize = static_cast<uint32_t>(atlasPixels.size() * sizeof(uint32_t));
    m_defaultAtlas = device.createTexture(atlasDesc);
}

void TilemapPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_paramsUniform);
    device.destroy(m_gridUniform);
    device.destroy(m_indexSampler);
    device.destroy(m_atlasSampler);
    device.destroy(m_defaultAtlas);
    for (auto& it : m_indexTextures) {
        if (it.handle.isValid()) device.destroy(it.handle);
    }
    m_indexTextures.clear();
    for (auto& [id, idx] : m_retainedIndex) {
        if (idx.handle.isValid()) device.destroy(idx.handle);
    }
    m_retainedIndex.clear();
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

    std::unordered_set<uint32_t> seenRetained;  // retained chunk ids present this frame (for GC)
    uint32_t ephemeralSlot = 0;                  // positional slot for id==0 chunks

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

        // Resolve the resident R16UI index texture for this chunk and upload its tile ids.
        // The region overload covers the full grid but avoids the full-update m_width/m_height bug.
        const uint32_t bytes = static_cast<uint32_t>(chunk.width) * chunk.height * sizeof(uint16_t);
        rhi::TextureHandle indexTex;

        if (chunk.id != 0) {
            // Retained (Slice A4): cache by chunk id, upload ONLY when the chunk is dirty (the frame
            // it was added/updated) or just (re)created. A static chunk uploads exactly once.
            seenRetained.insert(chunk.id);
            IndexTexture& idx = m_retainedIndex[chunk.id];
            const bool needsCreate =
                !idx.handle.isValid() || idx.width != chunk.width || idx.height != chunk.height;
            if (needsCreate) {
                if (idx.handle.isValid()) device.destroy(idx.handle);
                idx.handle = createIndexTexture(device, chunk.width, chunk.height);
                idx.width = chunk.width;
                idx.height = chunk.height;
            }
            if (needsCreate || chunk.dirty) {
                device.updateTexture(idx.handle, chunk.tiles, bytes, 0, 0, chunk.width, chunk.height);
            }
            indexTex = idx.handle;
        } else {
            // Ephemeral (id == 0): positional cache, re-uploaded every frame (immediate mode).
            if (ephemeralSlot >= m_indexTextures.size()) {
                m_indexTextures.resize(ephemeralSlot + 1);
            }
            IndexTexture& idx = m_indexTextures[ephemeralSlot++];
            if (!idx.handle.isValid() || idx.width != chunk.width || idx.height != chunk.height) {
                if (idx.handle.isValid()) device.destroy(idx.handle);
                idx.handle = createIndexTexture(device, chunk.width, chunk.height);
                idx.width = chunk.width;
                idx.height = chunk.height;
            }
            device.updateTexture(idx.handle, chunk.tiles, bytes, 0, 0, chunk.width, chunk.height);
            indexTex = idx.handle;
        }

        // Atlas = the procedural color ARRAY (A3). A real per-textureId atlas array — built by
        // slicing a grid-PNG into layers — is the A3.3 follow-on; binding a 2D texture here would be
        // invalid against the sampler2DArray. Until then the index path always samples the array,
        // exercising array indexing end-to-end (tile id N -> layer N-1 -> its color).
        rhi::TextureHandle tileset = m_defaultAtlas;

        // Per-chunk draw: state, uniforms, two textures (index + atlas), one quad.
        cmd.setState(state);

        float params[4] = { chunk.x, chunk.y, tw, th };
        float grid[4]   = { static_cast<float>(chunk.width), static_cast<float>(chunk.height),
                            static_cast<float>(m_tilesPerRow), static_cast<float>(m_tilesPerCol) };
        cmd.setUniform(m_paramsUniform, params, 1);
        cmd.setUniform(m_gridUniform, grid, 1);

        cmd.setTexture(0, indexTex, m_indexSampler);
        cmd.setTexture(1, tileset, m_atlasSampler);
        cmd.setVertexBuffer(m_quadVB);
        cmd.setIndexBuffer(m_quadIB);
        cmd.drawIndexed(6);
        cmd.submit(0, m_shader, 0);
    }

    // GC retained index textures whose chunk id was not present this frame (chunk removed). Keeps
    // the cache bounded without leaking GPU textures across removes.
    for (auto it = m_retainedIndex.begin(); it != m_retainedIndex.end(); ) {
        if (seenRetained.find(it->first) == seenRetained.end()) {
            if (it->second.handle.isValid()) device.destroy(it->second.handle);
            it = m_retainedIndex.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace grove
