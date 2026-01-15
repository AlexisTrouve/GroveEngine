/**
 * Minimal bgfx test for Windows - no DLL, just renders a red screen
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "=== Minimal bgfx test ===\n";

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "bgfx minimal test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow failed\n";
        SDL_Quit();
        return 1;
    }

    // Get native window handle
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    SDL_GetWindowWMInfo(window, &wmi);

    // Setup bgfx
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.resolution.width = 800;
    init.resolution.height = 600;
    init.resolution.reset = BGFX_RESET_VSYNC;

#ifdef _WIN32
    init.platformData.nwh = wmi.info.win.window;
#endif

    std::cout << "Initializing bgfx with D3D11...\n";

    if (!bgfx::init(init)) {
        std::cerr << "bgfx::init failed\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const bgfx::Caps* caps = bgfx::getCaps();
    std::cout << "Renderer: " << bgfx::getRendererName(caps->rendererType) << "\n";

    // Set bright red clear color
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xFF0000FF, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, 800, 600);

    std::cout << "Running for 3 seconds...\n";

    Uint32 start = SDL_GetTicks();
    bool running = true;
    int frames = 0;

    while (running && (SDL_GetTicks() - start) < 3000) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        bgfx::touch(0);
        bgfx::frame();
        frames++;
    }

    std::cout << "Rendered " << frames << " frames\n";

    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "=== Test complete ===\n";
    return 0;
}
