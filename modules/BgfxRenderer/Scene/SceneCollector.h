#pragma once

#include "../Frame/FramePacket.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace grove {

class IIO;
class IDataNode;
class FrameAllocator;
namespace assets { class AssetManager; }   // resolves a sprite's "asset" id -> texture id (streaming)

// ============================================================================
// Scene Collector - Gathers render data from IIO messages
// ============================================================================

class SceneCollector {
public:
    SceneCollector() = default;

    // Configure IIO subscriptions (called in setConfiguration)
    // width/height: Window dimensions for default view initialization
    void setup(IIO* io, uint16_t width = 1280, uint16_t height = 720);

    // The streaming AssetManager: when a sprite carries an "asset" string id, the collector resolves it to a
    // texture id (on-demand load + cache) instead of using a raw textureId. nullptr = id-only (legacy).
    void setAssetManager(assets::AssetManager* mgr) { m_assetMgr = mgr; }

    // Collect all IIO messages at frame start (called in process)
    // Pull-based: module controls when to read messages
    void collect(IIO* io, float deltaTime);

    // Generate immutable FramePacket for render passes
    FramePacket finalize(FrameAllocator& allocator);

    // BULK direct-feed: append N GPU-ready instances straight into this frame's ephemeral
    // sprite list, bypassing IIO + JSON entirely. The host calls this between frames (after
    // clear(), before process()'s finalize) when it already holds packed SpriteInstances —
    // the high-throughput path (~ns/sprite) vs render:sprite (one JSON message each, deep-
    // copied by IIO at ~10µs/sprite). World-space, no asset/clip resolution: final instances.
    void addSpritesBulk(const SpriteInstance* data, size_t count);

    // BULK direct-feed for PARTICLES — same high-throughput contract as addSpritesBulk (no IIO, no
    // JSON). ParticleInstance is already the POD the ParticlePass consumes, so this is a single insert.
    // For a swarm game's per-agent thruster/impact particles (render:particle otherwise = one JSON
    // message each, ~10µs). World-space, ephemeral (dropped on clear()).
    void addParticlesBulk(const ParticleInstance* data, size_t count);

    // BULK direct-feed for TEXT — N labels in one call, bypassing IIO+JSON (render:text = one message
    // each). Each item carries its string via TextCommand.text (null-terminated); we COPY it into the
    // frame's string staging (the caller's buffers need not outlive the call), the pointer is fixed in
    // finalize() — exactly like the render:text path but without the per-label message. World-space,
    // ephemeral. For per-agent unit labels (name/HP) over a crowd.
    void addTextsBulk(const TextCommand* items, size_t count);

    // Reset for next frame
    void clear();

private:
    // Retained mode: persistent sprites/texts (not cleared each frame)
    std::unordered_map<uint32_t, SpriteInstance> m_retainedSprites;
    std::unordered_map<uint32_t, TextCommand> m_retainedTexts;
    std::unordered_map<uint32_t, std::string> m_retainedTextStrings;  // Text content for retained texts

    // Retained-mode HUD (screen-space): persistent widgets that must IGNORE the world camera. Parallels
    // m_retainedSprites/Texts but is drawn on m_hudView (fixed screen-space), not m_mainView. Populated
    // when a retained command carries space:"screen" — the UIModule tags all its widgets (they ARE the HUD).
    // WHY: without a retained HUD bucket, a UIModule widget (which renders via render:sprite:add/text:add)
    // lands in the world bucket and pans/zooms with the terrain under a live render:camera. Ephemeral HUD
    // already had m_hudSprites/m_hudTexts; this is the missing retained twin.
    std::unordered_map<uint32_t, SpriteInstance> m_retainedHudSprites;
    std::unordered_map<uint32_t, TextCommand> m_retainedHudTexts;
    std::unordered_map<uint32_t, std::string> m_retainedHudTextStrings;

    // Retained tilemaps (Slice A4): persistent chunks by id, owning their tile data. Merged into the
    // frame each finalize; `dirty` is set on add/update and cleared once the chunk is copied into a
    // frame, so the pass uploads a static chunk exactly once.
    struct RetainedTilemap {
        TilemapChunk chunk;
        std::vector<uint16_t> tiles;
        std::vector<uint8_t> fog;   // per-tile visibility 0..255 (empty = no fog); Slice fog
        // Multi-layer (Strategy A): each layer's tile grid + tileset id. Empty = single-layer (uses `tiles`).
        // layerTiles[0] mirrors `tiles` (layer 0 = the legacy path). Pointers fixed into frame memory in finalize.
        std::vector<std::vector<uint16_t>> layerTiles;
        std::vector<uint16_t> layerTexIds;
    };
    std::unordered_map<uint32_t, RetainedTilemap> m_retainedTilemaps;

    // Ephemeral mode: staging buffers (filled during collect, cleared each frame)
    std::vector<SpriteInstance> m_sprites;
    std::vector<TilemapChunk> m_tilemaps;
    std::vector<std::vector<uint16_t>> m_tilemapTiles;  // Owns tile data until finalize
    std::vector<TextCommand> m_texts;
    std::vector<std::string> m_textStrings;  // Owns text data until finalize
    std::vector<ParticleInstance> m_particles;
    std::vector<DebugLine> m_debugLines;
    std::vector<DebugRect> m_debugRects;
    std::vector<SectorCommand> m_sectors;       // render:sector (world), ephemeral

    // HUD / screen-space staging (filled when a command carries space:"screen"). Ephemeral
    // only — drawn on m_hudView so the HUD ignores the world camera's zoom/pan.
    std::vector<SpriteInstance> m_hudSprites;
    std::vector<TextCommand> m_hudTexts;
    std::vector<std::string> m_hudTextStrings;  // Owns HUD text data until finalize
    std::vector<SectorCommand> m_hudSectors;    // render:sector with space:"screen"

    // View state
    ViewInfo m_mainView;
    ViewInfo m_hudView;  // Fixed screen-space view for the HUD bucket (see FramePacket::hudView)
    uint32_t m_clearColor = 0x303030FF;
    // Global ambient light (lighting L1). 0 = UNSET => lighting inactive => the renderer skips the
    // offscreen targets entirely. Global frame state: survives clear(), like m_clearColor.
    uint32_t m_ambientColor = 0;
    // Bloom settings (plan B). Global frame state like m_ambientColor: intensity 0 = OFF = the
    // default, and the whole zero-cost bypass of the post-processing chain hangs off that.
    FramePacket::BloomSettings m_bloom;
    // Tonemapping (plan T). Reglage global persistant, INDEPENDANT du bloom : `None` = eteint.
    FramePacket::TonemapSettings m_tonemap;
    // Fondu plein écran (plan F2). Réglage global persistant ; `amount 0` = éteint, et cette passe
    // n'exige NI éclairage NI cible HDR — voir FramePacket::FadeSettings.
    FramePacket::FadeSettings m_fade;
    // Colorimetrie (plan G). Reglage global persistant ; les trois neutres = eteint, et c'est
    // grove::light::gradeIsNeutral qui en decide.
    light::GradeParams m_grade;
    std::vector<OccluderCommand> m_occluders;   // ephemeral, like m_lights
    // RETAINED occluders, by renderId. The opposite choice to lights, and for the opposite reason:
    // a wall does not move, so re-publishing the level every frame would charge a cost proportional
    // to its size for a constant. Survives clear(); merged with the ephemeral list in finalize().
    std::unordered_map<uint32_t, OccluderCommand> m_retainedOccluders;
    std::vector<FilterCommand> m_filters;   // ephemeral, like m_occluders
    std::vector<NebulaCommand> m_nebulae;   // ephemeral: a soft radial medium (A4)
    // RETAINED nebulae, by renderId (A5). Simpler than the fog and filter registers: a nebula's
    // parameters are NOT converted on the CPU (its density varies per pixel, so Beer-Lambert lives
    // in the shader), so the record IS the packet command — there is nothing to re-derive.
    std::unordered_map<uint32_t, NebulaCommand> m_retainedNebulae;
    std::vector<FogCommand> m_fogs;         // ephemeral

    // RETAINED media, by renderId (A3). Stores the AUTHOR's numbers, not the converted ones, so that
    // a partial update naming only `color` can still re-derive from the density it did not restate.
    // (Unlike a filter, the conversion has no geometry in it — resizing a volume cannot change what
    // it absorbs per unit — so this is about partial merges only.)
    struct RetainedFog {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        float density = 0.0f;
        float tintR = 1.0f, tintG = 1.0f, tintB = 1.0f;
        float scatter = 0.0f;
    };
    std::unordered_map<uint32_t, RetainedFog> m_retainedFogs;
    static FogCommand buildFog(const RetainedFog& src);
    static void readFogFields(const IDataNode& data, RetainedFog& out);

    // RETAINED filters, by renderId (F3). Same rationale as the retained occluders beside them: a
    // stained-glass window does not move.
    //
    // ⚠️ It stores the AUTHOR's tint, not the per-unit value the packet carries — because the
    // conversion depends on the pane's THICKNESS. An update that resizes the pane must therefore
    // re-derive it; keeping only the converted value would let a window widened at runtime hold a
    // per-unit figure computed for its old thickness, and its tint would drift silently.
    struct RetainedFilter {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        float tintR = 1.0f, tintG = 1.0f, tintB = 1.0f;   // as authored, BEFORE opacity and conversion
        float opacity = 1.0f;
    };
    std::unordered_map<uint32_t, RetainedFilter> m_retainedFilters;
    // Both shared by the ephemeral and retained paths so the two can never diverge on what a tint
    // means, nor on which fields a message may omit.
    static FilterCommand buildFilter(const RetainedFilter& src);
    static void readFilterFields(const IDataNode& data, RetainedFilter& out);

    std::vector<LightCommand> m_lights;   // ephemeral: cleared at the frame boundary
    uint64_t m_frameNumber = 0;
    float m_deltaTime = 0.0f;
    float m_elapsedTime = 0.0f;   // accumulated dt (running clock for time-based shaders, e.g. animated tiles)

    // Resolve a sprite's texture: an "asset" string id (-> AssetManager: texId + atlas UV rect, which is
    // written into `sprite`'s UVs) wins over a raw "textureId" (-> `fallback` if absent). Returns the tex id.
    int resolveSpriteTexture(const IDataNode& data, SpriteInstance& sprite, int fallback = 0) const;
    assets::AssetManager* m_assetMgr = nullptr;

    // Message parsing helpers (ephemeral mode - legacy)
    void parseSprite(const IDataNode& data);
    void parseRect(const IDataNode& data);  // filled colored quad via the layered sprite path (A2)
    void parseSpriteBatch(const IDataNode& data);
    void parseTilemap(const IDataNode& data);
    void parseText(const IDataNode& data);
    void parseParticle(const IDataNode& data);
    void parseCamera(const IDataNode& data);
    void parseClear(const IDataNode& data);
    void parseAmbient(const IDataNode& data);
    void parseBloom(const IDataNode& data);   // `render:bloom` — post-processing settings (plan B)
    void parseTonemap(const IDataNode& data); // `render:tonemap` — courbe + exposition (plan T)
    void parseFade(const IDataNode& data);    // `render:fade` — fondu plein écran (plan F2)
    void parseGrade(const IDataNode& data);   // `render:grade` — teinte/contraste/saturation (plan G)
    void parseLight(const IDataNode& data);
    void parseOccluder(const IDataNode& data);
    void parseFilter(const IDataNode& data);
    void parseFog(const IDataNode& data);
    void parseNebula(const IDataNode& data);
    void parseNebulaAdd(const IDataNode& data);
    void parseNebulaUpdate(const IDataNode& data);
    void parseNebulaRemove(const IDataNode& data);
    // Shared by the ephemeral and retained paths, so the two cannot diverge on which fields a
    // message may omit. Merges INTO `out`, each field defaulting to its current value.
    static void readNebulaFields(const IDataNode& data, NebulaCommand& out);
    void parseFogAdd(const IDataNode& data);
    void parseFogUpdate(const IDataNode& data);
    void parseFogRemove(const IDataNode& data);
    void parseFilterAdd(const IDataNode& data);
    void parseFilterUpdate(const IDataNode& data);
    void parseFilterRemove(const IDataNode& data);
    void parseOccluderAdd(const IDataNode& data);
    void parseOccluderUpdate(const IDataNode& data);
    void parseOccluderRemove(const IDataNode& data);
    void parseDebugLine(const IDataNode& data);
    void parseDebugRect(const IDataNode& data);
    void parseSector(const IDataNode& data);

    // Message parsing helpers (retained mode - new)
    void parseSpriteAdd(const IDataNode& data);
    void parseSpriteUpdate(const IDataNode& data);
    void parseSpriteRemove(const IDataNode& data);

    // 9-slice (nine-patch) frame — retained. ONE render:nineslice:{add,update} describes a bordered box
    // (target rect + border texture/asset + margin insets); we EXPAND it into up to 9 retained sprites
    // (corners native, edges/centre stretched) so the existing sprite pipeline (HUD bucket, clip, tint)
    // draws it — no new pass. add == update == a full re-expand (erase the 9 children, rebuild); remove
    // drops the 9 children. The children live in a RESERVED render-id space (nineSliceChildId, top bit set)
    // so they never collide with ordinary retained sprites (whose ids are small + never set the top bit).
    void parseNineSliceAdd(const IDataNode& data);
    void parseNineSliceUpdate(const IDataNode& data);
    void parseNineSliceRemove(const IDataNode& data);
    void expandNineSlice(const IDataNode& data);   // shared add/update body (erase children + rebuild)
    // Derive the i-th (0..8) child sprite id of a nine-slice parent. Reserved top-bit space, parent masked
    // to 28 bits then shifted 4 to leave room for the index — collision-free vs ordinary small render ids.
    static uint32_t nineSliceChildId(uint32_t parent, int i) {
        return 0x80000000u | ((parent & 0x0FFFFFFFu) << 4) | (static_cast<uint32_t>(i) & 0xFu);
    }
    void parseTextAdd(const IDataNode& data);
    void parseTextUpdate(const IDataNode& data);
    void parseTextRemove(const IDataNode& data);
    // Lecteur de couches PARTAGÉ par l'add et l'update : c'est ce partage qui empêche les deux
    // chemins de diverger (cf. le commentaire à sa définition).
    void readTilemapLayers(const IDataNode& data, RetainedTilemap& rt);
    void parseTilemapAdd(const IDataNode& data);
    void parseTilemapUpdate(const IDataNode& data);
    void parseTilemapRemove(const IDataNode& data);
    void parseTilemapFog(const IDataNode& data);   // fog-only partial reveal (render:tilemap:fog)

    // Parse a tile-index array from either a "tiles" child node or a comma-separated "tileData"
    // string. Shared by the ephemeral and retained tilemap paths.
    static std::vector<uint16_t> parseTileArray(const IDataNode& data);

    // Initialize default view
    void initDefaultView(uint16_t width, uint16_t height);
};

} // namespace grove
