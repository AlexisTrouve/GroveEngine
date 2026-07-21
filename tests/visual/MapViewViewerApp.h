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

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
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
#include "grove/mapview/Region.h"
#include "grove/mapview/TileChunkStreamer.h"

namespace grove {
namespace mvdemo {

// Pack a mapview Rgba (floats 0..1) into the renderer's 0xRRGGBBAA uint32 (SectorPass::unpackColor order).
inline uint32_t packRGBA8(const mapview::Rgba& c) {
    auto to8 = [](float v) {
        const int i = static_cast<int>(v * 255.0f + 0.5f);
        return static_cast<uint32_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
    };
    return (to8(c.r) << 24) | (to8(c.g) << 16) | (to8(c.b) << 8) | to8(c.a);
}

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
                case SDLK_t: if (tilingEnabled_) { tiling_ = !tiling_; rebuildLens(); } break;
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

    // Cull → compile the visible cells/tiles + overlays; if a renderer/bus is wired, submit them and step the
    // engine. Two mutually-exclusive terrain paths: flat-colour bulk sprites (default) or retained tiles ('T').
    void renderFrame(float dt) {
        const camera::WorldBounds wb = camera::visibleWorldBounds(cam_);
        mv_.setViewport(mapview::Viewport{wb.minX, wb.minY, wb.maxX, wb.maxY});

        // BAKED-MAP path ("gen once"): a static --load world is compiled ONCE into an RGBA texture (the whole
        //   planet) and drawn each frame as a SINGLE world-space quad the camera pans/zooms over — the per-frame
        //   cost stops scaling with the number of cells. Active only when opted in (enableBakedMap, --load) AND
        //   not in the retained-tile 'T' mode. Probes once (doBake): a world with no bounded extent (synthetic
        //   demo) or larger than one texture fails the probe -> we keep the per-frame sprite+LOD path (fallback).
        //   Re-bakes on a lens/hillshade change (bakeDirty_), NEVER on pan/zoom.
        bool drawingBaked = false;
        if (bakedRequested_ && !tiling_) {
            if (!bakeProbed_)                     { bakedActive_ = doBake(); bakeProbed_ = true; bakeDirty_ = false; }
            else if (bakedActive_ && bakeDirty_)  { doBake(); bakeDirty_ = false; }
            drawingBaked = bakedActive_;
        }
        if (drawingBaked) {
            mv_.setCompileCells(false);           // overlays only -> zero per-cell compile per frame (the O(1) win)
            if (!bakeSpritePresent_) addBakeSprite();
        } else {
            mv_.setCompileCells(true);
            // LOD : le zoom caméra (px écran / unité monde) pilote le stride d'échantillonnage per-cellule de
            //   MapView (chemin sprite fallback). En vue d'ensemble, cellules sub-pixel -> MapView en saute ->
            //   coût borné par l'écran ; en zoom, stride 1 -> grain fin. (cf. MapView::setLod).
            mv_.setLod(static_cast<double>(cam_.zoom));
            if (bakeSpritePresent_) removeBakeSprite();   // left baked mode ('T' tiling) -> hide the baked quad
        }
        mv_.update();

        lastTileAdded_ = 0;
        lastTileRemoved_ = 0;
        if (tiling_) {
            // Tiling path: stream chunks in/out of the retained tilemap (add newly-visible, remove departed).
            // The diff is pure logic (runs without a renderer); publishing needs the bus.
            streamTiles();
            if (renderer_) { sprites_.clear(); renderer_->submitSpriteBatch(sprites_.data(), 0); }  // no cells
        } else {
            // Left tiling since last frame -> drop every retained chunk so it doesn't linger under the sprites.
            if (tileStreamer_.residentCount() > 0) flushTiles();
            if (drawingBaked) {
                // The baked map is a RETAINED world-space quad the renderer draws itself; submit no per-cell sprites.
                if (renderer_) { sprites_.clear(); renderer_->submitSpriteBatch(sprites_.data(), 0); }
            } else if (renderer_) {
                const auto& cells = mv_.cells();
                sprites_.resize(cells.size());
                if (!cells.empty()) mapview::render::toSpriteInstances(cells.data(), cells.size(), sprites_.data());
                renderer_->submitSpriteBatch(sprites_.data(), sprites_.size());
            }
        }

        if (io_) {
            // Marker icons: world-space PNG sprites pinned to the terrain (ephemeral, republished each frame).
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
            // Region rings: world-space filled ring-sectors (render:sector), so they pan/zoom with the map.
            for (const auto& rd : mv_.regionDraws()) {
                auto sec = std::make_unique<JsonDataNode>("sector");
                sec->setDouble("cx", rd.cx); sec->setDouble("cy", rd.cy);
                sec->setDouble("r0", rd.r0); sec->setDouble("r1", rd.r1);
                sec->setDouble("a0", rd.a0); sec->setDouble("a1", rd.a1);
                sec->setInt("color", static_cast<int>(packRGBA8(rd.color)));
                sec->setInt("layer", rd.layer);
                io_->publish("render:sector", std::move(sec));
            }

            auto camNode = std::make_unique<JsonDataNode>("camera");
            camNode->setDouble("x", cam_.x); camNode->setDouble("y", cam_.y); camNode->setDouble("zoom", cam_.zoom);
            camNode->setInt("viewportX", 0); camNode->setInt("viewportY", 0);
            camNode->setInt("viewportW", w_); camNode->setInt("viewportH", h_);
            io_->publish("render:camera", std::move(camNode));
        }

        if (engine_) engine_->step(dt);
    }

    // Enable the 'T' tiling toggle: the lens to switch to and the tileset texture id the retained chunks use.
    // The host must have loaded that tileset (render:tilemap:tileset) for the tiles to actually draw.
    void enableTiling(mapview::Lens tileLens, int tilesetTextureId) {
        tileLens_ = std::move(tileLens);
        tilesetTexId_ = tilesetTextureId;
        tilingEnabled_ = true;
    }

    // HUD lens control: swap to an arbitrary active lens (a resource density heatmap), or back to the terrain
    // lens (with the current hillshade/banded/tiling state). Additive — the terrain/tile toggles are unchanged.
    void setLens(mapview::Lens lens) { mv_.setLens(std::move(lens)); bakeDirty_ = true; }
    void useTerrainLens() { rebuildLens(); }

    // Opt into the baked-map path (the viewer's --load default). The bake itself is probed lazily on the first
    //   renderFrame; a world that can't be baked (unbounded extent / larger than one texture) transparently keeps
    //   the per-frame sprite+LOD path, so calling this is always safe.
    void enableBakedMap() { bakedRequested_ = true; }

    // --- state accessors (live main scripting + E2E assertions) ---
    const camera::CameraView& camera() const { return cam_; }
    void setCamera(camera::CameraView c) { cam_ = c; }
    void resetCamera() { cam_ = resetCam_; }
    void setMarkers(std::vector<mapview::Marker> m) { mv_.setMarkers(std::move(m)); }
    void setRegions(std::vector<mapview::Region> r) { mv_.setRegions(std::move(r)); }
    size_t cellCount() const { return mv_.cellCount(); }
    bool running() const { return running_; }
    bool hillshade() const { return hillshade_; }
    bool banded() const { return banded_; }

    // Tiling + overlay introspection (E2E assertions on the retained-tile lifecycle and the region overlays).
    bool   tiling() const { return tiling_; }
    size_t residentTileChunks() const { return tileStreamer_.residentCount(); }
    size_t tileChunkCount() const { return mv_.tileChunkCount(); }
    size_t lastTileAdded() const { return lastTileAdded_; }
    size_t lastTileRemoved() const { return lastTileRemoved_; }
    size_t regionDrawCount() const { return mv_.regionDraws().size(); }

private:
    // Active lens: the tile lens in tiling mode, else the (hillshade/banded-parameterised) sprite lens.
    void rebuildLens() {
        if (tiling_ && tilingEnabled_) mv_.setLens(tileLens_);
        else mv_.setLens(lensBuilder_(hillshade_, banded_));
        bakeDirty_ = true;   // lens/hillshade/banded changed -> the baked texture is stale, re-bake next baked frame
    }

    // Compile the active lens's WHOLE world into one RGBA texture and (re)upload it. Returns true when the baked
    //   texture is ready to draw, false when the world can't be baked (caller keeps the sprite path). The GPU
    //   texture is created ONCE; every re-bake re-uploads pixels to the SAME id, so the retained quad refreshes
    //   in place with no re-add. Mirrors the video slice's runtime-texture path (render:texture:create/upload).
    bool doBake() {
        if (!io_) return false;   // no bus -> can't publish the texture (headless logic test) -> keep sprite path
        std::vector<uint8_t> buf;
        int bw = 0, bh = 0;
        double mnx = 0.0, mny = 0.0, spx = 0.0, spy = 0.0;
        if (!mv_.bakeLensRGBA(buf, bw, bh, mnx, mny, spx, spy)) {
            std::fprintf(stderr, "[mapview] bake: world has no bounded extent -> keeping per-frame sprite path\n");
            return false;
        }
        // Texture-size guard: a world wider/taller than one GPU texture is NEVER silently truncated — log + fall
        //   back to the sprite+LOD path (tiling the bake into a texture grid is a documented follow-on).
        if (bw > kMaxBakeTex || bh > kMaxBakeTex) {
            std::fprintf(stderr, "[mapview] bake: world %dx%d cells exceeds max texture %d px -> keeping sprite+LOD"
                                 " path (tiling the bake is a follow-on)\n", bw, bh, kMaxBakeTex);
            return false;
        }
        bakeCx_ = mnx + spx * 0.5; bakeCy_ = mny + spy * 0.5;   // quad CENTRE (render:sprite anchor = cx,cy)
        bakeSx_ = spx; bakeSy_ = spy;
        if (!bakeTexCreated_) {
            // NOTE the key names: render:texture:create reads "width"/"height" (BgfxRendererModule), whereas
            //   render:texture:upload reads "w"/"h" — mixing them up silently no-ops the create (w<=0 guard) and
            //   the sprite then samples the 4x4 white default. Keep them exact.
            auto t = std::make_unique<JsonDataNode>("tex");
            t->setString("id", kBakeTexId); t->setInt("width", bw); t->setInt("height", bh);
            io_->publish("render:texture:create", std::move(t));
            bakeTexCreated_ = true;
        }
        auto up = std::make_unique<JsonDataNode>("tex");
        up->setString("id", kBakeTexId); up->setInt("w", bw); up->setInt("h", bh);
        up->setBlob("pixels", buf.data(), buf.size());
        io_->publish("render:texture:upload", std::move(up));
        std::fprintf(stdout, "[mapview] baked the lens into a %dx%d texture (whole world -> one quad/frame)\n", bw, bh);
        return true;
    }

    // (Re)add the retained world-space quad that displays the baked texture (idempotent on its renderId).
    void addBakeSprite() {
        if (!io_) return;
        auto s = std::make_unique<JsonDataNode>("sprite");
        s->setInt("renderId", kBakeSpriteRenderId);
        s->setString("asset", kBakeTexId);
        s->setDouble("cx", bakeCx_); s->setDouble("cy", bakeCy_);       // world-space CENTRE
        s->setDouble("scaleX", bakeSx_); s->setDouble("scaleY", bakeSy_);
        s->setInt("color", static_cast<int>(0xFFFFFFFFu));              // white tint -> texel colour unchanged
        s->setInt("layer", kBakeLayer);                                // under regions(900) + markers(1000)
        io_->publish("render:sprite:add", std::move(s));
        bakeSpritePresent_ = true;
    }

    void removeBakeSprite() {
        bakeSpritePresent_ = false;
        if (!io_) return;
        auto s = std::make_unique<JsonDataNode>("sprite");
        s->setInt("renderId", kBakeSpriteRenderId);
        io_->publish("render:sprite:remove", std::move(s));
    }

    // Diff the visible tile chunks against what's resident; publish the add/remove deltas to the retained tilemap.
    void streamTiles() {
        const mapview::TileChunkStreamer::Delta delta = tileStreamer_.sync(mv_.tileChunks());
        lastTileAdded_ = delta.added.size();
        lastTileRemoved_ = delta.removed.size();
        if (!io_) return;  // logic updated; without a bus there is nothing to publish
        for (const auto& tc : delta.added) publishTileAdd(tc);
        for (int id : delta.removed) publishTileRemove(id);
    }

    // Remove every retained chunk (tiling toggled off): sync against an empty set -> all residents become removes.
    void flushTiles() {
        const mapview::TileChunkStreamer::Delta delta = tileStreamer_.sync({});
        lastTileRemoved_ = delta.removed.size();
        if (io_) for (int id : delta.removed) publishTileRemove(id);
    }

    void publishTileAdd(const mapview::TileChunkDraw& tc) {
        std::string tileData;
        tileData.reserve(tc.tiles.size() * 3);
        for (size_t i = 0; i < tc.tiles.size(); ++i) {
            if (i) tileData.push_back(',');
            tileData += std::to_string(tc.tiles[i]);
        }
        auto tm = std::make_unique<JsonDataNode>("tilemap");
        tm->setInt("id", mapview::TileChunkStreamer::chunkId(tc.chunkX, tc.chunkY));
        tm->setDouble("x", tc.worldX); tm->setDouble("y", tc.worldY);
        tm->setInt("width", tc.width); tm->setInt("height", tc.height);
        tm->setInt("tileW", static_cast<int>(tc.tileW)); tm->setInt("tileH", static_cast<int>(tc.tileH));
        tm->setInt("textureId", tilesetTexId_);
        tm->setString("tileData", tileData);
        io_->publish("render:tilemap:add", std::move(tm));
    }

    void publishTileRemove(int id) {
        auto tm = std::make_unique<JsonDataNode>("tilemap");
        tm->setInt("id", id);
        io_->publish("render:tilemap:remove", std::move(tm));
    }

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

    // Live-tiling state ('T'): the tile lens to switch to, its tileset texture id, and the retained-chunk
    // streamer that turns each frame's visible set into render:tilemap:add/remove deltas.
    mapview::Lens              tileLens_;
    int                        tilesetTexId_ = 0;
    bool                       tilingEnabled_ = false;   // enableTiling() called -> 'T' is live
    bool                       tiling_ = false;          // currently drawing tiles (vs flat-colour cells)
    mapview::TileChunkStreamer tileStreamer_;
    size_t                     lastTileAdded_ = 0;        // deltas from the last renderFrame (E2E introspection)
    size_t                     lastTileRemoved_ = 0;

    // Baked-map state ("gen once"): the active lens compiled once into a single RGBA texture, drawn each frame as
    //   one retained world-space quad. bakedRequested_ = opted in (enableBakedMap); bakedActive_ = the probe
    //   succeeded (world is bakeable); bakeProbed_ = probe ran; bakeDirty_ = pixels stale (lens/hillshade changed)
    //   -> re-bake on the next baked frame; bakeTexCreated_/bakeSpritePresent_ track the GPU texture + quad.
    bool   bakedRequested_ = false;
    bool   bakedActive_ = false;
    bool   bakeProbed_ = false;
    bool   bakeDirty_ = true;
    bool   bakeTexCreated_ = false;
    bool   bakeSpritePresent_ = false;
    double bakeCx_ = 0.0, bakeCy_ = 0.0, bakeSx_ = 0.0, bakeSy_ = 0.0;   // baked quad centre + size (world units)
    static constexpr const char* kBakeTexId = "mapview_baked";
    static constexpr int kBakeSpriteRenderId = 900001;   // high id: never collides with UI (from 1) / video sprites
    static constexpr int kBakeLayer = 0;                 // terrain layer: below regions (900) + markers (1000)
    static constexpr int kMaxBakeTex = 16384;            // D3D11/GL max 2D texture side; full-res 1625x812 fits
};

} // namespace mvdemo
} // namespace grove
