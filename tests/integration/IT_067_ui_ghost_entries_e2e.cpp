/**
 * Integration Test IT_067: cacher un widget ne doit RIEN laisser à l'écran.
 *
 * QUOI     : pour chaque type de widget, on compte les `render:*:add` qu'il publie en apparaissant,
 *            puis les `render:*:remove` qu'il publie en disparaissant. Les deux doivent être ÉGAUX.
 *
 * POURQUOI : le rendu de l'UI est RETENU — une entrée publiée reste affichée jusqu'à son `:remove`.
 *            Un widget qui se cache sans libérer TOUTES ses entrées laisse donc des rectangles et du
 *            texte fantômes par-dessus le jeu. `UIWidget::releaseRenderEntries()` libère l'entrée
 *            principale et récurse dans les enfants ; tout widget qui en possède d'AUTRES (texte,
 *            bordure, curseur, remplissage, chrome 9-slice…) doit surcharger pour les libérer aussi.
 *            Neuf widgets le faisaient, cinq l'avaient oublié — et rien ne le disait.
 *
 *            Ce test ne verrouille pas ces cinq widgets : il verrouille **la classe de bug**. Un
 *            futur widget multi-entrées qui oublie sa surcharge tombera rouge ici sans que personne
 *            ait à y penser.
 *
 * COMMENT  : le piège est l'ATTRIBUTION — les `:add` ne portent pas le nom du widget qui les publie.
 *            On le résout par l'ordre : chaque widget de la fixture démarre INVISIBLE, donc ne rend
 *            rien. On le révèle seul → les seuls `:add` de la frame sont les siens. On le recache →
 *            les seuls `:remove` sont les siens. Aucune attribution à deviner.
 *
 *            ⚠️ On ne peut PAS mesurer en cachant puis re-montrant : un widget qui fuit garde ses ids
 *            et republierait des `:update`, pas des `:add` — le compte serait équilibré et le test
 *            passerait au vert en pleine fuite.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

TEST_CASE("IT_067: cacher un widget libere TOUTES ses entrees retained",
          "[integration][ui][e2e][retained]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("ghost_input");
    auto uiIO     = mgr.createInstance("ghost_ui");
    auto observer = mgr.createInstance("ghost_obs");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "ghost_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_ghost_entries.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    // Les trois familles d'entrées retenues que l'UI publie (cf. UIRenderer::unregisterEntry).
    int adds = 0, removes = 0;
    for (const char* t : {"render:sprite:add", "render:text:add", "render:nineslice:add"}) {
        observer->subscribe(t, [&](const Message&) { adds++; });
    }
    for (const char* t : {"render:sprite:remove", "render:text:remove", "render:nineslice:remove"}) {
        observer->subscribe(t, [&](const Message&) { removes++; });
    }

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto setVisible = [&](const std::string& id, bool vis) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setString("id", id); d->setBool("visible", vis);
        inputPub->publish("ui:set_visible", std::move(d));
    };

    // Frames à vide : la racine et tout ce qui est visible s'enregistre une bonne fois, pour que les
    // compteurs qui suivent ne voient plus que le widget révélé.
    for (int i = 0; i < 3; ++i) pump();

    struct Case { const char* id; const char* why; };
    const std::vector<Case> cases = {
        // Les cinq sans surcharge au moment où ce test a été écrit.
        {"w_textinput",   "cadre, bordure, texte, placeholder, curseur, selection"},
        {"w_progressbar", "cadre, cadre de remplissage, remplissage, texte"},
        {"w_checkbox",    "cadre, coche, libelle"},
        {"w_slider",      "remplissage, poignee"},
        {"w_drawer",      "cadre 9-slice paresseux (+ son bouton enfant)"},
        // Témoins : surcharges déjà correctes. Rouge ici = le test est faux, pas le widget.
        {"ok_button",     "TEMOIN -- surcharge correcte"},
        {"ok_panel",      "TEMOIN -- surcharge correcte"},
        {"ok_window",     "TEMOIN -- surcharge correcte"},
    };

    // DEUX cycles par widget, et c'est essentiel : le second vérifie que la libération a bien REMIS
    // les compteurs à zéro. Une entrée 9-slice est enregistrée PARESSEUSEMENT, sous la garde d'un
    // drapeau `m_frameRegistered` ; libérer son id sans rabaisser le drapeau laisse le widget croire
    // que son cadre existe encore, et le chrome ne revient JAMAIS après un cycle cacher/montrer. Un
    // seul cycle ne verrait rien : c'est le 2e `publiees` qui manque à l'appel. On échangerait un
    // fantôme contre une disparition silencieuse — un défaut pire que celui qu'on corrige.
    for (const Case& c : cases) {
        int publishedPerCycle[2] = {0, 0};

        for (int cycle = 0; cycle < 2; ++cycle) {
            // Révéler SEUL -> les :add comptés lui appartiennent tous.
            adds = 0;
            setVisible(c.id, true);
            pump();
            pump();                   // 2e frame : les entrées paresseuses (chrome) arrivent aussi
            const int published = adds;
            publishedPerCycle[cycle] = published;

            // Recacher -> les :remove comptés lui appartiennent tous.
            removes = 0;
            setVisible(c.id, false);
            pump();
            const int released = removes;

            INFO("widget=" << c.id << " (" << c.why << ")  cycle=" << cycle
                 << "  publiees=" << published << "  liberees=" << released);
            // Un widget qui ne publie rien invaliderait la mesure (fixture cassée), pas le widget.
            CHECK(published > 0);
            // Le coeur : autant d'entrees liberees que publiees. L'ecart EST le nombre de fantomes.
            CHECK(released == published);
        }

        INFO("widget=" << c.id << "  cycle0=" << publishedPerCycle[0]
             << "  cycle1=" << publishedPerCycle[1]);
        // Se re-montrer doit redonner EXACTEMENT le meme widget. Moins d'entrees au 2e tour = une
        // partie de l'habillage n'est jamais revenue.
        CHECK(publishedPerCycle[1] == publishedPerCycle[0]);
    }

    uiModule->shutdown();
}
