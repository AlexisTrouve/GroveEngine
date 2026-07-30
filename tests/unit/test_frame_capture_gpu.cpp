/**
 * FrameCaptureGpu — la capture headless doit rendre CE QUE LE JOUEUR VERRAIT.
 *
 * QUOI     : `BgfxRendererModule::setCaptureTarget` détourne la sortie finale du renderer vers une
 *            cible relisible au CPU. Ce test publie une scène connue — un rect MONDE vert plein
 *            cadre, un rect HUD rouge dans un coin — et vérifie les deux dans la capture.
 *
 * POURQUOI : parce que « lier les vues 0 et 1 », le geste évident, est FAUX dès qu'un jeu allume
 *            l'éclairage. Mesuré avant d'écrire ce test :
 *
 *              config                        HUD(8,8)     MONDE(40,40)
 *              sans éclairage, vues 0+1      255,0,0      0,255,0   ✅
 *              AVEC éclairage,  vues 0+1     255,0,0      0,0,0     ❌ monde disparu
 *              AVEC éclairage,  vues 3+1     255,0,0      0,255,0   ✅
 *
 *            Avec l'éclairage, la vue 0 part dans la cible de scène et c'est le COMPOSITE qui sort ;
 *            si le post-traitement tourne, c'est la PRÉSENTATION. L'ensemble des vues finales dépend
 *            donc des effets actifs, et seul le module le sait.
 *
 * ⚠️ LE PIÈGE QUE CE TEST EXISTE POUR ATTRAPER : dans le cas faux, **le HUD reste correct**. Un test
 *    qui ne vérifierait que le HUD — la tentation, puisque la capture sert d'abord à tester un HUD —
 *    passerait au vert en mentant sur la scène. D'où l'assertion sur les DEUX, dans les DEUX
 *    configurations. C'est la discrimination, pas la couverture, qui fait ce test.
 *
 * [gpu] — s'abstient proprement sans GPU.
 */
#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>

#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "RHI/RHIDevice.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include <memory>
#include <vector>

using namespace grove;

namespace {

struct Px { int r, g, b; };

Px pixelAt(const std::vector<uint8_t>& rgba, int W, int x, int y) {
    const size_t i = (static_cast<size_t>(y) * W + x) * 4;
    return { rgba[i + 0], rgba[i + 1], rgba[i + 2] };
}

}  // namespace

TEST_CASE("La capture headless rend le monde ET le HUD, eclairage allume ou non",
          "[gpu][capture]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("pas de video SDL — test ignore"); return; }
    const int W = 64, H = 64;
    SDL_Window* win = SDL_CreateWindow("capture-gpu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("pas de fenetre — test ignore"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("cap_r");
    auto gIO = mgr.createInstance("cap_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle",
                    static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("cap_r"); mgr.removeInstance("cap_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("pas de GPU — test ignore"); return;
    }
    rhi::IRHIDevice* dev = renderer->getDevice();

    // Scène de référence : monde VERT plein cadre, HUD ROUGE 16x16 en haut à gauche.
    //
    // ⚠️ L'ambiant est à MOITIÉ, pas blanc, et c'est tout l'intérêt. Un ambiant blanc est NEUTRE par
    // construction : éclairé et non éclairé rendent alors le même vert, et le test ne discrimine
    // plus rien — il passe même contre une capture qui court-circuite le composite. (Écrit blanc
    // d'abord, et il passait effectivement contre l'implémentation fausse.) À moitié, seul un
    // chemin qui traverse RÉELLEMENT le composite peut rendre un vert assombri.
    auto publishScene = [&](bool withLighting) {
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x", 0); cam->setDouble("y", 0); cam->setDouble("zoom", 1.0);
          cam->setInt("viewportX", 0); cam->setInt("viewportY", 0);
          cam->setInt("viewportW", W); cam->setInt("viewportH", H);
          gIO->publish("render:camera", std::move(cam)); }
        { auto r = std::make_unique<JsonDataNode>("rect");
          r->setDouble("x", 0); r->setDouble("y", 0); r->setDouble("w", W); r->setDouble("h", H);
          r->setInt("color", static_cast<int>(0x00FF00FFu)); r->setInt("layer", 0);
          gIO->publish("render:rect", std::move(r)); }
        { auto r = std::make_unique<JsonDataNode>("rect");
          r->setDouble("x", 0); r->setDouble("y", 0); r->setDouble("w", 16); r->setDouble("h", 16);
          r->setInt("color", static_cast<int>(0xFF0000FFu)); r->setInt("layer", 10);
          r->setString("space", "screen");
          gIO->publish("render:rect", std::move(r)); }
        if (withLighting) {
            // ⚠️ Le contrat est UN entier `color` RGBA, pas des doubles r/g/b — et 0 signifie
            // ÉTEINT. Publier {r,g,b} laisse `color` à 0 et coupe l'éclairage en croyant l'allumer :
            // les trois lignes de la mesure se ressemblent alors, et on conclut à l'envers.
            auto a = std::make_unique<JsonDataNode>("amb");
            a->setInt("color", static_cast<int>(0x808080FFu));   // gris moyen : NON neutre
            gIO->publish("render:ambient", std::move(a));
        }
    };

    auto step = [&] {
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    // Une frame de mise en place (le module reconfigure son pipeline quand l'ambiant change), puis
    // la frame capturée.
    auto shoot = [&](bool withLighting) {
        rhi::FramebufferHandle fb = dev->createFramebuffer(W, H, rhi::TargetFormat::RGBA8);
        publishScene(withLighting);
        step();
        publishScene(withLighting);
        renderer->setCaptureTarget(fb);
        step();
        renderer->setCaptureTarget(rhi::FramebufferHandle{});   // rend la main à l'écran
        std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
        REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));
        return rgba;
    };

    SECTION("sans eclairage") {
        const auto px = shoot(false);
        const Px hud   = pixelAt(px, W, 8, 8);
        const Px world = pixelAt(px, W, 40, 40);
        INFO("HUD=" << hud.r << "," << hud.g << "," << hud.b
             << "  MONDE=" << world.r << "," << world.g << "," << world.b);
        CHECK(hud.r > 200);   CHECK(hud.g < 60);     // le panneau HUD est bien rouge
        CHECK(world.g > 200); CHECK(world.r < 60);   // ...et le monde bien vert
    }

    SECTION("avec eclairage (ambiant a MOITIE, non neutre)") {
        const auto px = shoot(true);
        const Px hud   = pixelAt(px, W, 8, 8);
        const Px world = pixelAt(px, W, 40, 40);
        INFO("HUD=" << hud.r << "," << hud.g << "," << hud.b
             << "  MONDE=" << world.r << "," << world.g << "," << world.b);

        // Le HUD n'est PAS eclaire (il est soumis apres la presentation) : il reste plein rouge.
        CHECK(hud.r > 200);   CHECK(hud.g < 60);

        // LE COEUR DU TEST. Le monde doit ressortir ASSOMBRI, donc avoir traverse le composite.
        //   - capture correcte (sortie finale)      -> vert a moitie, ~128
        //   - capture qui lie la vue 0              -> vert PLEIN, ~255 : le composite est saute
        //   - capture qui lie une vue jamais ecrite -> NOIR, ~0
        // Les trois cas sont distincts : c'est ce qui fait de ce test une discrimination et pas une
        // simple couverture.
        CHECK(world.r < 60);
        CHECK(world.g > 80);
        CHECK(world.g < 200);
    }

    renderer->shutdown();
    mgr.removeInstance("cap_r"); mgr.removeInstance("cap_g");
    SDL_DestroyWindow(win); SDL_Quit();
}
