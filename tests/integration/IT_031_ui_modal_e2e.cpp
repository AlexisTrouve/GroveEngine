/**
 * Integration Test IT_031: modal dialog — focus-trap + open/close (UI framework slice 5a).
 *
 * A closed modal over a big background button. We assert:
 *   1. closed -> the background button is clickable,
 *   2. opened -> the dialog's button is clickable,
 *   3. a click on the DIM (outside the dialog) is trapped (background does NOT fire) AND closes the
 *      modal (ui:modal:closed),
 *   4. closed again -> the background button is clickable once more.
 *
 * dialog 300x200 centered in 800x600 -> [250,550]x[200,400]; ok_btn at content (20,20) -> abs
 * [270,370]x[220,250] (center 320,235). Point (100,100) is on the background and OUTSIDE the dialog.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_031: a modal traps input and closes on an outside click (UI slice 5a)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("md_host");
    auto uiIO     = mgr.createInstance("md_ui");
    auto observer = mgr.createInstance("md_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "md_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_modal.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastAction;
    int modalClosed = 0;
    observer->subscribe("ui:action", [&](const Message& m) { lastAction = m.data->getString("action", ""); });
    observer->subscribe("ui:modal:closed", [&](const Message&) { modalClosed++; });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d));
    };
    auto click = [&](double x, double y) { sendMove(x, y); pump(); sendButton(true); pump(); sendButton(false); pump(); };
    auto openModal = [&] {
        auto d = std::make_unique<JsonDataNode>("d"); d->setString("id", "modal");
        hostPub->publish("ui:modal:open", std::move(d));
        pump();
    };

    pump();  // settle

    // 1. Closed: the background is clickable.
    lastAction.clear();
    click(100, 100);
    INFO("closed action='" << lastAction << "'");
    REQUIRE(lastAction == "background");

    // 2. Open: the dialog's button is clickable.
    openModal();
    lastAction.clear();
    click(320, 235);
    INFO("dialog action='" << lastAction << "'");
    REQUIRE(lastAction == "dialogok");

    // 3. Click the dim (outside the dialog): trapped (no background) AND the modal closes.
    lastAction.clear();
    click(100, 100);
    INFO("trap action='" << lastAction << "' modalClosed=" << modalClosed);
    REQUIRE(lastAction.empty());
    REQUIRE(modalClosed == 1);

    // 4. Closed again: the background is clickable.
    lastAction.clear();
    click(100, 100);
    INFO("reopened-bg action='" << lastAction << "'");
    REQUIRE(lastAction == "background");

    uiModule->shutdown();
}

// ============================================================================
// Étape 2 du chantier updateUI (2026-08-01) — renforcer AVANT de déplacer.
//
// La branche `modal` d'updateUI porte DEUX gardes : `mousePressed` (on ferme au front d'appui) et
// `!pointInDialog` (un clic DANS le dialogue ne ferme pas). Le second est le plus important de tout
// le lot : un modal qui se ferme quand on clique son propre contenu est inutilisable.
//
// ⚠️ Il n'était vérifié qu'INDIRECTEMENT. Le cas nominal clique le bouton du dialogue et asserte
// l'action, sans jamais dire que le modal est RESTÉ ouvert ; il n'échouait que par ricochet, à
// l'étape suivante, sur un compteur qui aurait alors valu 2 au lieu de 1. Assez pour rougir
// aujourd'hui, pas assez pour exprimer l'intention — et un assouplissement anodin de ce compteur
// (`>= 1`) aurait fait disparaître la protection sans que personne le remarque.
// ============================================================================

TEST_CASE("IT_031b: cliquer DANS le dialogue ne ferme pas le modal", "[integration][ui][e2e][modal]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("mdg_host");
    auto uiIO     = mgr.createInstance("mdg_ui");
    auto observer = mgr.createInstance("mdg_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "mdg_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_modal.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastAction;
    int modalClosed = 0;
    observer->subscribe("ui:action", [&](const Message& m) { lastAction = m.data->getString("action", ""); });
    observer->subscribe("ui:modal:closed", [&](const Message&) { modalClosed++; });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d));
    };
    auto click = [&](double x, double y) { sendMove(x, y); pump(); sendButton(true); pump(); sendButton(false); pump(); };
    auto openModal = [&] {
        auto d = std::make_unique<JsonDataNode>("d"); d->setString("id", "modal");
        hostPub->publish("ui:modal:open", std::move(d));
        pump();
    };

    pump();     // settle
    openModal();

    // GARDE — un clic sur le bouton du dialogue agit ET laisse le modal OUVERT. L'assertion sur
    // modalClosed est celle qui manquait : elle dit l'intention au lieu de la déduire d'un compteur.
    lastAction.clear();
    click(320, 235);                 // le bouton OK, à l'intérieur du dialogue (300x200 centré)
    INFO("clic dans le dialogue : action='" << lastAction << "' fermetures=" << modalClosed);
    REQUIRE(lastAction == "dialogok");
    REQUIRE(modalClosed == 0);

    // Un clic dans le dialogue mais HORS de tout bouton ne ferme pas davantage : c'est la surface
    // vide du dialogue, pas le fond. Sans `pointInDialog`, c'est ce clic-là qui fermerait, et il est
    // banal — on clique le vide d'une boîte de dialogue tout le temps.
    lastAction.clear();
    click(400, 320);                 // dans le dialogue, sous le bouton
    INFO("clic sur le vide du dialogue : fermetures=" << modalClosed);
    REQUIRE(modalClosed == 0);

    // Et le fond ferme toujours — sinon on aurait verrouillé un modal qu'on ne peut plus fermer.
    click(100, 100);
    REQUIRE(modalClosed == 1);

    uiModule->shutdown();
}
