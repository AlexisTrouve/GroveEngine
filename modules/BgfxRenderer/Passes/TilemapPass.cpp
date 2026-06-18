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

// Tile palette (Slice A3/B): tile id -> color. Shared by the procedural atlas array (setup) and the
// LOD color texture (bakeLodColor). id 0 = empty -> transparent, so empty/solid edges fade in alpha
// when averaged into coarser mips (clean anti-aliased borders at zoom-out). RGBA8 bytes [R,G,B,A]
// -> little-endian 0xAABBGGRR.
static constexpr int kPaletteSize = 8;
static const uint32_t kTileColors[kPaletteSize] = {
    0xFFC8C8C8u, // 1: light grey
    0xFF50C83Cu, // 2: green
    0xFFE68246u, // 3: blue
    0xFF3CD2E6u, // 4: yellow
    0xFFC84CB4u, // 5: magenta
    0xFF00B4C8u, // 6: amber
    0xFFF0781Eu, // 7: cyan-blue
    0xFFFFFFFFu, // 8: white
};
static uint32_t paletteColor(uint16_t id) {
    if (id == 0) return 0x00000000u;                 // empty -> transparent
    return kTileColors[(id - 1) % kPaletteSize];
}

// Average four RGBA8 texels component-wise (2x2 box filter).
static uint32_t avg4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const uint32_t sum = ((a >> shift) & 0xFFu) + ((b >> shift) & 0xFFu)
                           + ((c >> shift) & 0xFFu) + ((d >> shift) & 0xFFu);
        out |= ((sum >> 2) & 0xFFu) << shift;
    }
    return out;
}

// Bake the chunk's "LOD color" texture (Slice B): mip0[t] = paletteColor(tile_id[t]), 1 texel per
// tile; finer mips are 2x2 box-filters down to 1x1, uploaded as one contiguous mip chain. Linear +
// mips => GPU trilinear gives the smooth, alias-free zoom-out band. Indices can't be mipped (their
// average is meaningless), which is the whole reason this separate COLOR representation exists.
static rhi::TextureHandle bakeLodColor(rhi::IRHIDevice& device, const TilemapChunk& chunk) {
    const int w0 = chunk.width;
    const int h0 = chunk.height;

    int mips = 1;
    for (int d = (w0 > h0 ? w0 : h0); d > 1; d >>= 1) ++mips;

    std::vector<uint32_t> buf;                       // mip0 || mip1 || ... (bgfx layout)
    std::vector<uint32_t> prev(static_cast<size_t>(w0) * h0);
    for (size_t i = 0; i < prev.size(); ++i) {
        prev[i] = paletteColor(chunk.tiles[i]);
    }
    buf.insert(buf.end(), prev.begin(), prev.end());

    int pw = w0, ph = h0;
    for (int m = 1; m < mips; ++m) {
        const int nw = pw > 1 ? pw >> 1 : 1;
        const int nh = ph > 1 ? ph >> 1 : 1;
        std::vector<uint32_t> cur(static_cast<size_t>(nw) * nh);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                const int x0 = x * 2, y0 = y * 2;
                const int x1 = (x0 + 1 < pw) ? x0 + 1 : x0;   // clamp for odd dims
                const int y1 = (y0 + 1 < ph) ? y0 + 1 : y0;
                cur[static_cast<size_t>(y) * nw + x] = avg4(
                    prev[static_cast<size_t>(y0) * pw + x0], prev[static_cast<size_t>(y0) * pw + x1],
                    prev[static_cast<size_t>(y1) * pw + x0], prev[static_cast<size_t>(y1) * pw + x1]);
            }
        }
        buf.insert(buf.end(), cur.begin(), cur.end());
        prev.swap(cur);
        pw = nw; ph = nh;
    }

    rhi::TextureDesc d;
    d.width     = static_cast<uint16_t>(w0);
    d.height    = static_cast<uint16_t>(h0);
    d.mipLevels = static_cast<uint8_t>(mips);
    d.format    = rhi::TextureDesc::RGBA8;
    d.filter    = rhi::TextureDesc::Linear;          // trilinear over the mip chain
    d.wrap      = rhi::TextureDesc::Clamp;
    d.data      = buf.data();
    d.dataSize  = static_cast<uint32_t>(buf.size() * sizeof(uint32_t));
    return device.createTexture(d);                  // bgfx::copy duplicates `buf`
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
    // renders as a distinct color (id 1 -> layer 0, ...). Uses the SAME palette as the LOD band
    // (kTileColors), so a tile looks identical detailed vs zoomed-out. Layers are contiguous.
    constexpr int ATLAS_TILE = 8;       // px per layer; solid color -> exact size is irrelevant
    std::vector<uint32_t> atlasPixels(static_cast<size_t>(ATLAS_TILE) * ATLAS_TILE * kPaletteSize);
    for (int layer = 0; layer < kPaletteSize; ++layer) {
        const size_t base = static_cast<size_t>(layer) * ATLAS_TILE * ATLAS_TILE;
        for (int p = 0; p < ATLAS_TILE * ATLAS_TILE; ++p) {
            atlasPixels[base + p] = kTileColors[layer];
        }
    }
    rhi::TextureDesc atlasDesc;
    atlasDesc.width  = ATLAS_TILE;
    atlasDesc.height = ATLAS_TILE;
    atlasDesc.layers = kPaletteSize;
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
        if (it.lod.isValid()) device.destroy(it.lod);
    }
    m_indexTextures.clear();
    for (auto& [id, idx] : m_retainedIndex) {
        if (idx.handle.isValid()) device.destroy(idx.handle);
        if (idx.lod.isValid()) device.destroy(idx.lod);
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

        rhi::TextureHandle lodTex;

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
                // The LOD color band depends on tile CONTENT, so any upload re-bakes it (Slice B).
                if (idx.lod.isValid()) device.destroy(idx.lod);
                idx.lod = bakeLodColor(device, chunk);
            }
            indexTex = idx.handle;
            lodTex = idx.lod;
        } else {
            // Ephemeral (id == 0): positional cache, re-uploaded every frame (immediate mode). The
            // LOD is baked on (re)create only — ephemeral content is assumed stable (true for the
            // showcase); retained is the path with proper per-update LOD re-bake.
            if (ephemeralSlot >= m_indexTextures.size()) {
                m_indexTextures.resize(ephemeralSlot + 1);
            }
            IndexTexture& idx = m_indexTextures[ephemeralSlot++];
            if (!idx.handle.isValid() || idx.width != chunk.width || idx.height != chunk.height) {
                if (idx.handle.isValid()) device.destroy(idx.handle);
                if (idx.lod.isValid()) device.destroy(idx.lod);
                idx.handle = createIndexTexture(device, chunk.width, chunk.height);
                idx.lod = bakeLodColor(device, chunk);
                idx.width = chunk.width;
                idx.height = chunk.height;
            }
            device.updateTexture(idx.handle, chunk.tiles, bytes, 0, 0, chunk.width, chunk.height);
            indexTex = idx.handle;
            lodTex = idx.lod;
        }
        (void)lodTex;  // bound + sampled by the shader in B2; baked + cached here in B1

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
            if (it->second.lod.isValid()) device.destroy(it->second.lod);
            it = m_retainedIndex.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace grove
