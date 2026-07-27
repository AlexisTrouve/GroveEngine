#pragma once

#include "../Core/UIWidget.h"
#include <cstdint>
#include <string>

namespace grove {

/**
 * @brief A widget's 9-slice (nine-patch) CHROME — the shared piece behind every `frame:{...}` block.
 *
 * WHAT : the authored border art (`asset` + its native dims + the four unstretched margins) plus the
 *        three operations every widget needs: parse it from JSON, emit it, and collapse it when the
 *        widget has no frame. One `render:nineslice` draws the whole chrome — corners native,
 *        edges/centre stretched — so a border stays crisp and continuous at any widget size.
 *
 * WHY  : this used to be copy-pasted THREE ways per widget (the JSON parse in UITree, the seven
 *        fields + the `setProp` case in the widget header, the emit/collapse pair in its render).
 *        Two widgets carried it; spreading it to the rest would have multiplied that by eight.
 *        Factored here, adding `frame:` support to a widget is a member, a parse call and an
 *        emit call — the breadth becomes nearly free and only the ART still costs anything.
 *
 * HOW  : deliberately NOT a renderer wrapper — the caller passes the `layer` and keeps ownership of
 *        `renderer.nextLayer()`. The NUMBER and ORDER of nextLayer() calls in a widget's render()
 *        carry its z-order; a helper that allocated layers itself would silently reorder anything
 *        whose branches don't allocate symmetrically (UIButton's frame path allocates four, its flat
 *        path one). Keeping allocation at the call site makes this factoring provably layout-neutral.
 *        The tint is a PARAMETER, not a policy: a button tints its frame with the current state's
 *        bgColor so hover/pressed re-tint for free, while a window passes white (its art carries the
 *        whole look, and tinting by the dark window bg would crush it to near-black).
 */
struct UIFrame {
    // Border art: a streamed asset id (atlas-aware) + its native pixel dims. Empty asset = no frame.
    std::string asset;
    float srcW = 0.0f, srcH = 0.0f;
    // Margin thicknesses in SOURCE pixels (left/right/top/bottom) — the bands that never stretch.
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;

    /// A frame draws only with art AND source dims — without the dims its UVs can't be mapped.
    bool active() const { return !asset.empty() && srcW > 0.0f && srcH > 0.0f; }

    /// Fill from a `frame` JSON block. `inset` sets all four margins; per-side keys override it.
    void parse(const IDataNode& frameNode) {
        asset = frameNode.getString("asset", "");
        srcW  = static_cast<float>(frameNode.getDouble("srcW", 0.0));
        srcH  = static_cast<float>(frameNode.getDouble("srcH", 0.0));
        const double inset = frameNode.getDouble("inset", 0.0);
        left   = static_cast<float>(frameNode.getDouble("left",   inset));
        right  = static_cast<float>(frameNode.getDouble("right",  inset));
        top    = static_cast<float>(frameNode.getDouble("top",    inset));
        bottom = static_cast<float>(frameNode.getDouble("bottom", inset));
    }

    /// Draw the chrome over (x,y,w,h) — (x,y) = top-left CORNER — tinted by `color`, at `layer`.
    void emit(UIRenderer& renderer, uint32_t entryId, float x, float y, float w, float h,
              uint32_t color, int layer) const {
        renderer.updateNineSlice(entryId, x, y, w, h, asset, /*textureId=*/0,
                                 srcW, srcH, left, right, top, bottom, color, layer);
    }

    /// Park the entry at zero size so it never co-draws with the flat look (and so a widget that
    /// turns its frame back off recovers). Zero area => nothing is rasterised, tint is irrelevant.
    static void collapse(UIRenderer& renderer, uint32_t entryId, int layer) {
        renderer.updateNineSlice(entryId, 0, 0, 0, 0, "", 0, 0, 0, 0, 0, 0, 0, 0, layer);
    }
};

} // namespace grove
