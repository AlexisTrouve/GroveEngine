// ============================================================================
//  BANC DE LAMPES — what a lit scene actually costs
// ============================================================================
//
// WHY: the consumer docs used to promise "tens of lights per frame". That figure predated the
//   occlusion march entirely, so it described a different renderer; it was REMOVED rather than
//   adjusted, because a number nobody measured is worse than no number. This program is what
//   replaces it.
//
// WHAT it answers, in the order the answers matter:
//
//   1. Is a lamp's cost driven by their COUNT or by their SIZE? (Fill rate says size; a per-draw
//      model says count. Only one of them is true, and the advice differs completely.)
//   2. What does the OCCLUSION MARCH cost — and does it cost anything in a scene with NO matter?
//      Every current consumer (Drifterra, DAOS, Fractax) publishes no walls at all, so if the march
//      bills them anyway that is a bug-shaped finding, not a benchmark row.
//   3. What does adding actual matter cost on top?
//
// HOW it reports: not "N lamps at 60 fps" — that number does not transfer between lamp sizes or
//   resolutions, which is exactly how the old figure went stale. The currency is **lit megapixels
//   per frame**, because fill rate is what is actually being spent. A game can then divide by its
//   own lamp sizes and get an answer that stays true.
//
// HONESTY:
//   - spdlog is silenced: the renderer logs per frame at info level, which would skew the timing.
//   - Lamps are ephemeral, so each row also pays the real IIO cost of publishing them. That IS what
//     a game pays; it is not subtracted. The CPU/GPU split below says which one is spending.
//   - Coverage is computed as the lamps' quad area CLIPPED to the viewport, then divided by the
//     viewport — overlapping lamps therefore count twice, which is correct: overdraw is real work.
//   - bgfx is forced single-threaded by the static lib's config, so `cpuMs` is our submit cost, not
//     a render thread's.
//
// RUN: from build/  ->  ./tests/benchmark_lighting        (ESC bails early)
//   Not a ctest: it is a wall-clock measurement, windowed and machine/driver dependent — same
//   status as benchmark_render_savage.
// ============================================================================

#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_syswm.h>
#include <bgfx/bgfx.h>

#include "BgfxRendererModule.h"

#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

namespace {

constexpr int VIEW_W = 1280;
constexpr int VIEW_H = 720;
constexpr int WARM_FRAMES = 30;
constexpr int MEAS_FRAMES = 60;

struct Row {
    std::string label;
    int lamps = 0;
    double radius = 0.0;
    int matter = 0;
    double coverage = 0.0;   // lit viewport-fulls per frame (overdraw counted)
    double wallMs = 0.0;
    double gpuMs = 0.0;
    double cpuMs = 0.0;
};

// Area of a lamp's quad clipped to the viewport. A lamp half off-screen only pays for what it
// covers, and pretending otherwise would flatter every large-radius row.
double clippedArea(double cx, double cy, double r) {
    const double x0 = std::max(0.0, cx - r), x1 = std::min<double>(VIEW_W, cx + r);
    const double y0 = std::max(0.0, cy - r), y1 = std::min<double>(VIEW_H, cy + r);
    if (x1 <= x0 || y1 <= y0) return 0.0;
    return (x1 - x0) * (y1 - y0);
}

// ⚠️ DIAGNOSTIC (2026-08-02) — `--vsync` rend l'horloge MURALE dependante du GPU.
//
// Sans vsync, bgfx n'attend pas le GPU a frame() : le CPU empile des frames d'avance, donc `wall`
// mesure le DEBIT DE SOUMISSION, pas le cout d'une frame. C'est ce qui a produit une contradiction
// impossible dans la sortie du 02/08 -- `gpu ms` 31.80 dans une frame `wall ms` de 14.18.
//
// Avec vsync, frame() bloque jusqu'au present : `wall` devient la vraie periode d'affichage, donc
// un multiple de 16.6 ms. Le discriminant est grossier mais net -- 31 ms de GPU reel donne ~33 ms
// (deux periodes), 14 ms en donne ~16.6 (une seule).
//
// Le commentaire d'origine disait "vsync would clamp every row to 16.6 ms and measure nothing".
// C'est vrai pour MESURER un cout, faux pour VERIFIER un instrument : ici c'est le palier qu'on lit.
static bool g_vsync = false;

class Bench {
public:
    bool init(SDL_Window* win) {
        SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version);
        if (!SDL_GetWindowWMInfo(win, &wmi)) return false;

        m_rIO = IntraIOManager::getInstance().createInstance("bl_r");
        m_gIO = IntraIOManager::getInstance().createInstance("bl_g");
        m_renderer = std::make_unique<BgfxRendererModule>();

        JsonDataNode cfg("config");
        cfg.setDouble("nativeWindowHandle", double(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        cfg.setInt("windowWidth", VIEW_W);
        cfg.setInt("windowHeight", VIEW_H);
        cfg.setBool("vsync", g_vsync);   // cf. g_vsync : off pour mesurer, on pour verifier l'instrument
        m_renderer->setConfiguration(cfg, m_rIO.get(), nullptr);
        return m_renderer->getDevice() != nullptr;
    }

    void shutdown() {
        if (m_renderer) m_renderer->shutdown();
        IntraIOManager::getInstance().removeInstance("bl_r");
        IntraIOManager::getInstance().removeInstance("bl_g");
    }

    // The scene every regime shares: one full-screen sprite, so the composite has something to
    // multiply. Without it the lamps would light nothing and the fill rate would be a fiction.
    void sendScene() {
        { auto c = std::make_unique<JsonDataNode>("c");
          c->setInt("color", 0x101418FF);
          m_gIO->publish("render:clear", std::move(c)); }
        { auto s = std::make_unique<JsonDataNode>("s");
          s->setDouble("cx", VIEW_W * 0.5); s->setDouble("cy", VIEW_H * 0.5);
          s->setDouble("scaleX", VIEW_W);   s->setDouble("scaleY", VIEW_H);
          s->setInt("color", int(0xFFFFFFFFu)); s->setInt("layer", 1);
          m_gIO->publish("render:sprite", std::move(s)); }
        { auto cam = std::make_unique<JsonDataNode>("cam");
          cam->setDouble("x", 0); cam->setDouble("y", 0); cam->setDouble("zoom", 1.0);
          cam->setInt("viewportX", 0); cam->setInt("viewportY", 0);
          cam->setInt("viewportW", VIEW_W); cam->setInt("viewportH", VIEW_H);
          m_gIO->publish("render:camera", std::move(cam)); }
        { auto a = std::make_unique<JsonDataNode>("a");
          a->setInt("color", 0x181c28FF);
          m_gIO->publish("render:ambient", std::move(a)); }
    }

    // Scatter `n` lamps of `radius` over the viewport on a deterministic lattice, and return the
    // coverage they add. Deterministic so two runs are comparable.
    double sendLamps(int n, double radius) {
        double area = 0.0;
        for (int i = 0; i < n; ++i) {
            const double t = double(i);
            const double cx = std::fmod(t * 137.0, double(VIEW_W));
            const double cy = std::fmod(t * 89.0,  double(VIEW_H));
            auto l = std::make_unique<JsonDataNode>("l");
            l->setDouble("cx", cx); l->setDouble("cy", cy);
            l->setDouble("radius", radius);
            l->setInt("color", int(0xFFE0C0FFu));
            l->setDouble("intensity", 1.0);
            m_gIO->publish("render:light", std::move(l));
            area += clippedArea(cx, cy, radius);
        }
        return area / (double(VIEW_W) * double(VIEW_H));
    }

    // `n` opaque walls, ephemeral. Small on purpose: this measures the cost of the map being BOUND
    // and marched through, not of rasterising a lot of geometry.
    void sendMatter(int n) {
        for (int i = 0; i < n; ++i) {
            const double t = double(i);
            auto o = std::make_unique<JsonDataNode>("o");
            o->setDouble("x", std::fmod(t * 211.0, double(VIEW_W)));
            o->setDouble("y", std::fmod(t * 97.0,  double(VIEW_H)));
            o->setDouble("w", 24.0); o->setDouble("h", 24.0);
            m_gIO->publish("render:occluder", std::move(o));
        }
    }

    Row measure(const char* label, int lamps, double radius, int matter, bool& quit) {
        Row r; r.label = label; r.lamps = lamps; r.radius = radius; r.matter = matter;

        for (int f = 0; f < WARM_FRAMES && !quit; ++f) {
            pump(quit); sendScene(); sendLamps(lamps, radius); sendMatter(matter); step(1.0 / 60.0);
        }
        double wall = 0, gpu = 0, cpu = 0, cov = 0;
        int done = 0;
        for (int f = 0; f < MEAS_FRAMES && !quit; ++f) {
            pump(quit);
            sendScene();
            cov += sendLamps(lamps, radius);
            sendMatter(matter);
            wall += step(1.0 / 60.0);
            const bgfx::Stats* s = bgfx::getStats();
            if (s && s->gpuTimerFreq) gpu += double(s->gpuTimeEnd - s->gpuTimeBegin) * 1000.0 / double(s->gpuTimerFreq);
            if (s && s->cpuTimerFreq) cpu += double(s->cpuTimeEnd - s->cpuTimeBegin) * 1000.0 / double(s->cpuTimerFreq);
            ++done;
        }
        if (done) { r.wallMs = wall / done; r.gpuMs = gpu / done; r.cpuMs = cpu / done; r.coverage = cov / done; }
        return r;
    }

private:
    double step(double dt) {
        const Uint64 t0 = SDL_GetPerformanceCounter();
        JsonDataNode in("input"); in.setDouble("deltaTime", dt);
        m_renderer->process(in);
        const Uint64 t1 = SDL_GetPerformanceCounter();
        return double(t1 - t0) * 1000.0 / double(SDL_GetPerformanceFrequency());
    }
    void pump(bool& quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) quit = true;
        }
    }

    std::shared_ptr<IIO> m_rIO, m_gIO;
    std::unique_ptr<BgfxRendererModule> m_renderer;
};

void printHeader(const char* title, const char* question) {
    std::printf("\n%s\n%s\n", title, question);
    std::printf("  %-26s %6s %7s %8s %9s %8s %8s\n",
                "regime", "lamps", "radius", "cover x", "wall ms", "gpu ms", "cpu ms");
    std::fflush(stdout);
}

// ⚠️ Chaque ligne est imprimee ET VIDEE des qu'elle est mesuree.
// Le premier essai a forte charge est mort en cours de route et a emporte toutes les lignes deja
// mesurees avec lui : un banc qui n'ecrit qu'a la fin ne mesure rien le jour ou la charge fait
// exactement ce qu'on lui demande de faire. La ligne imprimee juste avant un crash EST le resultat.
void printRow(const Row& r) {
    std::printf("  %-26s %6d %7.0f %8.2f %9.2f %8.2f %8.2f\n",
                r.label.c_str(), r.lamps, r.radius, r.coverage, r.wallMs, r.gpuMs, r.cpuMs);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--vsync") g_vsync = true;
    // ⚠️ set_level ne touche que le logger PAR DEFAUT et ceux crees ensuite. IntraIOManager a le
    // sien, cree avant : sans apply_all il crache des dizaines de milliers de lignes pendant la
    // mesure, ce qui la fausse autant que ca la noie.
    spdlog::set_level(spdlog::level::off);
    spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) { l->set_level(spdlog::level::off); });

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::printf("no SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("banc de lampes", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       VIEW_W, VIEW_H, SDL_WINDOW_SHOWN);
    if (!win) { std::printf("no window\n"); SDL_Quit(); return 1; }

    Bench b;
    if (!b.init(win)) { std::printf("no GPU device\n"); SDL_DestroyWindow(win); SDL_Quit(); return 2; }

    bool quit = false;
    std::printf("=== BANC DE LAMPES -- %dx%d, vsync %s, %d frames mesurees par ligne ===\n",
                VIEW_W, VIEW_H, g_vsync ? "ON (verification d'instrument)" : "off", MEAS_FRAMES);
    std::fflush(stdout);

    printHeader("--- REFERENCE ---", "    Ce que coute le pipeline eclaire sans une seule lampe.");
    printRow(b.measure("ambiant seul", 0, 0.0, 0, quit));

    std::vector<Row> all;
    auto run = [&](const char* label, int n, double radius, int matter) {
        if (quit) return;
        Row r = b.measure(label, n, radius, matter, quit);
        printRow(r);
        all.push_back(r);
    };

    printHeader("--- A. LE NOMBRE, a petit rayon (60 px) ---",
                "    Si le cout suivait le NOMBRE, ces lignes monteraient lineairement.");
    for (int n : {16, 64, 256, 1024, 4096}) run("petites lampes", n, 60.0, 0);

    printHeader("--- B. LE NOMBRE, a grand rayon (300 px) ---",
                "    Memes nombres qu'en A, 25x la surface. C'est la comparaison qui tranche.");
    for (int n : {16, 64, 256, 1024}) run("grandes lampes", n, 300.0, 0);

    printHeader("--- B bis. RAYON ENORME (700 px) ---",
                "    Chaque lampe couvre a elle seule plus que le viewport entier.");
    for (int n : {4, 16, 64, 256}) run("lampes enormes", n, 700.0, 0);

    // LA question du regime C : une scene SANS AUCUNE matiere execute quand meme la marche (le
    // shader calcule son nombre de pas avant de pouvoir savoir s'il y a quelque chose a traverser).
    // Si la ligne 1 coute autant que les suivantes, la marche facture les jeux qui n'ont aucun mur
    // -- c'est-a-dire tous les consommateurs actuels.
    printHeader("--- C. LA MARCHE D'OCCULTATION ---",
                "    Ligne 1 = AUCUNE matiere publiee. Si elle coute autant que les suivantes,\n"
                "    la marche facture les jeux qui n'ont aucun mur.");
    for (int m : {0, 1, 64, 512}) {
        run(m == 0 ? "256 lampes, AUCUNE matiere" : "256 lampes + murs", 256, 300.0, m);
    }

    // Le régime C montre que la matière multiplie le coût par ~20. C'est donc ICI, et pas dans les
    // régimes A/B, que se lit le budget d'un jeu qui a des murs — c'est-à-dire le seul chiffre
    // qu'un consommateur puisse utiliser.
    printHeader("--- D. LE VRAI BUDGET : avec de la matiere ---",
                "    UN SEUL mur suffit a faire basculer le cout. On cherche donc le decrochage ici.");
    std::vector<Row> withMatter;
    for (int n : {8, 16, 32, 64, 128}) {
        if (quit) break;
        Row r = b.measure("lampes + 1 mur", n, 300.0, 1, quit);
        printRow(r);
        withMatter.push_back(r);
    }

    std::printf("\n--- CE QU'IL FAUT RETENIR ---\n");

    // Le PREMIER franchissement de chaque régime, pas tous : au-delà, la ligne se répète sans rien
    // apprendre. Un banc qui imprime quatre fois le même constat le dilue.
    auto firstKnee = [](const std::vector<Row>& rows, const char* what) {
        if (rows.empty()) return;
        for (const Row& r : rows) {
            if (r.gpuMs > 16.6) {
                std::printf("  %-24s : sous 60 fps des %5d lampes (%6.1f viewports, %6.2f ms GPU)\n",
                            what, r.lamps, r.coverage, r.gpuMs);
                return;
            }
        }
        const Row& last = rows.back();
        std::printf("  %-24s : rien jusqu'a %5d lampes (%6.1f viewports, %6.2f ms GPU -- marge x%.0f)\n",
                    what, last.lamps, last.coverage, last.gpuMs,
                    last.gpuMs > 0.01 ? 16.6 / last.gpuMs : 0.0);
    };

    firstKnee(withMatter, "AVEC matiere (r=300)");
    std::printf("  -- pour comparaison, sans aucune matiere : --\n");
    {
        std::vector<Row> a, bb, c;
        for (const Row& r : all) {
            if (r.matter == 0 && r.radius == 60.0)  a.push_back(r);
            if (r.matter == 0 && r.radius == 300.0) bb.push_back(r);
            if (r.matter == 0 && r.radius == 700.0) c.push_back(r);
        }
        firstKnee(a,  "sans matiere (r=60)");
        firstKnee(bb, "sans matiere (r=300)");
        firstKnee(c,  "sans matiere (r=700)");
    }
    std::printf("  (couverture = surfaces de lampes cumulees / surface du viewport ; le recouvrement compte double)\n");

    b.shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
