/**
 * GroveEngine — INTERACTIVE 9-slice (nine-patch) border demo.
 *
 * QUOI : ouvre une fenetre OS chargeant demo_nineslice.json — des boutons et des fenetres habilles d'un CADRE
 *        compose (une seule texture 9-slicee). Boutons de tailles differentes (le meme 32x32) montrent que le
 *        bord reste continu ; survol = re-teinte ; un bouton PULSE en largeur (auto-anime) ; les fenetres se
 *        REDIMENSIONNENT au drag du coin -> preuve LIVE que les coins restent nets a toute taille. Un bouton +
 *        une fenetre PLATS servent de contraste.
 *
 * POURQUOI : la preuve "a l'oeil" du bord continu — complement des tests automatises (NineSliceUnit /
 *            NineSliceCollectorTest / UINineSliceE2E / NineSliceGpu). Les textures de cadre sont GENEREES au
 *            runtime (panneau arrondi translucide) et uploadees via render:texture:upload -> aucun PNG requis.
 *
 * Lancer depuis la RACINE :  ./build/tests/test_nineslice_demo
 * Controles : survole/clique les boutons · drag la barre de titre / le coin d'une fenetre · ESC quitte.
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "BgfxRendererModule.h"
#include "UIModule.h"
#include "RHI/RHIDevice.h"
#include "PngCapture.h"   // headless --shot: readback framebuffer -> PNG (proves the frames render)
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

using namespace grove;

namespace {
// QUOI : genere une texture RGBA8 NxN "panneau arrondi" — un anneau de bord `borderColor` autour d'un
//   interieur `fillColor` translucide, coins arrondis (rayon = inset) et anti-aliases. POURQUOI : donne un
//   cadre 9-slice ou les COINS ronds restent nets a toute taille (c'est ce qui rend l'effet visible) sans
//   fournir de PNG. COMMENT : SDF de rounded-rect (dist<0 dedans) ; hors du bord -> transparent ; bande de
//   `bw` px sous la frontiere -> couleur de bord ; plus profond -> remplissage ; alpha lisse sur la frontiere.
std::vector<uint8_t> roundedPanel(int N, float radius, float bw, uint32_t borderColor, uint32_t fillColor) {
    auto R = [](uint32_t c){ return static_cast<float>((c >> 24) & 0xFF); };
    auto G = [](uint32_t c){ return static_cast<float>((c >> 16) & 0xFF); };
    auto B = [](uint32_t c){ return static_cast<float>((c >> 8)  & 0xFF); };
    auto A = [](uint32_t c){ return static_cast<float>( c        & 0xFF); };

    std::vector<uint8_t> px(static_cast<size_t>(N) * N * 4, 0);
    const float half = N * 0.5f;
    const float b = half - radius;   // demi-extent de la partie DROITE (hors coins arrondis)
    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        const float pxp = x + 0.5f - half, pyp = y + 0.5f - half;
        const float qx = std::max(std::fabs(pxp) - b, 0.0f);
        const float qy = std::max(std::fabs(pyp) - b, 0.0f);
        const float dist = std::sqrt(qx * qx + qy * qy) - radius;   // <0 dedans, >0 dehors
        const float aa = std::min(std::max(0.5f - dist, 0.0f), 1.0f);   // lissage de la frontiere externe
        uint8_t* p = &px[(static_cast<size_t>(y) * N + x) * 4];
        if (aa <= 0.0f) { p[0] = p[1] = p[2] = p[3] = 0; continue; }   // hors du panneau -> transparent
        const bool border = dist > -bw;                                // bande de bord
        const uint32_t c = border ? borderColor : fillColor;
        p[0] = static_cast<uint8_t>(R(c));
        p[1] = static_cast<uint8_t>(G(c));
        p[2] = static_cast<uint8_t>(B(c));
        p[3] = static_cast<uint8_t>(A(c) * aa);                        // alpha * anti-alias de bord
    }
    return px;
}
} // namespace

class NineSliceDemo {
public:
    bool init(SDL_Window* window, int w, int h) {
        m_w = w; m_h = h;
        SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(window, &wmi);

        m_rIOPtr = IntraIOManager::getInstance().createInstance("renderer");
        m_uIOPtr = IntraIOManager::getInstance().createInstance("ui_module");
        m_gIOPtr = IntraIOManager::getInstance().createInstance("game");
        m_rIO = m_rIOPtr.get(); m_uIO = m_uIOPtr.get(); m_gIO = m_gIOPtr.get();

        m_renderer = std::make_unique<BgfxRendererModule>();
        {
            JsonDataNode c("config");
            c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
            c.setInt("windowWidth", m_w); c.setInt("windowHeight", m_h); c.setBool("vsync", true);
            m_renderer->setConfiguration(c, m_rIO, nullptr);
        }
        if (!m_renderer->getDevice()) { std::cerr << "no GPU device\n"; return false; }

        // 1. Upload the two frame textures BEFORE the UI renders, so the nineslice asset resolves to a real
        //    texture on its first publish (asset id is resolved at collect time — a not-yet-resident texture
        //    would latch textureId 0 = white). teal button frame + gold window frame.
        // Button frame art is NEUTRAL/WHITE (border opaque white, glass ~90% white) so the button's state
        // bgColor TINTS it to the real colour (and hover/press re-tint). Window frame is authored in colour
        // (gold ring + dark translucent glass) — the window draws it white-tinted (as-is).
        uploadFrame("ui_btn_frame", 32, 10.0f, 3.0f, 0xFFFFFFFFu, 0xFFFFFFe6u);   // white ring / white glass
        uploadFrame("ui_win_frame", 48, 14.0f, 4.0f, 0xe0b060FFu, 0x1a2230f0u);   // gold ring / dark glass
        for (int i = 0; i < 3; ++i) { publishCamera(); JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); m_renderer->process(in); }

        // 2. Now bring up the UI (its first nineslice:add will resolve the resident textures).
        m_uiModule = std::make_unique<UIModule>();
        {
            JsonDataNode c("config");
            c.setString("layoutFile", "assets/ui/demo_nineslice.json");
            c.setInt("windowWidth", m_w); c.setInt("windowHeight", m_h); c.setInt("baseLayer", 1000);
            m_uiModule->setConfiguration(c, m_uIO, nullptr);
        }

        std::cout << "=== 9-slice demo : survole/clique les boutons, drag le coin d'une fenetre, ESC quitte ===\n";
        return true;
    }

    void handleSDLEvent(SDL_Event& e) {
        if (e.type == SDL_WINDOWEVENT &&
            (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED)) {
            m_w = e.window.data1; m_h = e.window.data2;
            if (auto* dev = m_renderer->getDevice()) dev->reset(static_cast<uint16_t>(m_w), static_cast<uint16_t>(m_h));
            { auto d=std::make_unique<JsonDataNode>("d"); d->setInt("width",m_w); d->setInt("height",m_h); m_gIO->publish("ui:resize", std::move(d)); }
        } else if (e.type == SDL_MOUSEMOTION) {
            auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",e.motion.x); d->setDouble("y",e.motion.y);
            m_gIO->publish("input:mouse:move", std::move(d));
        } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            const int b = (e.button.button == SDL_BUTTON_RIGHT) ? 1 : (e.button.button == SDL_BUTTON_MIDDLE ? 2 : 0);
            auto d=std::make_unique<JsonDataNode>("d");
            d->setInt("button", b); d->setBool("pressed", e.type == SDL_MOUSEBUTTONDOWN);
            d->setDouble("x", e.button.x); d->setDouble("y", e.button.y);
            m_gIO->publish("input:mouse:button", std::move(d));
        }
    }

    void frame(float dt) {
        while (m_gIO->hasMessages() > 0) m_gIO->pullAndDispatch();

        // Auto-animate the "Pulse" button's width (sine 120..300) -> the 9-slice frame re-expands each change,
        // proving the border stays continuous while the box grows/shrinks, no drag needed.
        m_t += dt;
        const double pw = 210.0 + 90.0 * std::sin(m_t * 1.6);
        { auto d=std::make_unique<JsonDataNode>("d"); d->setString("id","b_anim");
          d->setDouble("x", 40.0); d->setDouble("y", 268.0); d->setDouble("width", pw); d->setDouble("height", 46.0);
          m_gIO->publish("ui:set_position", std::move(d)); }

        publishCamera();
        JsonDataNode input("input"); input.setDouble("deltaTime", dt);
        m_uiModule->process(input);
        m_renderer->process(input);
    }

    // Headless capture: render a few frames into an offscreen framebuffer and write it to a PNG. Used by
    // --shot to PROVE (not assume) the framed widgets actually render — a not-yet-resident frame texture would
    // show as a white box, which the readback would reveal. Also returns a crude "did anything colourful draw"
    // stat so a caller/log can fail-franc.
    bool captureShot(const std::string& path) {
        rhi::IRHIDevice* dev = m_renderer->getDevice();
        rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(m_w), static_cast<uint16_t>(m_h));
        dev->setViewFramebuffer(0, fb); dev->setViewFramebuffer(1, fb);
        for (int i = 0; i < 6; ++i) frame(0.016f);   // flush the retained UI (incl. the nineslice) into the fb

        std::vector<uint8_t> rgba(static_cast<size_t>(m_w) * m_h * 4, 0);
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) return false;

        // Quick sanity: count teal-ish (button frames, tinted by their teal bgColor) and gold-ish (window
        // frame ring) pixels. Both ~0 would mean the frame textures didn't resolve (white boxes) — the exact
        // failure a code-read can't catch. The PNG is the real by-eye proof; this is the automated smoke.
        int teal = 0, gold = 0;
        for (size_t i = 0; i < static_cast<size_t>(m_w) * m_h; ++i) {
            const int r = rgba[i*4], g = rgba[i*4+1], b = rgba[i*4+2];
            if (b > 120 && g > 100 && r < 120) ++teal;          // teal-tinted button frame
            else if (r > 150 && g > 110 && b < 130) ++gold;     // gold window-frame ring
        }
        std::cout << "shot: teal(button-frame)=" << teal << " gold(window-frame)=" << gold << " px\n";
        return grove::mvdemo::writeRgbaAsPng(path, m_w, m_h, rgba);
    }

    void shutdown() {
        if (m_uiModule) m_uiModule->shutdown();
        m_renderer->shutdown();
        for (const char* n : {"renderer","ui_module","game"}) IntraIOManager::getInstance().removeInstance(n);
    }

private:
    void publishCamera() {
        auto cam=std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
        cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",m_w); cam->setInt("viewportH",m_h);
        m_gIO->publish("render:camera", std::move(cam));
    }

    // Create a mutable runtime texture by string id then upload the generated rounded-panel pixels into it.
    void uploadFrame(const char* id, int N, float radius, float bw, uint32_t borderColor, uint32_t fillColor) {
        { auto d=std::make_unique<JsonDataNode>("d");
          d->setString("id", id); d->setInt("width", N); d->setInt("height", N); d->setInt("color", 0);
          m_gIO->publish("render:texture:create", std::move(d)); }
        std::vector<uint8_t> px = roundedPanel(N, radius, bw, borderColor, fillColor);
        { auto d=std::make_unique<JsonDataNode>("d");
          d->setString("id", id); d->setInt("w", N); d->setInt("h", N);
          d->setBlob("pixels", px.data(), px.size());
          m_gIO->publish("render:texture:upload", std::move(d)); }
    }

    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::unique_ptr<UIModule> m_uiModule;
    std::shared_ptr<IntraIO> m_rIOPtr, m_uIOPtr, m_gIOPtr;
    IIO* m_rIO=nullptr; IIO* m_uIO=nullptr; IIO* m_gIO=nullptr;
    int m_w=1280, m_h=720;
    float m_t=0.0f;
};

int main(int argc, char** argv) {
    // --shot [out.png] : headless one-frame capture (hidden window) to verify rendering; else interactive.
    std::string shotPath; bool shot = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--shot") { shot = true; if (i + 1 < argc && argv[i+1][0] != '-') shotPath = argv[++i]; }
    }
    if (shot && shotPath.empty()) shotPath = "nineslice_demo.png";

    if (SDL_Init(SDL_INIT_VIDEO) < 0) { std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return 1; }
    const int W = 1280, H = 720;
    SDL_Window* window = SDL_CreateWindow("GroveEngine — 9-slice borders",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
        shot ? SDL_WINDOW_HIDDEN : (SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE));
    if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n"; SDL_Quit(); return 1; }

    NineSliceDemo demo;
    if (!demo.init(window, W, H)) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    if (shot) {
        const bool ok = demo.captureShot(shotPath);
        std::cout << (ok ? "wrote " : "FAILED to write ") << shotPath << "\n";
        demo.shutdown(); SDL_DestroyWindow(window); SDL_Quit();
        return ok ? 0 : 1;
    }

    bool running = true; Uint64 last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) running = false;
            demo.handleSDLEvent(e);
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - last) / SDL_GetPerformanceFrequency(); last = now;
        demo.frame(dt);
        SDL_Delay(1);
    }
    demo.shutdown();
    SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
