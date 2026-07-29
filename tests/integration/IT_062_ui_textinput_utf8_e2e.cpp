/**
 * Integration Test IT_062: éditer du texte ACCENTUÉ dans un UITextInput (E2E, vrai module).
 *
 * QUOI     : taper, effacer et déplacer le curseur sur des caractères UTF-8 multi-octets.
 *
 * POURQUOI : UITextInput compte son curseur en OCTETS —
 *              text.erase(cursorPosition - 1, 1)   (UITextInput.cpp:269)
 *            donc un Backspace sur "é" (0xC3 0xA9, DEUX octets) n'en retire qu'un et laisse une
 *            demi-séquence UTF-8 dans le champ. En français c'est un bug de tous les jours : tout
 *            "é", "à", "ç", "—" est cassé. Ironie : insertFilteredText() avait été écrit exprès pour
 *            l'UTF-8 multi-octets EN ENTRÉE (fix #5/C2) — la suppression le recasse derrière.
 *            Même famille de défaut que les flèches gauche/droite, qui sautent d'un octet.
 *
 * COMMENT  : E2E par le vrai module chargé dynamiquement, piloté par les MÊMES topics que publie le
 *            véritable InputModule (input:keyboard:text pour les caractères, input:keyboard:key
 *            {scancode} pour les touches d'édition) — pas d'appel direct au widget. On observe
 *            ui:text_changed, la seule sortie publique : le curseur n'est pas publié, donc son
 *            exactitude est prouvée INDIRECTEMENT (insérer après un déplacement révèle où il était).
 *
 *            Les littéraux sont écrits en octets explicites ("\xC3\xA9") plutôt qu'en "é" : le test
 *            porte précisément sur des octets, il ne doit pas dépendre de l'encodage du fichier
 *            source ni des options du compilateur.
 */

#include <catch2/catch_test_macros.hpp>

#include "helpers/UITextInputHarness.h"

#include <grove/text/TextMetricsWire.h>

#include <memory>
#include <string>

using namespace grove;
using namespace grove::uitest;

TEST_CASE("IT_062: Backspace efface un caractere accentue ENTIER", "[integration][ui][e2e][utf8]") {
    // Le cas nominal du bug. Taper "é" puis Backspace doit vider le champ. Avec un curseur compté en
    // octets, il reste l'octet de tête 0xC3 — une séquence UTF-8 invalide que le rendu affichera en
    // caractère de remplacement, et que le jeu recevra telle quelle dans ui:text_changed.
    TextInputHarness h("utf8_bs");
    h.focusField();

    h.type(kEAigu);
    INFO("apres saisie : " << hexdump(h.lastText));
    REQUIRE(h.lastText == kEAigu);
    REQUIRE(h.lastText.size() == 2u);  // bien DEUX octets pour UN caractere

    h.pressKey(kScanBackspace);
    INFO("apres backspace : " << hexdump(h.lastText));
    REQUIRE(h.lastText.empty());  // <-- ROUGE aujourd'hui : il reste "\xC3"
}

TEST_CASE("IT_062: Backspace au milieu d'un mot accentue ne casse pas les voisins",
          "[integration][ui][e2e][utf8]") {
    // Plus proche d'un vrai usage : effacer la fin d'un mot accentue doit laisser un prefixe VALIDE.
    TextInputHarness h("utf8_word");
    h.focusField();

    h.type("caf" + kEAigu);  // "café"
    REQUIRE(h.lastText == "caf" + kEAigu);

    h.pressKey(kScanBackspace);
    INFO("apres backspace : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "caf");  // <-- ROUGE aujourd'hui : "caf\xC3"
}

TEST_CASE("IT_062: Suppr efface un caractere accentue ENTIER", "[integration][ui][e2e][utf8]") {
    // Le pendant avant-curseur : deleteCharAfter() a exactement le meme defaut d'un octet.
    TextInputHarness h("utf8_del");
    h.focusField();

    h.type(kEAigu + std::string("a"));  // "éa", curseur en fin
    REQUIRE(h.lastText == kEAigu + std::string("a"));

    h.pressKey(kScanLeft);   // avant 'a'
    h.pressKey(kScanLeft);   // avant 'é'  (doit enjamber DEUX octets)
    h.pressKey(kScanDelete); // supprime 'é' en entier

    INFO("apres suppr : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "a");
}

TEST_CASE("IT_062: la fleche gauche enjambe un caractere accentue ENTIER",
          "[integration][ui][e2e][utf8]") {
    // Le curseur n'est pas publie : on prouve sa position en INSERANT apres l'avoir deplace.
    // Un pas d'un octet placerait le 'X' ENTRE les deux octets de 'é' et produirait une chaine
    // corrompue que ce REQUIRE attrape.
    TextInputHarness h("utf8_left");
    h.focusField();

    h.type("a" + kEAigu);   // "aé", curseur en fin
    h.pressKey(kScanLeft);  // doit se poser AVANT 'é', pas entre ses deux octets
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "aX" + kEAigu);
}

TEST_CASE("IT_062: la fleche droite enjambe un caractere accentue ENTIER",
          "[integration][ui][e2e][utf8]") {
    // NB : on part de Home et NON de deux Left. Avec "Left, Left, Right" les erreurs d'un octet se
    // COMPENSENT (on retombe sur la bonne position par accident) et le test ne morde pas. Depuis une
    // origine franche, un seul Right doit enjamber les DEUX octets du 'é' d'un coup.
    TextInputHarness h("utf8_right");
    h.focusField();

    h.type(kEAigu + std::string("b"));  // "éb", curseur en fin
    h.pressKey(kScanHome);              // debut de chaine, position 0
    h.pressKey(kScanRight);             // doit se poser APRES 'é' (octet 2), pas entre ses octets
    h.type("X");

    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == kEAigu + std::string("Xb"));
}

TEST_CASE("IT_062: l'ASCII pur reste inchange (non-regression)", "[integration][ui][e2e][utf8]") {
    // Garde-fou : le correctif UTF-8 ne doit RIEN changer au comportement mono-octet existant,
    // deja verrouille par IT_016. Si ce cas casse, le correctif est allé trop loin.
    TextInputHarness h("utf8_ascii");
    h.focusField();

    h.type("AB");
    REQUIRE(h.lastText == "AB");
    h.pressKey(kScanBackspace);
    REQUIRE(h.lastText == "A");
    h.pressKey(kScanLeft);
    h.type("Z");
    REQUIRE(h.lastText == "ZA");
}

// ============================================================================
// T0c/T0d — la table d'avances du renderer, et le clic qui place le curseur.
//
// La position du curseur n'est jamais publiée : on la prouve, comme plus haut, en INSÉRANT après
// avoir cliqué. C'est aussi ce qui rend le test insensible au rendu (headless) tout en testant la
// vraie chaîne : topic de métriques -> UIContext -> widget -> index d'insertion.
// ============================================================================

namespace {

// Pousse une table d'avances DÉLIBÉRÉMENT proportionnelle, encodée comme le fait le renderer.
// 'i' fait 4px, tout le reste 20px : sous une mesure monospace, tout clic tomberait ailleurs.
void publishProportionalMetrics(IntraIO& pub) {
    text::Metrics m;
    m.baseSize = 10.0f;
    m.lineHeight = 12.0f;
    m.fallbackAdvance = 20.0f;
    m.advances[static_cast<uint32_t>('i')] = 4.0f;

    const text::MetricsWire w = text::encodeDense(m, 32, 255);
    auto d = std::make_unique<JsonDataNode>("d");
    d->setDouble("baseSize", w.baseSize);
    d->setDouble("lineHeight", w.lineHeight);
    d->setInt("firstCodepoint", static_cast<int>(w.firstCodepoint));
    d->setString("advances", w.advances);
    pub.publish("render:font:metrics", std::move(d));
}

}  // namespace

TEST_CASE("IT_062: un clic place le curseur sur le caractere vise", "[integration][ui][e2e][caret]") {
    // Le champ est en x=100 avec PADDING=8 -> le texte commence a x=108. La fixture demande
    // fontSize 18 ; la table est cuite a 10, donc chaque avance est mise a l'echelle x1.8 :
    // 'i' = 7.2px, les autres 36px.
    //
    // Texte "MMMM" (4 x 36 = 144px). Cliquer a 108 + 80 = 188 tombe entre la 2e et la 3e lettre
    // (frontieres a 0, 36, 72, 108, 144 ; 80 est plus proche de 72). Le curseur doit donc se poser
    // a l'index 2, et un 'X' tape ensuite donne "MMXMM".
    //
    // Avec l'ancienne mesure monospace 8px, 80px correspondrait a l'index 10 -> saturation en fin de
    // chaine -> "MMMMX". Ce cas discrimine donc les deux implementations.
    TextInputHarness h("caret_click");
    publishProportionalMetrics(*h.inputPub);
    h.pump();  // laisse le module consommer les metriques

    h.focusField();
    h.type("MMMM");
    REQUIRE(h.lastText == "MMMM");

    // Clic a 80px dans le texte (x ecran = 100 + 8 + 80 = 188), sur la meme ligne.
    {
        auto move = std::make_unique<JsonDataNode>("d");
        move->setDouble("x", 188.0); move->setDouble("y", 120.0);
        h.inputPub->publish("input:mouse:move", std::move(move));
        h.pump();
        for (bool pressed : {true, false}) {
            auto d = std::make_unique<JsonDataNode>("d");
            d->setInt("button", 0); d->setBool("pressed", pressed);
            d->setDouble("x", 188.0); d->setDouble("y", 120.0);
            h.inputPub->publish("input:mouse:button", std::move(d));
            h.pump();
        }
    }

    h.type("X");
    INFO("resultat : " << hexdump(h.lastText));
    REQUIRE(h.lastText == "MMXMM");
}

TEST_CASE("IT_062: un clic au-dela de la fin du texte place le curseur EN FIN",
          "[integration][ui][e2e][caret]") {
    // Cliquer dans le vide a droite du texte est le geste le plus courant pour "aller a la fin".
    TextInputHarness h("caret_end");
    publishProportionalMetrics(*h.inputPub);
    h.pump();

    h.focusField();
    h.type("ab");

    {
        auto move = std::make_unique<JsonDataNode>("d");
        move->setDouble("x", 380.0); move->setDouble("y", 120.0);  // pres du bord droit du champ
        h.inputPub->publish("input:mouse:move", std::move(move));
        h.pump();
        for (bool pressed : {true, false}) {
            auto d = std::make_unique<JsonDataNode>("d");
            d->setInt("button", 0); d->setBool("pressed", pressed);
            d->setDouble("x", 380.0); d->setDouble("y", 120.0);
            h.inputPub->publish("input:mouse:button", std::move(d));
            h.pump();
        }
    }

    h.type("Z");
    REQUIRE(h.lastText == "abZ");
}

TEST_CASE("IT_062: un clic ne coupe JAMAIS un caractere accentue", "[integration][ui][e2e][caret][utf8]") {
    // La jonction des deux defauts : un clic calcule une position en OCTETS, un accent en fait deux.
    // On balaye tout le champ ; aucune position de clic ne doit produire une chaine corrompue.
    TextInputHarness h("caret_utf8");
    publishProportionalMetrics(*h.inputPub);
    h.pump();

    h.focusField();
    const std::string base = kEAigu + kEAigu + kEAigu;  // "别别别" -> ici "ééé"
    h.type(base);
    REQUIRE(h.lastText == base);

    for (double clickX = 102.0; clickX < 390.0; clickX += 7.0) {
        auto move = std::make_unique<JsonDataNode>("d");
        move->setDouble("x", clickX); move->setDouble("y", 120.0);
        h.inputPub->publish("input:mouse:move", std::move(move));
        h.pump();
        for (bool pressed : {true, false}) {
            auto d = std::make_unique<JsonDataNode>("d");
            d->setInt("button", 0); d->setBool("pressed", pressed);
            d->setDouble("x", clickX); d->setDouble("y", 120.0);
            h.inputPub->publish("input:mouse:button", std::move(d));
            h.pump();
        }
        // Le texte ne doit pas avoir bouge, et surtout rester une sequence UTF-8 VALIDE :
        // chaque octet 0xC3 doit etre suivi de son 0xA9.
        REQUIRE(h.lastText == base);
    }

    // Et une frappe apres n'importe lequel de ces clics produit toujours une chaine valide :
    // le 'X' ne peut pas s'etre glisse entre deux octets.
    h.type("X");
    INFO("resultat : " << hexdump(h.lastText));
    for (size_t i = 0; i + 1 < h.lastText.size(); ++i) {
        if (static_cast<unsigned char>(h.lastText[i]) == 0xC3) {
            REQUIRE(static_cast<unsigned char>(h.lastText[i + 1]) == 0xA9);
        }
    }
}
