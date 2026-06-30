#pragma once

/**
 * grove::mapview::render::SpriteAdapter — bridge the neutral map-view emit to the renderer (P1).
 *
 * WHAT  : Maps grove::mapview::CellDraw (the renderer-independent draw unit emitted by MapView) to the
 *         engine's grove::SpriteInstance, ready for BgfxRendererModule::submitSpriteBatch (the bulk path).
 *
 * WHY   : This is the ONLY renderer-coupled piece of the map viewer — it lives here (in BgfxRenderer), not in
 *         the pure core, so grove::mapview stays 100% renderer-independent. The mapping is intentionally
 *         trivial (a near 1:1 field copy): that triviality is the payoff of having designed CellDraw to carry
 *         exactly what the bulk path needs (world-space centre, size, rotation, layer, tint).
 *
 * HOW   : Header-only, std-only — no GPU, so it is headless unit-testable (FramePacket.h is a plain struct).
 *         CellDraw positions are world-space CENTRE and CellDraw w/h are world-space size, which is exactly
 *         what SpriteInstance expects (x,y = centre, scaleX/scaleY = size multipliers on the unit quad).
 *         textureId = 0 selects the renderer's built-in 1x1 white texture, so the rgba tint shows as a solid
 *         colour. doubles are narrowed to float at this boundary (the renderer is float).
 */

#include <cstddef>

#include "Frame/FramePacket.h"        // grove::SpriteInstance
#include "grove/mapview/CellDraw.h"   // grove::mapview::CellDraw

namespace grove {
namespace mapview {
namespace render {

// Convert one CellDraw to a solid-colour SpriteInstance (white texture, rgba tint).
inline SpriteInstance toSpriteInstance(const CellDraw& c) {
    SpriteInstance s;
    s.x = static_cast<float>(c.x);          // world-space centre (CellDraw is already centre)
    s.y = static_cast<float>(c.y);
    s.scaleX = static_cast<float>(c.w);     // size multiplier on the unit quad = world-space cell size
    s.scaleY = static_cast<float>(c.h);
    s.rotation = static_cast<float>(c.rotation);
    s.u0 = 0.0f; s.v0 = 0.0f; s.u1 = 1.0f; s.v1 = 1.0f;  // full white texel
    s.textureId = 0.0f;                     // built-in 1x1 white texture -> solid colour
    s.layer = static_cast<float>(c.layer);
    s.padding0 = 0.0f;
    s.reserved[0] = s.reserved[1] = s.reserved[2] = s.reserved[3] = 0.0f;  // no clip rect
    s.r = c.color.r; s.g = c.color.g; s.b = c.color.b; s.a = c.color.a;
    return s;
}

// Convert `n` CellDraws into the caller-provided `out` buffer (no allocation); returns the count written.
// Pair with MapView::drainCells -> submitSpriteBatch for the per-frame path.
inline size_t toSpriteInstances(const CellDraw* cells, size_t n, SpriteInstance* out) {
    for (size_t i = 0; i < n; ++i) out[i] = toSpriteInstance(cells[i]);
    return n;
}

} // namespace render
} // namespace mapview
} // namespace grove
