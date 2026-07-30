/**
 * Integration Test IT_068: une poussée `ui:data` SANS CHANGEMENT ne doit rien reconstruire.
 *
 * QUOI     : on pousse deux fois de suite exactement les mêmes données sur un layout à répéteur, et
 *            on compte le trafic retenu (`render:*:add` / `:remove`) que la seconde produit.
 *
 * POURQUOI : `expandRepeaters()` n'avait aucune garde. À chaque poussée il purgeait toutes les
 *            instances, les détruisait, puis re-parsait le gabarit JSON **par élément** et
 *            reconstruisait chaque widget — même quand rien n'avait bougé. Mesuré avant correctif sur
 *            30 lignes : **180 `:remove` + 180 `:add` et 15,8 ms** pour une poussée identique, sur un
 *            budget de 16,6 ms à 60 fps. Un HUD qui rafraîchit ses données chaque frame — l'usage
 *            normal d'un HUD — ne tenait donc pas la frame à lui seul. Linéaire en N (5 lignes →
 *            2,5 ms), donc c'était bien la reconstruction et pas un coût fixe.
 *
 * COMMENT  : trois cas, et le troisième est celui qui compte vraiment.
 *            1. la PREMIÈRE poussée construit (trafic > 0) — sinon la mesure ne mesure rien ;
 *            2. la SECONDE, identique, ne produit RIEN ;
 *            3. une poussée DIFFÉRENTE reconstruit et le contenu suit.
 *
 *            Sans le cas 3, une garde dégénérée en « ne jamais rien reconstruire » passerait au vert
 *            tout en cassant la fonctionnalité. C'est le piège naturel de ce genre d'optimisation :
 *            le test facile récompense exactement la mauvaise implémentation.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace grove;
using json = nlohmann::json;

TEST_CASE("IT_068: une poussee ui:data identique ne reconstruit pas le repeteur",
          "[integration][ui][e2e][repeater][perf]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("idle_host");
    auto uiIO     = mgr.createInstance("idle_ui");
    auto observer = mgr.createInstance("idle_obs");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "idle_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_repeater.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int adds = 0, removes = 0;
    std::vector<std::string> texts;
    for (const char* t : {"render:sprite:add", "render:text:add", "render:nineslice:add"}) {
        observer->subscribe(t, [&](const Message&) { adds++; });
    }
    for (const char* t : {"render:sprite:remove", "render:text:remove", "render:nineslice:remove"}) {
        observer->subscribe(t, [&](const Message&) { removes++; });
    }
    observer->subscribe("render:text:add",    [&](const Message& m) { texts.push_back(m.data->getString("text", "")); });
    observer->subscribe("render:text:update", [&](const Message& m) { texts.push_back(m.data->getString("text", "")); });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto pushData = [&](json j) {
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", std::move(j)));
    };
    auto saw = [&](const std::string& t) {
        return std::find(texts.begin(), texts.end(), t) != texts.end();
    };

    // Une flotte de taille realiste pour un HUD.
    const json fleet = json{ {"fleet", json::array({
        {{"id","s1"},{"name","Aurora"}},
        {{"id","s2"},{"name","Borealis"}},
        {{"id","s3"},{"name","Cassiopee"}},
        {{"id","s4"},{"name","Deneb"}},
    })} };

    pump();   // settle

    // ---- 1. Premiere poussee : le repeteur se construit. ----
    adds = removes = 0;
    pushData(fleet);
    pump(); pump();
    const int firstAdds = adds;

    INFO("1ere poussee: adds=" << firstAdds);
    REQUIRE(firstAdds > 0);        // sinon les cas suivants ne prouvent rien
    REQUIRE(saw("Aurora"));
    REQUIRE(saw("Deneb"));

    // ---- 2. Seconde poussee, DONNEES IDENTIQUES : rien ne doit bouger. ----
    adds = removes = 0;
    pushData(fleet);              // meme json, au bit pres
    pump(); pump();

    INFO("2e poussee IDENTIQUE: adds=" << adds << " removes=" << removes
         << " (1ere poussee en avait publie " << firstAdds << ")");
    CHECK(removes == 0);          // aucune instance detruite
    CHECK(adds == 0);             // aucune instance reconstruite

    // ---- 3. Poussee DIFFERENTE : la reconstruction doit bien se produire. ----
    // LE cas qui empeche la garde de degenerer. Une implementation qui ne reconstruit plus JAMAIS
    // passerait les cas 1 et 2 et casserait le repeteur ; ici elle tombe.
    texts.clear();
    adds = removes = 0;
    pushData(json{ {"fleet", json::array({
        {{"id","s9"},{"name","Zephyr"}},
        {{"id","s8"},{"name","Yggdrasil"}},
    })} });
    pump(); pump();

    INFO("3e poussee MODIFIEE: adds=" << adds << " removes=" << removes);
    CHECK(adds > 0);              // le repeteur a bien ete refait
    CHECK(saw("Zephyr"));         // ...avec les NOUVELLES donnees
    CHECK(saw("Yggdrasil"));
    CHECK_FALSE(saw("Aurora"));   // ...et plus les anciennes

    uiModule->shutdown();
}
