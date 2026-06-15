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
    }

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
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_LEFT:  m_cameraVX = -200.0f; break;
                case SDLK_RIGHT: m_cameraVX = 200.0f; break;
                case SDLK_UP:    m_cameraVY = -200.0f; break;
                case SDLK_DOWN:  m_cameraVY = 200.0f; break;
                case SDLK_PLUS:
                case SDLK_EQUALS:
                    m_cameraZoom = std::min(4.0f, m_cameraZoom * 1.1f);
                    break;
                case SDLK_MINUS:
                    m_cameraZoom = std::max(0.25f, m_cameraZoom / 1.1f);
                    break;
                case SDLK_SPACE:
                    spawnExplosion(512.0f + m_cameraX, 400.0f + m_cameraY);
                    break;
                case SDLK_c:
                    m_clearColorIndex = (m_clearColorIndex + 1) % numClearColors;
                    break;
            }
        }
        else if (e.type == SDL_KEYUP) {
            switch (e.key.keysym.sym) {
                case SDLK_LEFT:
                case SDLK_RIGHT:
                    m_cameraVX = 0.0f;
                    break;
                case SDLK_UP:
                case SDLK_DOWN:
                    m_cameraVY = 0.0f;
                    break;
            }
        }
    }

    void update(float dt) {
        m_time += dt;
        m_frameCount++;

        // Update camera position
        m_cameraX += m_cameraVX * dt;
        m_cameraY += m_cameraVY * dt;

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
            text->setString("text", "SPACE: Particles | C: Color | Arrows: Pan | +/-: Zoom");
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

    // Clear color
    int m_clearColorIndex = 0;

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
