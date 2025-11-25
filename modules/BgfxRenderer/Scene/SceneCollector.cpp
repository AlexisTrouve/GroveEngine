#include "SceneCollector.h"
#include "grove/IIO.h"
#include "grove/IDataNode.h"
#include "../Frame/FrameAllocator.h"
#include <cstring>

namespace grove {

void SceneCollector::setup(IIO* io) {
    // Subscribe to all render topics
    io->subscribe("render:*");

    // Initialize default view (will be overridden by camera messages)
    initDefaultView(1280, 720);
}

void SceneCollector::collect(IIO* io, float deltaTime) {
    m_deltaTime = deltaTime;
    m_frameNumber++;

    // Pull all pending messages
    while (io->hasMessages() > 0) {
        Message msg = io->pullMessage();

        if (!msg.data) continue;

        // Route message based on topic
        if (msg.topic == "render:sprite") {
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
    }
}

FramePacket SceneCollector::finalize(FrameAllocator& allocator) {
    FramePacket packet;

    packet.frameNumber = m_frameNumber;
    packet.deltaTime = m_deltaTime;
    packet.clearColor = m_clearColor;
    packet.mainView = m_mainView;
    packet.allocator = &allocator;

    // Copy sprites to frame allocator
    if (!m_sprites.empty()) {
        SpriteInstance* sprites = allocator.allocateArray<SpriteInstance>(m_sprites.size());
        if (sprites) {
            std::memcpy(sprites, m_sprites.data(), m_sprites.size() * sizeof(SpriteInstance));
            packet.sprites = sprites;
            packet.spriteCount = m_sprites.size();
        }
    } else {
        packet.sprites = nullptr;
        packet.spriteCount = 0;
    }

    // Copy tilemaps
    if (!m_tilemaps.empty()) {
        TilemapChunk* tilemaps = allocator.allocateArray<TilemapChunk>(m_tilemaps.size());
        if (tilemaps) {
            std::memcpy(tilemaps, m_tilemaps.data(), m_tilemaps.size() * sizeof(TilemapChunk));
            packet.tilemaps = tilemaps;
            packet.tilemapCount = m_tilemaps.size();
        }
    } else {
        packet.tilemaps = nullptr;
        packet.tilemapCount = 0;
    }

    // Copy texts
    if (!m_texts.empty()) {
        TextCommand* texts = allocator.allocateArray<TextCommand>(m_texts.size());
        if (texts) {
            std::memcpy(texts, m_texts.data(), m_texts.size() * sizeof(TextCommand));
            packet.texts = texts;
            packet.textCount = m_texts.size();
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
    m_texts.clear();
    m_particles.clear();
    m_debugLines.clear();
    m_debugRects.clear();
}

// ============================================================================
// Message Parsing
// ============================================================================

void SceneCollector::parseSprite(const IDataNode& data) {
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
    sprite.color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    sprite.textureId = static_cast<uint16_t>(data.getInt("textureId", 0));
    sprite.layer = static_cast<uint16_t>(data.getInt("layer", 0));

    m_sprites.push_back(sprite);
}

void SceneCollector::parseSpriteBatch(const IDataNode& data) {
    // Get sprites child node and iterate
    IDataNode* spritesNode = data.getChildReadOnly("sprites");
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
    chunk.tiles = nullptr; // TODO: Parse tile array
    chunk.tileCount = 0;

    m_tilemaps.push_back(chunk);
}

void SceneCollector::parseText(const IDataNode& data) {
    TextCommand text;
    text.x = static_cast<float>(data.getDouble("x", 0.0));
    text.y = static_cast<float>(data.getDouble("y", 0.0));
    text.text = nullptr; // TODO: Copy string to frame allocator
    text.fontId = static_cast<uint16_t>(data.getInt("fontId", 0));
    text.fontSize = static_cast<uint16_t>(data.getInt("fontSize", 16));
    text.color = static_cast<uint32_t>(data.getInt("color", 0xFFFFFFFF));
    text.layer = static_cast<uint16_t>(data.getInt("layer", 0));

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

    // TODO: Compute view and projection matrices from camera params
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
    rect.w = static_cast<float>(data.getDouble("w", 0.0));
    rect.h = static_cast<float>(data.getDouble("h", 0.0));
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

} // namespace grove
