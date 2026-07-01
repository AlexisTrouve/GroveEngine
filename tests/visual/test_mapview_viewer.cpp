/**
 * Interactive VIEWER for grove::mapview, in groveEngine (slice S2a).
 *
 * A real window you open and navigate: the synthetic procedural world (MapViewDemoScene) rendered live
 * through the full chain (MapView -> SpriteAdapter -> submitSpriteBatch), with grove::camera driving
 * pan/zoom. This is the "show it in a viewer directly in groveEngine" deliverable — the offscreen PNG
 * (capture_mapview) is only the regression proof of the render; this is the manipulable tool.
 *
 * Controls: left-drag = pan (grab), mouse wheel = zoom toward cursor, H = toggle hillshade,
 *           B = toggle banded/continuous palette, R = reset camera, Esc = quit.
 *
 * Usage: test_mapview_viewer                       (interactive window)
 *        test_mapview_viewer --selftest [out.png]  (headless: scripted pan+zoom over N frames -> PNG,
 *                                                    proves the window/render/camera pipeline without input)
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "Frame/FramePacket.h"
#include "MapView/SpriteAdapter.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include "Scene/Camera.h"
#include "Scene/DragPan.h"

#include <grove/DebugEngine.h>
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include "MapViewDemoScene.h"
#include "PngCapture.h"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

int main(int argc, char** argv) {
    const bool selftest = (argc > 1 && std::string(argv[1]) == "--selftest");
    const std::string outPath = (argc > 2) ? argv[2] : "mapview_viewer_selftest.png";
    const int W = 1280, H = 720;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL: %s\n", SDL_GetError()); return 1; }
    const Uint32 flags = selftest ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    SDL_Window* win = SDL_CreateWindow("grove::mapview viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, flags);
    if (!win) { std::fprintf(stderr, "no window\n"); SDL_Quit(); return 1; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto gIO = mgr.createInstance("mv_view");

    DebugEngine engine;
    engine.initialize();

    auto rendererOwned = std::make_unique<BgfxRendererModule>();
    BgfxRendererModule* renderer = rendererOwned.get();
    {
        auto rCfg = std::make_unique<JsonDataNode>("config");
        rCfg->setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        rCfg->setInt("windowWidth", W); rCfg->setInt("windowHeight", H); rCfg->setBool("vsync", !selftest);
        engine.registerStaticModule("renderer", std::move(rendererOwned), ModuleSystemType::SEQUENTIAL, std::move(rCfg));
    }

    // --- map viewer setup (shared synthetic scene) ---
    mvdemo::ProceduralWorld world;
    mapview::SquareLayout layout(1.0, 1.0);
    mapview::TopDownProjection proj;
    const auto schema = mvdemo::demoSchema();
    mapview::MapView mv(schema, mvdemo::demoGrid(world), layout, proj, world, 128);

    bool hillshade = true, banded = false;
    auto rebuildLens = [&] { mv.setLens(mvdemo::makeTerrainLens(hillshade, banded)); };
    rebuildLens();
    mv.setMarkers(mvdemo::demoMarkers());

    // Register the PNG marker icon with the streaming AssetManager (resolved by render:sprite{asset}).
    {
        auto a = std::make_unique<JsonDataNode>("asset");
        a->setString("id", "mvicon");
        a->setString("path", "assets/textures/1f440.png");
        gIO->publish("asset:register", std::move(a));
    }

    // Camera: world (0,0) at top-left, ~256 world units across the width.
    auto resetCam = [&] {
        camera::CameraView c;
        c.x = 0.0f; c.y = 0.0f; c.zoom = static_cast<float>(W) / 256.0f;
        c.viewportW = static_cast<float>(W); c.viewportH = static_cast<float>(H); c.rotation = 0.0f;
        return c;
    };
    camera::CameraView cam = resetCam();
    camera::DragPan dragPan;

    std::vector<SpriteInstance> sprites;
    auto renderFrame = [&](float dt) {
        const camera::WorldBounds wb = camera::visibleWorldBounds(cam);
        mv.setViewport(mapview::Viewport{wb.minX, wb.minY, wb.maxX, wb.maxY});
        mv.update();
        const auto& cells = mv.cells();
        sprites.resize(cells.size());
        if (!cells.empty()) mapview::render::toSpriteInstances(cells.data(), cells.size(), sprites.data());
        renderer->submitSpriteBatch(sprites.data(), sprites.size());

        // Markers -> PNG icon sprites (world-space; they pin to the terrain and scale with zoom).
        for (const auto& md : mv.markerDraws()) {
            auto s = std::make_unique<JsonDataNode>("sprite");
            s->setString("asset", "mvicon");
            s->setDouble("x", md.x); s->setDouble("y", md.y);
            s->setDouble("scaleX", md.scale); s->setDouble("scaleY", md.scale);
            s->setDouble("rotation", md.rotation);
            s->setInt("layer", md.layer);
            s->setInt("color", static_cast<int>(0xFFFFFFFFu));  // white tint -> the PNG as-is
            gIO->publish("render:sprite", std::move(s));
        }

        auto camNode = std::make_unique<JsonDataNode>("camera");
        camNode->setDouble("x", cam.x); camNode->setDouble("y", cam.y); camNode->setDouble("zoom", cam.zoom);
        camNode->setInt("viewportX", 0); camNode->setInt("viewportY", 0); camNode->setInt("viewportW", W); camNode->setInt("viewportH", H);
        gIO->publish("render:camera", std::move(camNode));

        engine.step(dt);
    };

    if (selftest) {
        // Scripted pan + zoom-to-centre over N frames, captured to a PNG — proves the live pipeline
        // (camera -> viewport -> cull -> render) responds, with no input. Offscreen so it's headless.
        rhi::IRHIDevice* dev = renderer->getDevice();
        if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
        rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
        dev->setViewFramebuffer(0, fb);
        dev->setViewFramebuffer(1, fb);
        for (int i = 0; i < 45; ++i) {
            cam.x += 0.4f;  // gentle pan east (keeps the markers in frame)
            cam = camera::zoomAt(cam, camera::clampZoom(cam.zoom * 1.006f, 0.5f, 64.0f),
                                 static_cast<float>(W) * 0.5f, static_cast<float>(H) * 0.5f);  // zoom to centre
            renderFrame(1.0f / 60.0f);
        }
        std::fprintf(stdout, "selftest: cells/frame=%zu final zoom=%.2f\n", mv.cellCount(), cam.zoom);
        std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
            std::fprintf(stderr, "readback failed\n"); return 3;
        }
        if (!mvdemo::writeRgbaAsPng(outPath, W, H, rgba)) { std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 4; }
        std::fprintf(stdout, "wrote %s — scripted pan+zoom through the live viewer pipeline\n", outPath.c_str());
    } else {
        std::fprintf(stdout, "grove::mapview viewer — drag=pan, wheel=zoom, H=hillshade, B=banded, R=reset, Esc=quit\n");
        Uint32 last = SDL_GetTicks();
        bool running = true;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                else if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE: running = false; break;
                        case SDLK_h: hillshade = !hillshade; rebuildLens(); break;
                        case SDLK_b: banded = !banded; rebuildLens(); break;
                        case SDLK_r: cam = resetCam(); break;
                        default: break;
                    }
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    dragPan.begin(static_cast<float>(e.button.x), static_cast<float>(e.button.y));
                } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    dragPan.end();
                } else if (e.type == SDL_MOUSEMOTION && dragPan.active()) {
                    const camera::ScreenDelta d = dragPan.update(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y));
                    cam.x -= d.dx / cam.zoom;  // grab pan: world follows the cursor
                    cam.y -= d.dy / cam.zoom;
                } else if (e.type == SDL_MOUSEWHEEL) {
                    int mx = 0, my = 0; SDL_GetMouseState(&mx, &my);
                    const float factor = (e.wheel.y > 0) ? 1.12f : (1.0f / 1.12f);
                    cam = camera::zoomAt(cam, camera::clampZoom(cam.zoom * factor, 0.5f, 64.0f),
                                         static_cast<float>(mx), static_cast<float>(my));
                }
            }
            const Uint32 now = SDL_GetTicks();
            float dt = (now - last) / 1000.0f;
            last = now;
            if (dt > 0.1f) dt = 0.1f;
            renderFrame(dt);
        }
    }

    engine.shutdown();
    mgr.removeInstance("mv_view");
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
