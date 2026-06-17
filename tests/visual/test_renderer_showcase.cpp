/**
 * BgfxRenderer Complete Showcase
 *
 * Demonstrates ALL rendering features of BgfxRendererModule:
 * - Sprites (static, animated, colored, layered, rotated, scaled)
 * - Text (different sizes, colors, multi-line)
 * - Particles (with life, velocity, size, additive blending)
 * - Tilemap (simple grid with tile indices)
 * - Debug primitives (lines, rectangles wireframe and filled)
 * - Camera (orthographic projection, panning, zooming)
 * - Clear color
 *
 * Controls:
 * - Arrow keys: Move camera
 * - +/-: Zoom in/out
 * - SPACE: Spawn explosion particles
 * - C: Cycle clear color
 * - ESC: Exit
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <cmath>
#include <memory>
#include <vector>
#include <random>

#include "../helpers/WindowIcon.h"
#include "BgfxRendererModule.h"
#include "Scene/Camera.h"               // grove::camera helpers (damp/focusOn)
#include "InputModule/ActionMap.h"      // grove::input::ActionMap (scancode bindings, AZERTY-proof)
#include "grove/anim/AnimationPlayer.h" // grove::anim: Hierarchy + Clip + player (procedural)
#include "grove/anim/Flipbook.h"        // grove::anim: SpriteSheet + Flipbook (frame-by-frame)
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace grove;

// Particle structure for CPU-side simulation
struct Particle {
    float x, y;
    float vx, vy;
    float size;
    float life;
    float maxLife;
    uint32_t color;
    bool alive;
};

// Simple tilemap data (10x8 grid)
static const int TILEMAP_WIDTH = 10;
static const int TILEMAP_HEIGHT = 8;
static const int TILE_SIZE = 32;
static uint16_t tilemapData[TILEMAP_WIDTH * TILEMAP_HEIGHT] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 2, 2, 0, 0, 3, 3, 0, 1,
    1, 0, 2, 2, 0, 0, 3, 3, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 4, 4, 4, 4, 4, 4, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

// Clear colors to cycle through
static const uint32_t clearColors[] = {
    0x1a1a2eFF,  // Dark blue-gray
    0x16213eFF,  // Navy blue
    0x0f3460FF,  // Deep blue
    0x1e5128FF,  // Forest green
    0x2c3333FF,  // Dark teal
    0x3d0000FF,  // Dark red
};
static const int numClearColors = sizeof(clearColors) / sizeof(clearColors[0]);

class RendererShowcase {
public:
    RendererShowcase()
        : m_rng(std::random_device{}())
    {
        m_particles.reserve(500);
        bindDefaultActions();
        buildAnimation();
    }

    // Build the animation demo: a 2-node rig (hull + turret) + a looping keyframe clip, and a
    // flipbook over a 2x2 view of a texture. No device needed (pure grove::anim data).
    void buildAnimation() {
        using namespace grove::anim;

        // Hull (root) at a visible spot; turret child offset 80px along the hull's local +X.
        m_hull   = m_rig.addNode(-1,     Transform2D{520.0f, 600.0f, 0.0f, 1.0f, 1.0f});
        m_turret = m_rig.addNode(m_hull, Transform2D{80.0f,  0.0f,   0.0f, 1.0f, 1.0f});

        // One 6s looping clip: hull does 1 turn, turret does 4 turns. The turret ORBITS with
        // the hull (its offset is rotated by the hull) AND spins on itself -> hierarchy + clip.
        const float TWO_PI = 6.28318530718f;
        m_rigClip.duration = 6.0f;
        Track hullRot;
        hullRot.nodeId = m_hull; hullRot.property = Property::Rotation;
        hullRot.keys = { {0.0f, 0.0f, Easing::Linear}, {6.0f, TWO_PI, Easing::Linear} };
        Track turRot;
        turRot.nodeId = m_turret; turRot.property = Property::Rotation;
        turRot.keys = { {0.0f, 0.0f, Easing::Linear}, {6.0f, TWO_PI * 4.0f, Easing::Linear} };
        m_rigClip.tracks.push_back(hullRot);
        m_rigClip.tracks.push_back(turRot);
        m_rigPlayer.play(&m_rigClip, /*loop*/ true);

        // Flipbook: treat texture 2 as a 2x2 sheet and cycle its 4 quadrants at 4 fps.
        m_flipSheet.columns = 2; m_flipSheet.rows = 2;
        m_flip.frames = { 0, 1, 2, 3 };
        m_flip.setFps(4.0f);
        m_flip.loop = true;
    }

    // Default key map, bound by SCANCODE (SDL_SCANCODE_* = physical position) so it works the
    // same on QWERTY and AZERTY — no character-keycode surprises. Multiple bindings per action
    // (main-row AND numpad for zoom). Remappable at runtime via m_actions.clearAction/bind*.
    void bindDefaultActions() {
        m_actions.bindKey("pan_left",  SDL_SCANCODE_LEFT);
        m_actions.bindKey("pan_right", SDL_SCANCODE_RIGHT);
        m_actions.bindKey("pan_up",    SDL_SCANCODE_UP);
        m_actions.bindKey("pan_down",  SDL_SCANCODE_DOWN);
        // Zoom defaults: PgUp/PgDn — same label on every layout, reachable on a laptop with no
        // numpad. We ALSO bind '='/'-' and numpad, but beware: SDL_SCANCODE_MINUS is the US
        // physical position (right of '0'), which on AZERTY is the ')°' key, NOT where '-' is
        // printed — exactly why a "minus" default felt broken. Sensible default keys matter;
        // anything else is the game's remap (ActionMap::clearAction + bind*).
        m_actions.bindKey("zoom_in",   SDL_SCANCODE_PAGEUP);
        m_actions.bindKey("zoom_in",   SDL_SCANCODE_EQUALS);
        m_actions.bindKey("zoom_in",   SDL_SCANCODE_KP_PLUS);
        m_actions.bindKey("zoom_out",  SDL_SCANCODE_PAGEDOWN);
        m_actions.bindKey("zoom_out",  SDL_SCANCODE_MINUS);
        m_actions.bindKey("zoom_out",  SDL_SCANCODE_KP_MINUS);
        m_actions.bindKey("spawn",     SDL_SCANCODE_SPACE);
        m_actions.bindKey("cycle_color", SDL_SCANCODE_C);
    }

    // Clear per-frame action edges (justPressed/justReleased). Call once per frame BEFORE
    // feeding that frame's events.
    void beginFrame() { m_actions.beginFrame(); }

    bool init(SDL_Window* window) {
        // Get native window handle
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        SDL_GetWindowWMInfo(window, &wmi);

        m_logger = spdlog::stdout_color_mt("Showcase");
        spdlog::set_level(spdlog::level::info);

        m_logger->info("=== BgfxRenderer Complete Showcase ===");

        // Create IIO instances - IMPORTANT: separate for publisher and subscriber
        // Keep shared_ptr alive, use IIO* abstract interface
        m_rendererIOPtr = IntraIOManager::getInstance().createInstance("renderer");
        m_gameIOPtr = IntraIOManager::getInstance().createInstance("game");
        m_rendererIO = m_rendererIOPtr.get();
        m_gameIO = m_gameIOPtr.get();

        // Create and configure renderer
        m_renderer = std::make_unique<BgfxRendererModule>();

        JsonDataNode config("config");
        config.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        config.setInt("windowWidth", 1024);
        config.setInt("windowHeight", 768);
        config.setString("backend", "d3d11");
        config.setBool("vsync", true);

        // Load textures - try multiple paths for flexibility
        // Works from build/ or build/tests/
        config.setString("texture1", "../assets/textures/1f440.png");      // Eye emoji
        config.setString("texture2", "../assets/textures/5oxaxt1vo2f91.jpg"); // Image

        m_renderer->setConfiguration(config, m_rendererIO, nullptr);

        m_logger->info("Renderer initialized");
        m_logger->info("Controls:");
        m_logger->info("  Arrows: Move camera");
        m_logger->info("  +/-: Zoom");
        m_logger->info("  SPACE: Spawn particles");
        m_logger->info("  C: Cycle clear color");
        m_logger->info("  ESC: Exit");

        return true;
    }

    void handleInput(SDL_Event& e) {
        // Feed raw keyboard events to the ActionMap by SCANCODE (physical position). All
        // gameplay then reads semantic actions in update() — never a raw keycode. This is why
        // the controls behave identically on AZERTY and QWERTY (the old SDLK_MINUS bug).
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            m_actions.onKey(e.key.keysym.scancode, e.type == SDL_KEYDOWN);
        }
        // Mouse wheel: smooth zoom toward the SCREEN CENTER. (Zooming toward the cursor made
        // the scene travel when the cursor was off-center, which reads as "zoom + dezoom" —
        // center zoom keeps the motion purely a scale.) Robust to inverted-scroll devices and
        // zero-delta precise-scroll events. One notch = a firm 1.25x; update() damps toward it.
        else if (e.type == SDL_MOUSEWHEEL) {
            float wy = static_cast<float>(e.wheel.y);
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) wy = -wy;
            if (wy != 0.0f) {
                const float factor = (wy > 0.0f) ? 1.25f : (1.0f / 1.25f);
                setZoomTarget(m_targetZoom * factor, 512.0f, 384.0f);  // 1024x768 center
            }
        }
    }

    // Set a smooth zoom target anchored at a screen point. Remembers the WORLD point currently
    // under that screen point so update() can keep it pinned as the zoom glides in/out.
    void setZoomTarget(float newZoom, float screenX, float screenY) {
        m_targetZoom = camera::clampZoom(newZoom, 0.2f, 6.0f);
        camera::CameraView view{m_cameraX, m_cameraY, m_cameraZoom, 1024.0f, 768.0f};
        camera::screenToWorld(view, screenX, screenY, m_zoomFocusWorldX, m_zoomFocusWorldY);
        m_zoomFocusScreenX = screenX;
        m_zoomFocusScreenY = screenY;
    }

    void update(float dt) {
        m_time += dt;
        m_frameCount++;

        // --- Consume semantic actions (fed this frame by handleInput, via scancode) ---
        // Pan: held actions set the camera velocity.
        m_cameraVX = (m_actions.isActive("pan_right") ? 200.0f : 0.0f) - (m_actions.isActive("pan_left") ? 200.0f : 0.0f);
        m_cameraVY = (m_actions.isActive("pan_down")  ? 200.0f : 0.0f) - (m_actions.isActive("pan_up")   ? 200.0f : 0.0f);
        // Zoom: continuous while held — ramps the smooth target (the wheel feeds it too).
        if (m_actions.isActive("zoom_in"))  setZoomTarget(m_targetZoom * (1.0f + 2.0f * dt), 512.0f, 384.0f);
        if (m_actions.isActive("zoom_out")) setZoomTarget(m_targetZoom / (1.0f + 2.0f * dt), 512.0f, 384.0f);
        // Discrete: fire once on the press edge (key-repeat is idempotent in ActionMap).
        if (m_actions.justPressed("spawn"))       spawnExplosion(512.0f + m_cameraX, 400.0f + m_cameraY);
        if (m_actions.justPressed("cycle_color")) m_clearColorIndex = (m_clearColorIndex + 1) % numClearColors;

        // Smooth zoom: glide toward the target (framerate-independent camera::damp), re-framing
        // so the focus world point stays pinned under its screen anchor — buttery wheel/key
        // zoom instead of per-notch jumps. Pan (arrow velocity) applies when not mid-zoom.
        if (std::fabs(m_cameraZoom - m_targetZoom) > 0.0005f) {
            m_cameraZoom = camera::damp(m_cameraZoom, m_targetZoom, 16.0f, dt);
            camera::CameraView v = camera::focusOn(m_zoomFocusWorldX, m_zoomFocusWorldY, m_cameraZoom,
                                                   1024.0f, 768.0f, m_zoomFocusScreenX, m_zoomFocusScreenY);
            m_cameraX = v.x;
            m_cameraY = v.y;
        } else {
            m_cameraX += m_cameraVX * dt;
            m_cameraY += m_cameraVY * dt;
        }

        // Advance the animation rig: the player writes node locals, then one update() composes
        // all world transforms (the high-perf shape — one compose for the whole rig).
        m_rigPlayer.update(dt, m_rig);
        m_rig.update();

        // Update particles
        updateParticles(dt);
    }

    void render() {
        // 1. Set clear color
        sendClearColor();

        // 2. Set camera
        sendCamera();

        // 3. Render tilemap (background layer)
        sendTilemap();

        // 4. Render sprites (multiple layers)
        sendSprites();

        // 5. Render particles
        sendParticles();

        // 6. Render text
        sendText();

        // 7. Render debug primitives
        sendDebugPrimitives();

        // 8. Render the animation demo (linked-object rig + flipbook) from grove::anim.
        sendAnimation();

        // 9. Render the screen-space HUD overlay (space:"screen") — stays FIXED while the
        //    world (sprites/tilemap/world-text) zooms & pans. Visual proof of the HUD view.
        sendHud();

        // Process frame
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        input.setInt("frameCount", m_frameCount);
        m_renderer->process(input);
    }

    void shutdown() {
        m_renderer->shutdown();
        IntraIOManager::getInstance().removeInstance("renderer");
        IntraIOManager::getInstance().removeInstance("game");
        m_logger->info("Showcase shutdown complete");
    }

    int getFrameCount() const { return m_frameCount; }

private:
    void sendClearColor() {
        auto clear = std::make_unique<JsonDataNode>("clear");
        clear->setInt("color", clearColors[m_clearColorIndex]);
        m_gameIO->publish("render:clear", std::move(clear));
    }

    void sendCamera() {
        auto cam = std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x", m_cameraX);
        cam->setDouble("y", m_cameraY);
        cam->setDouble("zoom", m_cameraZoom);
        cam->setInt("viewportX", 0);
        cam->setInt("viewportY", 0);
        cam->setInt("viewportW", 1024);
        cam->setInt("viewportH", 768);
        m_gameIO->publish("render:camera", std::move(cam));
    }

    void sendTilemap() {
        auto tilemap = std::make_unique<JsonDataNode>("tilemap");
        tilemap->setDouble("x", 50.0);
        tilemap->setDouble("y", 400.0);
        tilemap->setInt("width", TILEMAP_WIDTH);
        tilemap->setInt("height", TILEMAP_HEIGHT);
        tilemap->setInt("tileW", TILE_SIZE);
        tilemap->setInt("tileH", TILE_SIZE);
        tilemap->setInt("textureId", 0);

        // Convert tilemap to comma-separated string
        std::string tileData;
        for (int i = 0; i < TILEMAP_WIDTH * TILEMAP_HEIGHT; ++i) {
            if (i > 0) tileData += ",";
            tileData += std::to_string(tilemapData[i]);
        }
        tilemap->setString("tileData", tileData);

        m_gameIO->publish("render:tilemap", std::move(tilemap));
    }

    void sendSprites() {
        // Layer 0: Background sprites with TEXTURE 2 (image)
        for (int i = 0; i < 5; ++i) {
            auto sprite = std::make_unique<JsonDataNode>("sprite");
            sprite->setDouble("x", 100 + i * 150);
            sprite->setDouble("y", 50);
            sprite->setDouble("scaleX", 120.0);
            sprite->setDouble("scaleY", 90.0);
            sprite->setDouble("rotation", 0.0);
            sprite->setInt("color", 0xFFFFFFFF);  // White (no tint)
            sprite->setInt("layer", 0);
            sprite->setInt("textureId", 2);  // Image texture
            m_gameIO->publish("render:sprite", std::move(sprite));
        }

        // Layer 5: Bouncing EYE EMOJIS with texture 1
        for (int i = 0; i < 5; ++i) {
            float offset = std::sin(m_time * 2.0f + i * 1.2f) * 40.0f;
            auto sprite = std::make_unique<JsonDataNode>("sprite");
            sprite->setDouble("x", 100 + i * 180);
            sprite->setDouble("y", 200 + offset);
            sprite->setDouble("scaleX", 64.0);
            sprite->setDouble("scaleY", 64.0);
            sprite->setDouble("rotation", 0.0);
            sprite->setInt("color", 0xFFFFFFFF);  // No tint
            sprite->setInt("layer", 5);
            sprite->setInt("textureId", 1);  // Eye emoji
            m_gameIO->publish("render:sprite", std::move(sprite));
        }

        // Layer 10: Rotating eye emoji
        {
            auto sprite = std::make_unique<JsonDataNode>("sprite");
            sprite->setDouble("x", 700);
            sprite->setDouble("y", 200);
            sprite->setDouble("scaleX", 100.0);
            sprite->setDouble("scaleY", 100.0);
            sprite->setDouble("rotation", m_time);  // Radians
            sprite->setInt("color", 0xFFFFFFFF);
            sprite->setInt("layer", 10);
            sprite->setInt("textureId", 1);  // Eye emoji
            m_gameIO->publish("render:sprite", std::move(sprite));
        }

        // Layer 15: Scaling/pulsing image
        {
            float scale = 80.0f + std::sin(m_time * 3.0f) * 30.0f;
            auto sprite = std::make_unique<JsonDataNode>("sprite");
            sprite->setDouble("x", 850);
            sprite->setDouble("y", 200);
            sprite->setDouble("scaleX", scale);
            sprite->setDouble("scaleY", scale * 0.75f);
            sprite->setDouble("rotation", 0.0);
            sprite->setInt("color", 0xFFFFFFFF);
            sprite->setInt("layer", 15);
            sprite->setInt("textureId", 2);  // Image
            m_gameIO->publish("render:sprite", std::move(sprite));
        }

        // Layer 20: Tinted sprites (color overlay on texture)
        {
            uint32_t colors[] = { 0xFF8888FF, 0x88FF88FF, 0x8888FFFF, 0xFFFF88FF };
            for (int i = 0; i < 4; ++i) {
                auto sprite = std::make_unique<JsonDataNode>("sprite");
                sprite->setDouble("x", 100 + i * 100);
                sprite->setDouble("y", 320);
                sprite->setDouble("scaleX", 80.0);
                sprite->setDouble("scaleY", 80.0);
                sprite->setDouble("rotation", 0.0);
                sprite->setInt("color", colors[i]);  // Tinted
                sprite->setInt("layer", 20);
                sprite->setInt("textureId", 1);  // Eye emoji with color tint
                m_gameIO->publish("render:sprite", std::move(sprite));
            }
        }

        // Layer 25: Grid of small images
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 4; ++col) {
                auto sprite = std::make_unique<JsonDataNode>("sprite");
                sprite->setDouble("x", 550 + col * 70);
                sprite->setDouble("y", 320 + row * 55);
                sprite->setDouble("scaleX", 60.0);
                sprite->setDouble("scaleY", 45.0);
                sprite->setDouble("rotation", 0.0);
                sprite->setInt("color", 0xFFFFFFFF);
                sprite->setInt("layer", 25);
                sprite->setInt("textureId", 2);  // Image
                m_gameIO->publish("render:sprite", std::move(sprite));
            }
        }
    }

    void sendParticles() {
        for (const auto& p : m_particles) {
            if (!p.alive) continue;

            auto particle = std::make_unique<JsonDataNode>("particle");
            particle->setDouble("x", p.x);
            particle->setDouble("y", p.y);
            particle->setDouble("vx", p.vx);
            particle->setDouble("vy", p.vy);
            particle->setDouble("size", p.size);
            particle->setDouble("life", p.life / p.maxLife);  // Normalized 0-1
            particle->setInt("color", p.color);
            particle->setInt("textureId", 0);
            m_gameIO->publish("render:particle", std::move(particle));
        }
    }

    void sendText() {
        // Title (large)
        {
            auto text = std::make_unique<JsonDataNode>("text");
            text->setDouble("x", 10);
            text->setDouble("y", 10);
            text->setString("text", "BgfxRenderer Showcase");
            text->setInt("fontSize", 32);
            text->setInt("color", 0xFFFFFFFF);
            text->setInt("layer", 100);
            m_gameIO->publish("render:text", std::move(text));
        }

        // Info text
        {
            auto text = std::make_unique<JsonDataNode>("text");
            text->setDouble("x", 10);
            text->setDouble("y", 50);
            char buf[128];
            snprintf(buf, sizeof(buf), "Frame: %d  Camera: (%.0f, %.0f)  Zoom: %.2fx",
                     m_frameCount, m_cameraX, m_cameraY, m_cameraZoom);
            text->setString("text", buf);
            text->setInt("fontSize", 16);
            text->setInt("color", 0xAAAAAAFF);
            text->setInt("layer", 100);
            m_gameIO->publish("render:text", std::move(text));
        }

        // Particles count
        {
            auto text = std::make_unique<JsonDataNode>("text");
            text->setDouble("x", 10);
            text->setDouble("y", 70);
            char buf[64];
            int aliveCount = 0;
            for (const auto& p : m_particles) if (p.alive) aliveCount++;
            snprintf(buf, sizeof(buf), "Particles: %d", aliveCount);
            text->setString("text", buf);
            text->setInt("fontSize", 16);
            text->setInt("color", 0xFFCC00FF);
            text->setInt("layer", 100);
            m_gameIO->publish("render:text", std::move(text));
        }

        // Controls help
        {
            auto text = std::make_unique<JsonDataNode>("text");
            text->setDouble("x", 750);
            text->setDouble("y", 730);
            text->setString("text", "SPACE: Particles | C: Color | Arrows: Pan | Molette/PgUp-PgDn: Zoom");
            text->setInt("fontSize", 14);
            text->setInt("color", 0x888888FF);
            text->setInt("layer", 100);
            m_gameIO->publish("render:text", std::move(text));
        }

        // Multi-line text demo
        {
            auto text = std::make_unique<JsonDataNode>("text");
            text->setDouble("x", 700);
            text->setDouble("y", 300);
            text->setString("text", "Multi-line\nText Demo\nWith Colors!");
            text->setInt("fontSize", 20);
            text->setInt("color", 0x00FF88FF);
            text->setInt("layer", 100);
            m_gameIO->publish("render:text", std::move(text));
        }
    }

    // Publish one rig node as a textured sprite from its WORLD transform (render:sprite x,y =
    // CENTER; rotation in radians; the node's world scale multiplies the base pixel size).
    void publishNodeSprite(int node, int textureId, float pixelSize, int layer) {
        const grove::anim::Transform2D& w = m_rig.world(node);
        auto s = std::make_unique<JsonDataNode>("sprite");
        s->setDouble("x", w.x);
        s->setDouble("y", w.y);
        s->setDouble("scaleX", pixelSize * w.scaleX);
        s->setDouble("scaleY", pixelSize * w.scaleY);
        s->setDouble("rotation", w.rotation);
        s->setInt("color", 0xFFFFFFFF);
        s->setInt("layer", layer);
        s->setInt("textureId", textureId);
        m_gameIO->publish("render:sprite", std::move(s));
    }

    // Animation demo: the linked-object rig (procedural keyframe) + a flipbook (frame-by-frame).
    void sendAnimation() {
        // Hull + turret from their composed world transforms: the turret orbits the hull AND
        // spins. Hull = image (texture 2, 110px), turret = eye (texture 1, 56px).
        publishNodeSprite(m_hull,   2, 110.0f, 30);
        publishNodeSprite(m_turret, 1, 56.0f, 31);

        // Flipbook: a sprite whose UV cycles the 2x2 quadrants of texture 2 (proves
        // SpriteSheet+Flipbook -> UV -> render:sprite on the GPU).
        float u0, v0, u1, v1;
        m_flip.uvAt(m_time, m_flipSheet, u0, v0, u1, v1);
        auto fb = std::make_unique<JsonDataNode>("sprite");
        fb->setDouble("x", 770.0); fb->setDouble("y", 600.0);
        fb->setDouble("scaleX", 96.0); fb->setDouble("scaleY", 96.0);
        fb->setDouble("u0", u0); fb->setDouble("v0", v0);
        fb->setDouble("u1", u1); fb->setDouble("v1", v1);
        fb->setInt("color", 0xFFFFFFFF); fb->setInt("layer", 30); fb->setInt("textureId", 2);
        m_gameIO->publish("render:sprite", std::move(fb));

        // World-space labels so it's clear what each demo is.
        auto label = [this](double x, double y, const char* txt) {
            auto t = std::make_unique<JsonDataNode>("text");
            t->setDouble("x", x); t->setDouble("y", y);
            t->setString("text", txt);
            t->setInt("fontSize", 14); t->setInt("color", 0x66CCFFFF); t->setInt("layer", 90);
            m_gameIO->publish("render:text", std::move(t));
        };
        label(440.0, 670.0, "anim: hull + turret (cutout)");
        label(720.0, 670.0, "anim: flipbook (UV cycle)");
    }

    // Screen-space HUD overlay (engine help: space:"screen" → fixed bgfx view 1). Everything
    // here is in literal pixel coordinates and is INVARIANT under the world camera — pan/zoom
    // the world with arrows/+- and this bar + label + corner panel do not budge.
    void sendHud() {
        // Top bar background (full width, semi-opaque dark).
        {
            auto bar = std::make_unique<JsonDataNode>("rect");
            bar->setDouble("x", 0); bar->setDouble("y", 0);
            bar->setDouble("w", 1024); bar->setDouble("h", 30);
            bar->setInt("color", 0x101822E0);
            bar->setInt("layer", 0);
            bar->setString("space", "screen");     // <-- fixed overlay
            m_gameIO->publish("render:rect", std::move(bar));
        }
        // Label on the bar (above the bar's layer).
        {
            auto text = std::make_unique<JsonDataNode>("text");
            text->setDouble("x", 8); text->setDouble("y", 6);
            char buf[128];
            snprintf(buf, sizeof(buf), "HUD screen-space (FIXE) | world zoom %.2fx -- pan/zoom: le HUD ne bouge pas", m_cameraZoom);
            text->setString("text", buf);
            text->setInt("fontSize", 16);
            text->setInt("color", 0x33FFAAFF);
            text->setInt("layer", 1);
            text->setString("space", "screen");
            m_gameIO->publish("render:text", std::move(text));
        }
        // Bottom-right corner panel + label (proves corner anchoring stays put).
        {
            auto panel = std::make_unique<JsonDataNode>("rect");
            panel->setDouble("x", 824); panel->setDouble("y", 688);
            panel->setDouble("w", 192); panel->setDouble("h", 72);
            panel->setInt("color", 0x202838C0);
            panel->setInt("layer", 0);
            panel->setString("space", "screen");
            m_gameIO->publish("render:rect", std::move(panel));

            auto text = std::make_unique<JsonDataNode>("text");
            text->setDouble("x", 834); text->setDouble("y", 700);
            text->setString("text", "HUD panel\n(coin fixe)");
            text->setInt("fontSize", 16);
            text->setInt("color", 0xFFFFFFFF);
            text->setInt("layer", 1);
            text->setString("space", "screen");
            m_gameIO->publish("render:text", std::move(text));
        }
    }

    void sendDebugPrimitives() {
        // Grid lines
        for (int i = 0; i <= 10; ++i) {
            // Vertical lines
            auto vline = std::make_unique<JsonDataNode>("line");
            vline->setDouble("x1", 50 + i * 32);
            vline->setDouble("y1", 400);
            vline->setDouble("x2", 50 + i * 32);
            vline->setDouble("y2", 400 + TILEMAP_HEIGHT * 32);
            vline->setInt("color", 0x444444FF);
            m_gameIO->publish("render:debug:line", std::move(vline));
        }
        for (int i = 0; i <= 8; ++i) {
            // Horizontal lines
            auto hline = std::make_unique<JsonDataNode>("line");
            hline->setDouble("x1", 50);
            hline->setDouble("y1", 400 + i * 32);
            hline->setDouble("x2", 50 + TILEMAP_WIDTH * 32);
            hline->setDouble("y2", 400 + i * 32);
            hline->setInt("color", 0x444444FF);
            m_gameIO->publish("render:debug:line", std::move(hline));
        }

        // Diagonal line (animated)
        {
            auto line = std::make_unique<JsonDataNode>("line");
            float offset = std::sin(m_time) * 50.0f;
            line->setDouble("x1", 500);
            line->setDouble("y1", 450 + offset);
            line->setDouble("x2", 700);
            line->setDouble("y2", 550 - offset);
            line->setInt("color", 0xFFFF00FF);  // Yellow
            m_gameIO->publish("render:debug:line", std::move(line));
        }

        // Wireframe rectangle
        {
            auto rect = std::make_unique<JsonDataNode>("rect");
            rect->setDouble("x", 750);
            rect->setDouble("y", 400);
            rect->setDouble("w", 100);
            rect->setDouble("h", 80);
            rect->setInt("color", 0x00FFFFFF);  // Cyan
            rect->setBool("filled", false);
            m_gameIO->publish("render:debug:rect", std::move(rect));
        }

        // Filled rectangle (pulsing)
        {
            float pulse = (std::sin(m_time * 2.0f) + 1.0f) * 0.5f;
            uint8_t alpha = static_cast<uint8_t>(128 + pulse * 127);
            uint32_t color = 0xFF4444FF | (alpha);

            auto rect = std::make_unique<JsonDataNode>("rect");
            rect->setDouble("x", 870);
            rect->setDouble("y", 400);
            rect->setDouble("w", 80);
            rect->setDouble("h", 80);
            rect->setInt("color", color);
            rect->setBool("filled", true);
            m_gameIO->publish("render:debug:rect", std::move(rect));
        }

        // Crosshair at center
        {
            float cx = 512.0f, cy = 384.0f;
            auto hline = std::make_unique<JsonDataNode>("line");
            hline->setDouble("x1", cx - 20);
            hline->setDouble("y1", cy);
            hline->setDouble("x2", cx + 20);
            hline->setDouble("y2", cy);
            hline->setInt("color", 0xFF0000FF);
            m_gameIO->publish("render:debug:line", std::move(hline));

            auto vline = std::make_unique<JsonDataNode>("line");
            vline->setDouble("x1", cx);
            vline->setDouble("y1", cy - 20);
            vline->setDouble("x2", cx);
            vline->setDouble("y2", cy + 20);
            vline->setInt("color", 0xFF0000FF);
            m_gameIO->publish("render:debug:line", std::move(vline));
        }
    }

    void spawnExplosion(float x, float y) {
        std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
        std::uniform_real_distribution<float> speedDist(50.0f, 200.0f);
        std::uniform_real_distribution<float> sizeDist(5.0f, 20.0f);
        std::uniform_real_distribution<float> lifeDist(0.5f, 2.0f);

        uint32_t colors[] = {
            0xFF4444FF, 0xFF8844FF, 0xFFCC44FF, 0xFFFF44FF,
            0xFF6644FF, 0xFFAA00FF
        };
        std::uniform_int_distribution<int> colorDist(0, 5);

        for (int i = 0; i < 50; ++i) {
            Particle p;
            float angle = angleDist(m_rng);
            float speed = speedDist(m_rng);
            p.x = x;
            p.y = y;
            p.vx = std::cos(angle) * speed;
            p.vy = std::sin(angle) * speed;
            p.size = sizeDist(m_rng);
            p.life = lifeDist(m_rng);
            p.maxLife = p.life;
            p.color = colors[colorDist(m_rng)];
            p.alive = true;

            // Find dead particle slot or add new
            bool found = false;
            for (auto& existing : m_particles) {
                if (!existing.alive) {
                    existing = p;
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_particles.push_back(p);
            }
        }

        m_logger->info("Spawned explosion at ({:.0f}, {:.0f})", x, y);
    }

    void updateParticles(float dt) {
        for (auto& p : m_particles) {
            if (!p.alive) continue;

            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.vy += 100.0f * dt;  // Gravity
            p.life -= dt;

            if (p.life <= 0.0f) {
                p.alive = false;
            }
        }
    }

    std::shared_ptr<spdlog::logger> m_logger;
    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::shared_ptr<IntraIO> m_rendererIOPtr;  // Keep shared_ptr alive
    std::shared_ptr<IntraIO> m_gameIOPtr;      // Keep shared_ptr alive
    IIO* m_rendererIO = nullptr;               // Abstract interface
    IIO* m_gameIO = nullptr;                   // Abstract interface

    float m_time = 0.0f;
    int m_frameCount = 0;

    // Camera
    float m_cameraX = 0.0f;
    float m_cameraY = 0.0f;
    float m_cameraVX = 0.0f;
    float m_cameraVY = 0.0f;
    float m_cameraZoom = 1.0f;

    // Smooth-zoom state: glide m_cameraZoom toward m_targetZoom while pinning the focus point.
    float m_targetZoom = 1.0f;
    float m_zoomFocusWorldX = 0.0f, m_zoomFocusWorldY = 0.0f;
    float m_zoomFocusScreenX = 512.0f, m_zoomFocusScreenY = 384.0f;

    // Clear color
    int m_clearColorIndex = 0;

    // Input: semantic actions bound by SCANCODE (physical key) — layout-proof. The whole
    // event loop talks in actions ("zoom_in"), never raw keycodes, so AZERTY/QWERTY behave
    // identically. This is the live dogfood of grove::input::ActionMap.
    grove::input::ActionMap m_actions;

    // Animation demo (grove::anim): a linked-object rig (hull + turret) driven by a keyframe
    // clip, and a flipbook cycling a texture's cells. Proves the animation system on GPU.
    grove::anim::Hierarchy m_rig;
    grove::anim::Clip m_rigClip;
    grove::anim::AnimationPlayer m_rigPlayer;
    int m_hull = -1;
    int m_turret = -1;
    grove::anim::SpriteSheet m_flipSheet;
    grove::anim::Flipbook m_flip;

    // Particles
    std::vector<Particle> m_particles;
    std::mt19937 m_rng;
};

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "BgfxRenderer Showcase",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 768,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // Default GroveEngine window icon (cross-platform; falls back to SDL's default).
    grove::test::setWindowIconGrove(window);

    RendererShowcase showcase;
    if (!showcase.init(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    Uint64 lastTime = SDL_GetPerformanceCounter();

    while (running) {
        showcase.beginFrame();   // clear per-frame action edges before feeding this frame's events
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
            showcase.handleInput(e);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - lastTime) / SDL_GetPerformanceFrequency();
        lastTime = now;

        showcase.update(dt);
        showcase.render();

        // Cap to ~60 FPS
        SDL_Delay(1);
    }

    Uint64 endTime = SDL_GetPerformanceCounter();
    int frames = showcase.getFrameCount();

    showcase.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Rendered " << frames << " frames" << std::endl;
    return 0;
}
