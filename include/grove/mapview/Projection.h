#pragma once

/**
 * grove::mapview::Projection — the projection axis ② (S1a).
 *
 * WHAT  : IProjection maps a world point to the space submitted to the renderer, plus a depth key for
 *         painter's-order sorting. TopDownProjection is the v1 concrete impl.
 *
 * WHY   : Projection is ORTHOGONAL to topology (the classic confusion: isometric is NOT a grid shape — a
 *         square grid renders top-down OR iso). Defining the interface day-one lets iso plug in later with
 *         no rework. Crucially, projection here is CAMERA-INDEPENDENT: for top-down it is the identity in
 *         XY, and the engine's existing camera (render:camera / grove::camera / ZoneNavigator) performs the
 *         pan/zoom/rotation to screen. mapview emits world-space geometry and does not fight the camera.
 *
 * HOW   : Header-only, std-only. TopDownProjection.project(w) = {w.x, w.y} and depthKey = 0 (top-down draw
 *         order comes from the layer field, not depth). A future IsometricProjection will return the skewed
 *         coordinates and a real depthKey (e.g. x+y+z) so the emit stage can sort back-to-front.
 */

#include "grove/mapview/Geometry.h"

namespace grove {
namespace mapview {

// Projection ② — world point -> renderer space, + painter's-order depth.
struct IProjection {
    virtual ~IProjection() = default;

    // Map a world point to the coordinates handed to the renderer.
    virtual RenderPos project(WorldPos w) const = 0;
    // Painter's-order key (larger = drawn later/in-front). Constant for top-down.
    virtual double depthKey(WorldPos w) const = 0;
};

// Straight top-down: render space == world XY; the renderer's camera maps it to the screen.
class TopDownProjection final : public IProjection {
public:
    RenderPos project(WorldPos w) const override { return RenderPos{w.x, w.y}; }
    // No back-to-front ordering needed top-down (layering is the layer field's job).
    double depthKey(WorldPos /*w*/) const override { return 0.0; }
};

} // namespace mapview
} // namespace grove
