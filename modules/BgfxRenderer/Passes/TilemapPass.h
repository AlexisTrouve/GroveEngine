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
     * @brief Register the LOD colour table DERIVED from a tileset — the per-layer average colour
     *        computed at load time (atlas::averageLayers). The zoom-out band then uses the tileset's
     *        own colours instead of the built-in 8-colour palette, so both bands agree.
     *        OWNING (plain CPU data, unlike the non-owning texture handles above).
     *        Convention: colors[i] = colour of tile id i+1 (the atlas layer mapping).
     */
    void setTilesetLodColors(uint16_t textureId, std::vector<uint32_t> colors) {
        m_lodDerived[textureId] = std::move(colors);
        ++m_lodEpoch;   // cached LOD textures are now stale — see m_lodEpoch
    }

    /**
     * @brief Register an EXPLICIT LOD palette pushed by the game (render:tilemap:palette). Overrides
     *        the derived table for that tileset — for a game whose tile colours are pure data with no
     *        art to average. textureId 0 addresses the procedural-atlas path. Same indexing as above.
     */
    void setLodPalette(uint16_t textureId, std::vector<uint32_t> colors) {
        m_lodPalette[textureId] = std::move(colors);
        ++m_lodEpoch;
    }

    /**
     * @brief Set the tiled fog texture (Slice fog): hidden tiles show this (wrap=Repeat, world-uv)
     *        instead of going black. NON-OWNING. Invalid handle -> hidden tiles render black.
     */
    void setFogTexture(rhi::TextureHandle tex) { m_fogNoise = tex; }

    /**
     * @brief Taille MONDE que couvre une tuile de la texture de brouillard.
     *
     * POURQUOI ce réglage existe : l'échelle était écrite en dur dans le shader (`/64.0`), ce qui
     * rendait « utiliser un asset de brouillard plus grand » littéralement inexprimable — un jeu
     * pouvait changer l'image, pas la taille à laquelle elle se répète. Une valeur <= 0 est ignorée
     * (elle ferait exploser l'uv), donc un appelant maladroit dégrade vers le défaut au lieu de
     * casser l'affichage.
     */
    void setFogScale(float worldUnits) { if (worldUnits > 0.0f) m_fogScale = worldUnits; }
    float fogScale() const { return m_fogScale; }

    /**
     * @brief Décalage d'échantillonnage du brouillard, en unités MONDE.
     *
     * Un hôte qui le fait dériver dans le temps obtient un brouillard qui bouge pour le prix d'un
     * uniform. ⚠️ Seul le NUAGE bouge : le masque de révélation reste exactement où le jeu l'a mis —
     * un brouillard qui dérive ne doit jamais re-cacher ce qui a été exploré.
     */
    void setFogOffset(float x, float y) { m_fogOffsetX = x; m_fogOffsetY = y; }
    float fogOffsetX() const { return m_fogOffsetX; }
    float fogOffsetY() const { return m_fogOffsetY; }

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
    rhi::UniformHandle m_fogParamsUniform; // u_fogParams: {worldScale, offsetX, offsetY, _}

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
    // 64 = la constante qui était codée en dur dans le shader : le défaut reproduit donc EXACTEMENT
    // le rendu d'avant pour tout hôte qui ne configure rien.
    float m_fogScale = 64.0f;
    float m_fogOffsetX = 0.0f;
    float m_fogOffsetY = 0.0f;

    // Per-textureId atlas arrays registered by the host (Slice A3.3); NON-owning. A chunk's
    // textureId selects one, else the procedural m_defaultAtlas is bound.
    std::unordered_map<uint16_t, rhi::TextureHandle> m_tilesets;

    // LOD colour tables per tileset id, indexed by tile id - 1 (the atlas layer convention). OWNING.
    // Resolution order for a layer whose tileset is T: explicit palette[T] (game override) > derived[T]
    // (the tileset's per-layer average) > lod::paletteColor (the built-in 8 colours). T = 0 (the
    // procedural atlas) never gets a derived table, so it keeps the historical look unless a game
    // explicitly paints it — that is where the non-regression guarantee comes from.
    std::unordered_map<uint16_t, std::vector<uint32_t>> m_lodDerived;
    std::unordered_map<uint16_t, std::vector<uint32_t>> m_lodPalette;

    // Bumped on every table change; each cached LOD texture records the epoch it was baked at and is
    // re-baked on mismatch. WHY: a chunk's LOD is baked once (on add/update) and cached by chunk id,
    // so a table registered AFTER its chunks would never be seen — the feature would silently depend
    // on publish order, which is exactly the kind of hidden ordering contract we refuse to ship.
    uint32_t m_lodEpoch = 0;

    // Resolve the LOD colour table for a tileset id. nullptr => fall back to lod::paletteColor.
    const std::vector<uint32_t>* lodTableFor(uint16_t textureId) const {
        auto p = m_lodPalette.find(textureId);
        if (p != m_lodPalette.end() && !p->second.empty()) return &p->second;   // game override wins
        auto d = m_lodDerived.find(textureId);
        if (d != m_lodDerived.end() && !d->second.empty()) return &d->second;   // tileset average
        return nullptr;                                                         // built-in palette
    }

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
        uint32_t lodEpoch = 0;       // m_lodEpoch this entry's LOD textures were baked at (0 = the
                                     // no-table default, so a fresh chunk starts up-to-date)
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
