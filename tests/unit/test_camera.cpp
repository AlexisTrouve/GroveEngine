/**
 * Unit Tests: Camera helper (grove::camera) — engine help for seamless zoom/pan.
 *
 * WHAT  : Locks the pure 2D camera math that the engine exposes to consuming games
 *         (Drifterra) so they don't re-derive the projection convention by hand:
 *           - world <-> screen conversions (picking, zoom-to-cursor, HUD placement)
 *           - focusOn / centerOn (place a world point under a screen anchor)
 *           - zoomAt (THE seamless-zoom primitive: keep a world point under the
 *             cursor stationary while changing zoom)
 *           - damp (framerate-independent smoothing for "zoom fluide / momentum")
 *
 * WHY   : The renderer already applies `render:camera {x,y,zoom}` (ortho / zoom), but
 *         the convention (camera (x,y) = world coord at the viewport TOP-LEFT corner,
 *         screen = zoom*(world-cam)) was undocumented and unproven. A game that gets
 *         the pivot/inverse wrong ships a broken zoom. These helpers + this test make
 *         the contract a verified fact, not a comment.
 *
 * HOW   : Pure header math, no bgfx / no GPU — fully headless. The cross-check that the
 *         helper actually matches the renderer's matrices lives in
 *         test_scene_collector.cpp (it owns the SceneCollector infra). Here we lock the
 *         helper's internal consistency (round-trips, anchor invariants, smoothing).
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Scene/Camera.h"

#include <cmath>

using namespace grove;
using Catch::Matchers::WithinAbs;

// ============================================================================
// world <-> screen
// ============================================================================

TEST_CASE("Camera - worldToScreen scales by zoom around the top-left origin", "[camera][unit]") {
    camera::CameraView c;
    c.x = 100.0f; c.y = 200.0f; c.zoom = 2.0f; c.viewportW = 1280.0f; c.viewportH = 720.0f;

    float sx = 0.0f, sy = 0.0f;

    // The camera origin (x,y) maps to the screen TOP-LEFT (0,0) at any zoom.
    camera::worldToScreen(c, 100.0f, 200.0f, sx, sy);
    REQUIRE_THAT(sx, WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(sy, WithinAbs(0.0f, 0.001f));

    // A world point 100 units right of the camera lands at zoom*100 = 200 px at zoom 2.
    camera::worldToScreen(c, 200.0f, 200.0f, sx, sy);
    REQUIRE_THAT(sx, WithinAbs(200.0f, 0.001f));
    REQUIRE_THAT(sy, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Camera - screenToWorld is the exact inverse of worldToScreen", "[camera][unit]") {
    camera::CameraView c;
    c.x = -50.0f; c.y = 33.0f; c.zoom = 0.25f; c.viewportW = 1920.0f; c.viewportH = 1080.0f;

    // Round-trip an arbitrary world point through screen space and back.
    const float wx0 = 412.5f, wy0 = -77.25f;
    float sx = 0.0f, sy = 0.0f;
    camera::worldToScreen(c, wx0, wy0, sx, sy);

    float wx1 = 0.0f, wy1 = 0.0f;
    camera::screenToWorld(c, sx, sy, wx1, wy1);

    REQUIRE_THAT(wx1, WithinAbs(wx0, 0.001f));
    REQUIRE_THAT(wy1, WithinAbs(wy0, 0.001f));
}

// ============================================================================
// focusOn / centerOn — place a world point under a screen anchor
// ============================================================================

TEST_CASE("Camera - focusOn puts the focal world point exactly under the anchor", "[camera][unit]") {
    const float fx = 1000.0f, fy = -250.0f, zoom = 3.0f, vpW = 800.0f, vpH = 600.0f;
    const float anchorX = 123.0f, anchorY = 456.0f;

    camera::CameraView c = camera::focusOn(fx, fy, zoom, vpW, vpH, anchorX, anchorY);

    float sx = 0.0f, sy = 0.0f;
    camera::worldToScreen(c, fx, fy, sx, sy);
    REQUIRE_THAT(sx, WithinAbs(anchorX, 0.001f));
    REQUIRE_THAT(sy, WithinAbs(anchorY, 0.001f));
    REQUIRE_THAT(c.zoom, WithinAbs(zoom, 0.001f));
}

TEST_CASE("Camera - centerOn lands the focal point at the viewport center", "[camera][unit]") {
    const float fx = 42.0f, fy = 84.0f, zoom = 1.5f, vpW = 1280.0f, vpH = 720.0f;

    camera::CameraView c = camera::centerOn(fx, fy, zoom, vpW, vpH);

    float sx = 0.0f, sy = 0.0f;
    camera::worldToScreen(c, fx, fy, sx, sy);
    REQUIRE_THAT(sx, WithinAbs(vpW * 0.5f, 0.001f));
    REQUIRE_THAT(sy, WithinAbs(vpH * 0.5f, 0.001f));
}

// ============================================================================
// zoomAt — the seamless-zoom primitive
// ============================================================================

TEST_CASE("Camera - zoomAt keeps the world point under the anchor stationary", "[camera][unit]") {
    camera::CameraView c;
    c.x = 0.0f; c.y = 0.0f; c.zoom = 1.0f; c.viewportW = 1280.0f; c.viewportH = 720.0f;

    // Anchor = cursor at an arbitrary screen position.
    const float cursorX = 900.0f, cursorY = 300.0f;

    // World point currently under the cursor (the thing we want to keep pinned).
    float pinX = 0.0f, pinY = 0.0f;
    camera::screenToWorld(c, cursorX, cursorY, pinX, pinY);

    // Zoom in hard toward the cursor.
    camera::CameraView z = camera::zoomAt(c, 8.0f, cursorX, cursorY);

    // The pinned world point must still project to the same screen position.
    float sx = 0.0f, sy = 0.0f;
    camera::worldToScreen(z, pinX, pinY, sx, sy);
    REQUIRE_THAT(sx, WithinAbs(cursorX, 0.01f));
    REQUIRE_THAT(sy, WithinAbs(cursorY, 0.01f));
    REQUIRE_THAT(z.zoom, WithinAbs(8.0f, 0.001f));
}

// ============================================================================
// damp — framerate-independent smoothing
// ============================================================================

TEST_CASE("Camera - damp approaches the target without overshoot", "[camera][unit]") {
    float v = 0.0f;
    const float target = 10.0f;
    for (int i = 0; i < 240; ++i) {       // 4 s at 60 fps
        v = camera::damp(v, target, 6.0f, 1.0f / 60.0f);
        REQUIRE(v <= target + 1e-4f);     // never overshoots
        REQUIRE(v >= 0.0f);               // monotonic toward target from below
    }
    REQUIRE_THAT(v, WithinAbs(target, 0.05f));   // converged
}

TEST_CASE("Camera - damp is framerate independent (two half-steps == one full step)", "[camera][unit]") {
    const float target = 1.0f, rate = 5.0f, dt = 0.1f;

    // One step of dt.
    const float oneStep = camera::damp(0.0f, target, rate, dt);

    // Two steps of dt/2 must close the same exponential gap.
    float twoHalf = camera::damp(0.0f, target, rate, dt * 0.5f);
    twoHalf = camera::damp(twoHalf, target, rate, dt * 0.5f);

    REQUIRE_THAT(twoHalf, WithinAbs(oneStep, 1e-5f));
}

TEST_CASE("Camera - clampZoom bounds the zoom range", "[camera][unit]") {
    REQUIRE_THAT(camera::clampZoom(0.001f, 0.05f, 50.0f), WithinAbs(0.05f, 1e-6f));
    REQUIRE_THAT(camera::clampZoom(999.0f, 0.05f, 50.0f),  WithinAbs(50.0f, 1e-6f));
    REQUIRE_THAT(camera::clampZoom(1.0f, 0.05f, 50.0f),    WithinAbs(1.0f, 1e-6f));
}
