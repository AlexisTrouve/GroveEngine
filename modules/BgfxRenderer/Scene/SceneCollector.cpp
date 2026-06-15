#include "SceneCollector.h"
#include "grove/IIO.h"
#include "grove/IDataNode.h"
#include "../Frame/FrameAllocator.h"
#include <cstring>
#include <spdlog/spdlog.h>

namespace grove {

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
    });

    // Initialize default view with provided dimensions (will be overridden by camera messages)
    initDefaultView(width > 0 ? width : 1280, height > 0 ? height : 720);
}

void SceneCollector::collect(IIO* io, float deltaTime) {
    m_deltaTime = deltaTime;
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
            packet.sprites = sprites;
            packet.spriteCount = totalSprites;
        }
    } else {
        packet.sprites = nullptr;
        packet.spriteCount = 0;
    }

    // Copy tilemaps (with tile data)
    if (!m_tilemaps.empty()) {
        TilemapChunk* tilemaps = allocator.allocateArray<TilemapChunk>(m_tilemaps.size());
        if (tilemaps) {
            std::memcpy(tilemaps, m_tilemaps.data(), m_tilemaps.size() * sizeof(TilemapChunk));

            // Copy tile data to frame allocator and fix up pointers
            for (size_t i = 0; i < m_tilemaps.size() && i < m_tilemapTiles.size(); ++i) {
                const std::vector<uint16_t>& tiles = m_tilemapTiles[i];
                if (!tiles.empty()) {
                    uint16_t* tilesCopy = static_cast<uint16_t*>(
                        allocator.allocate(tiles.size() * sizeof(uint16_t), alignof(uint16_t)));
                    if (tilesCopy) {
                        std::memcpy(tilesCopy, tiles.data(), tiles.size() * sizeof(uint16_t));
                        tilemaps[i].tiles = tilesCopy;
                        tilemaps[i].tileCount = tiles.size();
                    }
                }
            }

            packet.tilemaps = tilemaps;
            packet.tilemapCount = m_tilemaps.size();
        }
    } else {
        packet.tilemaps = nullptr;
        packet.tilemapCount = 0;
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
    sprite.textureId = static_cast<float>(data.getInt("textureId", 0));
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

    m_sprites.push_back(sprite);
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

void SceneCollector::parseTilemap(const IDataNode& data) {
    TilemapChunk chunk;
    chunk.x = static_cast<float>(data.getDouble("x", 0.0));
    chunk.y = static_cast<float>(data.getDouble("y", 0.0));
    chunk.width = static_cast<uint16_t>(data.getInt("width", 0));
    chunk.height = static_cast<uint16_t>(data.getInt("height", 0));
    chunk.tileWidth = static_cast<uint16_t>(data.getInt("tileW", 16));
    chunk.tileHeight = static_cast<uint16_t>(data.getInt("tileH", 16));
    chunk.textureId = static_cast<uint16_t>(data.getInt("textureId", 0));

    // Parse tile array from "tiles" child node
    std::vector<uint16_t> tiles;
    IDataNode* tilesNode = const_cast<IDataNode&>(data).getChildReadOnly("tiles");
    if (tilesNode) {
        // Each child is a tile index
        for (const auto& name : tilesNode->getChildNames()) {
            IDataNode* tileNode = tilesNode->getChildReadOnly(name);
            if (tileNode) {
                // Try to get as int (direct value)
                tiles.push_back(static_cast<uint16_t>(tileNode->getInt("v", 0)));
            }
        }
    }

    // Alternative: parse from comma-separated string "tileData"
    if (tiles.empty()) {
        std::string tileData = data.getString("tileData", "");
        if (!tileData.empty()) {
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
    }

    // Store tiles - pointer will be fixed in finalize
    m_tilemapTiles.push_back(std::move(tiles));
    chunk.tiles = nullptr;
    chunk.tileCount = 0;

    m_tilemaps.push_back(chunk);
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
    m_textStrings.push_back(std::move(textStr));
    text.text = nullptr;  // Will be set in finalize()

    m_texts.push_back(text);
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

    // Compute view matrix (translation by -camera position)
    std::memset(m_mainView.viewMatrix, 0, sizeof(m_mainView.viewMatrix));
    m_mainView.viewMatrix[0] = 1.0f;
    m_mainView.viewMatrix[5] = 1.0f;
    m_mainView.viewMatrix[10] = 1.0f;
    m_mainView.viewMatrix[12] = -m_mainView.positionX;
    m_mainView.viewMatrix[13] = -m_mainView.positionY;
    m_mainView.viewMatrix[15] = 1.0f;

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

void SceneCollector::initDefaultView(uint16_t width, uint16_t height) {
    m_mainView.positionX = 0.0f;
    m_mainView.positionY = 0.0f;
    m_mainView.zoom = 1.0f;
    m_mainView.viewportX = 0;
    m_mainView.viewportY = 0;
    m_mainView.viewportW = width;
    m_mainView.viewportH = height;

    // Identity view matrix
    for (int i = 0; i < 16; ++i) {
        m_mainView.viewMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    // Orthographic projection matrix (2D)
    // Maps (0,0)-(width,height) to (-1,-1)-(1,1)
    std::memset(m_mainView.projMatrix, 0, sizeof(m_mainView.projMatrix));
    m_mainView.projMatrix[0] = 2.0f / width;
    m_mainView.projMatrix[5] = -2.0f / height; // Y-flip for top-left origin
    m_mainView.projMatrix[10] = 1.0f;
    m_mainView.projMatrix[12] = -1.0f;
    m_mainView.projMatrix[13] = 1.0f;
    m_mainView.projMatrix[15] = 1.0f;
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
    sprite.textureId = static_cast<float>(data.getInt("textureId", 0));
    sprite.layer = static_cast<float>(data.getInt("layer", 0));
    sprite.padding0 = 0.0f;
    sprite.reserved[0] = 0.0f;
    sprite.reserved[1] = 0.0f;
    sprite.reserved[2] = 0.0f;
    sprite.reserved[3] = 0.0f;

    uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    sprite.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    sprite.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    sprite.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    sprite.a = static_cast<float>(color & 0xFF) / 255.0f;

    m_retainedSprites[renderId] = sprite;
    spdlog::info("📥 [SceneCollector] Stored SPRITE renderId={}, pos=({:.1f},{:.1f}), scale={}x{}, textureId={}, layer={}, color=({:.2f},{:.2f},{:.2f},{:.2f})",
        renderId, sprite.x, sprite.y, sprite.scaleX, sprite.scaleY, (int)sprite.textureId, (int)sprite.layer,
        sprite.r, sprite.g, sprite.b, sprite.a);
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
    sprite.textureId = static_cast<float>(data.getInt("textureId", static_cast<int>(sprite.textureId)));
    sprite.layer = static_cast<float>(data.getInt("layer", static_cast<int>(sprite.layer)));

    uint32_t color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    sprite.r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    sprite.g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    sprite.b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    sprite.a = static_cast<float>(color & 0xFF) / 255.0f;
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
