#include "SceneCollector.h"
#include "grove/IIO.h"
#include "grove/IDataNode.h"
#include "../Frame/FrameAllocator.h"
#include "../Assets/AssetManager.h"   // resolve a sprite's "asset" id -> texture id
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <spdlog/spdlog.h>

namespace grove {

// An "asset" string id (resolved through the streaming AssetManager: on-demand load + cache) wins over a
// raw numeric "textureId". `fallback` is used when neither is present (e.g. keep the current id on update).
int SceneCollector::resolveTextureId(const IDataNode& data, int fallback) const {
    const std::string asset = data.getString("asset", "");
    if (!asset.empty() && m_assetMgr) return static_cast<int>(m_assetMgr->resolve(asset));
    return data.getInt("textureId", fallback);
}

// QUOI : construit une vue orthographique screen-space (1px = 1 unité monde, origine
//   haut-gauche), zoom 1, sans translation.
// POURQUOI : c'est la vue du HUD (et la vue monde par défaut avant toute caméra). Le HUD
//   doit rester fixe quand le monde zoome → sa projection ne dépend QUE du viewport, jamais
//   du zoom/pan de render:camera. Même matrice que initDefaultView, factorisée pour servir
//   m_mainView (défaut) ET m_hudView.
// COMMENT : view = identité ; proj ortho mappant (0,0)-(w,h) vers (-1,-1)-(1,1) avec Y-flip.
static void buildScreenSpaceView(ViewInfo& v, uint16_t width, uint16_t height) {
    v.positionX = 0.0f;
    v.positionY = 0.0f;
    v.zoom = 1.0f;
    v.viewportX = 0;
    v.viewportY = 0;
    v.viewportW = width;
    v.viewportH = height;

    for (int i = 0; i < 16; ++i) v.viewMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;

    std::memset(v.projMatrix, 0, sizeof(v.projMatrix));
    v.projMatrix[0] = 2.0f / width;
    v.projMatrix[5] = -2.0f / height;  // Y-flip for top-left origin
    v.projMatrix[10] = 1.0f;
    v.projMatrix[12] = -1.0f;
    v.projMatrix[13] = 1.0f;
    v.projMatrix[15] = 1.0f;
}

// Un render:* est-il destiné au HUD (espace écran, fixe) plutôt qu'au monde (zoomable) ?
// Opt-in explicite via le champ "space":"screen" ; tout le reste reste monde par défaut.
static bool isScreenSpace(const IDataNode& data) {
    return data.getString("space", "") == "screen";
}

void SceneCollector::setup(IIO* io, uint16_t width, uint16_t height) {
    // Subscribe to all render topics with callback handler
    io->subscribe("render:.*", [this](const Message& msg) {
        if (!msg.data) return;

        // Route message based on topic.
        // NB: NO per-message logging here — this callback runs on the render hot path
        // (once per command, per frame). The previous spdlog::info() calls flooded the
        // log at 60fps×N sprites and cost formatting/mutex on every frame.
        //
        // Retained mode: sprites persist across frames, keyed by renderId
        // (add/update/remove). For static / long-lived sprites.
        if (msg.topic == "render:sprite:add") {
            parseSpriteAdd(*msg.data);
        }
        else if (msg.topic == "render:sprite:update") {
            parseSpriteUpdate(*msg.data);
        }
        else if (msg.topic == "render:sprite:remove") {
            parseSpriteRemove(*msg.data);
        }
        // Retained mode - text
        else if (msg.topic == "render:text:add") {
            parseTextAdd(*msg.data);
        }
        else if (msg.topic == "render:text:update") {
            parseTextUpdate(*msg.data);
        }
        else if (msg.topic == "render:text:remove") {
            parseTextRemove(*msg.data);
        }
        // Ephemeral / immediate mode: the sprite is drawn for THIS frame only (the
        // publisher re-sends it every frame; cleared in clear()). This is a LEGITIMATE,
        // supported path for dynamic per-frame entities — NOT an error. The old
        // "should not happen in retained mode" warning was wrong; both modes coexist
        // and are merged in finalize().
        else if (msg.topic == "render:sprite") {
            parseSprite(*msg.data);
        }
        else if (msg.topic == "render:sprite:batch") {
            parseSpriteBatch(*msg.data);
        }
        else if (msg.topic == "render:tilemap") {
            parseTilemap(*msg.data);
        }
        else if (msg.topic == "render:tilemap:add") {
            parseTilemapAdd(*msg.data);
        }
        else if (msg.topic == "render:tilemap:update") {
            parseTilemapUpdate(*msg.data);
        }
        else if (msg.topic == "render:tilemap:remove") {
            parseTilemapRemove(*msg.data);
        }
        else if (msg.topic == "render:text") {
            parseText(*msg.data);
        }
        else if (msg.topic == "render:particle") {
            parseParticle(*msg.data);
        }
        else if (msg.topic == "render:camera") {
            parseCamera(*msg.data);
        }
        else if (msg.topic == "render:clear") {
            parseClear(*msg.data);
        }
        else if (msg.topic == "render:debug:line") {
            parseDebugLine(*msg.data);
        }
        else if (msg.topic == "render:debug:rect") {
            parseDebugRect(*msg.data);
        }
        else if (msg.topic == "render:sector") {
            parseSector(*msg.data);
        }
        // Filled rect via the LAYERED sprite path (engine help A2): unlike debug:rect
        // (always-on-top, no layer), render:rect honors `layer` and draws before text —
        // so HUD backgrounds sit under their labels.
        else if (msg.topic == "render:rect") {
            parseRect(*msg.data);
        }
    });

    // Initialize default view with provided dimensions (will be overridden by camera messages)
    initDefaultView(width > 0 ? width : 1280, height > 0 ? height : 720);
}

void SceneCollector::collect(IIO* io, float deltaTime) {
    m_deltaTime = deltaTime;
    m_elapsedTime += deltaTime;   // running clock for time-based shaders (animated tiles)
    m_frameNumber++;

    // Pull and dispatch all pending messages (callbacks invoked automatically)
    while (io->hasMessages() > 0) {
        io->pullAndDispatch();
    }
}

FramePacket SceneCollector::finalize(FrameAllocator& allocator) {
    FramePacket packet;

    packet.frameNumber = m_frameNumber;
    packet.deltaTime = m_deltaTime;
    packet.elapsedTime = m_elapsedTime;
    packet.clearColor = m_clearColor;
    packet.mainView = m_mainView;
    packet.allocator = &allocator;

    // Copy sprites to frame allocator (merge retained + ephemeral)
    size_t totalSprites = m_retainedSprites.size() + m_sprites.size();
    if (totalSprites > 0) {
        SpriteInstance* sprites = allocator.allocateArray<SpriteInstance>(totalSprites);
        if (sprites) {
            size_t idx = 0;
            // Copy retained sprites first
            for (const auto& [renderId, sprite] : m_retainedSprites) {
                sprites[idx++] = sprite;
            }
            // Copy ephemeral sprites
            if (!m_sprites.empty()) {
                std::memcpy(&sprites[idx], m_sprites.data(), m_sprites.size() * sizeof(SpriteInstance));
            }
            // FIX #4 : trier par layer croissant → ordre de dessin = z-order.
            // POURQUOI : les sprites étaient émis "retained (ordre de hash unordered_map)
            //   puis ephemeral", sans tri ; comme la passe submit dans l'ordre du paquet,
            //   le z-order était non déterministe (un fond pouvait passer devant son texte).
            // COMMENT : stable_sort pour que, à layer ÉGAL, l'ordre d'insertion (retained
            //   avant ephemeral) soit préservé — pas de scintillement entre éléments du
            //   même plan.
            std::stable_sort(sprites, sprites + totalSprites,
                [](const SpriteInstance& a, const SpriteInstance& b) { return a.layer < b.layer; });
            packet.sprites = sprites;
            packet.spriteCount = totalSprites;
        }
    } else {
        packet.sprites = nullptr;
        packet.spriteCount = 0;
    }

    // Copy tilemaps (retained + ephemeral) into the frame allocator. Retained chunks come first;
    // each carries its `dirty` flag so the pass uploads only changed grids. After a retained chunk
    // is copied into a frame, its store dirty is cleared — it stays false until the next update,
    // which is what makes a static retained chunk upload exactly once.
    {
        const size_t totalTilemaps = m_retainedTilemaps.size() + m_tilemaps.size();
        if (totalTilemaps > 0) {
            TilemapChunk* tilemaps = allocator.allocateArray<TilemapChunk>(totalTilemaps);
            if (tilemaps) {
                size_t idx = 0;

                // Retained chunks (by id).
                for (auto& [id, rt] : m_retainedTilemaps) {
                    TilemapChunk chunk = rt.chunk;  // meta (incl. id + dirty)
                    if (!rt.tiles.empty()) {
                        uint16_t* tilesCopy = static_cast<uint16_t*>(
                            allocator.allocate(rt.tiles.size() * sizeof(uint16_t), alignof(uint16_t)));
                        if (tilesCopy) {
                            std::memcpy(tilesCopy, rt.tiles.data(), rt.tiles.size() * sizeof(uint16_t));
                            chunk.tiles = tilesCopy;
                            chunk.tileCount = rt.tiles.size();
                        }
                    }
                    if (!rt.fog.empty()) {
                        uint8_t* fogCopy = static_cast<uint8_t*>(
                            allocator.allocate(rt.fog.size(), alignof(uint8_t)));
                        if (fogCopy) {
                            std::memcpy(fogCopy, rt.fog.data(), rt.fog.size());
                            chunk.fog = fogCopy;
                        }
                    }
                    tilemaps[idx++] = chunk;
                    // Consumed this frame -> clean until the next update (clears the dirty rect too).
                    rt.chunk.dirty = false;
                    rt.chunk.dirtyX = 0; rt.chunk.dirtyY = 0;
                    rt.chunk.dirtyW = 0; rt.chunk.dirtyH = 0;
                }

                // Ephemeral chunks (re-sent every frame).
                for (size_t i = 0; i < m_tilemaps.size(); ++i) {
                    TilemapChunk chunk = m_tilemaps[i];
                    if (i < m_tilemapTiles.size() && !m_tilemapTiles[i].empty()) {
                        const std::vector<uint16_t>& tiles = m_tilemapTiles[i];
                        uint16_t* tilesCopy = static_cast<uint16_t*>(
                            allocator.allocate(tiles.size() * sizeof(uint16_t), alignof(uint16_t)));
                        if (tilesCopy) {
                            std::memcpy(tilesCopy, tiles.data(), tiles.size() * sizeof(uint16_t));
                            chunk.tiles = tilesCopy;
                            chunk.tileCount = tiles.size();
                        }
                    }
                    tilemaps[idx++] = chunk;
                }

                packet.tilemaps = tilemaps;
                packet.tilemapCount = idx;
            }
        } else {
            packet.tilemaps = nullptr;
            packet.tilemapCount = 0;
        }
    }

    // Copy texts (with string data) - merge retained + ephemeral
    size_t totalTexts = m_retainedTexts.size() + m_texts.size();
    if (totalTexts > 0) {
        TextCommand* texts = allocator.allocateArray<TextCommand>(totalTexts);
        if (texts) {
            size_t idx = 0;

            // Copy retained texts first
            for (const auto& [renderId, textCmd] : m_retainedTexts) {
                texts[idx] = textCmd;
                // Copy string data
                auto strIt = m_retainedTextStrings.find(renderId);
                if (strIt != m_retainedTextStrings.end() && !strIt->second.empty()) {
                    const std::string& str = strIt->second;
                    char* textCopy = static_cast<char*>(allocator.allocate(str.size() + 1, 1));
                    if (textCopy) {
                        std::memcpy(textCopy, str.c_str(), str.size() + 1);
                        texts[idx].text = textCopy;
                    }
                }
                idx++;
            }

            // Copy ephemeral texts
            for (size_t i = 0; i < m_texts.size(); ++i) {
                texts[idx] = m_texts[i];
                if (i < m_textStrings.size() && !m_textStrings[i].empty()) {
                    const std::string& str = m_textStrings[i];
                    char* textCopy = static_cast<char*>(allocator.allocate(str.size() + 1, 1));
                    if (textCopy) {
                        std::memcpy(textCopy, str.c_str(), str.size() + 1);
                        texts[idx].text = textCopy;
                    }
                }
                idx++;
            }

            // FIX #4 : même tri stable par layer que les sprites (z-order déterministe).
            // Le texte des labels UI doit passer DEVANT les fonds (layer supérieur).
            std::stable_sort(texts, texts + totalTexts,
                [](const TextCommand& a, const TextCommand& b) { return a.layer < b.layer; });
            packet.texts = texts;
            packet.textCount = totalTexts;
        }
    } else {
        packet.texts = nullptr;
        packet.textCount = 0;
    }

    // Copy particles
    if (!m_particles.empty()) {
        ParticleInstance* particles = allocator.allocateArray<ParticleInstance>(m_particles.size());
        if (particles) {
            std::memcpy(particles, m_particles.data(), m_particles.size() * sizeof(ParticleInstance));
            packet.particles = particles;
            packet.particleCount = m_particles.size();
        }
    } else {
        packet.particles = nullptr;
        packet.particleCount = 0;
    }

    // Copy debug lines
    if (!m_debugLines.empty()) {
        DebugLine* lines = allocator.allocateArray<DebugLine>(m_debugLines.size());
        if (lines) {
            std::memcpy(lines, m_debugLines.data(), m_debugLines.size() * sizeof(DebugLine));
            packet.debugLines = lines;
            packet.debugLineCount = m_debugLines.size();
        }
    } else {
        packet.debugLines = nullptr;
        packet.debugLineCount = 0;
    }

    // Copy debug rects
    if (!m_debugRects.empty()) {
        DebugRect* rects = allocator.allocateArray<DebugRect>(m_debugRects.size());
        if (rects) {
            std::memcpy(rects, m_debugRects.data(), m_debugRects.size() * sizeof(DebugRect));
            packet.debugRects = rects;
            packet.debugRectCount = m_debugRects.size();
        }
    } else {
        packet.debugRects = nullptr;
        packet.debugRectCount = 0;
    }

    // Sectors (filled wedges), ephemeral. World bucket -> packet.sectors, stable_sort by layer; the
    // HUD bucket is handled with the other screen-space buckets below.
    if (!m_sectors.empty()) {
        SectorCommand* sec = allocator.allocateArray<SectorCommand>(m_sectors.size());
        if (sec) {
            std::memcpy(sec, m_sectors.data(), m_sectors.size() * sizeof(SectorCommand));
            std::stable_sort(sec, sec + m_sectors.size(),
                [](const SectorCommand& a, const SectorCommand& b) { return a.layer < b.layer; });
            packet.sectors = sec;
            packet.sectorCount = m_sectors.size();
        }
    } else {
        packet.sectors = nullptr;
        packet.sectorCount = 0;
    }

    // HUD sprites (screen-space). Ephemeral only (no retained HUD bucket). Same per-layer
    // stable_sort as the world bucket so HUD z-order is deterministic too.
    if (!m_hudSprites.empty()) {
        SpriteInstance* hud = allocator.allocateArray<SpriteInstance>(m_hudSprites.size());
        if (hud) {
            std::memcpy(hud, m_hudSprites.data(), m_hudSprites.size() * sizeof(SpriteInstance));
            std::stable_sort(hud, hud + m_hudSprites.size(),
                [](const SpriteInstance& a, const SpriteInstance& b) { return a.layer < b.layer; });
            packet.hudSprites = hud;
            packet.hudSpriteCount = m_hudSprites.size();
        }
    } else {
        packet.hudSprites = nullptr;
        packet.hudSpriteCount = 0;
    }

    // HUD texts (screen-space). Pair each command with its string by index (same as the
    // ephemeral world-text path), then stable_sort by layer.
    if (!m_hudTexts.empty()) {
        TextCommand* hud = allocator.allocateArray<TextCommand>(m_hudTexts.size());
        if (hud) {
            for (size_t i = 0; i < m_hudTexts.size(); ++i) {
                hud[i] = m_hudTexts[i];
                if (i < m_hudTextStrings.size() && !m_hudTextStrings[i].empty()) {
                    const std::string& str = m_hudTextStrings[i];
                    char* textCopy = static_cast<char*>(allocator.allocate(str.size() + 1, 1));
                    if (textCopy) {
                        std::memcpy(textCopy, str.c_str(), str.size() + 1);
                        hud[i].text = textCopy;
                    }
                }
            }
            std::stable_sort(hud, hud + m_hudTexts.size(),
                [](const TextCommand& a, const TextCommand& b) { return a.layer < b.layer; });
            packet.hudTexts = hud;
            packet.hudTextCount = m_hudTexts.size();
        }
    } else {
        packet.hudTexts = nullptr;
        packet.hudTextCount = 0;
    }

    // HUD sectors (screen-space wedges, e.g. the action wheel). stable_sort by layer.
    if (!m_hudSectors.empty()) {
        SectorCommand* hud = allocator.allocateArray<SectorCommand>(m_hudSectors.size());
        if (hud) {
            std::memcpy(hud, m_hudSectors.data(), m_hudSectors.size() * sizeof(SectorCommand));
            std::stable_sort(hud, hud + m_hudSectors.size(),
                [](const SectorCommand& a, const SectorCommand& b) { return a.layer < b.layer; });
            packet.hudSectors = hud;
            packet.hudSectorCount = m_hudSectors.size();
        }
    } else {
        packet.hudSectors = nullptr;
        packet.hudSectorCount = 0;
    }

    packet.hudView = m_hudView;

    return packet;
}

void SceneCollector::clear() {
    m_sprites.clear();
    m_tilemaps.clear();
    m_tilemapTiles.clear();
    m_texts.clear();
    m_textStrings.clear();
    m_particles.clear();
    m_debugLines.clear();
    m_debugRects.clear();
    m_sectors.clear();
    m_hudSprites.clear();
    m_hudTexts.clear();
    m_hudTextStrings.clear();
    m_hudSectors.clear();
}

// ============================================================================
// Message Parsing
// ============================================================================

void SceneCollector::parseSprite(const IDataNode& data) {
    SpriteInstance sprite;
    // i_data0
    sprite.x = static_cast<float>(data.getDouble("x", 0.0));
    sprite.y = static_cast<float>(data.getDouble("y", 0.0));
    sprite.scaleX = static_cast<float>(data.getDouble("scaleX", 1.0));
    sprite.scaleY = static_cast<float>(data.getDouble("scaleY", 1.0));

    // i_data1
    sprite.rotation = static_cast<float>(data.getDouble("rotation", 0.0));
    sprite.u0 = static_cast<float>(data.getDouble("u0", 0.0));
    sprite.v0 = static_cast<float>(data.getDouble("v0", 0.0));
    sprite.u1 = static_cast<float>(data.getDouble("u1", 1.0));
    // i_data2
    sprite.v1 = static_cast<float>(data.getDouble("v1", 1.0));
    sprite.textureId = static_cast<float>(resolveTextureId(data));
    sprite.layer = static_cast<float>(data.getInt("layer", 0));
    sprite.padding0 = 0.0f;
    // i_data3 (reserved)
    sprite.reserved[0] = 0.0f;
    sprite.reserved[1] = 0.0f;
    sprite.reserved[2] = 0.0f;
    sprite.reserved[3] = 0.0f;
    // i_data4 (color as floats)
    uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    sprite.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    sprite.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    sprite.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    sprite.a = static_cast<float>(color & 0xFF) / 255.0f;

    // Route to the HUD bucket if screen-space, else the world bucket (default).
    (isScreenSpace(data) ? m_hudSprites : m_sprites).push_back(sprite);
}

void SceneCollector::parseRect(const IDataNode& data) {
    // QUOI : un rectangle plein coloré, posé comme un sprite teinté (texture 0 = blanc).
    // POURQUOI : donne aux jeux HUD-lourds (Drifterra) un rect qui RESPECTE le `layer`
    //   et passe par le SpritePass (avant le texte) — contrairement à render:debug:rect
    //   (dernière passe, toujours au-dessus, sans layer). Cf. aide moteur A2.
    // COMMENT : coords coin haut-gauche (x,y,w,h) comme debug:rect ; on les centre pour
    //   le sprite (x+w/2, y+h/2), scale = (w,h), textureId=0, UV plein quad, couleur teintée.
    SpriteInstance sprite;
    const float x = static_cast<float>(data.getDouble("x", 0.0));
    const float y = static_cast<float>(data.getDouble("y", 0.0));
    const float w = static_cast<float>(data.getDouble("w", 0.0));
    const float h = static_cast<float>(data.getDouble("h", 0.0));

    sprite.x = x + w * 0.5f;   // top-left -> center (same convention as render:sprite)
    sprite.y = y + h * 0.5f;
    sprite.scaleX = w;
    sprite.scaleY = h;

    sprite.rotation = 0.0f;
    sprite.u0 = 0.0f; sprite.v0 = 0.0f; sprite.u1 = 1.0f; sprite.v1 = 1.0f;  // full white texel
    sprite.textureId = 0.0f;  // 0 => default white texture in SpritePass => solid color
    sprite.layer = static_cast<float>(data.getInt("layer", 0));
    sprite.padding0 = 0.0f;
    sprite.reserved[0] = 0.0f;
    sprite.reserved[1] = 0.0f;
    sprite.reserved[2] = 0.0f;
    sprite.reserved[3] = 0.0f;

    const uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    sprite.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    sprite.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    sprite.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    sprite.a = static_cast<float>(color & 0xFF) / 255.0f;

    // HUD rect (space:"screen") → fixed overlay bucket; else world (zoomable). Cf. vue HUD.
    (isScreenSpace(data) ? m_hudSprites : m_sprites).push_back(sprite);
}

void SceneCollector::parseSpriteBatch(const IDataNode& data) {
    // Get sprites child node and iterate
    // Note: const_cast needed because IDataNode::getChildReadOnly() is not const
    // (it should be, but changing the interface requires broader refactoring)
    IDataNode* spritesNode = const_cast<IDataNode&>(data).getChildReadOnly("sprites");
    if (!spritesNode) return;

    for (const auto& name : spritesNode->getChildNames()) {
        IDataNode* spriteData = spritesNode->getChildReadOnly(name);
        if (spriteData) {
            parseSprite(*spriteData);
        }
    }
}

// Read chunk metadata (position, dims, tileset) from a data node. Tiles are handled separately.
static void readTilemapMeta(const IDataNode& data, TilemapChunk& chunk) {
    chunk.x = static_cast<float>(data.getDouble("x", 0.0));
    chunk.y = static_cast<float>(data.getDouble("y", 0.0));
    chunk.width = static_cast<uint16_t>(data.getInt("width", 0));
    chunk.height = static_cast<uint16_t>(data.getInt("height", 0));
    chunk.tileWidth = static_cast<uint16_t>(data.getInt("tileW", 16));
    chunk.tileHeight = static_cast<uint16_t>(data.getInt("tileH", 16));
    chunk.textureId = static_cast<uint16_t>(data.getInt("textureId", 0));
}

// Parse per-tile fog visibility (0..255) from a comma-separated "fogData" string. Empty = no fog.
static std::vector<uint8_t> parseFogData(const IDataNode& data) {
    std::vector<uint8_t> fog;
    std::string s = data.getString("fogData", "");
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(',', pos);
        if (end == std::string::npos) end = s.size();
        std::string n = s.substr(pos, end - pos);
        if (!n.empty()) fog.push_back(static_cast<uint8_t>(std::stoi(n)));
        pos = end + 1;
    }
    return fog;
}

// Shared tile-array parser: "tiles" child node (each child has int "v") OR a comma-separated
// "tileData" string. Used by both the ephemeral and retained tilemap paths.
std::vector<uint16_t> SceneCollector::parseTileArray(const IDataNode& data) {
    std::vector<uint16_t> tiles;

    IDataNode* tilesNode = const_cast<IDataNode&>(data).getChildReadOnly("tiles");
    if (tilesNode) {
        for (const auto& name : tilesNode->getChildNames()) {
            IDataNode* tileNode = tilesNode->getChildReadOnly(name);
            if (tileNode) {
                tiles.push_back(static_cast<uint16_t>(tileNode->getInt("v", 0)));
            }
        }
    }

    if (tiles.empty()) {
        std::string tileData = data.getString("tileData", "");
        size_t pos = 0;
        while (pos < tileData.size()) {
            size_t end = tileData.find(',', pos);
            if (end == std::string::npos) end = tileData.size();
            std::string numStr = tileData.substr(pos, end - pos);
            if (!numStr.empty()) {
                tiles.push_back(static_cast<uint16_t>(std::stoi(numStr)));
            }
            pos = end + 1;
        }
    }

    return tiles;
}

void SceneCollector::parseTilemap(const IDataNode& data) {
    TilemapChunk chunk{};
    readTilemapMeta(data, chunk);
    chunk.id = 0;        // ephemeral: re-sent every frame, always uploaded
    chunk.dirty = true;

    // Store tiles - pointer will be fixed in finalize
    m_tilemapTiles.push_back(parseTileArray(data));
    chunk.tiles = nullptr;
    chunk.tileCount = 0;

    m_tilemaps.push_back(chunk);
}

void SceneCollector::parseTilemapAdd(const IDataNode& data) {
    const uint32_t id = static_cast<uint32_t>(data.getInt("id", 0));
    if (id == 0) {
        return;  // retained chunks require a non-zero id (id 0 is the ephemeral sentinel)
    }
    RetainedTilemap rt;
    readTilemapMeta(data, rt.chunk);
    rt.chunk.id = id;
    rt.chunk.dirty = true;            // upload on the next finalize
    rt.tiles = parseTileArray(data);
    rt.fog = parseFogData(data);      // optional per-tile visibility (empty = no fog)
    rt.chunk.tiles = nullptr;         // pointers fixed in finalize
    rt.chunk.tileCount = rt.tiles.size();
    m_retainedTilemaps[id] = std::move(rt);
}

// Grow a chunk's dirty rect to also cover [x,y,w,h] (Slice A4.2). A pending full-grid upload
// (dirty && dirtyW==0) subsumes any patch; a clean chunk takes the rect as-is; otherwise union.
static void unionDirtyRect(TilemapChunk& c, int x, int y, int w, int h) {
    if (!c.dirty) {
        c.dirtyX = static_cast<uint16_t>(x); c.dirtyY = static_cast<uint16_t>(y);
        c.dirtyW = static_cast<uint16_t>(w); c.dirtyH = static_cast<uint16_t>(h);
        return;
    }
    if (c.dirtyW == 0) return;  // full-grid upload already pending — it covers everything
    const int x0 = (c.dirtyX < x) ? c.dirtyX : x;
    const int y0 = (c.dirtyY < y) ? c.dirtyY : y;
    const int x1 = (c.dirtyX + c.dirtyW > x + w) ? c.dirtyX + c.dirtyW : x + w;
    const int y1 = (c.dirtyY + c.dirtyH > y + h) ? c.dirtyY + c.dirtyH : y + h;
    c.dirtyX = static_cast<uint16_t>(x0); c.dirtyY = static_cast<uint16_t>(y0);
    c.dirtyW = static_cast<uint16_t>(x1 - x0); c.dirtyH = static_cast<uint16_t>(y1 - y0);
}

void SceneCollector::parseTilemapUpdate(const IDataNode& data) {
    const uint32_t id = static_cast<uint32_t>(data.getInt("id", 0));
    auto it = m_retainedTilemaps.find(id);
    if (id == 0 || it == m_retainedTilemaps.end()) {
        parseTilemapAdd(data);        // unknown id -> treat as add (mirrors sprite update)
        return;
    }
    RetainedTilemap& rt = it->second;

    const int w = data.getInt("w", 0);
    const int h = data.getInt("h", 0);
    if (w > 0 && h > 0) {
        // PARTIAL patch (Slice A4.2): write a w*h block of ids at (x,y) into the stored grid, and
        // grow the dirty rect — only those texels get re-uploaded, not the whole grid.
        const int x = data.getInt("x", 0);
        const int y = data.getInt("y", 0);
        const std::vector<uint16_t> patch = parseTileArray(data);   // w*h tile ids, row-major
        const int gw = rt.chunk.width;
        for (int ty = 0; ty < h; ++ty) {
            for (int tx = 0; tx < w; ++tx) {
                const size_t pi = static_cast<size_t>(ty) * w + tx;
                const size_t gi = static_cast<size_t>(y + ty) * gw + (x + tx);
                if (pi < patch.size() && gi < rt.tiles.size()) {
                    rt.tiles[gi] = patch[pi];
                }
            }
        }
        unionDirtyRect(rt.chunk, x, y, w, h);
        rt.chunk.dirty = true;
    } else {
        // FULL replace (same geometry — to change dims, remove + add). dirtyW=0 => full upload.
        rt.tiles = parseTileArray(data);
        std::vector<uint8_t> f = parseFogData(data);
        if (!f.empty()) rt.fog = std::move(f);   // replace fog only if provided
        rt.chunk.tileCount = rt.tiles.size();
        rt.chunk.dirty = true;
        rt.chunk.dirtyW = 0;
        rt.chunk.dirtyH = 0;
    }
}

void SceneCollector::parseTilemapRemove(const IDataNode& data) {
    const uint32_t id = static_cast<uint32_t>(data.getInt("id", 0));
    m_retainedTilemaps.erase(id);
}

void SceneCollector::parseText(const IDataNode& data) {
    TextCommand text;
    text.x = static_cast<float>(data.getDouble("x", 0.0));
    text.y = static_cast<float>(data.getDouble("y", 0.0));
    text.fontId = static_cast<uint16_t>(data.getInt("fontId", 0));
    text.fontSize = static_cast<uint16_t>(data.getInt("fontSize", 16));
    text.color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    text.layer = static_cast<uint16_t>(data.getInt("layer", 0));

    // Store text string - pointer will be fixed up in finalize()
    std::string textStr = data.getString("text", "");
    text.text = nullptr;  // Will be set in finalize()

    // HUD text (space:"screen") goes to the fixed overlay bucket; the string and the command
    // must stay index-aligned in their respective vectors (finalize pairs them by index).
    if (isScreenSpace(data)) {
        m_hudTextStrings.push_back(std::move(textStr));
        m_hudTexts.push_back(text);
    } else {
        m_textStrings.push_back(std::move(textStr));
        m_texts.push_back(text);
    }
}

void SceneCollector::parseParticle(const IDataNode& data) {
    ParticleInstance particle;
    particle.x = static_cast<float>(data.getDouble("x", 0.0));
    particle.y = static_cast<float>(data.getDouble("y", 0.0));
    particle.vx = static_cast<float>(data.getDouble("vx", 0.0));
    particle.vy = static_cast<float>(data.getDouble("vy", 0.0));
    particle.size = static_cast<float>(data.getDouble("size", 1.0));
    particle.life = static_cast<float>(data.getDouble("life", 1.0));
    particle.color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    particle.textureId = static_cast<uint16_t>(data.getInt("textureId", 0));

    m_particles.push_back(particle);
}

void SceneCollector::parseCamera(const IDataNode& data) {
    m_mainView.positionX = static_cast<float>(data.getDouble("x", 0.0));
    m_mainView.positionY = static_cast<float>(data.getDouble("y", 0.0));
    m_mainView.zoom = static_cast<float>(data.getDouble("zoom", 1.0));
    m_mainView.viewportX = static_cast<uint16_t>(data.getInt("viewportX", 0));
    m_mainView.viewportY = static_cast<uint16_t>(data.getInt("viewportY", 0));
    m_mainView.viewportW = static_cast<uint16_t>(data.getInt("viewportW", 1280));
    m_mainView.viewportH = static_cast<uint16_t>(data.getInt("viewportH", 720));
    m_mainView.rotation = static_cast<float>(data.getDouble("rotation", 0.0));

    // Compute the view matrix. QUOI : translate la caméra à l'origine ET tourne le monde autour du
    //   PIVOT centre-écran par `rotation` (la caméra peut "rouler"). POURQUOI : une caméra qui suit
    //   le cap d'une entité doit faire pivoter le monde autour du point montré au centre, pas autour
    //   du coin haut-gauche. COMMENT : eye' = R*(world - pivot) + eyeCentre, avec pivot = le point
    //   monde au centre écran = (x + vpW/(2*zoom), y + vpH/(2*zoom)) et eyeCentre = (vpW/(2*zoom),
    //   vpH/(2*zoom)). À rotation 0 ça retombe sur l'ancien translate(-x,-y) (verrouillé par
    //   SceneCollectorTest). Column-major ; R = [[c,-s],[s,c]].
    {
        const float c = std::cos(m_mainView.rotation);
        const float s = std::sin(m_mainView.rotation);
        const float halfW = static_cast<float>(m_mainView.viewportW) / (2.0f * m_mainView.zoom);
        const float halfH = static_cast<float>(m_mainView.viewportH) / (2.0f * m_mainView.zoom);
        const float pivotX = m_mainView.positionX + halfW;
        const float pivotY = m_mainView.positionY + halfH;
        std::memset(m_mainView.viewMatrix, 0, sizeof(m_mainView.viewMatrix));
        m_mainView.viewMatrix[0] = c;    m_mainView.viewMatrix[1] = s;     // col0 = (R00, R10)
        m_mainView.viewMatrix[4] = -s;   m_mainView.viewMatrix[5] = c;     // col1 = (R01, R11)
        m_mainView.viewMatrix[10] = 1.0f;
        m_mainView.viewMatrix[12] = halfW - (c * pivotX - s * pivotY);     // eyeCentre.x - R*pivot
        m_mainView.viewMatrix[13] = halfH - (s * pivotX + c * pivotY);     // eyeCentre.y - R*pivot
        m_mainView.viewMatrix[15] = 1.0f;
    }

    // Compute orthographic projection matrix with zoom
    float width = static_cast<float>(m_mainView.viewportW) / m_mainView.zoom;
    float height = static_cast<float>(m_mainView.viewportH) / m_mainView.zoom;

    std::memset(m_mainView.projMatrix, 0, sizeof(m_mainView.projMatrix));
    m_mainView.projMatrix[0] = 2.0f / width;
    m_mainView.projMatrix[5] = -2.0f / height;  // Y-flip for top-left origin
    m_mainView.projMatrix[10] = 1.0f;
    m_mainView.projMatrix[12] = -1.0f;
    m_mainView.projMatrix[13] = 1.0f;
    m_mainView.projMatrix[15] = 1.0f;

    // Keep the HUD overlay spanning the live viewport, but NEVER inherit the camera's zoom or
    // pan — that invariance is the whole point of the screen-space view.
    buildScreenSpaceView(m_hudView, m_mainView.viewportW, m_mainView.viewportH);
}

void SceneCollector::parseClear(const IDataNode& data) {
    m_clearColor = static_cast<uint32_t>(data.getInt("color", 0x303030FF));
}

void SceneCollector::parseDebugLine(const IDataNode& data) {
    DebugLine line;
    line.x1 = static_cast<float>(data.getDouble("x1", 0.0));
    line.y1 = static_cast<float>(data.getDouble("y1", 0.0));
    line.x2 = static_cast<float>(data.getDouble("x2", 0.0));
    line.y2 = static_cast<float>(data.getDouble("y2", 0.0));
    line.color = static_cast<uint32_t>(data.getInt("color", 0xFF0000FF));

    m_debugLines.push_back(line);
}

void SceneCollector::parseDebugRect(const IDataNode& data) {
    DebugRect rect;
    rect.x = static_cast<float>(data.getDouble("x", 0.0));
    rect.y = static_cast<float>(data.getDouble("y", 0.0));
    // Accept both "w"/"h" and "width"/"height" for convenience
    double w = data.getDouble("w", 0.0);
    if (w == 0.0) w = data.getDouble("width", 0.0);
    double h = data.getDouble("h", 0.0);
    if (h == 0.0) h = data.getDouble("height", 0.0);
    rect.w = static_cast<float>(w);
    rect.h = static_cast<float>(h);
    rect.color = static_cast<uint32_t>(data.getInt("color", 0x00FF00FF));
    rect.filled = data.getBool("filled", false);

    m_debugRects.push_back(rect);
}

void SceneCollector::parseSector(const IDataNode& data) {
    SectorCommand s;
    s.cx = static_cast<float>(data.getDouble("cx", 0.0));
    s.cy = static_cast<float>(data.getDouble("cy", 0.0));
    s.r0 = static_cast<float>(data.getDouble("r0", 0.0));
    s.r1 = static_cast<float>(data.getDouble("r1", 1.0));
    s.a0 = static_cast<float>(data.getDouble("a0", 0.0));
    s.a1 = static_cast<float>(data.getDouble("a1", 6.2831853));   // default = full circle
    s.color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    s.layer = static_cast<uint16_t>(data.getInt("layer", 0));
    // space:"screen" -> HUD (fixed view 1); else world (view 0). Same split as render:rect.
    (isScreenSpace(data) ? m_hudSectors : m_sectors).push_back(s);
}

void SceneCollector::initDefaultView(uint16_t width, uint16_t height) {
    // World view defaults to screen-space (identity camera) until a render:camera arrives.
    buildScreenSpaceView(m_mainView, width, height);
    // HUD view is ALWAYS screen-space — initialized here and only ever updated to track the
    // viewport size (never the camera's zoom/pan), see parseCamera.
    buildScreenSpaceView(m_hudView, width, height);
}

// ============================================================================
// Retained Mode Parsing (sprites persist across frames)
// ============================================================================

void SceneCollector::parseSpriteAdd(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    SpriteInstance sprite;
    sprite.x = static_cast<float>(data.getDouble("x", 0.0));
    sprite.y = static_cast<float>(data.getDouble("y", 0.0));
    sprite.scaleX = static_cast<float>(data.getDouble("scaleX", 1.0));
    sprite.scaleY = static_cast<float>(data.getDouble("scaleY", 1.0));
    sprite.rotation = static_cast<float>(data.getDouble("rotation", 0.0));
    sprite.u0 = static_cast<float>(data.getDouble("u0", 0.0));
    sprite.v0 = static_cast<float>(data.getDouble("v0", 0.0));
    sprite.u1 = static_cast<float>(data.getDouble("u1", 1.0));
    sprite.v1 = static_cast<float>(data.getDouble("v1", 1.0));
    sprite.textureId = static_cast<float>(resolveTextureId(data));
    sprite.layer = static_cast<float>(data.getInt("layer", 0));
    sprite.padding0 = 0.0f;
    // Optional UI clip rect rides in reserved[] (SpritePass reads it -> bgfx scissor). Absent = 0 = none.
    sprite.reserved[0] = static_cast<float>(data.getDouble("clipX", 0.0));
    sprite.reserved[1] = static_cast<float>(data.getDouble("clipY", 0.0));
    sprite.reserved[2] = static_cast<float>(data.getDouble("clipW", 0.0));
    sprite.reserved[3] = static_cast<float>(data.getDouble("clipH", 0.0));

    uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    sprite.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    sprite.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    sprite.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    sprite.a = static_cast<float>(color & 0xFF) / 255.0f;

    m_retainedSprites[renderId] = sprite;
    // (no per-add log: render hot path — same reason as the routing callback.)
}

void SceneCollector::parseSpriteUpdate(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    auto it = m_retainedSprites.find(renderId);
    if (it == m_retainedSprites.end()) {
        // Not found - treat as add
        parseSpriteAdd(data);
        return;
    }

    // Update existing sprite
    SpriteInstance& sprite = it->second;
    sprite.x = static_cast<float>(data.getDouble("x", sprite.x));
    sprite.y = static_cast<float>(data.getDouble("y", sprite.y));
    sprite.scaleX = static_cast<float>(data.getDouble("scaleX", sprite.scaleX));
    sprite.scaleY = static_cast<float>(data.getDouble("scaleY", sprite.scaleY));
    sprite.rotation = static_cast<float>(data.getDouble("rotation", sprite.rotation));
    sprite.textureId = static_cast<float>(resolveTextureId(data, static_cast<int>(sprite.textureId)));
    sprite.layer = static_cast<float>(data.getInt("layer", static_cast<int>(sprite.layer)));

    // Re-resolve the clip every update (full snapshot): absent -> 0 -> clip cleared. The UI
    // includes it whenever a container clip is active, so a still-clipped sprite keeps its scissor.
    sprite.reserved[0] = static_cast<float>(data.getDouble("clipX", 0.0));
    sprite.reserved[1] = static_cast<float>(data.getDouble("clipY", 0.0));
    sprite.reserved[2] = static_cast<float>(data.getDouble("clipW", 0.0));
    sprite.reserved[3] = static_cast<float>(data.getDouble("clipH", 0.0));

    // Preserve the existing color when the update omits "color" — exactly like x/y/
    // scale/layer above, which default to the sprite's current value. The old code
    // defaulted to 0xFFFFFFFF, so a color-less update silently RESET the sprite to
    // white (a retained-mode bug that no test exercised).
    if (data.hasProperty("color")) {
        uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
        sprite.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
        sprite.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
        sprite.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
        sprite.a = static_cast<float>(color & 0xFF) / 255.0f;
    }
}

void SceneCollector::parseSpriteRemove(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    m_retainedSprites.erase(renderId);
}

void SceneCollector::parseTextAdd(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    TextCommand text;
    text.x = static_cast<float>(data.getDouble("x", 0.0));
    text.y = static_cast<float>(data.getDouble("y", 0.0));
    text.fontId = static_cast<uint16_t>(data.getInt("fontId", 0));
    text.fontSize = static_cast<uint16_t>(data.getInt("fontSize", 16));
    text.color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    text.layer = static_cast<uint16_t>(data.getInt("layer", 0));
    text.clipX = static_cast<float>(data.getDouble("clipX", 0.0));
    text.clipY = static_cast<float>(data.getDouble("clipY", 0.0));
    text.clipW = static_cast<float>(data.getDouble("clipW", 0.0));
    text.clipH = static_cast<float>(data.getDouble("clipH", 0.0));
    text.text = nullptr;  // Will be set from m_retainedTextStrings in finalize

    m_retainedTexts[renderId] = text;
    m_retainedTextStrings[renderId] = data.getString("text", "");
}

void SceneCollector::parseTextUpdate(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    auto it = m_retainedTexts.find(renderId);
    if (it == m_retainedTexts.end()) {
        // Not found - treat as add
        parseTextAdd(data);
        return;
    }

    // Update existing text
    TextCommand& text = it->second;
    text.x = static_cast<float>(data.getDouble("x", text.x));
    text.y = static_cast<float>(data.getDouble("y", text.y));
    text.fontSize = static_cast<uint16_t>(data.getInt("fontSize", text.fontSize));
    text.color = static_cast<uint32_t>(data.getInt("color", text.color));
    text.layer = static_cast<uint16_t>(data.getInt("layer", text.layer));
    text.clipX = static_cast<float>(data.getDouble("clipX", 0.0));   // full snapshot: absent -> cleared
    text.clipY = static_cast<float>(data.getDouble("clipY", 0.0));
    text.clipW = static_cast<float>(data.getDouble("clipW", 0.0));
    text.clipH = static_cast<float>(data.getDouble("clipH", 0.0));

    // Update text string if provided
    std::string newText = data.getString("text", "");
    if (!newText.empty()) {
        m_retainedTextStrings[renderId] = newText;
    }
}

void SceneCollector::parseTextRemove(const IDataNode& data) {
    uint32_t renderId = static_cast<uint32_t>(data.getInt("renderId", 0));
    if (renderId == 0) return;

    m_retainedTexts.erase(renderId);
    m_retainedTextStrings.erase(renderId);
}

} // namespace grove
