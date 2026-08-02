/**
 * Integration Test IT_068: `UIModule::updateUI` publie ses phases DANS L'ORDRE.
 *
 * QUOI  : sur une seule frame, on enregistre la SÉQUENCE des topics `ui:*` publiés et on asserte
 *         leur ordre relatif, pas seulement leur présence.
 *
 * POURQUOI : `updateUI` est un pipeline de onze phases dont plusieurs portent une contrainte d'ordre
 *         **documentée dans le code et vérifiée par rien**. Exemples tirés de ses propres
 *         commentaires : l'interaction fenêtre doit venir APRÈS le dispatch clic (pour qu'un clic sur
 *         un bouton de contenu soit délivré avant qu'on cache la fenêtre) mais AVANT la passe
 *         d'update enfants (un raise réordonne `root->children`, on ne mute pas le vecteur en cours
 *         d'itération) ; l'émission live du slider doit venir APRÈS `m_root->update()`, parce que la
 *         valeur change DANS l'update et non aux fronts press/release.
 *
 *         Aucun test ne rougissait si on intervertissait deux phases. C'est le filet qu'il fallait
 *         poser AVANT de refactoriser cette fonction — sans lui, tout découpage est à l'aveugle.
 *
 * COMMENT : un observateur unique s'abonne à plusieurs topics et pousse le nom du topic dans un
 *         vecteur. On asserte des ordres RELATIFS (indexOf(a) < indexOf(b)), jamais une séquence
 *         exacte : le but est de verrouiller les contraintes réelles, pas de figer chaque message
 *         accessoire et de rendre le test cassant pour rien.
 *
 * ⚠️ CE QUE CE TEST NE COUVRE PAS : la contrainte « interaction fenêtre avant la passe d'update »
 *    n'est pas observable par l'ordre des messages — c'est une contrainte de sûreté mémoire (ne pas
 *    muter un vecteur pendant son itération), pas un effet visible sur le bus. Elle reste non
 *    verrouillée, et c'est dit plutôt que simulé par une assertion qui n'y toucherait pas.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace grove;

namespace {
std::string uiModulePath() {
#ifdef _WIN32
    return "../modules/libUIModule.dll";
#else
    return "../modules/libUIModule.so";
#endif
}

// Position du premier topic dans la séquence observée, ou -1 s'il n'y est pas.
int indexOf(const std::vector<std::string>& seq, const std::string& topic) {
    auto it = std::find(seq.begin(), seq.end(), topic);
    return (it == seq.end()) ? -1 : static_cast<int>(std::distance(seq.begin(), it));
}
} // namespace

TEST_CASE("IT_068: une frame publie ses phases dans l'ordre documenté", "[integration][ui][e2e][order]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("po_input");
    auto uiIO     = mgr.createInstance("po_ui");
    auto observer = mgr.createInstance("po_observer");

    ModuleLoader uiLoader;
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiModulePath(), "po_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_slider.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    // Un seul enregistreur pour tous les topics : c'est ce qui rend la SÉQUENCE observable. Des
    // abonnements séparés ne diraient que « chacun est arrivé », ce que les tests existants disent
    // déjà — et c'est précisément ce qui a laissé l'ordre non vérifié.
    std::vector<std::string> seq;
    double lastValue = -1.0;
    auto record = [&](const Message& m) { seq.push_back(m.topic); };
    observer->subscribe("ui:hover",         record);
    observer->subscribe("ui:capture",       record);
    observer->subscribe("ui:click",         record);
    observer->subscribe("ui:value_changed", [&](const Message& m) {
        seq.push_back(m.topic);
        lastValue = m.data->getDouble("value", -1.0);
    });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("x", x); d->setDouble("y", y);
        inputPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0);
        d->setBool("pressed", pressed);
        inputPub->publish("input:mouse:button", std::move(d));
    };

    // UNE seule frame qui traverse quatre phases : survol (1), capture (2), dispatch clic (4),
    // émission live du slider (10). Le clic tombe au milieu de la piste.
    sendMove(250.0, 115.0);
    sendButton(true);
    pump();

    INFO("séquence observée : " << [&]{ std::string s; for (auto& t : seq) { s += t; s += " "; } return s; }());

    const int iHover  = indexOf(seq, "ui:hover");
    const int iCapture= indexOf(seq, "ui:capture");
    const int iClick  = indexOf(seq, "ui:click");
    const int iValue  = indexOf(seq, "ui:value_changed");

    // Les quatre doivent être là : si l'un manque, ce n'est plus un test d'ordre, c'est un test
    // cassé qui passerait en silence (une comparaison avec -1 est toujours « inférieure »).
    REQUIRE(iHover   >= 0);
    REQUIRE(iCapture >= 0);
    REQUIRE(iClick   >= 0);
    REQUIRE(iValue   >= 0);

    // Phase 1 avant phase 2 : l'état de capture se calcule à partir du widget survolé.
    REQUIRE(iHover < iCapture);

    // ⚠️ L'ASSERTION QUI PORTE LE CHANTIER. Phase 4 (dispatch) avant phase 10 (émission live).
    // Le dispatch pose `m_draggingSliderId` ; le bloc post-update émet la valeur initiale. Déplacer
    // l'émission avant la passe d'update — ou avant le dispatch — inverse cet ordre, ou pire ne
    // publie plus rien du tout ce tour-ci, parce que l'id de drag n'est pas encore posé.
    REQUIRE(iClick < iValue);

    // ⚠️ ET LA FRAÎCHEUR, qui est l'AUTRE moitié de la contrainte — l'ordre seul ne la couvre pas.
    // Déplacer l'émission entre le dispatch et `m_root->update()` laisserait `ui:click` avant
    // `ui:value_changed` : les assertions ci-dessus resteraient vertes. Ce qui change, c'est que la
    // valeur publiée serait celle d'AVANT la passe d'update, donc en retard d'une frame.
    //
    // On fait donc glisser la souris à 75 % de la piste (x=100..400) sans relâcher, et on exige que
    // la valeur émise CETTE frame reflète la nouvelle position, pas l'ancienne. C'est exactement la
    // raison écrite dans le code : « la valeur est modifiée dans UISlider::update() (drag-move),
    // hors de la branche dispatch souris ».
    seq.clear();
    sendMove(325.0, 115.0);
    pump();

    REQUIRE(indexOf(seq, "ui:value_changed") >= 0);          // un drag DOIT émettre en continu
    INFO("valeur publiee au drag : " << lastValue);
    REQUIRE(lastValue > 70.0);                                // ~75 : la position de CETTE frame
    REQUIRE(lastValue < 80.0);                                // et non ~50, celle de la precedente
}
