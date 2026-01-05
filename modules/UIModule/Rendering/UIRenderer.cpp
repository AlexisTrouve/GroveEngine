#include "UIRenderer.h"
#include <grove/JsonDataNode.h>
#include <spdlog/spdlog.h>

namespace grove {

UIRenderer::UIRenderer(IIO* io)
    : m_io(io) {
}

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
