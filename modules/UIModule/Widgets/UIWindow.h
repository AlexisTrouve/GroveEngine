#pragma once

#include "../Core/UIWidget.h"
#include <string>
#include <cstdint>

namespace grove {

/**
 * @brief In-app window — a draggable, stackable, titled container rendered INSIDE the app's single
 *        window (NOT an OS window). UI framework slice 3b.
 *
 * Chrome: a title bar (title text + a close button) at the top; the content area is everything
 * below it and CLIPS its children (reuses the slice-2 clip stack). The window is OPAQUE: a click
 * anywhere in its bounds is absorbed (hit-test returns the window), so it never leaks to widgets
 * behind it. Z-order + drag + close are driven by UIModule (slice 3b-2) using the geometry helpers
 * here and bringToFront() (slice 3a).
 */
class UIWindow : public UIWidget {
public:
    ~UIWindow() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "window"; }

    // Opaque + clips content to the area BELOW the title bar.
    bool clipsHitTest() const override { return true; }
    void hitClipRect(float& outX, float& outY, float& outW, float& outH) const override;
    void releaseRenderEntries(UIRenderer& renderer) override;

    // Geometry helpers (screen px, post-layout) — used by the hit-test absorb + UIModule interaction.
    bool pointInWindow(float x, float y) const;
    bool pointInTitleBar(float x, float y) const;
    bool pointInCloseButton(float x, float y) const;
    bool pointInResizeGrip(float x, float y) const;   // bottom-right corner grip (resize)
    void contentRect(float& outX, float& outY, float& outW, float& outH) const;

    // Properties (data-driven via UITree).
    std::string title;
    float titleBarHeight = 28.0f;
    bool closable = true;
    bool draggable = true;
    bool resizable = true;
    float resizeGripSize = 14.0f;
    float minWidth = 120.0f;
    float minHeight = 70.0f;
    uint32_t bgColor = 0x222a33F0;        // content background (semi-transparent)
    uint32_t titleBarColor = 0x3a6ea5FF;
    uint32_t titleColor = 0xFFFFFFFF;
    uint32_t closeColor = 0xc0392bFF;
    float fontSize = 14.0f;
    float closeButtonSize = 18.0f;
    float padding = 6.0f;

private:
    // Extra retained entries beyond the base m_renderId (= the content background).
    uint32_t m_titleBarId = 0;
    uint32_t m_titleTextId = 0;
    uint32_t m_closeId = 0;
    uint32_t m_resizeGripId = 0;
};

} // namespace grove
