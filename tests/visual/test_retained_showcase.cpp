/**
 * Retained-mode demo (visual).
 *
 * Demonstrates the RETAINED rendering path (render:sprite:add / :update / :remove +
 * render:text:add) — the persistent, renderId-keyed mode, as opposed to the ephemeral
 * "re-send every sprite every frame" path.
 *
 * What you should SEE:
 *  - 5 colored squares are published ONCE via render:sprite:add. They keep rendering
 *    every frame WITHOUT being re-sent (that's the whole point of retained mode).
 *  - Square #1 orbits: it is moved each frame with render:sprite:update (position only,
 *    NO color) — it must stay RED, proving the update-preserves-color fix.
 *  - At t=3s, square #4 is removed with render:sprite:remove and vanishes.
 *  - At t=5s, square #2 is recolored to white with render:sprite:update.
 *  - A retained text label sits at the top (published once).
 *
 * ESC to exit.
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <cmath>
#include <memory>

#include "BgfxRendererModule.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace grove;

class RetainedDemo {
public:
    bool init(SDL_Window* window) {
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        SDL_GetWindowWMInfo(window, &wmi);

        m_logger = spdlog::stdout_color_mt("RetainedDemo");
        spdlog::set_level(spdlog::level::info);
        m_logger->info("=== Retained Mode Demo ===");

        // Separate IIO instances: renderer subscribes, game publishes.
        m_rendererIOPtr = IntraIOManager::getInstance().createInstance("renderer");
        m_gameIOPtr     = IntraIOManager::getInstance().createInstance("game");
        m_rendererIO    = m_rendererIOPtr.get();
        m_gameIO        = m_gameIOPtr.get();

        m_renderer = std::make_unique<BgfxRendererModule>();
        JsonDataNode config("config");
        config.setDouble("nativeWindowHandle",
            static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        config.setInt("windowWidth", 1024);
        config.setInt("windowHeight", 768);
        config.setString("backend", "d3d11");
        config.setBool("vsync", true);
        m_renderer->setConfiguration(config, m_rendererIO, nullptr);

        publishRetainedSceneOnce();
        return true;
    }

    // Publish the whole scene ONE time. In ephemeral mode this would have to be re-sent
    // every frame; in retained mode it persists until updated/removed.
    void publishRetainedSceneOnce() {
        const uint32_t cols[5] = { 0xFF4040FFu, 0x40FF40FFu, 0x4040FFFFu, 0xFFFF40FFu, 0xFF40FFFFu };
        for (int i = 0; i < 5; ++i) {
            auto s = std::make_unique<JsonDataNode>("s");
            s->setInt("renderId", i + 1);
            s->setDouble("x", 200.0 + i * 150.0);
            s->setDouble("y", 380.0);
            s->setDouble("scaleX", 90.0);
            s->setDouble("scaleY", 90.0);
            s->setInt("textureId", 0);             // default white texture -> colored quad
            s->setInt("color", static_cast<int>(cols[i]));
            s->setInt("layer", 5);
            m_gameIO->publish("render:sprite:add", std::move(s));
        }
        auto t = std::make_unique<JsonDataNode>("t");
        t->setInt("renderId", 100);
        t->setDouble("x", 200.0);
        t->setDouble("y", 120.0);
        t->setInt("fontSize", 28);
        t->setInt("color", static_cast<int>(0xFFFFFFFFu));
        t->setString("text", "RETAINED MODE: sprites added ONCE, never re-sent");
        m_gameIO->publish("render:text:add", std::move(t));

        m_logger->info("Published 5 retained sprites + 1 text ONCE — they now persist with no re-publish.");
    }

    void update(float dt) {
        m_time += dt;
        m_frame++;

        // Continuous UPDATE of sprite #1: position only, NO color. It must stay RED
        // (proves render:sprite:update preserves unspecified fields).
        {
            auto u = std::make_unique<JsonDataNode>("s");
            u->setInt("renderId", 1);
            u->setDouble("x", 200.0 + std::cos(m_time * 2.0f) * 70.0);
            u->setDouble("y", 380.0 + std::sin(m_time * 2.0f) * 70.0);
            m_gameIO->publish("render:sprite:update", std::move(u));
        }

        if (!m_removed && m_time > 3.0f) {
            m_removed = true;
            auto r = std::make_unique<JsonDataNode>("s");
            r->setInt("renderId", 4);
            m_gameIO->publish("render:sprite:remove", std::move(r));
            m_logger->info("[t=3s] render:sprite:remove renderId=4 -> it should vanish.");
        }
        if (!m_recolored && m_time > 5.0f) {
            m_recolored = true;
            auto u = std::make_unique<JsonDataNode>("s");
            u->setInt("renderId", 2);
            u->setInt("color", static_cast<int>(0xFFFFFFFFu));  // -> white, position kept
            m_gameIO->publish("render:sprite:update", std::move(u));
            m_logger->info("[t=5s] render:sprite:update renderId=2 color -> white (position preserved).");
        }
    }

    void render() {
        // Clear + camera are per-frame state (not the sprite list). NO sprite re-publish.
        {
            auto c = std::make_unique<JsonDataNode>("c");
            c->setInt("color", static_cast<int>(0x101828FFu));
            m_gameIO->publish("render:clear", std::move(c));
        }
        {
            auto cam = std::make_unique<JsonDataNode>("cam");
            cam->setDouble("x", 0.0);
            cam->setDouble("y", 0.0);
            cam->setDouble("zoom", 1.0);
            cam->setInt("viewportW", 1024);
            cam->setInt("viewportH", 768);
            m_gameIO->publish("render:camera", std::move(cam));
        }
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        input.setInt("frameCount", m_frame);
        m_renderer->process(input);
    }

    void shutdown() {
        m_renderer->shutdown();
        IntraIOManager::getInstance().removeInstance("renderer");
        IntraIOManager::getInstance().removeInstance("game");
    }

    int getFrameCount() const { return m_frame; }

private:
    std::shared_ptr<IntraIO> m_rendererIOPtr, m_gameIOPtr;
    IIO* m_rendererIO = nullptr;
    IIO* m_gameIO = nullptr;
    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::shared_ptr<spdlog::logger> m_logger;
    float m_time = 0.0f;
    int m_frame = 0;
    bool m_removed = false;
    bool m_recolored = false;
};

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "Retained Mode Demo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    RetainedDemo demo;
    if (!demo.init(window)) {
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
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - lastTime) / SDL_GetPerformanceFrequency();
        lastTime = now;

        demo.update(dt);
        demo.render();
        SDL_Delay(1);
    }

    int frames = demo.getFrameCount();
    demo.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "Retained demo rendered " << frames << " frames" << std::endl;
    return 0;
}
