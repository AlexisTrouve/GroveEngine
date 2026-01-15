#include "UIRenderer.h"
#include <grove/JsonDataNode.h>
#include <spdlog/spdlog.h>
#include <cmath>

namespace grove {

UIRenderer::UIRenderer(IIO* io)
    : m_io(io) {
}

// ============================================================================
// Retained Mode Implementation
// ============================================================================

uint32_t UIRenderer::registerEntry() {
    return m_nextRenderId++;
}

void UIRenderer::unregisterEntry(uint32_t renderId) {
    auto it = m_entries.find(renderId);
    if (it != m_entries.end()) {
        // Send remove message based on type
        if (it->second.type == RenderEntryType::Text) {
            publishTextRemove(renderId);
        } else {
            publishSpriteRemove(renderId);
        }
        m_entries.erase(it);
    }
}

static bool floatEqual(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) < epsilon;
}

bool UIRenderer::updateRect(uint32_t renderId, float x, float y, float w, float h, uint32_t color, int layer) {
    if (!m_io) return false;

    auto it = m_entries.find(renderId);
    if (it == m_entries.end()) {
        // New entry - add it
        RenderEntry entry;
        entry.type = RenderEntryType::Rect;
        entry.x = x;
        entry.y = y;
        entry.w = w;
        entry.h = h;
        entry.color = color;
        entry.textureId = 0;
        entry.layer = layer;  // Store initial layer (stable)
        m_entries[renderId] = entry;
        publishSpriteAdd(renderId, x, y, w, h, 0, color, layer);
        return true;
    }

    // Check if changed (ignore layer - it's set once at registration)
    RenderEntry& entry = it->second;
    bool changed = !floatEqual(entry.x, x) || !floatEqual(entry.y, y) ||
                   !floatEqual(entry.w, w) || !floatEqual(entry.h, h) ||
                   entry.color != color;

    if (changed) {
        entry.x = x;
        entry.y = y;
        entry.w = w;
        entry.h = h;
        entry.color = color;
        // Keep original layer (don't update it)
        publishSpriteUpdate(renderId, x, y, w, h, 0, color, entry.layer);
        return true;
    }

    return false;  // No change, no publish
}

bool UIRenderer::updateText(uint32_t renderId, float x, float y, const std::string& text, float fontSize, uint32_t color, int layer) {
    if (!m_io) return false;

    auto it = m_entries.find(renderId);
    if (it == m_entries.end()) {
        // New entry - add it
        RenderEntry entry;
        entry.type = RenderEntryType::Text;
        entry.x = x;
        entry.y = y;
        entry.text = text;
        entry.fontSize = fontSize;
        entry.color = color;
        entry.layer = layer;  // Store initial layer (stable)
        m_entries[renderId] = entry;
        publishTextAdd(renderId, x, y, text, fontSize, color, layer);
        return true;
    }

    // Check if changed (ignore layer - it's set once at registration)
    RenderEntry& entry = it->second;
    bool changed = !floatEqual(entry.x, x) || !floatEqual(entry.y, y) ||
                   entry.text != text || !floatEqual(entry.fontSize, fontSize) ||
                   entry.color != color;

    if (changed) {
        entry.x = x;
        entry.y = y;
        entry.text = text;
        entry.fontSize = fontSize;
        entry.color = color;
        // Keep original layer (don't update it)
        publishTextUpdate(renderId, x, y, text, fontSize, color, entry.layer);
        return true;
    }

    return false;
}

bool UIRenderer::updateSprite(uint32_t renderId, float x, float y, float w, float h, int textureId, uint32_t color, int layer) {
    if (!m_io) return false;

    auto it = m_entries.find(renderId);
    if (it == m_entries.end()) {
        // New entry - add it
        RenderEntry entry;
        entry.type = RenderEntryType::Sprite;
        entry.x = x;
        entry.y = y;
        entry.w = w;
        entry.h = h;
        entry.textureId = textureId;
        entry.color = color;
        entry.layer = layer;  // Store initial layer (stable)
        m_entries[renderId] = entry;
        publishSpriteAdd(renderId, x, y, w, h, textureId, color, layer);
        return true;
    }

    // Check if changed (ignore layer - it's set once at registration)
    RenderEntry& entry = it->second;
    bool changed = !floatEqual(entry.x, x) || !floatEqual(entry.y, y) ||
                   !floatEqual(entry.w, w) || !floatEqual(entry.h, h) ||
                   entry.textureId != textureId || entry.color != color;

    if (changed) {
        entry.x = x;
        entry.y = y;
        entry.w = w;
        entry.h = h;
        entry.textureId = textureId;
        entry.color = color;
        // Keep original layer (don't update it)
        publishSpriteUpdate(renderId, x, y, w, h, textureId, color, entry.layer);
        return true;
    }

    return false;
}

void UIRenderer::publishSpriteAdd(uint32_t renderId, float x, float y, float w, float h, int textureId, uint32_t color, int layer) {
    spdlog::info("📤 [UIRenderer] Publishing render:sprite:add - renderId={}, center=({:.1f},{:.1f}), scale={}x{}, textureId={}, layer={}",
        renderId, x + w * 0.5f, y + h * 0.5f, w, h, textureId, layer);

    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setInt("renderId", static_cast<int>(renderId));
    sprite->setDouble("x", static_cast<double>(x + w * 0.5f));
    sprite->setDouble("y", static_cast<double>(y + h * 0.5f));
    sprite->setDouble("scaleX", static_cast<double>(w));
    sprite->setDouble("scaleY", static_cast<double>(h));
    sprite->setDouble("rotation", 0.0);
    sprite->setDouble("u0", 0.0);
    sprite->setDouble("v0", 0.0);
    sprite->setDouble("u1", 1.0);
    sprite->setDouble("v1", 1.0);
    sprite->setInt("color", static_cast<int>(color));
    sprite->setInt("textureId", textureId);
    sprite->setInt("layer", layer);
    m_io->publish("render:sprite:add", std::move(sprite));
}

void UIRenderer::publishSpriteUpdate(uint32_t renderId, float x, float y, float w, float h, int textureId, uint32_t color, int layer) {
    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setInt("renderId", static_cast<int>(renderId));
    sprite->setDouble("x", static_cast<double>(x + w * 0.5f));
    sprite->setDouble("y", static_cast<double>(y + h * 0.5f));
    sprite->setDouble("scaleX", static_cast<double>(w));
    sprite->setDouble("scaleY", static_cast<double>(h));
    sprite->setDouble("rotation", 0.0);
    sprite->setDouble("u0", 0.0);
    sprite->setDouble("v0", 0.0);
    sprite->setDouble("u1", 1.0);
    sprite->setDouble("v1", 1.0);
    sprite->setInt("color", static_cast<int>(color));
    sprite->setInt("textureId", textureId);
    sprite->setInt("layer", layer);
    m_io->publish("render:sprite:update", std::move(sprite));
}

void UIRenderer::publishSpriteRemove(uint32_t renderId) {
    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setInt("renderId", static_cast<int>(renderId));
    m_io->publish("render:sprite:remove", std::move(sprite));
}

void UIRenderer::publishTextAdd(uint32_t renderId, float x, float y, const std::string& text, float fontSize, uint32_t color, int layer) {
    auto textNode = std::make_unique<JsonDataNode>("text");
    textNode->setInt("renderId", static_cast<int>(renderId));
    textNode->setDouble("x", static_cast<double>(x));
    textNode->setDouble("y", static_cast<double>(y));
    textNode->setString("text", text);
    textNode->setDouble("fontSize", static_cast<double>(fontSize));
    textNode->setInt("color", static_cast<int>(color));
    textNode->setInt("layer", layer);
    m_io->publish("render:text:add", std::move(textNode));
}

void UIRenderer::publishTextUpdate(uint32_t renderId, float x, float y, const std::string& text, float fontSize, uint32_t color, int layer) {
    auto textNode = std::make_unique<JsonDataNode>("text");
    textNode->setInt("renderId", static_cast<int>(renderId));
    textNode->setDouble("x", static_cast<double>(x));
    textNode->setDouble("y", static_cast<double>(y));
    textNode->setString("text", text);
    textNode->setDouble("fontSize", static_cast<double>(fontSize));
    textNode->setInt("color", static_cast<int>(color));
    textNode->setInt("layer", layer);
    m_io->publish("render:text:update", std::move(textNode));
}

void UIRenderer::publishTextRemove(uint32_t renderId) {
    auto textNode = std::make_unique<JsonDataNode>("text");
    textNode->setInt("renderId", static_cast<int>(renderId));
    m_io->publish("render:text:remove", std::move(textNode));
}

// ============================================================================
// Immediate Mode (Legacy)
// ============================================================================

void UIRenderer::drawRect(float x, float y, float w, float h, uint32_t color) {
    if (!m_io) return;

    // DEBUG: Log color being sent
    static uint32_t lastLoggedColor = 0;
    if (color != lastLoggedColor && (color == 0xFF0000FF || color == 0x00FF00FF)) {
        spdlog::info("UIRenderer::drawRect color=0x{:08X} at ({}, {})", color, x, y);
        lastLoggedColor = color;
    }

    auto sprite = std::make_unique<JsonDataNode>("sprite");
    // Position at center of rect (sprite shader centers quads)
    sprite->setDouble("x", static_cast<double>(x + w * 0.5f));
    sprite->setDouble("y", static_cast<double>(y + h * 0.5f));
    sprite->setDouble("scaleX", static_cast<double>(w));
    sprite->setDouble("scaleY", static_cast<double>(h));
    sprite->setInt("color", static_cast<int>(color));
    sprite->setInt("textureId", 0);  // White/solid color texture
    sprite->setInt("layer", nextLayer());

    m_io->publish("render:sprite", std::move(sprite));
}

void UIRenderer::drawText(float x, float y, const std::string& text, float fontSize, uint32_t color) {
    if (!m_io) return;

    auto textNode = std::make_unique<JsonDataNode>("text");
    textNode->setDouble("x", static_cast<double>(x));
    textNode->setDouble("y", static_cast<double>(y));
    textNode->setString("text", text);
    textNode->setDouble("fontSize", static_cast<double>(fontSize));
    textNode->setInt("color", static_cast<int>(color));
    textNode->setInt("layer", nextLayer());

    m_io->publish("render:text", std::move(textNode));
}

void UIRenderer::drawSprite(float x, float y, float w, float h, int textureId, uint32_t color) {
    if (!m_io) return;

    auto sprite = std::make_unique<JsonDataNode>("sprite");
    // Position at center of sprite (sprite shader centers quads)
    sprite->setDouble("x", static_cast<double>(x + w * 0.5f));
    sprite->setDouble("y", static_cast<double>(y + h * 0.5f));
    sprite->setDouble("scaleX", static_cast<double>(w));
    sprite->setDouble("scaleY", static_cast<double>(h));
    sprite->setInt("color", static_cast<int>(color));
    sprite->setInt("textureId", textureId);
    sprite->setInt("layer", nextLayer());

    m_io->publish("render:sprite", std::move(sprite));
}

} // namespace grove
