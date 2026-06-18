/**
 * GPU test: tilemap detail<->LOD, end-to-end, asserted analytically (Slice ②.2).
 *
 * WHAT  : Render the real TilemapPass into an offscreen framebuffer at a controlled tiles-per-pixel,
 *         read the center pixel back, and assert its value against an ANALYTICAL oracle:
 *           - zoomed IN  (chunk fits the FB, ~0.1 tile/pixel)  -> detail band -> the tile's color.
 *           - zoomed OUT (chunk >> FB, ~4 tiles/pixel)         -> LOD band   -> the AVERAGE color.
 *         No eyeballing — the expected pixel is computed from the palette.
 *
 * WHY    : LodColorUnit proves the CPU box-filter; ②.1 proves readback. This proves the WHOLE GPU
 *         path (index texelFetch -> atlas array; mipped LOD sample; derivative crossfade) actually
 *         produces the right pixel. The seamless-zoom claim, objectively verified.
 *
 * HOW    : an ortho that maps the chunk's world rect onto the full FB, so tiles/pixel = grid/Pfb.
 *          grid=256 in a 64px FB -> 4 tiles/pixel (LOD); grid=8 -> 0.125 (detail). [gpu] test:
 *          needs a real bgfx context; skips cleanly without one.
 */

#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>

#include <SDL.h>
#include <SDL_syswm.h>

#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include "RHI/RHICommandBuffer.h"
#include "Frame/FramePacket.h"
#include "Passes/TilemapPass.h"
#include "Passes/LodColor.h"
#include "Shaders/ShaderManager.h"

#include <cstdint>
#include <vector>

using namespace grove;

namespace {
int byteOf(uint32_t c, int shift) { return static_cast<int>((c >> shift) & 0xFFu); }
}

TEST_CASE("Tilemap detail->tile color, LOD->average color (end-to-end GPU)", "[gpu][tilemap][lod]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("tilemap-lod", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       64, 64, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));
#ifdef _WIN32
    void* nwh = wmi.info.win.window; void* ndt = nullptr;
#else
    void* nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));
    void* ndt = wmi.info.x11.display;
#endif

    auto device = rhi::IRHIDevice::create();
    if (!device->init(nwh, ndt, 64, 64)) { SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return; }

    ShaderManager shaders;
    shaders.init(*device, device->getCapabilities().rendererName);
    rhi::ShaderHandle prog = shaders.getProgram("tilemap");
    REQUIRE(prog.isValid());

    TilemapPass pass(prog);
    pass.setup(*device);

    const uint16_t P = 64;  // framebuffer size
    rhi::FramebufferHandle fb = device->createFramebuffer(P, P);

    // Render a chunk so its world rect [0,grid]x[0,grid] fills the whole FB -> tiles/pixel = grid/P.
    // Returns the center pixel as RGBA bytes packed 0xAABBGGRR-style via byteOf(shift).
    auto renderCenter = [&](const TilemapChunk& chunk, int grid) -> uint32_t {
        const float g = static_cast<float>(grid);
        // Column-major ortho mapping world [0,g] -> NDC [-1,1] on x and y (center symmetric, so the
        // Y convention doesn't matter for the center pixel).
        float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        float proj[16] = {
            2.0f / g, 0, 0, 0,
            0, 2.0f / g, 0, 0,
            0, 0, 1, 0,
            -1.0f, -1.0f, 0, 1
        };
        device->setViewFramebuffer(0, fb);
        device->setViewRect(0, 0, 0, P, P);
        device->setViewClear(0, 0x000000FFu, 1.0f);   // black: if the quad misses, we'll see it
        device->setViewTransform(0, view, proj);

        FramePacket frame;
        frame.tilemaps = &chunk;
        frame.tilemapCount = 1;
        // Wide visible bounds so the pass's chunk-level cull always keeps it.
        frame.mainView.positionX = 0.0f; frame.mainView.positionY = 0.0f;
        frame.mainView.zoom = 1.0f;
        frame.mainView.viewportW = 100000; frame.mainView.viewportH = 100000;

        rhi::RHICommandBuffer cmd;
        pass.execute(frame, *device, cmd);
        device->executeCommandBuffer(cmd);
        device->frame();

        std::vector<uint8_t> px(static_cast<size_t>(P) * P * 4, 0);
        REQUIRE(device->readFramebuffer(fb, px.data(), static_cast<uint32_t>(px.size())));
        const size_t c = (static_cast<size_t>(P / 2) * P + (P / 2)) * 4;
        return (static_cast<uint32_t>(px[c + 3]) << 24) | (static_cast<uint32_t>(px[c + 2]) << 16)
             | (static_cast<uint32_t>(px[c + 1]) << 8) | static_cast<uint32_t>(px[c + 0]);
        // packed as 0xAABBGGRR -> same layout as the palette literals
    };

    // --- DETAIL: a uniform 8x8 chunk of tile id 1 -> 0.125 tiles/pixel -> the tile's exact color.
    {
        const int G = 8;
        std::vector<uint16_t> tiles(static_cast<size_t>(G) * G, static_cast<uint16_t>(1));
        TilemapChunk chunk{};
        chunk.x = 0; chunk.y = 0; chunk.width = G; chunk.height = G;
        chunk.tileWidth = 1; chunk.tileHeight = 1;
        chunk.tiles = tiles.data(); chunk.tileCount = tiles.size();
        chunk.id = 100; chunk.dirty = true;

        const uint32_t got = renderCenter(chunk, G);
        const uint32_t want = lod::paletteColor(1);   // light grey
        INFO("detail got=" << std::hex << got << " want=" << want);
        CHECK(byteOf(got, 0)  == byteOf(want, 0));     // R, exact (uniform color)
        CHECK(byteOf(got, 8)  == byteOf(want, 8));     // G
        CHECK(byteOf(got, 16) == byteOf(want, 16));    // B
    }

    // --- LOD: a 256x256 checkerboard of ids 1 / 3 -> 4 tiles/pixel -> the AVERAGE of the two colors.
    {
        const int G = 256;
        std::vector<uint16_t> tiles(static_cast<size_t>(G) * G);
        for (int y = 0; y < G; ++y)
            for (int x = 0; x < G; ++x)
                tiles[static_cast<size_t>(y) * G + x] = ((x + y) & 1) ? 1 : 3;
        TilemapChunk chunk{};
        chunk.x = 0; chunk.y = 0; chunk.width = G; chunk.height = G;
        chunk.tileWidth = 1; chunk.tileHeight = 1;
        chunk.tiles = tiles.data(); chunk.tileCount = tiles.size();
        chunk.id = 101; chunk.dirty = true;

        const uint32_t got = renderCenter(chunk, G);
        const uint32_t a = lod::paletteColor(1), b = lod::paletteColor(3);
        INFO("lod got=" << std::hex << got);
        for (int shift = 0; shift <= 16; shift += 8) {
            const int expected = (byteOf(a, shift) + byteOf(b, shift)) / 2;
            CHECK(byteOf(got, shift) >= expected - 16);   // trilinear + rounding tolerance
            CHECK(byteOf(got, shift) <= expected + 16);
        }
    }

    device->destroy(fb);
    pass.shutdown(*device);
    shaders.shutdown(*device);
    device->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
}
