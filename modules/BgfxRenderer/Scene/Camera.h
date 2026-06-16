#pragma once

/**
 * grove::camera — pure 2D camera math for consuming games (header-only).
 *
 * WHAT  : Helpers to drive the engine's `render:camera {x,y,zoom}` topic without
 *         re-deriving the projection convention by hand. Conversions (world<->screen),
 *         anchored framing (focusOn/centerOn), the seamless-zoom primitive (zoomAt),
 *         and framerate-independent smoothing (damp).
 *
 * WHY   : The BgfxRenderer applies the camera as an orthographic projection scaled by
 *         `zoom` (see SceneCollector::parseCamera). The convention is NOT obvious:
 *           - camera (x,y) is the world coordinate at the viewport's TOP-LEFT corner
 *             (NOT the center — unlike `render:sprite` whose x,y is the sprite CENTER);
 *           - the relation collapses to:  screen = zoom * (world - cameraTopLeft)
 *             and its inverse           :  world  = cameraTopLeft + screen / zoom.
 *         A game that gets the pivot or the inverse wrong ships a broken zoom (e.g.
 *         zoom drifts away from the cursor). Owning this math in the engine — the side
 *         that owns the projection — makes "seamless zoom" a one-liner for the game and
 *         keeps the convention in exactly one place. Locked by tests/unit/test_camera.cpp
 *         and cross-checked against the real renderer matrices in test_scene_collector.cpp.
 *
 * HOW   : Header-only, zero dependencies beyond <cmath>, no bgfx — so a static-link host
 *         (Drifterra links BgfxRenderer_static, whose source dir is a PUBLIC include) just
 *         does  #include "Scene/Camera.h"  and gets these for free, also unit-testable
 *         headless. All functions are inline/pure; CameraView mirrors the fields a game
 *         already publishes on `render:camera`.
 */

#include <cmath>

namespace grove {
namespace camera {

// ----------------------------------------------------------------------------
// CameraView — mirrors the payload of `render:camera`. (x,y) = world coordinate
// at the viewport TOP-LEFT corner; zoom = screen-pixels per world-unit (>1 zoom-in,
// <1 zoom-out). viewportW/H are needed only for the anchored/centered helpers.
// ----------------------------------------------------------------------------
struct CameraView {
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 1.0f;
    float viewportW = 1280.0f;
    float viewportH = 720.0f;
};

// ----------------------------------------------------------------------------
// world -> screen (pixels, top-left origin, y-down). The canonical relation the
// renderer's matrices implement: screen = zoom * (world - cameraTopLeft).
// ----------------------------------------------------------------------------
inline void worldToScreen(const CameraView& c, float worldX, float worldY,
                          float& outScreenX, float& outScreenY) {
    outScreenX = c.zoom * (worldX - c.x);
    outScreenY = c.zoom * (worldY - c.y);
}

// ----------------------------------------------------------------------------
// screen -> world. Exact inverse of worldToScreen — used for picking and to find
// "what world point is under the cursor" before a zoom step.
// HOW: divide by zoom; guard a zero zoom (degenerate input) to avoid producing NaN.
// ----------------------------------------------------------------------------
inline void screenToWorld(const CameraView& c, float screenX, float screenY,
                          float& outWorldX, float& outWorldY) {
    const float z = (c.zoom != 0.0f) ? c.zoom : 1.0f;
    outWorldX = c.x + screenX / z;
    outWorldY = c.y + screenY / z;
}

// ----------------------------------------------------------------------------
// focusOn — build a CameraView so that world point (focalX,focalY) projects exactly
// to the screen anchor (anchorScreenX,anchorScreenY) at the given zoom.
// WHY: lets a game frame a target ("put this planet at this HUD slot") without solving
//      for the top-left corner itself.
// HOW: from screen = zoom*(world - cam)  =>  cam = world - anchor/zoom.
// ----------------------------------------------------------------------------
inline CameraView focusOn(float focalX, float focalY, float zoom,
                          float viewportW, float viewportH,
                          float anchorScreenX, float anchorScreenY) {
    const float z = (zoom != 0.0f) ? zoom : 1.0f;
    CameraView c;
    c.zoom = zoom;
    c.viewportW = viewportW;
    c.viewportH = viewportH;
    c.x = focalX - anchorScreenX / z;
    c.y = focalY - anchorScreenY / z;
    return c;
}

// ----------------------------------------------------------------------------
// centerOn — focalpoint at the viewport center (the common "look at" case).
// ----------------------------------------------------------------------------
inline CameraView centerOn(float focalX, float focalY, float zoom,
                           float viewportW, float viewportH) {
    return focusOn(focalX, focalY, zoom, viewportW, viewportH,
                   viewportW * 0.5f, viewportH * 0.5f);
}

// ----------------------------------------------------------------------------
// zoomAt — THE seamless-zoom primitive. Change zoom while keeping the world point
// currently under a fixed screen anchor (e.g. the cursor, or the screen center)
// stationary. This is what makes "zoom toward the cursor" feel natural instead of
// drifting toward the top-left origin.
// HOW: 1. find the world point under the anchor at the current zoom (screenToWorld);
//      2. re-frame so that same world point stays under the anchor at the new zoom.
// ----------------------------------------------------------------------------
inline CameraView zoomAt(const CameraView& c, float newZoom,
                         float anchorScreenX, float anchorScreenY) {
    float pinX = 0.0f, pinY = 0.0f;
    screenToWorld(c, anchorScreenX, anchorScreenY, pinX, pinY);
    return focusOn(pinX, pinY, newZoom, c.viewportW, c.viewportH,
                   anchorScreenX, anchorScreenY);
}

// ----------------------------------------------------------------------------
// damp — framerate-independent exponential approach toward a target. Use it per frame
// for smooth pan/zoom ("zoom fluide / momentum préservé"): zoom = damp(zoom, target, k, dt).
// WHY: a fixed lerp factor (v += (target-v)*0.1) is framerate-dependent and jitters when
//      dt varies; the exponential form closes a constant *fraction of remaining gap per
//      second*, so the motion is identical at 30 or 144 fps.
// HOW: fraction closed this frame = 1 - e^{-rate*dt}; larger rate = snappier. dt in seconds.
//      Works on any scalar (zoom, x, y) — call it once per axis.
// ----------------------------------------------------------------------------
inline float damp(float current, float target, float rate, float dt) {
    const float t = 1.0f - std::exp(-rate * dt);
    return current + (target - current) * t;
}

// ----------------------------------------------------------------------------
// clampZoom — convenience bound so the game can't zoom past sane limits.
// ----------------------------------------------------------------------------
inline float clampZoom(float zoom, float minZoom, float maxZoom) {
    return zoom < minZoom ? minZoom : (zoom > maxZoom ? maxZoom : zoom);
}

} // namespace camera
} // namespace grove
