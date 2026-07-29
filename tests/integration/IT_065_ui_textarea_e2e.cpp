/**
 * Integration Test IT_065: zone de texte MULTILIGNE (UITextArea) — E2E, vrai module.
 *
 * QUOI     : Entrée qui insère un saut de ligne, navigation haut/bas, Début/Fin sur la LIGNE,
 *            sélection multi-lignes, clic qui vise la bonne ligne, et Ctrl+Entrée qui soumet.
 *
 * POURQUOI : le multiligne n'existait pas. Il a été construit comme un widget SÉPARÉ au-dessus du
 *            modèle partagé `grove::text::EditModel` plutôt qu'en greffant un `multiline: true` sur
 *            le champ monoligne — greffer un booléen aurait ramifié chaque méthode (rendu, clic,
 *            navigation, défilement) et rendu le widget illisible.
 *
 * CE QUE CE TEST VERROUILLE EN PRIORITÉ : les points où la sémantique DIFFÈRE du monoligne, parce
 *            que c'est là qu'une régression passerait inaperçue —
 *              - Entrée insère au lieu de soumettre (et Ctrl+Entrée reprend la soumission) ;
 *              - Début/Fin agissent sur la ligne et non sur tout le texte ;
 *              - Haut/Bas conservent la colonne.
 *            Tout le reste (UTF-8, sélection, presse-papiers) est déjà prouvé sur le modèle partagé
 *            par TextEditUnit — le re-tester ici ne prouverait rien de neuf.
 *
 * COMMENT  : la fixture place la zone en (100,100) 300x120, fontSize 16 / lineHeight 20. Sans
 *            renderer la police reste la 8x8 monospace : chaque caractère fait 8px, chaque ligne
 *            20px, donc toutes les positions attendues se comptent à la main.
 */

#include <catch2/catch_test_macros.hpp>

#include "helpers/UITextInputHarness.h"

#include <string>

using namespace grove;
using namespace grove::uitest;

namespace {

// Monte le harnais sur la fixture multiligne et prend le focus par un clic dans la zone.
TextInputHarness makeArea(const std::string& suffix) {
    return TextInputHarness(suffix, "../../assets/ui/test_e2e_textarea.json");
}

constexpr double kAreaCenterX = 250.0;
constexpr double kAreaFirstLineY = 118.0;  // kAreaTextOriginY(108) + moitie d'une ligne de 20

}  // namespace

// ============================================================================
// La différence de sémantique n°1 : Entrée
// ============================================================================

TEST_CASE("IT_065: Entree INSERE un saut de ligne", "[integration][ui][e2e][textarea]") {
    auto h = makeArea("ta_enter");
    h.clickAt(kAreaCenterX, kAreaFirstLineY);

    h.type("ab");
    h.pressKey(kScanReturn);
    h.type("cd");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "ab\ncd");
}

TEST_CASE("IT_065: Ctrl+Entree SOUMET sans inserer", "[integration][ui][e2e][textarea]") {
    // Consequence directe : Entree etant prise par le saut de ligne, la soumission doit passer
    // ailleurs. Sans ce report, une zone de texte n'aurait aucun moyen d'etre validee au clavier.
    auto h = makeArea("ta_submit");

    int submits = 0;
    std::string submitted, action;
    h.observer->subscribe("ui:text_submit", [&](const Message& m) {
        ++submits;
        submitted = m.data->getString("text", "");
    });
    h.observer->subscribe("ui:action", [&](const Message& m) {
        action = m.data->getString("action", "");
    });

    h.clickAt(kAreaCenterX, kAreaFirstLineY);
    h.type("bonjour");
    h.pressKey(kScanReturn, /*shift=*/false, /*ctrl=*/true);
    h.pump();

    REQUIRE(submits == 1);
    REQUIRE(submitted == "bonjour");     // le texte est soumis TEL QUEL...
    REQUIRE(action == "test:submit");    // ...et l'action declarative part aussi
    REQUIRE(h.lastText == "bonjour");    // ...sans qu'un saut de ligne ait ete insere
}

// ============================================================================
// La différence n°2 : Début/Fin agissent sur la LIGNE
// ============================================================================

TEST_CASE("IT_065: Debut et Fin agissent sur la LIGNE, pas sur tout le texte",
          "[integration][ui][e2e][textarea]") {
    auto h = makeArea("ta_homeend");
    h.clickAt(kAreaCenterX, kAreaFirstLineY);

    h.type("abc");
    h.pressKey(kScanReturn);
    h.type("def");        // curseur en fin de la 2e ligne

    h.pressKey(kScanHome);  // debut de la LIGNE COURANTE, pas du texte
    h.type("X");
    INFO("apres Debut : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "abc\nXdef");

    h.pressKey(kScanEnd);   // fin de la LIGNE COURANTE
    h.type("Y");
    INFO("apres Fin : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "abc\nXdefY");
}

// ============================================================================
// La différence n°3 : Haut/Bas conservent la colonne
// ============================================================================

TEST_CASE("IT_065: les fleches Haut/Bas changent de ligne en gardant la colonne",
          "[integration][ui][e2e][textarea]") {
    auto h = makeArea("ta_updown");
    h.clickAt(kAreaCenterX, kAreaFirstLineY);

    h.type("abcdef");
    h.pressKey(kScanReturn);
    h.type("ghijkl");     // curseur en fin de ligne 1 (colonne 6)

    h.pressKey(kScanLeft);
    h.pressKey(kScanLeft);  // colonne 4 de la ligne 1
    h.pressKey(kScanUp);    // -> colonne 4 de la ligne 0
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "abcdXef\nghijkl");
}

TEST_CASE("IT_065: descendre vers une ligne PLUS COURTE sature en bout de ligne",
          "[integration][ui][e2e][textarea]") {
    auto h = makeArea("ta_short");
    h.clickAt(kAreaCenterX, kAreaFirstLineY);

    h.type("abcdefgh");
    h.pressKey(kScanReturn);
    h.type("xy");
    h.pressKey(kScanUp);     // ligne 0, colonne 2 (on etait en fin de "xy")
    h.pressKey(kScanEnd);    // fin de la ligne 0 -> colonne 8
    h.pressKey(kScanDown);   // ligne 1 n'a que 2 colonnes -> saturation en bout
    h.type("Z");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "abcdefgh\nxyZ");
}

// ============================================================================
// Clic : viser la bonne LIGNE
// ============================================================================

TEST_CASE("IT_065: un clic vise la ligne sous le curseur", "[integration][ui][e2e][textarea]") {
    // Chaque ligne fait 20px a partir de y=108. La ligne 0 couvre 108..128, la ligne 1 128..148.
    // On clique a y=138 (ligne 1), colonne 2 (x = 108 + 16).
    auto h = makeArea("ta_click");
    h.clickAt(kAreaCenterX, kAreaFirstLineY);

    h.type("abcd");
    h.pressKey(kScanReturn);
    h.type("efgh");

    h.clickAt(kAreaTextOriginX + 16.0, 138.0);
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "abcd\nefXgh");
}

// ============================================================================
// Sélection multi-lignes
// ============================================================================

TEST_CASE("IT_065: Maj+Bas selectionne a travers les lignes, et la frappe REMPLACE",
          "[integration][ui][e2e][textarea]") {
    auto h = makeArea("ta_selline");
    h.clickAt(kAreaCenterX, kAreaFirstLineY);

    h.type("abcd");
    h.pressKey(kScanReturn);
    h.type("efgh");

    // Curseur en fin de ligne 1. On remonte au debut du texte, puis on selectionne 1 ligne + 2 cols.
    h.pressKey(kScanUp);
    h.pressKey(kScanHome);                       // debut de la ligne 0
    h.pressKey(kScanDown, /*shift=*/true);       // selectionne "abcd\n"
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);      // ...+ "ef"
    h.type("Z");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "Zgh");
}

// ============================================================================
// Le presse-papiers est GENERIQUE : le meme service sert les deux widgets
// ============================================================================

TEST_CASE("IT_065: copier-coller fonctionne aussi dans la zone de texte",
          "[integration][ui][e2e][textarea][clipboard]") {
    auto h = makeArea("ta_clip");

    std::string clipboard;
    h.observer->subscribe("input:clipboard:set", [&](const Message& m) {
        clipboard = m.data->getString("text", "");
    });
    h.observer->subscribe("input:clipboard:get", [&](const Message&) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setString("text", clipboard);
        h.inputPub->publish("input:clipboard:text", std::move(d));
    });

    h.clickAt(kAreaCenterX, kAreaFirstLineY);
    h.type("abcd");
    h.pressKey(kScanHome);
    h.pressKey(kScanRight, /*shift=*/true);
    h.pressKey(kScanRight, /*shift=*/true);       // "ab" selectionne
    h.pressKey(kScanC, /*shift=*/false, /*ctrl=*/true);
    h.pump();
    REQUIRE(clipboard == "ab");

    h.pressKey(kScanEnd);
    h.pressKey(kScanV, /*shift=*/false, /*ctrl=*/true);
    h.pump();
    h.pump();

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "abcdab");
}

// ============================================================================
// Non-regression : le champ MONOLIGNE garde sa semantique d'Entree
// ============================================================================

TEST_CASE("IT_065: dans un champ monoligne, Entree SOUMET toujours",
          "[integration][ui][e2e][textarea]") {
    // Garde-fou du report Ctrl+Entree : il ne doit valoir QUE pour la zone de texte. Si le monoligne
    // se mettait a inserer des sauts de ligne, tous les formulaires existants casseraient.
    TextInputHarness h("ta_singleline_guard");   // fixture monoligne par defaut

    int submits = 0;
    h.observer->subscribe("ui:text_submit", [&](const Message&) { ++submits; });

    h.focusField();
    h.type("abc");
    h.pressKey(kScanReturn);
    h.pump();

    REQUIRE(submits == 1);
    REQUIRE(h.lastText == "abc");   // aucun '\n' insere
}
