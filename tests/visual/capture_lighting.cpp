/**
 * Headless CAPTURE of the 2D lighting (L1 + L2) to a series of PNGs.
 *
 * Renders the SAME scene five times, changing only the lighting, and writes one PNG each. Lets us
 * SEE what the engine draws — the pixel tests prove the numbers, these show the picture.
 *
 * ⚠️ Binds the COMPOSITE view, not view 0: once lighting is on the module redirects view 0 into the
 *    scene target, so capturing view 0 would grab the UNLIT scene. Same trap as the [gpu] test.
 *
 * Usage: capture_lighting [outDir]   (default: blog/)
 */

#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "Passes/CompositePass.h"
#include "RHI/RHIDevice.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

// --- svpng: minimal public-domain PNG writer (Milo Yip). Stored DEFLATE, no deps. ---
static void svpng(FILE* fp, unsigned w, unsigned h, const unsigned char* img, int alpha) {
    static const unsigned t[] = { 0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c };
    unsigned a = 1, b = 0, c, p = w * (alpha ? 4 : 3) + 1, x, y, i;
#define SVPNG_PUT(u) fputc(u, fp)
#define SVPNG_U8A(ua, l) for (i = 0; i < l; i++) SVPNG_PUT((ua)[i]);
#define SVPNG_U32(u) do { SVPNG_PUT((u) >> 24); SVPNG_PUT(((u) >> 16) & 255); SVPNG_PUT(((u) >> 8) & 255); SVPNG_PUT((u) & 255); } while(0)
#define SVPNG_U8C(u) do { SVPNG_PUT(u); c ^= (u); c = (c >> 4) ^ t[c & 15]; c = (c >> 4) ^ t[c & 15]; } while(0)
#define SVPNG_U8AC(ua, l) for (i = 0; i < l; i++) SVPNG_U8C((ua)[i])
#define SVPNG_U16LC(u) do { SVPNG_U8C((u) & 255); SVPNG_U8C(((u) >> 8) & 255); } while(0)
#define SVPNG_U32C(u) do { SVPNG_U8C((u) >> 24); SVPNG_U8C(((u) >> 16) & 255); SVPNG_U8C(((u) >> 8) & 255); SVPNG_U8C((u) & 255); } while(0)
#define SVPNG_U8ADLER(u) do { SVPNG_U8C(u); a = (a + (u)) % 65521; b = (b + a) % 65521; } while(0)
#define SVPNG_BEGIN(s, l) do { SVPNG_U32(l); c = ~0U; SVPNG_U8AC(s, 4); } while(0)
#define SVPNG_END() SVPNG_U32(~c)
    SVPNG_U8A("\x89PNG\r\n\32\n", 8);
    SVPNG_BEGIN("IHDR", 13);
    SVPNG_U32C(w); SVPNG_U32C(h);
    SVPNG_U8C(8); SVPNG_U8C(alpha ? 6 : 2); SVPNG_U8AC("\0\0\0", 3);
    SVPNG_END();
    SVPNG_BEGIN("IDAT", 2 + h * (5 + p) + 4);
    SVPNG_U8AC("\x78\1", 2);
    for (y = 0; y < h; y++) {
        SVPNG_U8C(y == h - 1);
        SVPNG_U16LC(p); SVPNG_U16LC(~p & 0xffff);
        SVPNG_U8ADLER(0);
        for (x = 0; x < w * (alpha ? 4 : 3); x++, img++)
            SVPNG_U8ADLER(*img);
    }
    SVPNG_U32C((b << 16) | a);
    SVPNG_END();
    SVPNG_BEGIN("IEND", 0);
    SVPNG_END();
}

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : "blog";
    const int W = 480, H = 270;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("cap-light", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { std::fprintf(stderr, "no window\n"); SDL_Quit(); return 1; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("capl_r");
    auto gIO = mgr.createInstance("capl_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    { JsonDataNode c("config");
      c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
      c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
      renderer->setConfiguration(c, rIO.get(), nullptr); }

    rhi::IRHIDevice* dev = renderer->getDevice();
    if (!dev) { std::fprintf(stderr, "no device\n"); SDL_Quit(); return 2; }

    auto sprite = [&](double cx, double cy, double w, double h, uint32_t col, int layer) {
        auto s = std::make_unique<JsonDataNode>("d");
        s->setDouble("cx", cx); s->setDouble("cy", cy);
        s->setDouble("scaleX", w); s->setDouble("scaleY", h);
        s->setInt("color", static_cast<int>(col)); s->setInt("layer", layer);
        gIO->publish("render:sprite", std::move(s));
    };
    // A STRETCHED, rotated, ADDITIVE quad - the plume primitive. blend:"additive" is the whole
    // difference: two of these crossing must be BRIGHTER where they overlap, which alpha never does.
    auto glow = [&](double cx, double cy, double w, double h, double rot, uint32_t col, int layer) {
        auto s = std::make_unique<JsonDataNode>("d");
        s->setDouble("cx", cx); s->setDouble("cy", cy);
        s->setDouble("scaleX", w); s->setDouble("scaleY", h);
        s->setDouble("rotation", rot);
        s->setString("blend", "additive");
        s->setInt("color", static_cast<int>(col)); s->setInt("layer", layer);
        gIO->publish("render:sprite", std::move(s));
    };
    // dirDeg/spreadDeg default to omni, so the earlier plates are unchanged by L3.
    auto light = [&](double cx, double cy, double radius, uint32_t col, double intensity,
                     double dirDeg = 0.0, double spreadDeg = 360.0) {
        auto l = std::make_unique<JsonDataNode>("d");
        l->setDouble("cx", cx); l->setDouble("cy", cy); l->setDouble("radius", radius);
        l->setInt("color", static_cast<int>(col)); l->setDouble("intensity", intensity);
        l->setDouble("dirDeg", dirDeg); l->setDouble("spreadDeg", spreadDeg);
        gIO->publish("render:light", std::move(l));
    };
    // A wall: opaque matter. x,y = top-left CORNER (not the sprite convention beside it).
    // ⚠️ It is INVISIBLE by itself — an occluder affects light, it does not draw. Every shot below
    //    pairs it with a sprite at the same rect, or the shadow would appear to come from nowhere.
    auto occluder = [&](double x, double y, double w, double h) {
        auto o = std::make_unique<JsonDataNode>("d");
        o->setDouble("x", x); o->setDouble("y", y);
        o->setDouble("w", w); o->setDouble("h", h);
        gIO->publish("render:occluder", std::move(o));
    };
    // A filter: the same rect, but it TINTS instead of blocking. `color` is the tint after one
    // perpendicular crossing of the pane's thin axis — here always the width.
    auto filter = [&](double x, double y, double w, double h, uint32_t col) {
        auto f = std::make_unique<JsonDataNode>("d");
        f->setDouble("x", x); f->setDouble("y", y);
        f->setDouble("w", w); f->setDouble("h", h);
        f->setInt("color", static_cast<int>(col));
        gIO->publish("render:filter", std::move(f));
    };
    auto ambient = [&](uint32_t col) {
        auto a = std::make_unique<JsonDataNode>("d");
        a->setInt("color", static_cast<int>(col));
        gIO->publish("render:ambient", std::move(a));
    };

    // The scene: a tiled stone floor with a few crates and a wall band. Flat tinted quads only — no
    // assets needed, and it keeps the lighting the only variable between shots.
    auto drawScene = [&]{
        const int TS = 30;
        for (int y = 0; y * TS < H; ++y) {
            for (int x = 0; x * TS < W; ++x) {
                const bool alt = ((x + y) & 1) != 0;
                const uint32_t col = alt ? 0x8a8f99FFu : 0x767b85FFu;   // two greys
                sprite(x * TS + TS * 0.5, y * TS + TS * 0.5, TS - 1.0, TS - 1.0, col, 5);
            }
        }
        sprite(W * 0.5, 22.0, static_cast<double>(W), 44.0, 0x4a5568FFu, 6);   // wall band, top
        sprite(120.0, 170.0, 46.0, 46.0, 0xa9744aFFu, 8);                       // crate
        sprite(300.0, 120.0, 38.0, 38.0, 0xa9744aFFu, 8);                       // crate
        sprite(390.0, 205.0, 54.0, 30.0, 0x6b8f5aFFu, 8);                       // green chest
    };

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016);
        renderer->process(in);
    };

    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    // `lit` selects WHICH view carries the picture: with no ambient the world still goes to view 0
    // (the zero-cost path), with lighting on the finished frame is on the composite view.
    // `plainGround` swaps the tiled floor for a near-black backdrop: an additive glow is only
    // legible against something dark, and on the lit stone floor the intersection would be washed
    // out by what is already there - the shot would show nothing even with the blend working.
    bool plainGround = false;
    auto shoot = [&](const char* name, bool lit, void (*setup)(void*), void* ctx) {
        for (int i = 0; i < 5; ++i) {
            if (plainGround) sprite(W * 0.5, H * 0.5, static_cast<double>(W), static_cast<double>(H), 0x0a0d14FFu, 1);
            else drawScene();
            if (setup) setup(ctx);
            if (lit) {
                dev->setViewFramebuffer(CompositePass::kCompositeView, fb);
            } else {
                dev->setViewFramebuffer(0, fb);
                dev->setViewFramebuffer(1, fb);
            }
            frame();
        }
        std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
            std::fprintf(stderr, "readback failed for %s\n", name);
            return;
        }
        std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3, 0);
        for (size_t i = 0, n = static_cast<size_t>(W) * H; i < n; ++i) {
            rgb[i*3+0] = rgba[i*4+0]; rgb[i*3+1] = rgba[i*4+1]; rgb[i*3+2] = rgba[i*4+2];
        }
        const std::string path = outDir + "/" + name;
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
        svpng(f, W, H, rgb.data(), 0);
        std::fclose(f);
        std::printf("wrote %s\n", path.c_str());
    };

    struct Ctx { decltype(light)* lightFn; decltype(ambient)* ambFn; decltype(sprite)* spriteFn;
                 decltype(glow)* glowFn; decltype(occluder)* occFn; decltype(filter)* filtFn; };
    Ctx ctx{ &light, &ambient, &sprite, &glow, &occluder, &filter };

    // 1. No lighting at all — the zero-cost path every current game is on.
    shoot("01_unlit.png", false, nullptr, nullptr);

    // 2. Dim ambient only: night, no lamps.
    shoot("02_ambient.png", true, [](void* c){
        (*static_cast<Ctx*>(c)->ambFn)(0x2a3040FFu);
    }, &ctx);

    // 3. One warm lamp over the left crate.
    shoot("03_one_lamp.png", true, [](void* c){
        Ctx* k = static_cast<Ctx*>(c);
        (*k->ambFn)(0x2a3040FFu);
        (*k->lightFn)(120.0, 170.0, 110.0, 0xFFC070FFu, 1.6);
    }, &ctx);

    // 4. Three coloured lamps: where they overlap the sum goes past what any one of them gives —
    //    that is the additive accumulation, and it is why the buffer is RGBA16F.
    shoot("04_three_lamps.png", true, [](void* c){
        Ctx* k = static_cast<Ctx*>(c);
        (*k->ambFn)(0x22283aFFu);
        (*k->lightFn)(140.0, 175.0, 130.0, 0xFF8040FFu, 1.5);   // warm
        (*k->lightFn)(300.0, 120.0, 130.0, 0x40A0FFFFu, 1.5);   // cold
        (*k->lightFn)(390.0, 205.0, 110.0, 0x60FF90FFu, 1.4);   // green
    }, &ctx);

    // 5. Overbright: intensity well past 1. RGBA8 would clip this to flat white and lose the
    //    gradient; the half-float target keeps it, which is what bloom will feed on.
    shoot("05_overbright.png", true, [](void* c){
        Ctx* k = static_cast<Ctx*>(c);
        (*k->ambFn)(0x1a1e2aFFu);
        (*k->lightFn)(240.0, 150.0, 170.0, 0xFFE0A0FFu, 4.0);
    }, &ctx);

    // 6. A CONE light (L3). Same lamp as plate 03, plus dirDeg/spreadDeg: the disc becomes a beam
    //    with a soft rim. A hard angular cut would read as a cardboard pie slice.
    shoot("06_cone.png", true, [](void* c){
        Ctx* k = static_cast<Ctx*>(c);
        (*k->ambFn)(0x22283aFFu);
        // Pointing right-and-down (30 deg), 55 deg wide.
        (*k->lightFn)(150.0, 90.0, 240.0, 0xFFE0B0FFu, 2.2, 30.0, 55.0);
    }, &ctx);

    // 7. THE use case: a thruster. The hull is a plain sprite, the flame is an ADDITIVE-looking
    //    bright quad, and the light is a cone pointing BACKWARD along the exhaust — the emitter and
    //    the lamp take the same dirDeg, which is exactly why the cone convention was borrowed from
    //    grove::fx::Emitter.
    shoot("07_thruster.png", true, [](void* c){
        Ctx* k = static_cast<Ctx*>(c);
        (*k->ambFn)(0x1c2230FFu);
        (*k->spriteFn)(300.0, 135.0, 54.0, 22.0, 0xc8d0dcFFu, 12);   // hull
        (*k->spriteFn)(268.0, 135.0, 16.0, 12.0, 0xFFD9A0FFu, 13);   // nozzle glow
        (*k->lightFn)(262.0, 135.0, 200.0, 0xFFB060FFu, 3.0, 180.0, 42.0);  // exhaust cone, pointing -x
    }, &ctx);

    // 8. ADDITIVE STRETCHED QUADS on a near-black ground - the Waterfall plume primitive.
    //    Two crossing beams: the intersection must be BRIGHTER than either beam. No lighting at all
    //    in this shot, so nothing but the blend can explain it.
    plainGround = true;
    shoot("08_additive_plume.png", false, [](void* c){
        Ctx* k = static_cast<Ctx*>(c);
        (*k->glowFn)(200.0, 135.0, 260.0, 26.0,  0.35, 0xFF9840FFu, 40);
        (*k->glowFn)(280.0, 135.0, 260.0, 26.0, -0.35, 0x50A0FFFFu, 41);
    }, &ctx);

    plainGround = false;

    // ⚠️ Why the walls and panes below are ~28 units thick and not 6.
    // The light shader marches the occlusion map in a FIXED number of steps between the lamp and the
    // fragment, so one step is worth (distance / OCCLUSION_STEPS) world units. At the reach used here
    // (~300 units) that is ~19 units per step, and matter thinner than a step can be STEPPED OVER
    // entirely — the shadow would come out full of holes. Thin walls work when lamps are small; these
    // plates use wide lamps, so the matter is scaled to match.

    // 9. A WALL casts a shadow (plan W). The band on the right is lit only by the ambient — the lamp
    //    contributes exactly nothing there, because a zero in the occlusion map annihilates the
    //    running product for the whole rest of that ray.
    shoot("09_wall_shadow.png", true, [](void* c){
        Ctx* k = static_cast<Ctx*>(c);
        (*k->ambFn)(0x22283aFFu);
        (*k->lightFn)(120.0, 135.0, 340.0, 0xFFD0A0FFu, 2.6);
        (*k->spriteFn)(244.0, 135.0, 28.0, static_cast<double>(H), 0x323844FFu, 9);   // the wall, drawn
        (*k->occFn)(230.0, 0.0, 28.0, static_cast<double>(H));                        // the wall, as matter
    }, &ctx);

    // 10. THE PAIR: identical shot, the wall swapped for a RED PANE. Light no longer stops — it comes
    //     through tinted. Same lamp, same rect, same everything else: the only difference in the
    //     picture is what the matter transmits.
    shoot("10_filter_red.png", true, [](void* c){
        Ctx* k = static_cast<Ctx*>(c);
        (*k->ambFn)(0x22283aFFu);
        (*k->lightFn)(120.0, 135.0, 340.0, 0xFFD0A0FFu, 2.6);
        (*k->spriteFn)(244.0, 135.0, 28.0, static_cast<double>(H), 0x9a3a3aFFu, 9);   // the glass, drawn
        (*k->filtFn)(230.0, 0.0, 28.0, static_cast<double>(H), 0xFF4040FFu);          // the glass, as matter
    }, &ctx);

    // 11. Two panes, three zones. Light crossing ONLY the amber one, ONLY the magenta one, and BOTH
    //     — the last being the product of the other two. No sorting anywhere: the product is
    //     commutative, so which pane was published first cannot change the picture.
    shoot("11_filter_stack.png", true, [](void* c){
        Ctx* k = static_cast<Ctx*>(c);
        (*k->ambFn)(0x1e2432FFu);
        (*k->lightFn)(70.0, 135.0, 460.0, 0xFFFFFFFFu, 1.9);
        // Amber pane: full height.
        (*k->spriteFn)(194.0, 135.0, 24.0, static_cast<double>(H), 0x8a6a30FFu, 9);
        (*k->filtFn)(182.0, 0.0, 24.0, static_cast<double>(H), 0xFFB050FFu);
        // Magenta pane: UPPER half only, so the lower half stays amber-only for comparison.
        (*k->spriteFn)(294.0, 67.0, 24.0, 134.0, 0x8a3080FFu, 9);
        (*k->filtFn)(282.0, 0.0, 24.0, 134.0, 0xFF60FFFFu);
    }, &ctx);

    // 12. The use case the whole plan is named after: a STAINED-GLASS WALL. Opaque segments with two
    //     glazed gaps, a lamp outside on the left. Walls and panes are the same mechanism at two ends
    //     of one scale — one transmits nothing, the other transmits a colour — and they are written
    //     into the same map by the same pass.
    shoot("12_stained_window.png", true, [](void* c){
        Ctx* k = static_cast<Ctx*>(c);
        (*k->ambFn)(0x161c28FFu);
        (*k->lightFn)(60.0, 135.0, 520.0, 0xFFF0D0FFu, 3.0);
        const double wx = 190.0, wt = 28.0;
        // Opaque masonry: three segments, leaving two gaps.
        const double segs[3][2] = { {0.0, 62.0}, {112.0, 46.0}, {212.0, 58.0} };
        for (auto& s : segs) {
            (*k->spriteFn)(wx + wt * 0.5, s[0] + s[1] * 0.5, wt, s[1], 0x2c323eFFu, 9);
            (*k->occFn)(wx, s[0], wt, s[1]);
        }
        // The two glazed gaps.
        (*k->spriteFn)(wx + wt * 0.5, 87.0, wt, 50.0, 0x8a2f2fFFu, 9);
        (*k->filtFn)(wx, 62.0, wt, 50.0, 0xFF4A3AFFu);          // warm red glass
        (*k->spriteFn)(wx + wt * 0.5, 185.0, wt, 54.0, 0x2a4a8aFFu, 9);
        (*k->filtFn)(wx, 158.0, wt, 54.0, 0x50A0FFFFu);         // cold blue glass
    }, &ctx);

    renderer->shutdown();
    mgr.removeInstance("capl_r");
    mgr.removeInstance("capl_g");
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
