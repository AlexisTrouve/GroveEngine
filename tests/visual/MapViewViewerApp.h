#pragma once

/**
 * tests/visual/MapViewViewerApp — the interactive map-viewer's STATE + input→camera→render logic, extracted
 * so the live window (test_mapview_viewer) AND an automated E2E (test_mapview_viewer_e2e) drive the exact
 * same code.
 *
 * WHY  : the viewer's interactivity (drag-pan, wheel-zoom, key toggles) used to live inline in main(), so it
 *        could only be "verified" by reading it — exactly the "ça devrait marcher" the doctrine forbids for
 *        UI. Pulling it into a class with a pumpEvents()/handleEvent()/renderFrame() surface makes the input
 *        wiring E2E-testable: a test injects real SDL events (SDL_PushEvent) through pumpEvents() and asserts
 *        the camera + visible-cell set respond. (Testability = an architecture constraint, not an afterthought.)
 *
 * HOW  : header-only, owns a MapView + camera + DragPan. handleEvent maps one SDL_Event to a camera/state
 *        change; pumpEvents drains the SDL queue through it (the same loop the live main runs); renderFrame
 *        culls→compiles→(optionally)submits. The renderer/engine/io are all OPTIONAL (nullable) so a headless
 *        logic test can drive the pure input→camera→cull half with no GPU. Wheel-zoom anchors on a tracked
 *        cursor position (updated on motion) instead of SDL_GetMouseState — a global the test harness can't
 *        set, and a source of event/state desync in the live app too.
 */

#include <SDL.h>

#include <functional>
#include <memory>
#include <vector>

#include "BgfxRendererModule.h"
#include "Frame/FramePacket.h"
#include "MapView/SpriteAdapter.h"
#include "Scene/Camera.h"
#include "Scene/DragPan.h"

#include <grove/DebugEngine.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/Lens.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Marker.h"
#include "grove/mapview/Projection.h"

namespace grove {
namespace mvdemo {

// Frame a finite world (world-space min + extent) into a screen: fit the wider axis, centre the shorter one.
inline camera::CameraView fitCamera(double worldMinX, double worldMinY, double worldW, double worldH,
                                    int screenW, int screenH) {
    const float zx = static_cast<float>(screenW) / static_cast<float>(worldW > 0.0 ? worldW : 1.0);
    const float zy = static_cast<float>(screenH) / static_cast<float>(worldH > 0.0 ? worldH : 1.0);
    const float zoom = zx < zy ? zx : zy;                     // fit both axes -> the smaller px/unit
    camera::CameraView c;
    c.zoom = zoom;
    // Centre: the visible span at this zoom is screen/zoom; offset the camera so the world sits in the middle.
    c.x = static_cast<float>(worldMinX) - 0.5f * (static_cast<float>(screenW) / zoom - static_cast<float>(worldW));
    c.y = static_cast<float>(worldMinY) - 0.5f * (static_cast<float>(screenH) / zoom - static_cast<float>(worldH));
    c.viewportW = static_cast<float>(screenW);
    c.viewportH = static_cast<float>(screenH);
    c.rotation = 0.0f;
    return c;
}

// The interactive viewer, as a driveable object. `provider` must outlive the app (MapView holds it by ref).
class ViewerApp {
public:
    ViewerApp(DebugEngine* engine, BgfxRendererModule* renderer, IIO* io, int w, int h,
              mapview::IChunkProvider& provider, std::vector<mapview::FieldDecl> schema, mapview::GridSpec grid,
              std::function<mapview::Lens(bool /*hillshade*/, bool /*banded*/)> lensBuilder,
              camera::CameraView resetCam)
        : engine_(engine), renderer_(renderer), io_(io), w_(w), h_(h),
          lensBuilder_(std::move(lensBuilder)),
          layout_(grid.cellW, grid.cellH), proj_(),
          mv_(std::move(schema), grid, layout_, proj_, provider, /*chunkBudget*/ 128),
          cam_(resetCam), resetCam_(resetCam),
          mouseX_(w / 2), mouseY_(h / 2) {
        rebuildLens();
    }

    // Drain the SDL event queue through handleEvent — the SAME loop the live window runs, so injecting events
    // with SDL_PushEvent in a test exercises the real dispatch, not a parallel copy.
    void pumpEvents() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
    }

    // Map one SDL event to a camera/state change (drag-pan, wheel zoom-to-cursor, H/B/R/Esc).
    void handleEvent(const SDL_Event& e) {
        if (e.type == SDL_QUIT) {
            running_ = false;
        } else if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running_ = false; break;
                case SDLK_h: hillshade_ = !hillshade_; rebuildLens(); break;
                case SDLK_b: banded_ = !banded_; rebuildLens(); break;
                case SDLK_r: cam_ = resetCam_; break;
                default: break;
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            dragPan_.begin(static_cast<float>(e.button.x), static_cast<float>(e.button.y));
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            dragPan_.end();
        } else if (e.type == SDL_MOUSEMOTION) {
            mouseX_ = e.motion.x; mouseY_ = e.motion.y;             // track the cursor for wheel anchoring
            if (dragPan_.active()) {
                const camera::ScreenDelta d = dragPan_.update(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y));
                cam_.x -= d.dx / cam_.zoom;                         // grab-pan: the world follows the cursor
                cam_.y -= d.dy / cam_.zoom;
            }
        } else if (e.type == SDL_MOUSEWHEEL) {
            const float factor = (e.wheel.y > 0) ? 1.12f : (1.0f / 1.12f);
            cam_ = camera::zoomAt(cam_, camera::clampZoom(cam_.zoom * factor, 0.5f, 64.0f),
                                  static_cast<float>(mouseX_), static_cast<float>(mouseY_));
        }
    }

    // Cull → compile the visible cells; if a renderer is wired, submit them (+ marker icons) and step the engine.
    void renderFrame(float dt) {
        const camera::WorldBounds wb = camera::visibleWorldBounds(cam_);
        mv_.setViewport(mapview::Viewport{wb.minX, wb.minY, wb.maxX, wb.maxY});
        mv_.update();

        if (renderer_) {
            const auto& cells = mv_.cells();
            sprites_.resize(cells.size());
            if (!cells.empty()) mapview::render::toSpriteInstances(cells.data(), cells.size(), sprites_.data());
            renderer_->submitSpriteBatch(sprites_.data(), sprites_.size());

            if (io_) {
                for (const auto& md : mv_.markerDraws()) {
                    auto s = std::make_unique<JsonDataNode>("sprite");
                    s->setString("asset", "mvicon");
                    s->setDouble("x", md.x); s->setDouble("y", md.y);
                    s->setDouble("scaleX", md.scale); s->setDouble("scaleY", md.scale);
                    s->setDouble("rotation", md.rotation);
                    s->setInt("layer", md.layer);
                    s->setInt("color", static_cast<int>(0xFFFFFFFFu));
                    io_->publish("render:sprite", std::move(s));
                }
            }
        }

        if (io_) {
            auto camNode = std::make_unique<JsonDataNode>("camera");
            camNode->setDouble("x", cam_.x); camNode->setDouble("y", cam_.y); camNode->setDouble("zoom", cam_.zoom);
            camNode->setInt("viewportX", 0); camNode->setInt("viewportY", 0);
            camNode->setInt("viewportW", w_); camNode->setInt("viewportH", h_);
            io_->publish("render:camera", std::move(camNode));
        }

        if (engine_) engine_->step(dt);
    }

    // --- state accessors (live main scripting + E2E assertions) ---
    const camera::CameraView& camera() const { return cam_; }
    void setCamera(camera::CameraView c) { cam_ = c; }
    void resetCamera() { cam_ = resetCam_; }
    void setMarkers(std::vector<mapview::Marker> m) { mv_.setMarkers(std::move(m)); }
    size_t cellCount() const { return mv_.cellCount(); }
    bool running() const { return running_; }
    bool hillshade() const { return hillshade_; }
    bool banded() const { return banded_; }

private:
    void rebuildLens() { mv_.setLens(lensBuilder_(hillshade_, banded_)); }

    DebugEngine*         engine_;    // nullable: no engine.step in a headless logic test
    BgfxRendererModule*  renderer_;  // nullable: no GPU submit in a headless logic test
    IIO*                 io_;        // nullable
    int                  w_, h_;
    std::function<mapview::Lens(bool, bool)> lensBuilder_;

    mapview::SquareLayout     layout_;   // declared before mv_ (MapView holds it by reference)
    mapview::TopDownProjection proj_;
    mapview::MapView          mv_;

    camera::CameraView cam_;
    camera::CameraView resetCam_;
    camera::DragPan    dragPan_;
    bool  hillshade_ = true;
    bool  banded_ = false;
    bool  running_ = true;
    int   mouseX_, mouseY_;
    std::vector<SpriteInstance> sprites_;
};

} // namespace mvdemo
} // namespace grove
