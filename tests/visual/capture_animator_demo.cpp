/**
 * Capture headless de l'ANIMATOR (grove::anim) en une séquence de PNG -> GIF.
 *
 * QUOI  : deux pantins IDENTIQUES, rigés par la même Hierarchy et animés par les mêmes Clips,
 *         parcourant exactement la même séquence d'états. Le seul écart : le pantin de GAUCHE a
 *         `setDefaultFade(0)` (coupe franche, le comportement d'avant l'Animator), celui de DROITE
 *         `setDefaultFade(0.18)`.
 *
 * POURQUOI : les 20 cas d'`AnimatorUnit` prouvent le contrat NUMÉRIQUEMENT, ce qui est la bonne
 *         preuve pour de la math pure — mais ils ne le rendent pas JUGEABLE. Une couture
 *         d'animation se juge à l'œil, et la seule façon honnête de montrer ce qu'un fondu apporte
 *         est de faire tourner à côté la version qui ne l'a pas. C'est « quelle image aurait donné
 *         la version fausse ? » rendu visuel.
 *
 * COMMENT : rig -> Animator -> `render:sprite` (un quad étiré et tourné par os) -> pipeline réel ->
 *         `setCaptureTarget` -> lecture CPU -> un PNG par frame. L'assemblage en GIF est fait par
 *         ffmpeg dans tools/make_animator_gif.sh (pas ici : encoder un GIF n'apprend rien).
 *
 * ⚠️ Ce que ce GIF NE montre PAS : l'arc le plus court sur les rotations. Le rendre visible
 *    demanderait de faire tourner À CÔTÉ une implémentation délibérément fausse ; il est verrouillé
 *    numériquement (`AnimatorUnit [fade]`, 0.0 au lieu de π sous sabotage).
 *
 * Lancer depuis la racine du projet : capture_animator_demo [dossier_de_sortie]
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include "PngCapture.h"

#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/anim/Animator.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace grove;
using namespace grove::anim;

namespace {

constexpr int   W = 720;
constexpr int   H = 400;
constexpr float kFps = 30.0f;
constexpr float kDt  = 1.0f / kFps;
constexpr float kPi  = 3.14159265f;

// ---------------------------------------------------------------------------
// Le squelette. Un os pointe le long de son +X local ; sa rotation est donc l'angle du membre, et
// c'est cette valeur-là que les Clips animent (Clip::apply ÉCRIT la valeur, il ne l'ajoute pas —
// les angles sont donc absolus dans le repère du parent).
// ---------------------------------------------------------------------------
// ⚠️ `root` porte la position à l'écran et n'est JAMAIS animé ; `pelvis` est son enfant et reçoit
// le ballant vertical. Séparer les deux n'est pas cosmétique : `Clip::apply` ÉCRIT la propriété
// (il ne l'ajoute pas), donc une piste TranslationY sur le nœud de placement ÉCRASE la position du
// pantin — mesuré, les deux pantins se sont retrouvés collés en haut de l'écran, y = 0..4.
struct Rig {
    Hierarchy h;
    int root, pelvis, torso, head, armU, armL, legLU, legLL, legRU, legRL;

    void build(float originX, float originY) {
        root   = h.addNode(-1, Transform2D{originX, originY, 0.0f, 1.0f, 1.0f});
        pelvis = h.addNode(root, Transform2D{0, 0, 0.0f, 1.0f, 1.0f});      // animé (ballant)
        torso  = h.addNode(pelvis, Transform2D{0, 0, -kPi / 2, 1, 1});      // vers le haut
        head   = h.addNode(torso,  Transform2D{72, 0, 0, 1, 1});            // au bout du torse
        armU   = h.addNode(torso,  Transform2D{58, 0, kPi / 2, 1, 1});      // épaule
        armL   = h.addNode(armU,   Transform2D{34, 0, 0, 1, 1});            // coude
        legLU  = h.addNode(pelvis, Transform2D{0, 0, kPi / 2, 1, 1});
        legLL  = h.addNode(legLU,  Transform2D{38, 0, 0, 1, 1});
        legRU  = h.addNode(pelvis, Transform2D{0, 0, kPi / 2, 1, 1});
        legRL  = h.addNode(legRU,  Transform2D{38, 0, 0, 1, 1});
    }
};

// Longueur dessinée de chaque os, par id de nœud (l'index suit l'ordre d'ajout de build()).
// root et pelvis ne portent pas d'os (0), la tête est dessinée à part en carré.
constexpr int   kNodeCount = 10;
constexpr float kBoneLen[kNodeCount] = { 0.0f, 0.0f, 72.0f, 0.0f, 34.0f, 32.0f, 38.0f, 36.0f, 38.0f, 36.0f };

// ---------------------------------------------------------------------------
// Les clips. Volontairement grossiers : ce qu'on regarde est la TRANSITION entre eux, pas leur
// qualité d'animation. Un tremblement de plus rendrait la couture MOINS lisible.
// ---------------------------------------------------------------------------
void addTrack(Clip& c, int node, Property prop, std::vector<Keyframe> keys) {
    Track t; t.nodeId = node; t.property = prop; t.keys = std::move(keys);
    c.tracks.push_back(t);
}

Clip makeIdle(const Rig& r) {
    Clip c; c.duration = 2.0f;
    addTrack(c, r.pelvis, Property::TranslationY, {{0.0f, 0.0f, Easing::InOutQuad},
                                                   {1.0f, 4.0f, Easing::InOutQuad},
                                                   {2.0f, 0.0f, Easing::Linear}});
    addTrack(c, r.armU, Property::Rotation, {{0.0f, kPi * 0.5f + 0.12f, Easing::Linear}});
    addTrack(c, r.armL, Property::Rotation, {{0.0f, 0.10f, Easing::Linear}});
    addTrack(c, r.legLU, Property::Rotation, {{0.0f, kPi * 0.5f - 0.06f, Easing::Linear}});
    addTrack(c, r.legRU, Property::Rotation, {{0.0f, kPi * 0.5f + 0.06f, Easing::Linear}});
    addTrack(c, r.torso, Property::Rotation, {{0.0f, -kPi * 0.5f, Easing::Linear}});
    return c;
}

Clip makeWalk(const Rig& r) {
    Clip c; c.duration = 0.8f;
    const float d = kPi * 0.5f;   // « vers le bas »
    addTrack(c, r.legLU, Property::Rotation, {{0.0f, d - 0.55f, Easing::InOutQuad},
                                              {0.4f, d + 0.55f, Easing::InOutQuad},
                                              {0.8f, d - 0.55f, Easing::Linear}});
    addTrack(c, r.legRU, Property::Rotation, {{0.0f, d + 0.55f, Easing::InOutQuad},
                                              {0.4f, d - 0.55f, Easing::InOutQuad},
                                              {0.8f, d + 0.55f, Easing::Linear}});
    addTrack(c, r.legLL, Property::Rotation, {{0.0f, 0.45f, Easing::InOutQuad},
                                              {0.4f, 0.05f, Easing::InOutQuad},
                                              {0.8f, 0.45f, Easing::Linear}});
    addTrack(c, r.legRL, Property::Rotation, {{0.0f, 0.05f, Easing::InOutQuad},
                                              {0.4f, 0.45f, Easing::InOutQuad},
                                              {0.8f, 0.05f, Easing::Linear}});
    addTrack(c, r.armU, Property::Rotation, {{0.0f, d + 0.45f, Easing::InOutQuad},
                                             {0.4f, d - 0.45f, Easing::InOutQuad},
                                             {0.8f, d + 0.45f, Easing::Linear}});
    addTrack(c, r.pelvis, Property::TranslationY, {{0.0f, 0.0f, Easing::InOutQuad},
                                                   {0.2f, -5.0f, Easing::InOutQuad},
                                                   {0.4f, 0.0f, Easing::InOutQuad},
                                                   {0.6f, -5.0f, Easing::InOutQuad},
                                                   {0.8f, 0.0f, Easing::Linear}});
    addTrack(c, r.torso, Property::Rotation, {{0.0f, -kPi * 0.5f + 0.10f, Easing::Linear}});
    return c;
}

// Non bouclé + Once{"idle"} : le bras part au-dessus de la tête puis s'abat. C'est la plus GROSSE
// amplitude de la démo, donc celle où une coupe franche s'entend le plus fort.
Clip makeAttack(const Rig& r) {
    Clip c; c.duration = 0.55f;
    addTrack(c, r.armU, Property::Rotation, {{0.00f, -kPi * 0.75f, Easing::OutCubic},
                                             {0.25f, -kPi * 0.55f, Easing::InCubic},
                                             {0.55f,  kPi * 0.30f, Easing::Linear}});
    addTrack(c, r.armL, Property::Rotation, {{0.00f, -0.9f, Easing::OutCubic},
                                             {0.25f, -0.5f, Easing::InCubic},
                                             {0.55f,  0.25f, Easing::Linear}});
    addTrack(c, r.torso, Property::Rotation, {{0.00f, -kPi * 0.5f - 0.18f, Easing::OutCubic},
                                              {0.55f, -kPi * 0.5f + 0.22f, Easing::Linear}});
    addTrack(c, r.legLU, Property::Rotation, {{0.0f, kPi * 0.5f - 0.30f, Easing::Linear}});
    addTrack(c, r.legRU, Property::Rotation, {{0.0f, kPi * 0.5f + 0.30f, Easing::Linear}});
    return c;
}

// Deux poses tenues, utilisées pour le cas RÉ-ENTRANT (on bascule de l'une à l'autre en plein fondu).
Clip makeJump(const Rig& r) {
    Clip c; c.duration = 1.0f;
    addTrack(c, r.armU, Property::Rotation, {{0.0f, -kPi * 0.62f, Easing::Linear}});
    addTrack(c, r.armL, Property::Rotation, {{0.0f, -0.35f, Easing::Linear}});
    addTrack(c, r.legLU, Property::Rotation, {{0.0f, kPi * 0.5f - 0.75f, Easing::Linear}});
    addTrack(c, r.legRU, Property::Rotation, {{0.0f, kPi * 0.5f - 0.35f, Easing::Linear}});
    addTrack(c, r.legLL, Property::Rotation, {{0.0f, 0.95f, Easing::Linear}});
    addTrack(c, r.legRL, Property::Rotation, {{0.0f, 0.55f, Easing::Linear}});
    addTrack(c, r.torso, Property::Rotation, {{0.0f, -kPi * 0.5f - 0.15f, Easing::Linear}});
    return c;
}

Clip makeFall(const Rig& r) {
    Clip c; c.duration = 1.0f;
    addTrack(c, r.armU, Property::Rotation, {{0.0f, kPi * 0.92f, Easing::Linear}});
    addTrack(c, r.armL, Property::Rotation, {{0.0f, 0.55f, Easing::Linear}});
    addTrack(c, r.legLU, Property::Rotation, {{0.0f, kPi * 0.5f + 0.55f, Easing::Linear}});
    addTrack(c, r.legRU, Property::Rotation, {{0.0f, kPi * 0.5f + 0.15f, Easing::Linear}});
    addTrack(c, r.legLL, Property::Rotation, {{0.0f, 0.25f, Easing::Linear}});
    addTrack(c, r.legRL, Property::Rotation, {{0.0f, 0.15f, Easing::Linear}});
    addTrack(c, r.torso, Property::Rotation, {{0.0f, -kPi * 0.5f + 0.30f, Easing::Linear}});
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// Dessin d'un os : un quad étiré. Le sprite est ancré au CENTRE (convention cx,cy) et sa rotation
// pivote sur ce centre — on place donc le centre à la MOITIÉ de l'os, obtenu en projetant le point
// local (len/2, 0) par le transform MONDE du nœud. C'est le seul endroit de la démo qui touche à de
// la géométrie, et il tient en trois lignes parce que grove::anim a déjà tout composé.
// ---------------------------------------------------------------------------
static void drawBone(IIO* io, const Transform2D& world, float len, float thick,
                     uint32_t color, int layer) {
    if (len <= 0.0f) return;
    float cx, cy;
    transformPoint(world, len * 0.5f, 0.0f, cx, cy);

    auto s = std::make_unique<JsonDataNode>("s");
    s->setDouble("cx", cx);  s->setDouble("cy", cy);
    s->setDouble("scaleX", len);  s->setDouble("scaleY", thick);
    s->setDouble("rotation", world.rotation);
    s->setInt("textureId", 0);                 // quad plein teinté
    s->setInt("color", static_cast<int>(color));
    s->setInt("layer", layer);
    io->publish("render:sprite", std::move(s));
}

static void drawRig(IIO* io, const Rig& r, uint32_t color) {
    // La tête : un quad carré posé au bout du torse, sans rotation propre à animer.
    for (int id = 0; id < kNodeCount; ++id) drawBone(io, r.h.world(id), kBoneLen[id], 9.0f, color, 20);
    {
        const Transform2D& hw = r.h.world(r.head);
        auto s = std::make_unique<JsonDataNode>("s");
        s->setDouble("cx", hw.x); s->setDouble("cy", hw.y);
        s->setDouble("scaleX", 26.0); s->setDouble("scaleY", 26.0);
        s->setInt("textureId", 0);
        s->setInt("color", static_cast<int>(color));
        s->setInt("layer", 21);
        io->publish("render:sprite", std::move(s));
    }
}

static void drawText(IIO* io, const char* text, double x, double y, uint32_t color, double size) {
    auto t = std::make_unique<JsonDataNode>("t");
    t->setString("text", text);
    t->setDouble("x", x); t->setDouble("y", y);
    t->setInt("color", static_cast<int>(color));
    t->setDouble("fontSize", size);
    t->setInt("layer", 40);
    io->publish("render:text", std::move(t));
}

// Le scénario, une seule fois pour les DEUX pantins : c'est ce qui rend la comparaison honnête.
// Chaque entrée = (instant en secondes, état demandé). Aucun des deux animateurs ne sait ce que
// l'autre fait ; ils reçoivent exactement les mêmes play() aux mêmes instants.
struct Cue { float t; const char* state; };

static const Cue kScript[] = {
    { 0.00f, "idle"   },
    { 1.30f, "walk"   },
    { 3.10f, "attack" },   // Once -> revient à idle tout seul vers 3.65 s
    { 4.40f, "walk"   },
    { 5.60f, "jump"   },
    { 5.72f, "fall"   },   // ⚠️ 120 ms plus tard : bascule EN PLEIN FONDU (le cas ré-entrant)
    { 6.90f, "idle"   },
};
constexpr float kTotalSeconds = 8.2f;

int main(int argc, char** argv) {
    const std::string outDir = (argc > 1) ? argv[1] : "build/animator_frames";

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("animator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       W, H, SDL_WINDOW_HIDDEN);
    if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(win, &wmi)) { std::fprintf(stderr, "no wm info\n"); return 1; }

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("anim_r");
    auto gIO = mgr.createInstance("anim_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    { JsonDataNode c("config");
      c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
      c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
      c.setString("fontPath", "assets/fonts/roboto-bold.ttf"); c.setInt("fontSize", 22);
      renderer->setConfiguration(c, rIO.get(), nullptr); }

    // Deux rigs, deux animateurs. MÊMES clips, MÊME script — seul le fondu par défaut diffère.
    Rig left, right;
    left.build(200.0f, 300.0f);
    right.build(520.0f, 300.0f);

    const Clip idleL = makeIdle(left),   idleR = makeIdle(right);
    const Clip walkL = makeWalk(left),   walkR = makeWalk(right);
    const Clip atkL  = makeAttack(left), atkR  = makeAttack(right);
    const Clip jmpL  = makeJump(left),   jmpR  = makeJump(right);
    const Clip flL   = makeFall(left),   flR   = makeFall(right);

    Animator animL, animR;
    auto wire = [](Animator& a, const Clip& id, const Clip& wk, const Clip& at,
                   const Clip& jp, const Clip& fa) {
        a.addState("idle", &id);  a.addState("walk", &wk);
        a.addState("attack", &at, Once{"idle"});
        a.addState("jump", &jp);  a.addState("fall", &fa);
    };
    wire(animL, idleL, walkL, atkL, jmpL, flL);
    wire(animR, idleR, walkR, atkR, jmpR, flR);

    animL.setDefaultFade(0.0f);                        // AVANT : coupe franche
    animR.setDefaultFade(0.18f);                       // APRÈS : fondu croisé
    animR.setFadeEasing(Easing::InOutQuad);

    rhi::IRHIDevice* dev = renderer->getDevice();
    if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);
    // setCaptureTarget plutôt qu'un setViewFramebuffer(0, fb) à la main : c'est le module qui sait
    // quelle vue écrit l'image finale (elle change dès qu'un effet est actif). Cf. frame-capture.md.
    renderer->setCaptureTarget(fb);

    std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
    const int totalFrames = static_cast<int>(kTotalSeconds * kFps);
    size_t nextCue = 0;
    int written = 0;

    for (int f = 0; f < totalFrames; ++f) {
        const float now = static_cast<float>(f) * kDt;

        // Le script pilote les DEUX animateurs avec le même appel au même instant.
        while (nextCue < sizeof(kScript) / sizeof(kScript[0]) && kScript[nextCue].t <= now) {
            animL.play(kScript[nextCue].state);
            animR.play(kScript[nextCue].state);
            ++nextCue;
        }

        animL.update(kDt, left.h);   left.h.update();
        animR.update(kDt, right.h);  right.h.update();

        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x", 0); cam->setDouble("y", 0); cam->setDouble("zoom", 1.0);
          cam->setInt("viewportX", 0); cam->setInt("viewportY", 0);
          cam->setInt("viewportW", W); cam->setInt("viewportH", H);
          gIO->publish("render:camera", std::move(cam)); }

        { auto clr = std::make_unique<JsonDataNode>("c");
          clr->setInt("color", static_cast<int>(0x101822FF));
          gIO->publish("render:clear", std::move(clr)); }

        drawRig(gIO.get(), left,  0xE05A4EFFu);        // rouge : sans fondu
        drawRig(gIO.get(), right, 0x5AC8A8FFu);        // vert  : avec fondu

        drawText(gIO.get(), "COUPE FRANCHE  (fade 0)", 68, 40, 0xE05A4EFFu, 20);
        drawText(gIO.get(), "FONDU CROISE   (fade 0.18s)", 380, 40, 0x5AC8A8FFu, 20);
        drawText(gIO.get(), animL.current().c_str(), 68, 348, 0xFFFFFFFFu, 26);
        drawText(gIO.get(), animR.current().c_str(), 380, 348, 0xFFFFFFFFu, 26);
        if (animR.isFading()) drawText(gIO.get(), "> fondu en cours", 380, 376, 0x8FE8D0FFu, 16);

        JsonDataNode in("input"); in.setDouble("deltaTime", static_cast<double>(kDt));
        renderer->process(in);

        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
            std::fprintf(stderr, "readback failed at frame %d\n", f); return 3;
        }
        char path[512];
        std::snprintf(path, sizeof path, "%s/f%04d.png", outDir.c_str(), f);
        if (!grove::mvdemo::writeRgbaAsPng(path, W, H, rgba)) { std::fprintf(stderr, "png write failed: %s\n", path); return 4; }
        ++written;
    }

    // ⚠️ Relâcher la capture AVANT le teardown : une vue laissée sur un framebuffer détruit corrompt
    // le tas à la destruction du renderer (piège documenté dans frame-capture.md).
    renderer->setCaptureTarget(rhi::FramebufferHandle{});
    renderer->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();

    std::fprintf(stdout, "wrote %d frames to %s\n", written, outDir.c_str());
    return 0;
}
