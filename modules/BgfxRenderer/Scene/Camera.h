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

// ----------------------------------------------------------------------------
// Culling — the world-space rectangle the viewport currently covers, and an AABB visibility
// test against it.
// WHY: lets the GAME skip both SUBMITTING and COMPUTING (rotation/hierarchy/anim) for off-screen
//      objects — the expensive presentation work — without freezing its simulation (that keeps
//      ticking elsewhere). The renderer can reuse the same bounds to cull draws.
// HOW: from screen = zoom·(world − cam), the visible world span is [cam, cam + viewport/zoom].
//      isVisible is a standard AABB overlap, widened by `margin` (world units) to avoid pop-in
//      at the edges when culling slightly before something is strictly on-screen.
// ----------------------------------------------------------------------------
struct WorldBounds {
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
};

inline WorldBounds visibleWorldBounds(const CameraView& c) {
    const float z = (c.zoom != 0.0f) ? c.zoom : 1.0f;
    WorldBounds b;
    b.minX = c.x;
    b.minY = c.y;
    b.maxX = c.x + c.viewportW / z;
    b.maxY = c.y + c.viewportH / z;
    return b;
}

// AABB [x, x+w] × [y, y+h] (partly) inside the bounds, widened by `margin`? Overload taking
// precomputed bounds — used by render passes that cull many instances against one view.
inline bool isVisible(const WorldBounds& b, float x, float y, float w, float h, float margin = 0.0f) {
    return (x + w >= b.minX - margin) && (x <= b.maxX + margin) &&
           (y + h >= b.minY - margin) && (y <= b.maxY + margin);
}

// Is the world-space AABB (partly) within the view, widened by `margin`?
inline bool isVisible(const CameraView& c, float x, float y, float w, float h, float margin = 0.0f) {
    return isVisible(visibleWorldBounds(c), x, y, w, h, margin);
}

// ----------------------------------------------------------------------------
// Zone navigation primitives (see docs/design/zone-navigation.md). fitBounds frames a world AABB,
// clampPanToBounds keeps the view inside one, worldPanForScreen scales pan by zoom. All PURE — the
// soft-magnet / elastic feel is the ZoneNavigator damping toward these targets, not these helpers.
// ----------------------------------------------------------------------------

// The camera that FRAMES a zone: centered on it, zoomed so the whole AABB fits the viewport (the
// constraining dimension). `margin` in [0,0.9) leaves padding (0.1 => the zone fills 90% of the
// viewport). This is the magnet's target view.
inline CameraView fitBounds(const WorldBounds& b, float viewportW, float viewportH, float margin = 0.0f) {
    const float zw = (b.maxX - b.minX) > 1e-6f ? (b.maxX - b.minX) : 1e-6f;
    const float zh = (b.maxY - b.minY) > 1e-6f ? (b.maxY - b.minY) : 1e-6f;
    const float zx = viewportW / zw;
    const float zy = viewportH / zh;
    float zoom = (zx < zy) ? zx : zy;                       // fit the constraining dimension
    const float m = margin < 0.0f ? 0.0f : (margin > 0.9f ? 0.9f : margin);
    zoom *= (1.0f - m);                                     // leave padding
    const float cx = (b.minX + b.maxX) * 0.5f;
    const float cy = (b.minY + b.maxY) * 0.5f;
    return centerOn(cx, cy, zoom, viewportW, viewportH);
}

// Keep the camera's visible rectangle inside a zone AABB, EXPANDED by an optional margin (world
// units, per axis) so the view can overshoot the zone edges a little — letting part of the screen
// sit slightly outside a POI for context. margin 0 = the strict hard clamp. If the (expanded) zone
// is smaller than the view on an axis, it's centered on that axis.
inline void clampPanToBounds(CameraView& c, const WorldBounds& b, float marginX = 0.0f, float marginY = 0.0f) {
    const float z = (c.zoom != 0.0f) ? c.zoom : 1.0f;
    const float visW = c.viewportW / z;
    const float visH = c.viewportH / z;
    const float minX = b.minX - marginX, maxX = b.maxX + marginX;
    const float minY = b.minY - marginY, maxY = b.maxY + marginY;

    if (visW >= (maxX - minX))    c.x = (minX + maxX) * 0.5f - visW * 0.5f;  // zone+margin < view -> center
    else if (c.x < minX)          c.x = minX;
    else if (c.x + visW > maxX)   c.x = maxX - visW;

    if (visH >= (maxY - minY))    c.y = (minY + maxY) * 0.5f - visH * 0.5f;
    else if (c.y < minY)          c.y = minY;
    else if (c.y + visH > maxY)   c.y = maxY - visH;
}

// World-space pan delta for an on-screen drag/velocity at the current zoom. Dividing by zoom gives a
// CONSTANT on-screen feel: the same screen drag covers less world zoomed in (precise), more zoomed
// out (fast traversal).
inline float worldPanForScreen(float screenDelta, float zoom) {
    const float z = (zoom != 0.0f) ? zoom : 1.0f;
    return screenDelta / z;
}

} // namespace camera
} // namespace grove
