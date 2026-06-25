#include "TilemapPass.h"
#include "LodColor.h"
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

// Bake the chunk's mipped LOD color texture (Slice B): build the mip chain with the pure helper in
// LodColor.h, then upload it. Linear + mips => GPU trilinear gives the smooth, alias-free zoom-out
// band. (The mip-chain math is unit-tested headless; see test_lod_color.)
static rhi::TextureHandle bakeLodColorFor(rhi::IRHIDevice& device, uint16_t w, uint16_t h, const uint16_t* tiles) {
    int mips = 1;
    std::vector<uint32_t> buf = lod::buildLodMipChain(w, h, tiles, mips);

    rhi::TextureDesc d;
    d.width     = w;
    d.height    = h;
    d.mipLevels = static_cast<uint8_t>(mips);
    d.format    = rhi::TextureDesc::RGBA8;
    d.filter    = rhi::TextureDesc::Linear;          // trilinear over the mip chain
    d.wrap      = rhi::TextureDesc::Clamp;
    d.data      = buf.data();
    d.dataSize  = static_cast<uint32_t>(buf.size() * sizeof(uint32_t));
    return device.createTexture(d);                  // bgfx::copy duplicates `buf`
}

static rhi::TextureHandle bakeLodColor(rhi::IRHIDevice& device, const TilemapChunk& chunk) {
    return bakeLodColorFor(device, chunk.width, chunk.height, chunk.tiles);
}

// Bake the chunk's mipped R8 visibility (fog) texture from chunk.fog (Slice fog). Linear + mips =>
// the fog dims/feathers correctly at every zoom (scalar visibility box-filters meaningfully).
static rhi::TextureHandle bakeFog(rhi::IRHIDevice& device, const TilemapChunk& chunk) {
    // Create EMPTY then fill via a region update. A bgfx texture created WITH initial data is IMMUTABLE
    // (updateTexture is ignored) — and the fog mask must be MUTABLE so a fog-of-war reveal can patch a
    // sub-rect (render:tilemap:fog) instead of re-baking the whole layer. Consequence: the fog is
    // NON-MIPPED (mipLevels 1) — the RHI region update only writes mip 0, so partial reveals stay exact.
    // Negligible: at extreme zoom-out the R8 mask is linear-sampled (mip 0) instead of trilinear.
    rhi::TextureDesc d;
    d.width     = chunk.width;
    d.height    = chunk.height;
    d.mipLevels = 1;
    d.format    = rhi::TextureDesc::R8;
    d.filter    = rhi::TextureDesc::Linear;
    d.wrap      = rhi::TextureDesc::Clamp;
    rhi::TextureHandle h = device.createTexture(d);   // empty -> mutable
    if (h.isValid()) {
        device.updateTexture(h, chunk.fog, static_cast<uint32_t>(chunk.width) * chunk.height,
                             0, 0, chunk.width, chunk.height);
    }
    return h;
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
    m_lodSampler    = device.createUniform("s_lod", 1);
    m_fogSampler    = device.createUniform("s_fog", 1);
    m_fogNoiseSampler = device.createUniform("s_fognoise", 1);
    m_animUniform     = device.createUniform("u_tileAnim", kMaxTileAnims);  // vec4[16] anim table
    m_animMetaUniform = device.createUniform("u_tileAnimMeta", 1);          // {count, time}

    // Procedural color atlas ARRAY (Slice A3 verification): one solid color per layer, so tile id N
    // renders as a distinct color (id 1 -> layer 0, ...). Uses the SAME palette as the LOD band
    // (kTileColors), so a tile looks identical detailed vs zoomed-out. Layers are contiguous.
    constexpr int ATLAS_TILE = 8;       // px per layer; solid color -> exact size is irrelevant
    std::vector<uint32_t> atlasPixels(static_cast<size_t>(ATLAS_TILE) * ATLAS_TILE * lod::kPaletteSize);
    for (int layer = 0; layer < lod::kPaletteSize; ++layer) {
        const size_t base = static_cast<size_t>(layer) * ATLAS_TILE * ATLAS_TILE;
        for (int p = 0; p < ATLAS_TILE * ATLAS_TILE; ++p) {
            atlasPixels[base + p] = lod::paletteColor(static_cast<uint16_t>(layer + 1));  // layer L = tile id L+1
        }
    }
    rhi::TextureDesc atlasDesc;
    atlasDesc.width  = ATLAS_TILE;
    atlasDesc.height = ATLAS_TILE;
    atlasDesc.layers = lod::kPaletteSize;
    atlasDesc.format = rhi::TextureDesc::RGBA8;
    atlasDesc.data = atlasPixels.data();
    atlasDesc.dataSize = static_cast<uint32_t>(atlasPixels.size() * sizeof(uint32_t));
    m_defaultAtlas = device.createTexture(atlasDesc);

    // 1x1 R8 = 255 (fully visible) — bound when a chunk carries no fog, so s_fog always samples 1.0.
    uint8_t fullVis = 255;
    rhi::TextureDesc fogDesc;
    fogDesc.width = 1;
    fogDesc.height = 1;
    fogDesc.format = rhi::TextureDesc::R8;
    fogDesc.data = &fullVis;
    fogDesc.dataSize = 1;
    m_defaultFog = device.createTexture(fogDesc);

    // 1x1 black fog texture — the no-fog-texture default, so mix(fog, color, vis) reduces to
    // "hidden tiles go black" (the original look) until a host sets a real fog texture.
    uint32_t blackPixel = 0xFF000000u;   // RGBA8 bytes [R,G,B,A] = [0,0,0,255] (opaque black)
    rhi::TextureDesc fnDesc;
    fnDesc.width = 1;
    fnDesc.height = 1;
    fnDesc.format = rhi::TextureDesc::RGBA8;
    fnDesc.data = &blackPixel;
    fnDesc.dataSize = sizeof(blackPixel);
    m_defaultFogNoise = device.createTexture(fnDesc);
}

void TilemapPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_paramsUniform);
    device.destroy(m_gridUniform);
    device.destroy(m_indexSampler);
    device.destroy(m_atlasSampler);
    device.destroy(m_lodSampler);
    device.destroy(m_fogSampler);
    device.destroy(m_fogNoiseSampler);
    device.destroy(m_animUniform);
    device.destroy(m_animMetaUniform);
    device.destroy(m_defaultAtlas);
    device.destroy(m_defaultFog);
    device.destroy(m_defaultFogNoise);
    for (auto& it : m_indexTextures) {
        if (it.handle.isValid()) device.destroy(it.handle);
        if (it.lod.isValid()) device.destroy(it.lod);
        if (it.fog.isValid()) device.destroy(it.fog);
    }
    m_indexTextures.clear();
    for (auto& [id, idx] : m_retainedIndex) {
        if (idx.handle.isValid()) device.destroy(idx.handle);
        if (idx.lod.isValid()) device.destroy(idx.lod);
        if (idx.fog.isValid()) device.destroy(idx.fog);
        for (auto h : idx.extraIndex) if (h.isValid()) device.destroy(h);   // multi-layer overlays
        for (auto h : idx.extraLod)   if (h.isValid()) device.destroy(h);
    }
    m_retainedIndex.clear();
}

void TilemapPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    if (frame.tilemapCount == 0) {
        return;
    }

    // Animated tiles: pack the declared table into the shader uniforms (zeroed — unused entries hold
    // id 0, which never matches a real tile in the shader). The clock is frame.elapsedTime (owned by
    // the frame, so the pass stays stateless and the GPU test can pin an exact time). Set per chunk.
    float animData[kMaxTileAnims * 4] = {};
    const int animCount = static_cast<int>(m_tileAnims.size());
    for (int i = 0; i < animCount; ++i) {
        animData[i * 4 + 0] = static_cast<float>(m_tileAnims[i].tileId);
        animData[i * 4 + 1] = static_cast<float>(m_tileAnims[i].frames);
        animData[i * 4 + 2] = m_tileAnims[i].fps;
    }
    const float animMeta[4] = { static_cast<float>(animCount), frame.elapsedTime, 0.0f, 0.0f };

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
                                  static_cast<float>(frame.mainView.viewportH),
                                  frame.mainView.rotation};   // cull AABB accounts for camera roll
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
        rhi::TextureHandle fogTex = m_defaultFog;
        IndexTexture* ridx = nullptr;   // set for retained chunks -> source of multi-layer overlay textures

        if (chunk.id != 0) {
            // Retained (Slice A4): cache by chunk id, upload ONLY when the chunk is dirty (the frame
            // it was added/updated) or just (re)created. A static chunk uploads exactly once.
            seenRetained.insert(chunk.id);
            IndexTexture& idx = m_retainedIndex[chunk.id];
            ridx = &idx;   // retained chunks carry the multi-layer overlay textures
            const bool needsCreate =
                !idx.handle.isValid() || idx.width != chunk.width || idx.height != chunk.height;
            if (needsCreate) {
                if (idx.handle.isValid()) device.destroy(idx.handle);
                idx.handle = createIndexTexture(device, chunk.width, chunk.height);
                idx.width = chunk.width;
                idx.height = chunk.height;
            }
            if (needsCreate || chunk.dirty) {
                const bool partial = !needsCreate && chunk.dirtyW > 0
                                     && (chunk.dirtyW < chunk.width || chunk.dirtyH < chunk.height);
                if (partial) {
                    // Slice A4.2: upload ONLY the dirty sub-rect. Extract it contiguously from the
                    // full grid (whose rows are strided by chunk.width).
                    std::vector<uint16_t> sub(static_cast<size_t>(chunk.dirtyW) * chunk.dirtyH);
                    for (int ty = 0; ty < chunk.dirtyH; ++ty) {
                        for (int tx = 0; tx < chunk.dirtyW; ++tx) {
                            sub[static_cast<size_t>(ty) * chunk.dirtyW + tx] =
                                chunk.tiles[static_cast<size_t>(chunk.dirtyY + ty) * chunk.width
                                            + (chunk.dirtyX + tx)];
                        }
                    }
                    device.updateTexture(idx.handle, sub.data(),
                                         static_cast<uint32_t>(sub.size() * sizeof(uint16_t)),
                                         chunk.dirtyX, chunk.dirtyY, chunk.dirtyW, chunk.dirtyH);
                } else {
                    device.updateTexture(idx.handle, chunk.tiles, bytes, 0, 0, chunk.width, chunk.height);
                }
                // The LOD color band depends on tile CONTENT, so any change re-bakes it (Slice B).
                if (idx.lod.isValid()) device.destroy(idx.lod);
                idx.lod = bakeLodColor(device, chunk);
                // Fog (Slice fog): re-bake the R8 visibility texture on any content change (covers fog too).
                if (idx.fog.isValid()) { device.destroy(idx.fog); idx.fog = rhi::TextureHandle{}; }
                if (chunk.fog != nullptr) idx.fog = bakeFog(device, chunk);
                // Multi-layer overlays (Strategy A): (re)create an index + LOD per layer beyond layer 0
                // (which is handle/lod above). Full re-upload on any content change (no partial for extras).
                const size_t extraCount = (chunk.layerCount > 1) ? chunk.layerCount - 1 : 0;
                for (auto h : idx.extraIndex) if (h.isValid()) device.destroy(h);
                for (auto h : idx.extraLod)   if (h.isValid()) device.destroy(h);
                idx.extraIndex.assign(extraCount, rhi::TextureHandle{});
                idx.extraLod.assign(extraCount, rhi::TextureHandle{});
                for (size_t li = 0; li < extraCount; ++li) {
                    const TilemapLayer& L = chunk.layers[li + 1];
                    idx.extraIndex[li] = createIndexTexture(device, chunk.width, chunk.height);
                    if (L.tiles) device.updateTexture(idx.extraIndex[li], L.tiles, bytes, 0, 0, chunk.width, chunk.height);
                    idx.extraLod[li] = bakeLodColorFor(device, chunk.width, chunk.height, L.tiles);
                }
            } else if (chunk.fogDirty && chunk.fog != nullptr && chunk.fogDirtyW > 0) {
                // FOG-ONLY partial reveal: patch just the R8 mask sub-rect (mip 0) — no tile upload, no
                // LOD re-bake. The fog texture is mutable (created empty), so the region update applies.
                if (!idx.fog.isValid()) {
                    idx.fog = bakeFog(device, chunk);   // first fog on this chunk -> create + fill it
                } else {
                    std::vector<uint8_t> sub(static_cast<size_t>(chunk.fogDirtyW) * chunk.fogDirtyH);
                    for (int ty = 0; ty < chunk.fogDirtyH; ++ty) {
                        for (int tx = 0; tx < chunk.fogDirtyW; ++tx) {
                            sub[static_cast<size_t>(ty) * chunk.fogDirtyW + tx] =
                                chunk.fog[static_cast<size_t>(chunk.fogDirtyY + ty) * chunk.width
                                          + (chunk.fogDirtyX + tx)];
                        }
                    }
                    device.updateTexture(idx.fog, sub.data(), static_cast<uint32_t>(sub.size()),
                                         chunk.fogDirtyX, chunk.fogDirtyY, chunk.fogDirtyW, chunk.fogDirtyH);
                }
            }
            indexTex = idx.handle;
            lodTex = idx.lod;
            fogTex = idx.fog.isValid() ? idx.fog : m_defaultFog;
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
                if (idx.fog.isValid()) { device.destroy(idx.fog); idx.fog = rhi::TextureHandle{}; }
                idx.handle = createIndexTexture(device, chunk.width, chunk.height);
                idx.lod = bakeLodColor(device, chunk);
                if (chunk.fog != nullptr) idx.fog = bakeFog(device, chunk);
                idx.width = chunk.width;
                idx.height = chunk.height;
            }
            device.updateTexture(idx.handle, chunk.tiles, bytes, 0, 0, chunk.width, chunk.height);
            indexTex = idx.handle;
            lodTex = idx.lod;
            fogTex = idx.fog.isValid() ? idx.fog : m_defaultFog;
        }

        // Per-LAYER draw (Strategy A). Same uniforms/state/quad; the layer supplies its own index + LOD +
        // tileset. The tilemap state is BlendMode::Alpha, so drawing layer 0 then the overlays back-to-front
        // composites them (layer 0 opaque covers; overlay tile id 0 is transparent + discarded -> base shows).
        const float params[4] = { chunk.x, chunk.y, tw, th };
        const float grid[4]   = { static_cast<float>(chunk.width), static_cast<float>(chunk.height),
                                  static_cast<float>(m_tilesPerRow), static_cast<float>(m_tilesPerCol) };
        auto drawLayer = [&](rhi::TextureHandle index, rhi::TextureHandle lod, uint16_t texId) {
            rhi::TextureHandle tileset = m_defaultAtlas;
            if (texId != 0) {
                auto it = m_tilesets.find(texId);
                if (it != m_tilesets.end() && it->second.isValid()) tileset = it->second;
            }
            cmd.setState(state);
            cmd.setUniform(m_paramsUniform, params, 1);
            cmd.setUniform(m_gridUniform, grid, 1);
            cmd.setUniform(m_animUniform, animData, kMaxTileAnims);   // animated-tile table + clock
            cmd.setUniform(m_animMetaUniform, animMeta, 1);
            cmd.setTexture(0, index, m_indexSampler);
            cmd.setTexture(1, tileset, m_atlasSampler);
            cmd.setTexture(2, lod, m_lodSampler);
            cmd.setTexture(3, fogTex, m_fogSampler);
            cmd.setTexture(4, m_fogNoise.isValid() ? m_fogNoise : m_defaultFogNoise, m_fogNoiseSampler);
            cmd.setVertexBuffer(m_quadVB);
            cmd.setIndexBuffer(m_quadIB);
            cmd.drawIndexed(6);
            cmd.submit(0, m_shader, 0);
        };

        // Layer 0 (= the legacy single grid: indexTex/lodTex/chunk.textureId).
        drawLayer(indexTex, lodTex, chunk.textureId);
        // Overlays (layers 1..N-1) for a retained multi-layer chunk, alpha-blended on top.
        if (ridx && chunk.layerCount > 1) {
            const size_t extraCount = chunk.layerCount - 1;
            for (size_t li = 0; li < extraCount && li < ridx->extraIndex.size(); ++li) {
                drawLayer(ridx->extraIndex[li], ridx->extraLod[li], chunk.layers[li + 1].textureId);
            }
        }
    }

    // GC retained index textures whose chunk id was not present this frame (chunk removed). Keeps
    // the cache bounded without leaking GPU textures across removes.
    for (auto it = m_retainedIndex.begin(); it != m_retainedIndex.end(); ) {
        if (seenRetained.find(it->first) == seenRetained.end()) {
            if (it->second.handle.isValid()) device.destroy(it->second.handle);
            if (it->second.lod.isValid()) device.destroy(it->second.lod);
            if (it->second.fog.isValid()) device.destroy(it->second.fog);
            for (auto h : it->second.extraIndex) if (h.isValid()) device.destroy(h);   // multi-layer overlays
            for (auto h : it->second.extraLod)   if (h.isValid()) device.destroy(h);
            it = m_retainedIndex.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace grove
