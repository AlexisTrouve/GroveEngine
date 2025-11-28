#include "UIRenderer.h"
#include <grove/JsonDataNode.h>

namespace grove {

UIRenderer::UIRenderer(IIO* io)
    : m_io(io) {
}

void UIRenderer::drawRect(float x, float y, float w, float h, uint32_t color) {
    if (!m_io) return;

    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("x", static_cast<double>(x));
    sprite->setDouble("y", static_cast<double>(y));
    sprite->setDouble("width", static_cast<double>(w));
    sprite->setDouble("height", static_cast<double>(h));
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
    sprite->setDouble("x", static_cast<double>(x));
    sprite->setDouble("y", static_cast<double>(y));
    sprite->setDouble("width", static_cast<double>(w));
    sprite->setDouble("height", static_cast<double>(h));
    sprite->setInt("color", static_cast<int>(color));
    sprite->setInt("textureId", textureId);
    sprite->setInt("layer", nextLayer());

    m_io->publish("render:sprite", std::move(sprite));
}

} // namespace grove
