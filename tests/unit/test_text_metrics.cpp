/**
 * Unit Tests: grove::text::Metrics — mesure de texte PURE, sans police, sans GPU, sans fenêtre.
 *
 * QUOI     : largeur d'une chaîne, position pixel d'un index, index sous un pixel, et pas de curseur
 *            UTF-8-safe. C'est le socle du curseur, de la sélection et du clic-pour-placer.
 *
 * POURQUOI : UITextInput place son curseur avec `index * 8.0f` (CHAR_WIDTH), une hypothèse MONOSPACE
 *            héritée de la police 8x8 intégrée. Depuis le passage à une vraie TTF proportionnelle, le
 *            curseur dérive. Et l'index est en OCTETS, donc un Backspace sur "é" (2 octets) coupe le
 *            codepoint en deux — un bug de tous les jours en français.
 *
 * COMMENT  : la table d'avances est une DONNÉE injectée, donc ce test n'a besoin d'aucune police. Elle
 *            est délibérément PROPORTIONNELLE ('i'=4, 'M'=20) : une implémentation monospace ne peut
 *            pas passer ces cas. Même contrat que TextFit.h (l'avance est une entrée, pas une
 *            dépendance) — c'est ce qui rend l'oracle valable quelle que soit la police réelle.
 */

#include <catch2/catch_test_macros.hpp>

#include <grove/text/TextMetrics.h>

#include <string>

using namespace grove::text;

namespace {

// Table volontairement proportionnelle. Base 10px : les valeurs attendues se comptent à la main.
//   'i' = 4, 'M' = 20, 'a' = 10, U+00E9 ('é') = 12, tout le reste = 10 (fallback).
Metrics proportional() {
    Metrics m;
    m.baseSize = 10.0f;
    m.lineHeight = 12.0f;
    m.fallbackAdvance = 10.0f;
    m.advances[static_cast<uint32_t>('i')] = 4.0f;
    m.advances[static_cast<uint32_t>('M')] = 20.0f;
    m.advances[static_cast<uint32_t>('a')] = 10.0f;
    m.advances[0x00E9u] = 12.0f;  // é
    return m;
}

}  // namespace

// ============================================================================
// Mesure
// ============================================================================

TEST_CASE("Metrics: la largeur suit les avances RÉELLES, pas une moyenne", "[text][unit][metrics]") {
    const Metrics m = proportional();
    // "iM" = 4 + 20 = 24. Un monospace à 10 donnerait 20 : ce cas le tue.
    REQUIRE(m.measure("iM", 10.0f) == 24.0f);
    REQUIRE(m.measure("Mi", 10.0f) == 24.0f);
    REQUIRE(m.measure("", 10.0f) == 0.0f);
}

TEST_CASE("Metrics: la mesure s'échelonne avec la taille demandée", "[text][unit][metrics]") {
    const Metrics m = proportional();
    // La table est cuite à baseSize=10 ; à 20 tout double. C'est ce qui permet UNE cuisson pour
    // toutes les tailles à l'écran (même raisonnement que BitmapFont::loadTTF).
    REQUIRE(m.measure("iM", 20.0f) == 48.0f);
    REQUIRE(m.measure("iM", 5.0f) == 12.0f);
}

TEST_CASE("Metrics: un codepoint absent de la table retombe sur l'avance par défaut",
          "[text][unit][metrics]") {
    const Metrics m = proportional();
    // 'Z' n'est pas dans la table → fallbackAdvance (10). Aucune police ne couvre tout.
    REQUIRE(m.measure("Z", 10.0f) == 10.0f);
    REQUIRE(m.measure("iZ", 10.0f) == 14.0f);
}

// ============================================================================
// Index → pixel (position du curseur, ancre de sélection)
// ============================================================================

TEST_CASE("Metrics: xAtIndex donne la position du curseur, proportionnelle", "[text][unit][metrics]") {
    const Metrics m = proportional();
    // "iM" : curseur avant 'i' = 0 ; entre 'i' et 'M' = 4 (PAS 8 comme le monospace) ; fin = 24.
    REQUIRE(m.xAtIndex("iM", 0, 10.0f) == 0.0f);
    REQUIRE(m.xAtIndex("iM", 1, 10.0f) == 4.0f);
    REQUIRE(m.xAtIndex("iM", 2, 10.0f) == 24.0f);
}

TEST_CASE("Metrics: un index au-delà de la fin sature à la fin de chaîne", "[text][unit][metrics]") {
    const Metrics m = proportional();
    // Robustesse : un curseur désynchronisé ne doit pas lire hors des bornes.
    REQUIRE(m.xAtIndex("iM", 99, 10.0f) == 24.0f);
}

// ============================================================================
// Pixel → index (clic pour placer le curseur)
// ============================================================================

TEST_CASE("Metrics: indexAtX choisit la frontière de codepoint la PLUS PROCHE",
          "[text][unit][metrics]") {
    const Metrics m = proportional();
    // "iM" : frontières à x = 0, 4, 24. Le basculement se fait au MILIEU du glyphe — c'est la
    // convention universelle (cliquer sur la moitié gauche d'une lettre place le curseur avant elle).
    REQUIRE(m.indexAtX("iM", 0.0f, 10.0f) == 0u);
    REQUIRE(m.indexAtX("iM", 1.0f, 10.0f) == 0u);   // moitié gauche du 'i' (milieu = 2)
    REQUIRE(m.indexAtX("iM", 3.0f, 10.0f) == 1u);   // moitié droite du 'i'
    REQUIRE(m.indexAtX("iM", 5.0f, 10.0f) == 1u);   // début du 'M' (milieu = 14)
    REQUIRE(m.indexAtX("iM", 20.0f, 10.0f) == 2u);  // moitié droite du 'M'
}

TEST_CASE("Metrics: un clic hors des bornes sature aux extrémités", "[text][unit][metrics]") {
    const Metrics m = proportional();
    REQUIRE(m.indexAtX("iM", -50.0f, 10.0f) == 0u);
    REQUIRE(m.indexAtX("iM", 9999.0f, 10.0f) == 2u);
    REQUIRE(m.indexAtX("", 42.0f, 10.0f) == 0u);
}

TEST_CASE("Metrics: aller-retour index -> pixel -> index", "[text][unit][metrics]") {
    const Metrics m = proportional();
    // Toute frontière repassée par xAtIndex doit revenir sur elle-même : c'est l'invariant qui garantit
    // qu'un clic sur le curseur ne le fait pas sauter.
    const char* s = "iMa";
    for (size_t i : {size_t(0), size_t(1), size_t(2), size_t(3)}) {
        REQUIRE(m.indexAtX(s, m.xAtIndex(s, i, 10.0f), 10.0f) == i);
    }
}

// ============================================================================
// UTF-8 — le défaut D2 : l'index est en OCTETS, le déplacement doit être en CODEPOINTS
// ============================================================================

TEST_CASE("Metrics: nextIndex/prevIndex enjambent un codepoint ENTIER", "[text][unit][metrics][utf8]") {
    // "é" = 0xC3 0xA9 : 2 octets, 1 caractère. Un pas de +1/-1 octet le coupe en deux et produit du
    // texte corrompu. C'est exactement ce que fait aujourd'hui UITextInput::deleteCharBefore().
    const std::string s = "aéb";  // octets : a(1) é(2) b(1) = 4
    REQUIRE(s.size() == 4u);

    REQUIRE(Metrics::nextIndex(s, 0) == 1u);  // après 'a'
    REQUIRE(Metrics::nextIndex(s, 1) == 3u);  // enjambe les DEUX octets de 'é'
    REQUIRE(Metrics::nextIndex(s, 3) == 4u);  // après 'b'
    REQUIRE(Metrics::nextIndex(s, 4) == 4u);  // fin : sature

    REQUIRE(Metrics::prevIndex(s, 4) == 3u);
    REQUIRE(Metrics::prevIndex(s, 3) == 1u);  // recule des DEUX octets de 'é'
    REQUIRE(Metrics::prevIndex(s, 1) == 0u);
    REQUIRE(Metrics::prevIndex(s, 0) == 0u);  // début : sature
}

TEST_CASE("Metrics: un index atterri au MILIEU d'un codepoint est recollé sur une frontière",
          "[text][unit][metrics][utf8]") {
    // Robustesse : si un index hérité (ou une désérialisation) tombe entre les deux octets de 'é',
    // avancer/reculer doit ramener sur une frontière valide, jamais empirer.
    const std::string s = "aéb";
    REQUIRE(Metrics::prevIndex(s, 2) == 1u);  // milieu de 'é' → devant 'é'
    REQUIRE(Metrics::nextIndex(s, 2) == 3u);  // milieu de 'é' → après 'é'
}

TEST_CASE("Metrics: la mesure compte les codepoints, pas les octets", "[text][unit][metrics][utf8]") {
    const Metrics m = proportional();
    // "é" = 12 dans la table. Compté en octets, on obtiendrait 2 x fallback = 20.
    REQUIRE(m.measure("é", 10.0f) == 12.0f);
    REQUIRE(m.measure("aé", 10.0f) == 22.0f);
    // Le curseur APRÈS 'é' est à l'octet 3 dans "aé" (a=1 octet, é=2).
    REQUIRE(m.xAtIndex("aé", 3, 10.0f) == 22.0f);
}

TEST_CASE("Metrics: indexAtX ne retourne JAMAIS un index au milieu d'un codepoint",
          "[text][unit][metrics][utf8]") {
    const Metrics m = proportional();
    const std::string s = "aé";  // frontières valides : 0, 1, 3 — jamais 2
    for (float x = -5.0f; x <= 40.0f; x += 0.5f) {
        const size_t idx = m.indexAtX(s, x, 10.0f);
        REQUIRE(idx != 2u);
        REQUIRE((idx == 0u || idx == 1u || idx == 3u));
    }
}

// ============================================================================
// Le repli monospace — la garantie de non-régression
// ============================================================================

TEST_CASE("Metrics: une table VIDE reproduit exactement l'ancien comportement monospace",
          "[text][unit][metrics][fallback]") {
    // POURQUOI ce test : la table d'avances arrive du renderer APRÈS le chargement de la police. Avant
    // qu'elle n'arrive — et pour tout hôte qui ne charge jamais de TTF — le champ doit se comporter
    // comme avant, au pixel près (UITextInput::CHAR_WIDTH = 8, sans mise à l'échelle).
    Metrics m;  // défauts : table vide, baseSize == fallbackAdvance == 8
    REQUIRE(m.empty());
    REQUIRE(m.measure("ABCD", 8.0f) == 32.0f);
    REQUIRE(m.xAtIndex("ABCD", 2, 8.0f) == 16.0f);
    REQUIRE(m.indexAtX("ABCD", 16.0f, 8.0f) == 2u);
}
