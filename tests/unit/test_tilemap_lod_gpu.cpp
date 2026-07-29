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
#include "Passes/TileAnim.h"
#include "Resources/AtlasSlice.h"
#include "Resources/AtlasAverage.h"
#include "Shaders/ShaderManager.h"

#include <cstdint>
#include <vector>

using namespace grove;

namespace {
int byteOf(uint32_t c, int shift) { return static_cast<int>((c >> shift) & 0xFFu); }
}

// ============================================================================
// Animated tiles — the frame-selection math (pure oracle, headless). The index texture stores the
// tile's BASE id; the shader cycles the atlas LAYER by time. Lock the only custom bit here.
// ============================================================================
TEST_CASE("Tile anim: frame = floor(time*fps) mod count; layer = base + frame", "[unit][tilemap][anim]") {
    using namespace grove::tilemap;
    REQUIRE(animFrame(0.0f, 1.0f, 3) == 0);
    REQUIRE(animFrame(0.5f, 1.0f, 3) == 0);   // floor(0.5) = 0
    REQUIRE(animFrame(1.0f, 1.0f, 3) == 1);
    REQUIRE(animFrame(2.0f, 1.0f, 3) == 2);
    REQUIRE(animFrame(3.0f, 1.0f, 3) == 0);   // wraps at count
    REQUIRE(animFrame(1.0f, 2.0f, 4) == 2);   // floor(1*2) = 2
    REQUIRE(animFrame(10.0f, 0.0f, 3) == 0);  // stopped clock -> frame 0
    REQUIRE(animFrame(10.0f, 1.0f, 1) == 0);  // single frame -> always 0
    REQUIRE(animLayer(4, 0.0f, 1.0f, 3) == 4);   // base layer, frame 0
    REQUIRE(animLayer(4, 2.0f, 1.0f, 3) == 6);   // base 4 + frame 2
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
    rhi::FramebufferHandle fb = device->createFramebuffer(P, P, rhi::TargetFormat::RGBA8);

    // Render a chunk so its world rect [0,grid]x[0,grid] fills the whole FB -> tiles/pixel = grid/P.
    // Returns the center pixel as RGBA bytes packed 0xAABBGGRR-style via byteOf(shift).
    auto renderCenter = [&](const TilemapChunk& chunk, int grid, float elapsed = 0.0f) -> uint32_t {
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
        frame.elapsedTime = elapsed;   // drives the animated-tile clock (u_tileAnimMeta.y)
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

    // Variante de renderCenter qui rend la COLONNE centrale au lieu d'un seul pixel. Necessaire pour
    // juger la FORME d'un bord : un pixel isole ne dit rien de son ondulation.
    auto renderColumn = [&](const TilemapChunk& chunk, int grid) -> std::vector<uint32_t> {
        const float g = static_cast<float>(grid);
        float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        float proj[16] = { 2.0f/g,0,0,0, 0,2.0f/g,0,0, 0,0,1,0, -1.0f,-1.0f,0,1 };
        device->setViewFramebuffer(0, fb);
        device->setViewRect(0, 0, 0, P, P);
        device->setViewClear(0, 0x000000FFu, 1.0f);
        device->setViewTransform(0, view, proj);

        FramePacket frame;
        frame.tilemaps = &chunk;
        frame.tilemapCount = 1;
        frame.mainView.positionX = 0.0f; frame.mainView.positionY = 0.0f;
        frame.mainView.zoom = 1.0f;
        frame.mainView.viewportW = 100000; frame.mainView.viewportH = 100000;

        rhi::RHICommandBuffer cmd;
        pass.execute(frame, *device, cmd);
        device->executeCommandBuffer(cmd);
        device->frame();

        std::vector<uint8_t> px(static_cast<size_t>(P) * P * 4, 0);
        REQUIRE(device->readFramebuffer(fb, px.data(), static_cast<uint32_t>(px.size())));
        std::vector<uint32_t> out;
        for (int y = 0; y < P; ++y) {
            const size_t c = (static_cast<size_t>(y) * P + (P / 2)) * 4;
            out.push_back((static_cast<uint32_t>(px[c + 3]) << 24) | (static_cast<uint32_t>(px[c + 2]) << 16)
                        | (static_cast<uint32_t>(px[c + 1]) << 8) | static_cast<uint32_t>(px[c + 0]));
        }
        return out;
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

    // --- TILESET (A3.3): a game-supplied atlas array (sliced from a grid) is selected by textureId
    //     and sampled, instead of the procedural color atlas.
    {
        // 2x1-tile grid (1x1 tiles): tile 0 = orange, tile 1 = teal (0xAABBGGRR). Slice -> 2 layers.
        const uint32_t ORANGE = 0xFF0080FFu;   // R=255 G=128 B=0
        const uint32_t TEAL   = 0xFFFF8000u;   // R=0   G=128 B=255
        std::vector<uint32_t> grid = { ORANGE, TEAL };
        int layers = 0;
        std::vector<uint32_t> arr = grove::atlas::sliceToArray(grid.data(), 2, 1, 1, 1, layers);
        REQUIRE(layers == 2);

        rhi::TextureDesc d;
        d.width = 1; d.height = 1; d.layers = static_cast<uint16_t>(layers);
        d.format = rhi::TextureDesc::RGBA8;
        d.data = arr.data();
        d.dataSize = static_cast<uint32_t>(arr.size() * 4);
        rhi::TextureHandle atlasArr = device->createTexture(d);
        pass.setTileset(7, atlasArr);

        // Uniform chunk of id 1 -> layer 0 = ORANGE; textureId 7 selects our atlas; zoomed in = detail.
        const int G = 8;
        std::vector<uint16_t> tiles(static_cast<size_t>(G) * G, static_cast<uint16_t>(1));
        TilemapChunk chunk{};
        chunk.x = 0; chunk.y = 0; chunk.width = G; chunk.height = G;
        chunk.tileWidth = 1; chunk.tileHeight = 1;
        chunk.tiles = tiles.data(); chunk.tileCount = tiles.size();
        chunk.textureId = 7; chunk.id = 102; chunk.dirty = true;

        const uint32_t got = renderCenter(chunk, G);
        INFO("tileset got=" << std::hex << got);
        CHECK(byteOf(got, 0)  == byteOf(ORANGE, 0));    // our atlas layer 0, NOT the procedural grey
        CHECK(byteOf(got, 8)  == byteOf(ORANGE, 8));
        CHECK(byteOf(got, 16) == byteOf(ORANGE, 16));
        device->destroy(atlasArr);
    }

    // --- FOG (Slice fog): half visibility dims a tile to ~half its color (objective, not just "dark").
    {
        const int G = 8;
        std::vector<uint16_t> tiles(static_cast<size_t>(G) * G, static_cast<uint16_t>(1));
        std::vector<uint8_t> fog(static_cast<size_t>(G) * G, static_cast<uint8_t>(128));  // ~50%
        TilemapChunk chunk{};
        chunk.x = 0; chunk.y = 0; chunk.width = G; chunk.height = G;
        chunk.tileWidth = 1; chunk.tileHeight = 1;
        chunk.tiles = tiles.data(); chunk.tileCount = tiles.size();
        chunk.fog = fog.data();
        chunk.id = 103; chunk.dirty = true;   // textureId 0 -> procedural atlas: id 1 = grey palette(1)

        const uint32_t got = renderCenter(chunk, G);
        const uint32_t full = lod::paletteColor(1);
        INFO("fog got=" << std::hex << got);
        for (int shift = 0; shift <= 16; shift += 8) {
            const int expected = byteOf(full, shift) * 128 / 255;   // dimmed to ~half
            CHECK(byteOf(got, shift) >= expected - 15);
            CHECK(byteOf(got, shift) <= expected + 15);
        }
    }

    // --- ANIMATED TILE (water/lava): id 1 declared as 3 frames @ 1 fps -> the atlas LAYER cycles
    //     0,1,2 over time = the colors of ids 1,2,3. The index texture never changes; only the clock.
    {
        pass.setTileAnim(1, 3, 1.0f);   // tile id 1: 3 consecutive layers (0,1,2) at 1 fps

        const int G = 8;                // zoomed in -> detail band (samples the atlas array)
        std::vector<uint16_t> tiles(static_cast<size_t>(G) * G, static_cast<uint16_t>(1));
        TilemapChunk chunk{};
        chunk.x = 0; chunk.y = 0; chunk.width = G; chunk.height = G;
        chunk.tileWidth = 1; chunk.tileHeight = 1;
        chunk.tiles = tiles.data(); chunk.tileCount = tiles.size();
        chunk.id = 200; chunk.dirty = true;

        // time -> expected tile id whose palette color the frame shows (layer = id-1 + frame).
        const float times[4]    = { 0.0f, 1.0f, 2.0f, 3.0f };
        const uint16_t wantId[4] = { 1,    2,    3,    1    };   // frame 0,1,2, then wrap to 0
        for (int s = 0; s < 4; ++s) {
            const uint32_t got = renderCenter(chunk, G, times[s]);
            const uint32_t want = lod::paletteColor(wantId[s]);
            INFO("anim t=" << times[s] << " got=" << std::hex << got << " want=" << want);
            CHECK(byteOf(got, 0)  == byteOf(want, 0));    // R
            CHECK(byteOf(got, 8)  == byteOf(want, 8));    // G
            CHECK(byteOf(got, 16) == byteOf(want, 16));   // B
        }
        pass.setTileAnim(1, 1, 0.0f);   // stop animating (hygiene)
    }

    // --- PARTIAL FOG REVEAL (render:tilemap:fog): a fog-ONLY sub-rect update patches just that region of
    //     the R8 mask (mip 0, region update) without re-uploading tiles or re-baking. Render the SAME
    //     retained chunk twice: all-hidden -> centre ~black; then reveal the centre -> centre = tile colour.
    {
        const int G = 8;
        std::vector<uint16_t> tiles(static_cast<size_t>(G) * G, static_cast<uint16_t>(1));
        std::vector<uint8_t>  fog(static_cast<size_t>(G) * G, static_cast<uint8_t>(0));   // ALL HIDDEN
        TilemapChunk chunk{};
        chunk.x = 0; chunk.y = 0; chunk.width = G; chunk.height = G;
        chunk.tileWidth = 1; chunk.tileHeight = 1;
        chunk.tiles = tiles.data(); chunk.tileCount = tiles.size();
        chunk.fog = fog.data(); chunk.id = 300; chunk.dirty = true;

        const uint32_t hidden = renderCenter(chunk, G);    // vis=0 -> fognoise (default black)
        INFO("fog hidden centre=" << std::hex << hidden);
        CHECK(byteOf(hidden, 0) < 40);                      // centre is hidden (dark)

        // Reveal the centre 4x4 via a FOG-ONLY partial update (tiles untouched: dirty=false).
        for (int y = 2; y < 6; ++y) for (int x = 2; x < 6; ++x) fog[static_cast<size_t>(y) * G + x] = 255;
        chunk.dirty = false;
        chunk.fogDirty = true;
        chunk.fogDirtyX = 2; chunk.fogDirtyY = 2; chunk.fogDirtyW = 4; chunk.fogDirtyH = 4;

        const uint32_t revealed = renderCenter(chunk, G);   // centre tile now visible -> its colour
        const uint32_t full = lod::paletteColor(1);
        INFO("fog revealed centre=" << std::hex << revealed << " full=" << full);
        CHECK(byteOf(revealed, 0)  >= byteOf(full, 0)  - 20);   // R back to ~full tile colour
        CHECK(byteOf(revealed, 8)  >= byteOf(full, 8)  - 20);   // G
        CHECK(byteOf(revealed, 16) >= byteOf(full, 16) - 20);   // B
    }

    // --- MULTI-LAYER (Strategy A): a chunk with 2 layers — base OPAQUE + overlay ON TOP. Tile id 0 in
    //     the overlay is transparent (base shows through); an opaque overlay tile covers the base.
    {
        const int G = 8;
        std::vector<uint16_t> base(static_cast<size_t>(G) * G, static_cast<uint16_t>(1));   // grey, opaque
        std::vector<uint16_t> overClear(static_cast<size_t>(G) * G, static_cast<uint16_t>(0)); // transparent
        std::vector<uint16_t> overTeal(static_cast<size_t>(G) * G, static_cast<uint16_t>(3));   // teal, opaque

        auto twoLayer = [&](std::vector<uint16_t>& l0, std::vector<uint16_t>& l1, uint32_t id,
                            TilemapLayer out[2]) -> TilemapChunk {
            out[0].tiles = l0.data(); out[0].tileCount = l0.size(); out[0].textureId = 0;
            out[1].tiles = l1.data(); out[1].tileCount = l1.size(); out[1].textureId = 0;
            TilemapChunk c{};
            c.x = 0; c.y = 0; c.width = G; c.height = G; c.tileWidth = 1; c.tileHeight = 1;
            c.tiles = l0.data(); c.tileCount = l0.size();   // layer 0 = legacy path (upload + LOD)
            c.layers = out; c.layerCount = 2;
            c.id = id; c.dirty = true;
            return c;
        };

        // A: overlay fully transparent -> centre shows the BASE grey (overlay lets it through).
        TilemapLayer la[2]; TilemapChunk ca = twoLayer(base, overClear, 400, la);
        const uint32_t gotBase = renderCenter(ca, G);
        const uint32_t grey = lod::paletteColor(1);
        INFO("multilayer base-through got=" << std::hex << gotBase << " grey=" << grey);
        CHECK(byteOf(gotBase, 0) == byteOf(grey, 0));   // base visible under a transparent overlay

        // B: overlay opaque teal -> centre shows the OVERLAY (drawn on top of the base).
        TilemapLayer lb[2]; TilemapChunk cb = twoLayer(base, overTeal, 401, lb);
        const uint32_t gotOver = renderCenter(cb, G);
        const uint32_t teal = lod::paletteColor(3);
        INFO("multilayer overlay got=" << std::hex << gotOver << " teal=" << teal);
        CHECK(byteOf(gotOver, 0)  == byteOf(teal, 0));   // overlay covers the base
        CHECK(byteOf(gotOver, 8)  == byteOf(teal, 8));
        CHECK(byteOf(gotOver, 16) == byteOf(teal, 16));
    }

    // --- FOG SCALE / OFFSET : l'asset de brouillard est enfin DIMENSIONNABLE.
    //
    // POURQUOI ce cas existe : l'echelle monde du brouillard etait ecrite en dur dans le shader
    // (`texture2D(s_fognoise, worldPos / 64.0)`). Un jeu pouvait changer l'IMAGE, pas la taille a
    // laquelle elle se repete -- donc "utiliser un asset de brouillard plus grand" etait litteralement
    // inexprimable. Le reglage ne se prouve qu'au PIXEL : rien dans le modele ne dit ou la texture a
    // ete echantillonnee.
    //
    // COMMENT : texture de brouillard de 2 texels -- ROUGE puis VERT, wrap Repeat. La tuile centrale
    // est a worldX = 4. On choisit les valeurs pour tomber au CENTRE d'un texel (uv .25 ou .75), donc
    // ni le filtrage lineaire ni l'arrondi ne peuvent brouiller le verdict :
    //     (scale 16, offset 0) -> uv .25 -> ROUGE
    //     (scale 16, offset 8) -> uv .75 -> VERT    <- l'OFFSET deplace l'echantillonnage
    //     (scale 48, offset 8) -> uv .25 -> ROUGE   <- a offset EGAL, l'ECHELLE change le resultat
    {
        const uint32_t RED   = 0xFF0000FFu;   // 0xAABBGGRR
        const uint32_t GREEN = 0xFF00FF00u;
        std::vector<uint32_t> px = { RED, GREEN };

        rhi::TextureDesc fd;
        fd.width = 2; fd.height = 1;
        fd.format = rhi::TextureDesc::RGBA8;
        fd.data = px.data();
        fd.dataSize = static_cast<uint32_t>(px.size() * 4);
        rhi::TextureHandle fogTex = device->createTexture(fd);
        REQUIRE(fogTex.isValid());
        pass.setFogTexture(fogTex);

        // Chunk ENTIEREMENT cache : vis = 0 partout, donc le pixel rendu EST la texture de brouillard.
        const int G = 8;
        std::vector<uint16_t> tiles(static_cast<size_t>(G) * G, static_cast<uint16_t>(1));
        std::vector<uint8_t>  fog(static_cast<size_t>(G) * G, static_cast<uint8_t>(0));

        auto renderWith = [&](float scale, float offX, uint32_t chunkId) -> uint32_t {
            pass.setFogScale(scale);
            pass.setFogOffset(offX, 0.0f);
            TilemapChunk c{};
            c.x = 0; c.y = 0; c.width = G; c.height = G;
            c.tileWidth = 1; c.tileHeight = 1;
            c.tiles = tiles.data(); c.tileCount = tiles.size();
            c.fog = fog.data();
            c.id = chunkId; c.dirty = true;
            return renderCenter(c, G);
        };

        const uint32_t atRed   = renderWith(16.0f, 0.0f, 500);
        const uint32_t atGreen = renderWith(16.0f, 8.0f, 501);
        const uint32_t backRed = renderWith(48.0f, 8.0f, 502);

        INFO("fog scale/offset: uv.25=" << std::hex << atRed
             << " uv.75=" << atGreen << " rescaled=" << backRed);

        // 1. Reference : on echantillonne bien le texel ROUGE.
        CHECK(byteOf(atRed, 0)  > 200);   // R fort
        CHECK(byteOf(atRed, 8)  < 60);    // G faible

        // 2. L'OFFSET deplace l'echantillonnage -> on tombe sur le texel VERT. Sans le reglage, les
        //    deux rendus seraient IDENTIQUES et ce cas serait rouge.
        CHECK(byteOf(atGreen, 8) > 200);  // G fort
        CHECK(byteOf(atGreen, 0) < 60);   // R faible

        // 3. A offset EGAL, changer l'ECHELLE ramene sur le ROUGE : c'est le knob qui manquait.
        CHECK(byteOf(backRed, 0) > 200);
        CHECK(byteOf(backRed, 8) < 60);

        // Remise a l'etat par defaut pour ne pas contaminer les cas suivants.
        pass.setFogScale(64.0f);
        pass.setFogOffset(0.0f, 0.0f);
        pass.setFogTexture(rhi::TextureHandle{});
        device->destroy(fogTex);
    }

    // --- BORD DE BROUILLARD ONDULE (fogEdge) : le bord cesse de suivre la grille.
    //
    // POURQUOI : le masque n'a qu'un texel par tuile ; interpole lineairement, son degrade s'etale sur
    // exactement une tuile et le bord reste visiblement ALIGNE SUR LA GRILLE. Le reflexe serait de
    // reclamer au jeu un masque sous-tuile -- ce serait une erreur : la visibilite est CONNUE par
    // tuile (DAOS revele en minant), donc un masque 4x plus fin ne porterait AUCUNE information de
    // plus, seulement du travail en plus cote jeu. On perturbe donc la lecture du masque avec du
    // bruit, a information constante.
    //
    // COMMENT LE PROUVER : masque coupe VERTICALEMENT, 2 tuiles a gauche de la colonne echantillonnee.
    // Sans ondulation cette colonne est franchement du cote visible, donc UNIFORME. Avec ondulation
    // le bord serpente et la colonne traverse tantot le cache tantot le visible : elle cesse d'etre
    // uniforme. C'est la FORME du bord qu'on mesure, pas une couleur.
    //
    // ⚠️ PIEGE PAYE ICI : un premier essai utilisait un damier 4x4 repete 8 fois sur le chunk. A cette
    // frequence le GPU descend dans les mips et rend la MOYENNE du damier -- exactement 0.5 -- donc une
    // perturbation rigoureusement nulle et un test vert-pour-de-mauvaises-raisons... ou plutot rouge
    // sans que le code soit en cause. Le bruit de test doit etre BASSE FREQUENCE : ici 2 texels sur la
    // hauteur, un seul cycle sur le chunk.
    {
        // Bruit vertical basse frequence : noir en haut, blanc en bas.
        const uint32_t BLACK = 0xFF000000u, WHITE = 0xFFFFFFFFu;
        std::vector<uint32_t> noise = { BLACK, WHITE };   // 1 x 2

        rhi::TextureDesc nd;
        nd.width = 1; nd.height = 2;
        nd.format = rhi::TextureDesc::RGBA8;
        nd.data = noise.data();
        nd.dataSize = static_cast<uint32_t>(noise.size() * 4);
        rhi::TextureHandle noiseTex = device->createTexture(nd);
        REQUIRE(noiseTex.isValid());
        pass.setFogTexture(noiseTex);
        pass.setFogScale(64.0f);      // chunk de 16 -> fogUv 0..0.25 -> x4 -> UN cycle vertical
        pass.setFogOffset(0.0f, 0.0f);

        const int G = 16;
        std::vector<uint16_t> tiles(static_cast<size_t>(G) * G, static_cast<uint16_t>(1));
        std::vector<uint8_t>  fog(static_cast<size_t>(G) * G);
        for (int y = 0; y < G; ++y)
            for (int x = 0; x < G; ++x)
                fog[static_cast<size_t>(y) * G + x] = (x < G / 2 - 2) ? 0 : 255;

        auto columnSpread = [&](float edge, uint32_t chunkId) -> int {
            pass.setFogEdge(edge);
            TilemapChunk c{};
            c.x = 0; c.y = 0; c.width = G; c.height = G;
            c.tileWidth = 1; c.tileHeight = 1;
            c.tiles = tiles.data(); c.tileCount = tiles.size();
            c.fog = fog.data();
            c.id = chunkId; c.dirty = true;
            const std::vector<uint32_t> colPx = renderColumn(c, G);
            int lo = 255, hi = 0;
            for (uint32_t v : colPx) {
                const int r = byteOf(v, 0);
                lo = std::min(lo, r); hi = std::max(hi, r);
            }
            return hi - lo;   // 0 = colonne uniforme = bord parfaitement droit
        };

        const int straight = columnSpread(0.0f, 600);
        const int wobbly   = columnSpread(4.0f, 601);

        INFO("bord droit spread=" << straight << " bord ondule spread=" << wobbly);

        // 1. Sans le reglage, le bord est une DROITE : la colonne centrale ne varie pas.
        CHECK(straight <= 2);

        // 2. Avec le reglage, le bord serpente : la meme colonne traverse cache ET visible.
        CHECK(wobbly > straight + 20);

        pass.setFogEdge(0.0f);
        pass.setFogScale(64.0f);
        pass.setFogTexture(rhi::TextureHandle{});
        device->destroy(noiseTex);
    }

    // --- DERIVED LOD COLOUR: the zoom-out band takes its colours from the TILESET (per-layer average)
    //     instead of the built-in 8-colour palette, so both bands agree. Two things are asserted:
    //     (1) with no table registered the LOD is UNCHANGED (the historical palette) — non-regression;
    //     (2) a table registered AFTER the chunk was baked still repaints it. (2) is the real trap: the
    //     LOD texture is cached per chunk id and re-baked only when the chunk is dirty, so without an
    //     explicit invalidation the feature would silently depend on publish order.
    {
        // 2-layer tileset of 1x1 tiles: layer 0 = ORANGE (= tile id 1), layer 1 = TEAL (= id 2).
        // A solid layer averages to its own colour, so the expected LOD colour is ORANGE exactly.
        const uint32_t ORANGE = 0xFF0080FFu;   // R=255 G=128 B=0
        const uint32_t TEAL   = 0xFFFF8000u;   // R=0   G=128 B=255
        std::vector<uint32_t> grid = { ORANGE, TEAL };
        int layers = 0;
        std::vector<uint32_t> arr = grove::atlas::sliceToArray(grid.data(), 2, 1, 1, 1, layers);
        REQUIRE(layers == 2);

        rhi::TextureDesc d;
        d.width = 1; d.height = 1; d.layers = static_cast<uint16_t>(layers);
        d.format = rhi::TextureDesc::RGBA8;
        d.data = arr.data();
        d.dataSize = static_cast<uint32_t>(arr.size() * 4);
        rhi::TextureHandle atlasArr = device->createTexture(d);
        pass.setTileset(8, atlasArr);

        // Uniform chunk of tile id 1, 256 tiles across a 64px FB -> 4 tiles/pixel = the pure LOD band.
        const int G = 256;
        std::vector<uint16_t> tiles(static_cast<size_t>(G) * G, static_cast<uint16_t>(1));
        TilemapChunk chunk{};
        chunk.x = 0; chunk.y = 0; chunk.width = G; chunk.height = G;
        chunk.tileWidth = 1; chunk.tileHeight = 1;
        chunk.tiles = tiles.data(); chunk.tileCount = tiles.size();
        chunk.textureId = 8; chunk.id = 500; chunk.dirty = true;

        // (1) BEFORE any table: the built-in palette, exactly as today. B separates the two candidates
        //     hard (grey B=200 vs orange B=0), so this cannot pass by accident.
        const uint32_t before = renderCenter(chunk, G);
        const uint32_t grey = lod::paletteColor(1);
        INFO("derived-lod before=" << std::hex << before << " palette=" << grey);
        for (int shift = 0; shift <= 16; shift += 8) {
            CHECK(byteOf(before, shift) >= byteOf(grey, shift) - 20);
            CHECK(byteOf(before, shift) <= byteOf(grey, shift) + 20);
        }

        // (2) Register the derived table AFTER the chunk was baked and cached, then re-render it
        //     NOT dirty: nothing but an invalidation can repaint the cached LOD texture.
        std::vector<uint32_t> derived = grove::atlas::averageLayers(arr.data(), 1, 1, layers);
        REQUIRE(derived.size() == 2u);
        REQUIRE(derived[0] == ORANGE);         // sanity: a solid layer averages to itself
        pass.setTilesetLodColors(8, derived);  // table[i] = colour of tile id i+1 (layer convention)

        chunk.dirty = false;
        const uint32_t after = renderCenter(chunk, G);
        INFO("derived-lod after=" << std::hex << after << " want=" << ORANGE);
        for (int shift = 0; shift <= 16; shift += 8) {
            CHECK(byteOf(after, shift) >= byteOf(ORANGE, shift) - 20);
            CHECK(byteOf(after, shift) <= byteOf(ORANGE, shift) + 20);
        }

        // (3) An EXPLICIT game palette OVERRIDES the derived table for the same tileset — the
        //     documented precedence (explicit > derived > built-in). Registered LAST and with the
        //     chunk still NOT dirty, so this re-proves the invalidation on the override path too.
        //     PURPLE differs from ORANGE *and* from the built-in grey on all three channels, so the
        //     assert cannot pass by landing on either of the other two candidates.
        const uint32_t PURPLE = 0xFF800080u;   // R=128 G=0 B=128
        pass.setLodPalette(8, std::vector<uint32_t>{ PURPLE, PURPLE });
        const uint32_t overridden = renderCenter(chunk, G);
        INFO("override-lod got=" << std::hex << overridden << " want=" << PURPLE);
        for (int shift = 0; shift <= 16; shift += 8) {
            CHECK(byteOf(overridden, shift) >= byteOf(PURPLE, shift) - 20);
            CHECK(byteOf(overridden, shift) <= byteOf(PURPLE, shift) + 20);
        }
        device->destroy(atlasArr);
    }

    device->destroy(fb);
    pass.shutdown(*device);
    shaders.shutdown(*device);
    device->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
}
