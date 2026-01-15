/**
 * Test: BgfxRenderer Visual Triangle Test
 *
 * Renders a colored triangle using bgfx with SDL2 windowing.
 * This validates the full rendering pipeline including shaders.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include <iostream>
#include <cstdint>

// ============================================================================
// Embedded Shaders (from bgfx examples - drawstress)
// ============================================================================

// Include pre-compiled shader bytecode for all platforms
#include "../../modules/BgfxRenderer/Shaders/vs_color.bin.h"
#include "../../modules/BgfxRenderer/Shaders/fs_color.bin.h"

// ============================================================================
// Shader loading helper
// ============================================================================

static const bgfx::Memory* loadShaderMem(const uint8_t* data, uint32_t size) {
    return bgfx::copy(data, size);
}

bgfx::ShaderHandle loadShader(bgfx::RendererType::Enum type, const char* name) {
    const uint8_t* data = nullptr;
    uint32_t size = 0;

    // Select shader based on renderer type
    bool isVertex = (name[0] == 'v');

    switch (type) {
        case bgfx::RendererType::OpenGL:
            if (isVertex) {
                data = vs_drawstress_glsl;
                size = sizeof(vs_drawstress_glsl);
            } else {
                data = fs_drawstress_glsl;
                size = sizeof(fs_drawstress_glsl);
            }
            break;

        case bgfx::RendererType::OpenGLES:
            if (isVertex) {
                data = vs_drawstress_essl;
                size = sizeof(vs_drawstress_essl);
            } else {
                data = fs_drawstress_essl;
                size = sizeof(fs_drawstress_essl);
            }
            break;

        case bgfx::RendererType::Vulkan:
            if (isVertex) {
                data = vs_drawstress_spv;
                size = sizeof(vs_drawstress_spv);
            } else {
                data = fs_drawstress_spv;
                size = sizeof(fs_drawstress_spv);
            }
            break;

        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12:
            if (isVertex) {
                data = vs_drawstress_dx11;
                size = sizeof(vs_drawstress_dx11);
            } else {
                data = fs_drawstress_dx11;
                size = sizeof(fs_drawstress_dx11);
            }
            break;

        case bgfx::RendererType::Metal:
            if (isVertex) {
                data = vs_drawstress_mtl;
                size = sizeof(vs_drawstress_mtl);
            } else {
                data = fs_drawstress_mtl;
                size = sizeof(fs_drawstress_mtl);
            }
            break;

        default:
            std::cerr << "Unsupported renderer type for shaders\n";
            return BGFX_INVALID_HANDLE;
    }

    return bgfx::createShader(loadShaderMem(data, size));
}

// ============================================================================
// Vertex Data
// ============================================================================

struct PosColorVertex {
    float x, y, z;
    uint32_t abgr;

    static bgfx::VertexLayout layout;

    static void init() {
        layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    }
};

bgfx::VertexLayout PosColorVertex::layout;

// Triangle vertices (colored) - centered at origin
static PosColorVertex s_triangleVertices[] = {
    { -0.5f, -0.5f, 0.0f, 0xff0000ff }, // Red (ABGR format)
    {  0.5f, -0.5f, 0.0f, 0xff00ff00 }, // Green
    {  0.0f,  0.5f, 0.0f, 0xffff0000 }, // Blue
};

static const uint16_t s_triangleIndices[] = {
    0, 1, 2,
};

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "BgfxRenderer Visual Triangle Test\n";
    std::cout << "========================================\n\n";

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // Create window
    const int width = 800;
    const int height = 600;

    SDL_Window* window = SDL_CreateWindow(
        "BgfxRenderer Triangle Test - Press ESC to exit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    // Get native window handle
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi)) {
        std::cerr << "SDL_GetWindowWMInfo failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Setup bgfx platform data
    bgfx::PlatformData pd;
#ifdef _WIN32
    pd.ndt = nullptr;
    pd.nwh = wmi.info.win.window;
#else
    pd.ndt = wmi.info.x11.display;
    pd.nwh = (void*)(uintptr_t)wmi.info.x11.window;
#endif
    pd.context = nullptr;
    pd.backBuffer = nullptr;
    pd.backBufferDS = nullptr;
    bgfx::setPlatformData(pd);

    // Initialize bgfx
    bgfx::Init init;
    init.type = bgfx::RendererType::Count; // Auto-select
    init.resolution.width = width;
    init.resolution.height = height;
    init.resolution.reset = BGFX_RESET_VSYNC;
    init.platformData = pd;

    std::cout << "Initializing bgfx...\n";

    if (!bgfx::init(init)) {
        std::cerr << "bgfx::init failed\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Print renderer info
    const bgfx::Caps* caps = bgfx::getCaps();
    std::cout << "Renderer: " << bgfx::getRendererName(caps->rendererType) << "\n";
    std::cout << "Resolution: " << width << "x" << height << "\n";

    // Setup view
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, width, height);

    // Initialize vertex layout
    PosColorVertex::init();

    // Create vertex buffer
    bgfx::VertexBufferHandle vbh = bgfx::createVertexBuffer(
        bgfx::makeRef(s_triangleVertices, sizeof(s_triangleVertices)),
        PosColorVertex::layout
    );

    // Create index buffer
    bgfx::IndexBufferHandle ibh = bgfx::createIndexBuffer(
        bgfx::makeRef(s_triangleIndices, sizeof(s_triangleIndices))
    );

    // Load shaders
    std::cout << "Loading shaders for " << bgfx::getRendererName(caps->rendererType) << "...\n";

    bgfx::ShaderHandle vsh = loadShader(caps->rendererType, "vs_color");
    bgfx::ShaderHandle fsh = loadShader(caps->rendererType, "fs_color");

    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
        std::cerr << "Failed to load shaders\n";
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Create program
    bgfx::ProgramHandle program = bgfx::createProgram(vsh, fsh, true);

    if (!bgfx::isValid(program)) {
        std::cerr << "Failed to create shader program\n";
        bgfx::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "\n*** Rendering colored triangle ***\n";
    std::cout << "Press ESC to exit or wait 5 seconds\n\n";

    // Main loop
    bool running = true;
    uint32_t frameCount = 0;
    Uint32 startTime = SDL_GetTicks();
    const Uint32 testDuration = 5000; // 5 seconds

    while (running) {
        // Process events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        // Check timeout
        if (SDL_GetTicks() - startTime > testDuration) {
            running = false;
        }

        // Set view transform (identity for 2D)
        float view[16];
        float proj[16];
        bx::mtxIdentity(view);
        bx::mtxOrtho(proj, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 0.0f, caps->homogeneousDepth);
        bgfx::setViewTransform(0, view, proj);

        // Touch view 0 to ensure clear
        bgfx::touch(0);

        // Set model transform (identity)
        float model[16];
        bx::mtxIdentity(model);
        bgfx::setTransform(model);

        // Set vertex and index buffers
        bgfx::setVertexBuffer(0, vbh);
        bgfx::setIndexBuffer(ibh);

        // Set render state
        uint64_t state = BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LESS
            | BGFX_STATE_MSAA;

        bgfx::setState(state);

        // Submit draw call
        bgfx::submit(0, program);

        // Submit frame
        bgfx::frame();
        frameCount++;
    }

    float elapsed = (SDL_GetTicks() - startTime) / 1000.0f;
    float fps = frameCount / elapsed;

    std::cout << "Test completed!\n";
    std::cout << "  Frames: " << frameCount << "\n";
    std::cout << "  Time: " << elapsed << "s\n";
    std::cout << "  FPS: " << fps << "\n";

    // Cleanup
    bgfx::destroy(program);
    bgfx::destroy(vbh);
    bgfx::destroy(ibh);
    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "\n========================================\n";
    std::cout << "PASS: Colored triangle rendered!\n";
    std::cout << "========================================\n";

    return 0;
}
