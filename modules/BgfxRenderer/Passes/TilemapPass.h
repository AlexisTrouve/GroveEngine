#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace grove {

class ResourceCache;

// ============================================================================
// Tilemap Pass - Renders 2D tilemaps efficiently
// ============================================================================

class TilemapPass : public RenderPass {
public:
    /**
     * @brief Construct TilemapPass with the GPU tilemap shader program ("tilemap")
     * @param shader The index-texture tilemap shader (vs_tilemap/fs_tilemap)
     */
    explicit TilemapPass(rhi::ShaderHandle shader);

    const char* getName() const override { return "Tilemaps"; }
    uint32_t getSortOrder() const override { return 50; }  // Before sprites
    std::vector<const char*> getDependencies() const override { return {"Clear"}; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

    /**
     * @brief Set resource cache for texture lookup
     */
    void setResourceCache(ResourceCache* cache) { m_resourceCache = cache; }

    /**
     * @brief Set default tileset texture
     */
    void setDefaultTileset(rhi::TextureHandle texture) { m_defaultTileset = texture; }

    /**
     * @brief Set tileset dimensions (tiles per row/column in atlas)
     */
    void setTilesetLayout(uint16_t tilesPerRow, uint16_t tilesPerCol) {
        m_tilesPerRow = tilesPerRow;
        m_tilesPerCol = tilesPerCol;
    }

    /**
     * @brief Register a per-textureId tileset as a texture2DArray (one tile type per layer, Slice
     *        A3.3). A chunk's textureId selects it; with none registered the procedural color atlas
     *        is used. NON-OWNING — the caller (host module / test) owns and destroys the handle.
     */
    void setTileset(uint16_t textureId, rhi::TextureHandle arrayTexture) {
        m_tilesets[textureId] = arrayTexture;
    }

    /**
     * @brief Set the tiled fog texture (Slice fog): hidden tiles show this (wrap=Repeat, world-uv)
     *        instead of going black. NON-OWNING. Invalid handle -> hidden tiles render black.
     */
    void setFogTexture(rhi::TextureHandle tex) { m_fogNoise = tex; }

    /**
     * @brief Declare a tile type as ANIMATED (water/lava): tile id `tileId` cycles through `frames`
     *        CONSECUTIVE atlas layers (from its base layer id-1) at `fps`. The index texture is
     *        UNCHANGED (stores the base id) — the shader offsets the LAYER by time, so animation is
     *        free (no per-frame upload). Re-declaring a tileId updates it; frames<=1 stops it. Capped
     *        at kMaxTileAnims (the shader's u_tileAnim[16] array); extras are ignored.
     */
    void setTileAnim(uint16_t tileId, uint16_t frames, float fps) {
        for (auto it = m_tileAnims.begin(); it != m_tileAnims.end(); ++it) {
            if (it->tileId == tileId) {
                if (frames <= 1) m_tileAnims.erase(it);       // <=1 frame -> stop animating this tile
                else { it->frames = frames; it->fps = fps; }
                return;
            }
        }
        if (frames > 1 && static_cast<int>(m_tileAnims.size()) < kMaxTileAnims)
            m_tileAnims.push_back(TileAnim{tileId, frames, fps});
    }

private:
    rhi::ShaderHandle m_shader;       // GPU tilemap program ("tilemap")
    rhi::BufferHandle m_quadVB;       // unit quad, scaled per chunk by the VS
    rhi::BufferHandle m_quadIB;

    // Shader uniforms (see vs_tilemap.sc / fs_tilemap.sc).
    rhi::UniformHandle m_paramsUniform;   // u_tilemapParams: originX, originY, tilePixW, tilePixH
    rhi::UniformHandle m_gridUniform;     // u_tilemapGrid:   gridW, gridH, atlasCols, atlasRows
    rhi::UniformHandle m_indexSampler;    // s_index (slot 0) — R16UI tile-index texture
    rhi::UniformHandle m_atlasSampler;    // s_atlas (slot 1) — tile atlas (texture2DArray)
    rhi::UniformHandle m_lodSampler;      // s_lod   (slot 2) — mipped LOD color texture (Slice B)
    rhi::UniformHandle m_fogSampler;      // s_fog   (slot 3) — mipped R8 visibility (Slice fog)
    rhi::UniformHandle m_fogNoiseSampler; // s_fognoise (slot 4) — tiled fog texture (wrap=Repeat)
    rhi::UniformHandle m_animUniform;     // u_tileAnim[16]: per animated tile {id, frames, fps, _}
    rhi::UniformHandle m_animMetaUniform; // u_tileAnimMeta: {animCount, time, _, _}

    // Animated tiles (water/lava). The index texture holds each tile's BASE id; the shader cycles the
    // atlas LAYER over time (clock = FramePacket::elapsedTime, so the pass is stateless re: time).
    // m_tileAnims is the small declared table, uploaded into u_tileAnim each frame; capped to the
    // shader's u_tileAnim[4] array size (4 vec4s = the 16-float setUniform command-buffer limit;
    // enough for water/lava-class tiles in v1 — extend by widening the command buffer if ever needed).
    static constexpr int kMaxTileAnims = 4;
    struct TileAnim { uint16_t tileId; uint16_t frames; float fps; };
    std::vector<TileAnim> m_tileAnims;

    // Procedural color atlas ARRAY (one solid color per layer) used as the tileset for A3:
    // tile id N -> layer N-1 -> a distinct color. Proves the array indexing visually. Slicing a
    // real grid-PNG into an array (via textureId/ResourceCache) is the A3.3 follow-on.
    rhi::TextureHandle m_defaultAtlas;
    rhi::TextureHandle m_defaultTileset;  // (reserved API; the real per-textureId atlas path is A3.3)
    rhi::TextureHandle m_defaultFog;      // 1x1 R8 = 255 (fully visible), bound when a chunk has no fog
    rhi::TextureHandle m_defaultFogNoise; // 1x1 black: hidden tiles go black (the no-fog-texture case)
    rhi::TextureHandle m_fogNoise;        // tiled fog texture set by the host (NON-owning); invalid -> default

    // Per-textureId atlas arrays registered by the host (Slice A3.3); NON-owning. A chunk's
    // textureId selects one, else the procedural m_defaultAtlas is bound.
    std::unordered_map<uint16_t, rhi::TextureHandle> m_tilesets;

    ResourceCache* m_resourceCache = nullptr;

    // Atlas layout (kept for the future grid path); the array atlas does not use grid UVs.
    uint16_t m_tilesPerRow = 16;
    uint16_t m_tilesPerCol = 16;

    // Per-chunk-slot resident index texture (R16UI). Reused across frames (not re-allocated each
    // frame); recreated only when a slot's dimensions change. Keyed by the chunk's position in the
    // frame — stable while the submitted chunk set is stable. A4's retained chunkId path replaces
    // this positional cache with a true upload-once grid.
    struct IndexTexture {
        rhi::TextureHandle handle;   // R16UI tile-index texture (detail band) — layer 0
        rhi::TextureHandle lod;      // RGBA8 mipped LOD color texture (zoom-out band, Slice B) — layer 0
        rhi::TextureHandle fog;      // R8 mipped visibility texture (Slice fog; invalid = no fog) — per chunk
        uint16_t width = 0;
        uint16_t height = 0;
        // Multi-layer overlays (Strategy A): one {index, lod} per layer beyond layer 0 (= handle/lod).
        // Sized to chunk.layerCount-1, (re)built with the chunk. Drawn alpha-blended on top of layer 0.
        std::vector<rhi::TextureHandle> extraIndex;
        std::vector<rhi::TextureHandle> extraLod;
    };
    // Ephemeral chunks (id == 0): positional cache, re-uploaded every frame (legacy immediate path).
    std::vector<IndexTexture> m_indexTextures;
    // Retained chunks (id != 0, Slice A4): cache keyed by chunkId, uploaded only when the chunk is
    // dirty (added/updated). Entries whose id is absent from a frame are evicted (GC).
    std::unordered_map<uint32_t, IndexTexture> m_retainedIndex;
};

} // namespace grove
