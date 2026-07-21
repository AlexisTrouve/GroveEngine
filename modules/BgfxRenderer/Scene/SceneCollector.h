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
