/**
 * Integration Test IT_064: PRESSE-PAPIERS d'un UITextInput (E2E, vrai module).
 *
 * QUOI     : Ctrl+C / Ctrl+X / Ctrl+V sur un champ de saisie, sélection comprise.
 *
 * POURQUOI : l'en-tête du widget annonçait « Copy/paste (future) » et rien n'existait — pas même le
 *            chemin : `grep -ri clipboard` sur tout le dépôt ne donnait AUCUNE occurrence.
 *
 * L'ARCHITECTURE QUE CE TEST VERROUILLE : l'UIModule est délibérément SDL-free, or le presse-papiers
 *            EST une ressource SDL. Le module ne peut donc pas le lire lui-même. Il passe par IIO :
 *
 *              copier/couper : UI --- input:clipboard:set {text} ---> InputModule (SDL_SetClipboardText)
 *              coller        : UI --- input:clipboard:get -------->  InputModule
 *                             UI <-- input:clipboard:text {text} --- InputModule (SDL_GetClipboardText)
 *
 *            Le collage a donc UNE FRAME DE LATENCE (requête puis réponse). C'est le prix du
 *            découplage SDL ; invisible à l'œil humain, et documenté plutôt que contourné par un
 *            raccourci qui ferait rentrer SDL dans l'UIModule.
 *
 * COMMENT  : le test joue le rôle du service presse-papiers — il écoute `input:clipboard:get` et
 *            répond `input:clipboard:text`, exactement comme le fera InputModule. Cela prouve le
 *            protocole de bout en bout sans dépendre d'un presse-papiers système, donc sans exiger
 *            de fenêtre ni de session graphique.
 */

#include <catch2/catch_test_macros.hpp>

#include "helpers/UITextInputHarness.h"

#include <string>

using namespace grove;
using namespace grove::uitest;

namespace {

// Un presse-papiers factice branché sur les mêmes topics que le vrai service.
struct FakeClipboard {
    std::string content;
    int setCalls = 0;
    int getCalls = 0;

    void attach(TextInputHarness& h) {
        // Côté "InputModule" : on mémorise ce qu'on nous demande de copier...
        h.observer->subscribe("input:clipboard:set", [this](const Message& m) {
            content = m.data->getString("text", "");
            ++setCalls;
        });
        // ...et on répond aux demandes de collage.
        h.observer->subscribe("input:clipboard:get", [this, &h](const Message&) {
            ++getCalls;
            auto d = std::make_unique<JsonDataNode>("d");
            d->setString("text", content);
            h.inputPub->publish("input:clipboard:text", std::move(d));
        });
    }
};

}  // namespace

// ============================================================================
// Copier
// ============================================================================

TEST_CASE("IT_064: Ctrl+C publie la SELECTION vers le presse-papiers",
          "[integration][ui][e2e][clipboard]") {
    TextInputHarness h("clip_copy");
    FakeClipboard clip;
    clip.attach(h);

    h.focusField();
    h.type("abcdef");
    h.pressKey(kScanHome);
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);  // "abc" selectionne
    h.pressKey(kScanC, /*shift=*/false, /*ctrl=*/true);
    h.pump();

    REQUIRE(clip.setCalls == 1);
    REQUIRE(clip.content == "abc");
    REQUIRE(h.lastText == "abcdef");  // copier ne modifie PAS le champ
}

TEST_CASE("IT_064: Ctrl+C sans selection ne copie RIEN", "[integration][ui][e2e][clipboard]") {
    // Discrimination : sans ce garde, un Ctrl+C par reflexe ecraserait le presse-papiers avec du
    // vide, detruisant ce que l'utilisateur y avait mis depuis une autre application.
    TextInputHarness h("clip_copy_empty");
    FakeClipboard clip;
    clip.content = "contenu precieux";
    clip.attach(h);

    h.focusField();
    h.type("abcdef");                                       // curseur en fin, aucune selection
    h.pressKey(kScanC, /*shift=*/false, /*ctrl=*/true);
    h.pump();

    REQUIRE(clip.setCalls == 0);
    REQUIRE(clip.content == "contenu precieux");
}

// ============================================================================
// Couper
// ============================================================================

TEST_CASE("IT_064: Ctrl+X copie ET supprime la selection", "[integration][ui][e2e][clipboard]") {
    TextInputHarness h("clip_cut");
    FakeClipboard clip;
    clip.attach(h);

    h.focusField();
    h.type("abcdef");
    h.pressKey(kScanHome);
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);  // "abc"
    h.pressKey(kScanX, /*shift=*/false, /*ctrl=*/true);
    h.pump();

    REQUIRE(clip.content == "abc");
    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "def");
}

// ============================================================================
// Coller
// ============================================================================

TEST_CASE("IT_064: Ctrl+V insere le presse-papiers au curseur", "[integration][ui][e2e][clipboard]") {
    TextInputHarness h("clip_paste");
    FakeClipboard clip;
    clip.content = "XY";
    clip.attach(h);

    h.focusField();
    h.type("abcd");
    h.pressKey(kScanHome);
    h.pressKey(kScanRight);  // curseur apres 'a'
    h.pressKey(kScanV, /*shift=*/false, /*ctrl=*/true);
    h.pump();   // la reponse du presse-papiers arrive a la frame suivante (latence assumee)
    h.pump();

    REQUIRE(clip.getCalls == 1);
    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "aXYbcd");
}

TEST_CASE("IT_064: Ctrl+V REMPLACE la selection courante", "[integration][ui][e2e][clipboard]") {
    TextInputHarness h("clip_paste_replace");
    FakeClipboard clip;
    clip.content = "ZZZ";
    clip.attach(h);

    h.focusField();
    h.type("abcdef");
    h.pressKey(kScanHome);
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);  // "abc" selectionne
    h.pressKey(kScanV, /*shift=*/false, /*ctrl=*/true);
    h.pump();
    h.pump();

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "ZZZdef");
}

TEST_CASE("IT_064: coller du texte ACCENTUE preserve les caracteres",
          "[integration][ui][e2e][clipboard][utf8]") {
    // Un collage passe par le meme chemin d'insertion que la frappe, donc il doit traiter l'UTF-8
    // multi-octets comme un tout -- et le curseur doit atterrir APRES, sur une frontiere valide.
    TextInputHarness h("clip_paste_utf8");
    FakeClipboard clip;
    clip.content = "caf" + kEAigu;
    clip.attach(h);

    h.focusField();
    h.pressKey(kScanV, /*shift=*/false, /*ctrl=*/true);
    h.pump();
    h.pump();

    INFO("apres collage : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "caf" + kEAigu);

    // Le curseur doit etre en fin, sur une frontiere : un Backspace enleve le 'é' ENTIER.
    h.pressKey(kScanBackspace);
    INFO("apres backspace : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "caf");
}

TEST_CASE("IT_064: coller un presse-papiers VIDE ne change rien",
          "[integration][ui][e2e][clipboard]") {
    TextInputHarness h("clip_paste_empty");
    FakeClipboard clip;   // content vide
    clip.attach(h);

    h.focusField();
    h.type("abc");
    h.pressKey(kScanV, /*shift=*/false, /*ctrl=*/true);
    h.pump();
    h.pump();

    REQUIRE(h.lastText == "abc");
}

// ============================================================================
// Le raccourci ne doit pas fuir dans le texte
// ============================================================================

TEST_CASE("IT_064: un raccourci Ctrl+lettre n'insere pas la lettre",
          "[integration][ui][e2e][clipboard]") {
    // Garde-fou : les raccourcis passent par le meme chemin que les caracteres imprimables. Sans le
    // garde `if (ctrl) return false`, un Ctrl+C ajouterait un 'c' au champ.
    TextInputHarness h("clip_no_leak");
    FakeClipboard clip;
    clip.attach(h);

    h.focusField();
    h.type("ab");
    h.pressKey(kScanC, /*shift=*/false, /*ctrl=*/true);
    h.pressKey(kScanX, /*shift=*/false, /*ctrl=*/true);
    h.pump();

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "ab");
}
