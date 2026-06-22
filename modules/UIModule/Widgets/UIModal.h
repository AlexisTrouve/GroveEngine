#pragma once

#include "../Core/UIWidget.h"
#include <cstdint>
#include <string>

namespace grove {

/**
 * @brief Modal dialog — a centered dialog over a full-screen dim backdrop (UI framework slice 5a).
 *
 * When open (visible), it covers the whole viewport with a semi-transparent dim that ABSORBS every
 * click outside the dialog (a focus-trap: nothing behind is reachable). The dialog box is centered
 * and CLIPS its content children (slice-2 clip). Clicking the dim (outside the dialog) closes it.
 * Open/close via ui:modal:open / ui:modal:close; closed = hidden (the base `visible` flag).
 */
class UIModal : public UIWidget {
public:
    ~UIModal() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "modal"; }

    bool clipsHitTest() const override { return true; }
    void hitClipRect(float& outX, float& outY, float& outW, float& outH) const override;
    void releaseRenderEntries(UIRenderer& renderer) override;

    bool pointInBounds(float x, float y) const;     // whole viewport (the trap)
    bool pointInDialog(float x, float y) const;
    void dialogRect(float& outX, float& outY, float& outW, float& outH) const;

    float dialogWidth = 400.0f;
    float dialogHeight = 250.0f;
    uint32_t dimColor = 0x00000099;     // semi-transparent backdrop (focus-trap)
    uint32_t dialogColor = 0x2a3038FF;

private:
    uint32_t m_dialogBgId = 0;
};

} // namespace grove
