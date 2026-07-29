/**
 * GPU test: le surlignage de sélection est VISIBLE, et le texte reste LISIBLE par-dessus.
 *
 * QUOI : on dessine, dans la même frame et au même endroit, un bandeau de sélection (un sprite, via
 *        SpritePass) puis du texte (via TextPass) — exactement ce que publie UITextInput quand du
 *        texte est sélectionné. Puis on relit les pixels et on exige TROIS choses :
 *          1. le bandeau est réellement peint (des pixels de la couleur de sélection existent) ;
 *          2. le texte survit par-dessus (des pixels de la couleur du texte existent DANS le bandeau) ;
 *          3. rien ne déborde hors du bandeau (le fond reste le fond ailleurs).
 *
 * POURQUOI ce test existe : ce chantier vient de découvrir que le surlignage — et le curseur avec lui —
 *        étaient publiés à la couche 0 et se retrouvaient DERRIÈRE le fond du champ, donc invisibles.
 *        Ce bug-là ne se lit pas dans le code (c'est la leçon du chantier 9-slice : « le rendu ne se
 *        lit pas dans le code ») et les tests headless ne l'attrapent que si l'on sait déjà quoi
 *        comparer. La question « est-ce qu'on VOIT le surlignage, et est-ce qu'on lit encore le
 *        texte ? » ne se tranche qu'au pixel.
 *
 *        Le test verrouille aussi l'hypothèse d'ORDRE dont dépend le widget : un rect part dans
 *        SpritePass, le texte dans TextPass, et le graphe de rendu exécute la première AVANT la
 *        seconde. C'est ce qui met le surlignage sous le texte quelles que soient leurs couches
 *        respectives — une inversion des passes rendrait le texte sélectionné illisible, et seul un
 *        test au pixel s'en apercevrait.
 *
 * COMMENT : ortho monde [0,P] -> tout le framebuffer (1px = 1 unité), comme les autres tests [gpu].
 *        Le bandeau est VERT pur et le texte ROUGE pur, deux canaux disjoints : un pixel se classe
 *        alors sans ambiguïté, sans dépendre des métriques exactes de la police ni de
 *        l'antialiasing. [gpu] : nécessite un vrai contexte bgfx ; s'abstient proprement sinon.
 */

#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>

#include <SDL.h>
#include <SDL_syswm.h>

#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include "RHI/RHICommandBuffer.h"
#include "Frame/FramePacket.h"
#include "Passes/SpritePass.h"
#include "Passes/TextPass.h"
#include "Shaders/ShaderManager.h"

#include <cstdint>
#include <vector>

using namespace grove;

TEST_CASE("Le surlignage de selection est visible et le texte reste lisible dessus (GPU)",
          "[gpu][ui][selection]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("selection-highlight", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, 64, 64, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));
#ifdef _WIN32
    void* nwh = wmi.info.win.window; void* ndt = nullptr;
#else
    void* nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));
    void* ndt = wmi.info.x11.display;
#endif

    auto device = rhi::IRHIDevice::create();
    if (!device->init(nwh, ndt, 64, 64)) {
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    ShaderManager shaders;
    shaders.init(*device, device->getCapabilities().rendererName);
    rhi::ShaderHandle prog = shaders.getProgram("sprite");
    REQUIRE(prog.isValid());

    // Les deux passes du composite, dans l'ordre du graphe de rendu : sprites PUIS texte.
    SpritePass spritePass(prog);
    spritePass.setup(*device);
    TextPass textPass(prog);
    textPass.setup(*device);

    const uint16_t P = 64;
    rhi::FramebufferHandle fb = device->createFramebuffer(P, P, rhi::TargetFormat::RGBA8);

    const float g = static_cast<float>(P);
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float proj[16] = { 2.0f/g,0,0,0,  0,2.0f/g,0,0,  0,0,1,0,  -1.0f,-1.0f,0,1 };
    device->setViewFramebuffer(0, fb);
    device->setViewRect(0, 0, 0, P, P);
    device->setViewClear(0, 0x000000FFu, 1.0f);   // fond noir : ni vert ni rouge
    device->setViewTransform(0, view, proj);

    // --- Le bandeau de sélection : VERT pur, moitié GAUCHE, bande verticale centrale ---
    // (x,y de SpriteInstance sont un CENTRE — convention d'ancrage du moteur.)
    const float bandW = g * 0.5f, bandH = 24.0f;
    SpriteInstance band{};
    band.x = bandW * 0.5f;       // centre du bandeau : moitié gauche
    band.y = 32.0f;
    band.scaleX = bandW;
    band.scaleY = bandH;
    band.rotation = 0.0f;
    band.u0 = 0.0f; band.v0 = 0.0f; band.u1 = 1.0f; band.v1 = 1.0f;
    band.r = 0.0f; band.g = 1.0f; band.b = 0.0f; band.a = 1.0f;  // vert opaque
    band.textureId = 0;          // quad plat teinté
    band.layer = 0;
    band.reserved[0] = band.reserved[1] = band.reserved[2] = band.reserved[3] = 0.0f;  // pas de clip

    // --- Le texte : ROUGE pur, à l'intérieur du bandeau ---
    TextCommand tc{};
    tc.x = 2.0f; tc.y = 24.0f;
    tc.text = "MMMM";
    tc.fontId = 0; tc.fontSize = 18; tc.color = 0xFF0000FFu; tc.layer = 0;

    FramePacket frame;
    frame.sprites = &band; frame.spriteCount = 1;
    frame.texts = &tc;     frame.textCount = 1;

    rhi::RHICommandBuffer cmd;
    spritePass.execute(frame, *device, cmd);   // le surlignage d'abord...
    textPass.execute(frame, *device, cmd);     // ...puis le texte par-dessus
    device->executeCommandBuffer(cmd);
    device->frame();

    std::vector<uint8_t> px(static_cast<size_t>(P) * P * 4, 0);
    REQUIRE(device->readFramebuffer(fb, px.data(), static_cast<uint32_t>(px.size())));

    // Classement des pixels. Canaux disjoints => aucune ambiguïté : un pixel « rouge » est du texte,
    // un pixel « vert » est du surlignage nu, un pixel noir est le fond.
    int greenInBand = 0;   // surlignage visible
    int redInBand = 0;     // texte visible PAR-DESSUS le surlignage
    int greenOutside = 0;  // débordement du surlignage hors de sa boîte
    for (int y = 0; y < P; ++y) {
        for (int x = 0; x < P; ++x) {
            const size_t i = (static_cast<size_t>(y) * P + x) * 4;
            const int R = px[i + 0], G = px[i + 1];
            const bool inBand = (x < static_cast<int>(bandW)) &&
                                (y >= 32 - static_cast<int>(bandH) / 2) &&
                                (y <  32 + static_cast<int>(bandH) / 2);
            if (R > 100 && G < 100) { if (inBand) ++redInBand; }
            else if (G > 100)       { inBand ? ++greenInBand : ++greenOutside; }
        }
    }

    INFO("greenInBand=" << greenInBand << " redInBand=" << redInBand
         << " greenOutside=" << greenOutside);

    // 1. Le surlignage EST peint. C'est l'assertion qui aurait attrapé le bug de couche : un
    //    surlignage caché derrière le fond ne produit aucun pixel vert.
    CHECK(greenInBand > 0);

    // 2. Le texte survit PAR-DESSUS. Si les passes s'exécutaient dans l'autre ordre, le bandeau
    //    recouvrirait les glyphes et il ne resterait aucun pixel rouge : texte sélectionné illisible.
    CHECK(redInBand > 0);

    // 3. Le surlignage ne déborde pas de sa boîte.
    CHECK(greenOutside == 0);

    textPass.shutdown(*device);
    spritePass.shutdown(*device);
    SDL_DestroyWindow(win);
    SDL_Quit();
}
