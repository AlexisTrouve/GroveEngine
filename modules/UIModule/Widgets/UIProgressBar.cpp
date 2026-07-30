#include <grove/IDataNode.h>
#include "UIProgressBar.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace grove {

void UIProgressBar::update(UIContext& ctx, float deltaTime) {
    // Progress bars are read-only, no interaction
    // Update children
    updateChildren(ctx, deltaTime);
}

void UIProgressBar::render(UIRenderer& renderer) {
    // Register with renderer on first render (need 3 entries: bg + fill + text)
    if (!m_registered) {
        m_renderId = renderer.registerEntry();       // Background track
        m_fillRenderId = renderer.registerEntry();   // Fill bar
        m_textRenderId = renderer.registerEntry();   // Text
        m_registered = true;
        // Set destroy callback to unregister all
        setDestroyCallback([&renderer, fillId = m_fillRenderId, textId = m_textRenderId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(fillId);
            renderer.unregisterEntry(textId);
        });
    }

    // Retained mode: only publish if changed
    int bgLayer = renderer.nextLayer();
    if (frame.active()) {
        if (!m_frameRegistered) { m_frameId = renderer.registerEntry(); m_frameRegistered = true; }
        frame.emit(renderer, m_frameId, absX, absY, width, height, bgTintColor, bgLayer);
        renderer.updateRect(m_renderId, 0, 0, 0, 0, 0, renderer.nextLayer());   // flat track idle
    } else {
        if (useBgTexture && bgTextureId > 0) {
            renderer.updateSprite(m_renderId, absX, absY, width, height, bgTextureId, bgTintColor, bgLayer);
        } else {
            renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, bgLayer);
        }
        if (m_frameRegistered) UIFrame::collapse(renderer, m_frameId, renderer.nextLayer());
    }

    // Render fill based on progress
    int fillLayer = renderer.nextLayer();
    // The filled rect, whichever way the bar runs: horizontal grows from the left, vertical from the
    // bottom. Computed once so the flat and 9-slice looks below cannot drift apart.
    const float fillW = horizontal ? progress * width  : width;
    const float fillH = horizontal ? height            : progress * height;
    const float fillX = absX;
    const float fillY = horizontal ? absY : absY + height - fillH;

    if (fillFrame.active()) {
        if (!m_fillFrameRegistered) { m_fillFrameId = renderer.registerEntry(); m_fillFrameRegistered = true; }
        fillFrame.emit(renderer, m_fillFrameId, fillX, fillY, fillW, fillH, fillColor, fillLayer);
        renderer.updateRect(m_fillRenderId, 0, 0, 0, 0, 0, renderer.nextLayer());   // flat fill idle
    } else {
        if (useFillTexture && fillTextureId > 0) {
            renderer.updateSprite(m_fillRenderId, fillX, fillY, fillW, fillH, fillTextureId, fillTintColor, fillLayer);
        } else {
            renderer.updateRect(m_fillRenderId, fillX, fillY, fillW, fillH, fillColor, fillLayer);
        }
        if (m_fillFrameRegistered) UIFrame::collapse(renderer, m_fillFrameId, renderer.nextLayer());
    }

    // Render percentage text if enabled
    if (showText) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0) << (progress * 100.0f) << "%";
        std::string progressText = oss.str();

        int textLayer = renderer.nextLayer();
        float textX = absX + width * 0.5f;
        float textY = absY + height * 0.5f;
        renderer.updateText(m_textRenderId, textX, textY, progressText, fontSize, textColor, textLayer);
    }

    // Render children on top
    renderChildren(renderer);
}

void UIProgressBar::setProgress(float newProgress) {
    progress = std::max(0.0f, std::min(1.0f, newProgress));
}

// Rendre le remplissage, le texte et les DEUX chromes 9-slice (piste et remplissage) que la base
// ignore. Les deux drapeaux paresseux repassent à false avec leurs ids — les laisser à true ferait
// disparaître le cadre pour de bon après un cycle cacher/montrer. Verrouillé par IT_067.
void UIProgressBar::releaseRenderEntries(UIRenderer& renderer) {
    if (m_frameId != 0)      { renderer.unregisterEntry(m_frameId);      m_frameId = 0; }
    m_frameRegistered = false;
    if (m_fillFrameId != 0)  { renderer.unregisterEntry(m_fillFrameId);  m_fillFrameId = 0; }
    m_fillFrameRegistered = false;
    if (m_fillRenderId != 0) { renderer.unregisterEntry(m_fillRenderId); m_fillRenderId = 0; }
    if (m_textRenderId != 0) { renderer.unregisterEntry(m_textRenderId); m_textRenderId = 0; }
    UIWidget::releaseRenderEntries(renderer);   // piste (m_renderId) + drapeaux + enfants
}


std::unique_ptr<UIWidget> UIProgressBar::fromNode(const IDataNode& node) {
    auto progressBar = std::make_unique<UIProgressBar>();
    progressBar->setProgress(static_cast<float>(node.getDouble("progress", 0.5)));
    progressBar->horizontal = node.getBool("horizontal", true);
    progressBar->showText = node.getBool("showText", false);

    auto& mutableNode = const_cast<IDataNode&>(node);
    if (auto* style = mutableNode.getChildReadOnly("style")) {
        std::string bgColorStr = style->getString("bgColor", "0x34495eFF");
        if (bgColorStr.size() >= 2 && (bgColorStr.substr(0, 2) == "0x" || bgColorStr.substr(0, 2) == "0X")) {
            progressBar->bgColor = static_cast<uint32_t>(std::stoul(bgColorStr, nullptr, 16));
        }
        std::string fillColorStr = style->getString("fillColor", "0x2ecc71FF");
        if (fillColorStr.size() >= 2 && (fillColorStr.substr(0, 2) == "0x" || fillColorStr.substr(0, 2) == "0X")) {
            progressBar->fillColor = static_cast<uint32_t>(std::stoul(fillColorStr, nullptr, 16));
        }
        std::string textColorStr = style->getString("textColor", "0xFFFFFFFF");
        if (textColorStr.size() >= 2 && (textColorStr.substr(0, 2) == "0x" || textColorStr.substr(0, 2) == "0X")) {
            progressBar->textColor = static_cast<uint32_t>(std::stoul(textColorStr, nullptr, 16));
        }
        progressBar->fontSize = static_cast<float>(style->getDouble("fontSize", 14.0));
    }

    // 9-slice FRAMES (optional) — `frame` = the track, `fillFrame` = the fill.
    if (auto* f = mutableNode.getChildReadOnly("frame")) progressBar->frame.parse(*f);
    if (auto* ff = mutableNode.getChildReadOnly("fillFrame")) progressBar->fillFrame.parse(*ff);

    return progressBar;
}

} // namespace grove
