#pragma once

// ============================================================================
// Mesure de texte — PURE, sans police, sans GPU, sans IIO. Header-only.
//
// QUOI : à partir d'une table d'avances (codepoint → largeur), répondre aux quatre questions dont
//        dépend TOUTE édition de texte :
//          - quelle largeur fait cette chaîne ?              → measure()
//          - où dessiner le curseur pour cet index ?          → xAtIndex()
//          - sur quel index l'utilisateur vient-il de cliquer ? → indexAtX()
//          - où est le caractère précédent / suivant ?        → prevIndex() / nextIndex()
//
// POURQUOI : UITextInput plaçait son curseur avec `index * 8.0f`, une hypothèse MONOSPACE héritée de
//        la police 8x8 intégrée. Depuis le passage à une vraie TTF proportionnelle, le curseur dérive
//        du vrai point d'insertion — et l'index étant compté en OCTETS, un Backspace sur "é" (2
//        octets) coupe le codepoint en deux. Les deux défauts sont ici, à un seul endroit, pour tout
//        le moteur.
//
// COMMENT : la table d'avances est une DONNÉE injectée, pas une dépendance — exactement le contrat de
//        TextFit.h ("l'avance des glyphes est un callable, donc ce fichier ne sait rien de la police
//        ni du GPU"). Conséquences directes :
//          1. testable headless avec une fausse table proportionnelle (aucune police requise) ;
//          2. l'UIModule peut mesurer sans dépendre du renderer — celui-ci lui POUSSE simplement sa
//             table au chargement de police (topic render:font:metrics), un seul sens, pas d'aller-retour ;
//          3. un jeu peut mesurer son propre texte de HUD avec la même API.
//
//        La table est cuite à une taille de base (`baseSize`) et mise à l'échelle par
//        `fontSize/baseSize` à la demande — même raisonnement que BitmapFont::loadTTF, qui cuit une
//        fois généreusement et rétrécit à l'affichage.
//
//        TABLE VIDE = REPLI MONOSPACE : les défauts (baseSize 8, fallbackAdvance 8) reproduisent au
//        pixel près l'ancien CHAR_WIDTH. Un hôte qui ne charge jamais de TTF, ou la fenêtre de frames
//        avant l'arrivée de la table, se comportent donc EXACTEMENT comme avant. C'est le patron
//        "défaut à coût zéro" déjà utilisé pour render:ambient et maxWidth.
//
// Périmètre : une LIGNE. Les sauts de ligne ne sont pas interprétés (l'appelant multiligne passera une
//        vue par ligne) — mais `lineHeight` est transporté ici parce qu'il vient de la même police.
// ============================================================================

#include <grove/text/Utf8.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace grove {
namespace text {

struct Metrics {
    // Avance par codepoint, exprimée à `baseSize`. Un codepoint absent retombe sur `fallbackAdvance`
    // (aucune police ne couvre tout l'Unicode ; mieux vaut une largeur plausible qu'un zéro qui
    // empilerait les glyphes au même endroit).
    std::unordered_map<uint32_t, float> advances;

    float baseSize = 8.0f;        // taille à laquelle la table a été cuite
    float lineHeight = 8.0f;      // interligne à baseSize (utile au multiligne)
    float fallbackAdvance = 8.0f; // avance des codepoints absents — et de TOUT si la table est vide

    // Aucune police chargée : on est en repli monospace historique.
    bool empty() const { return advances.empty(); }

    // ------------------------------------------------------------------
    // Avance d'un codepoint, mise à l'échelle de la taille demandée.
    // ------------------------------------------------------------------
    float advanceOf(uint32_t codepoint, float fontSize) const {
        const auto it = advances.find(codepoint);
        const float raw = (it != advances.end()) ? it->second : fallbackAdvance;
        return raw * scale(fontSize);
    }

    // ------------------------------------------------------------------
    // Largeur totale d'une chaîne.
    // ------------------------------------------------------------------
    float measure(std::string_view s, float fontSize) const {
        return widthUpTo(s, s.size(), fontSize);
    }

    // ------------------------------------------------------------------
    // Position pixel de la frontière `byteIndex` — c'est-à-dire où poser le curseur.
    // Un index au-delà de la fin sature à la fin (un curseur désynchronisé ne doit jamais lire hors
    // des bornes) ; un index tombé au milieu d'un codepoint mesure jusqu'à la frontière atteinte.
    // ------------------------------------------------------------------
    float xAtIndex(std::string_view s, size_t byteIndex, float fontSize) const {
        return widthUpTo(s, byteIndex, fontSize);
    }

    // ------------------------------------------------------------------
    // Index sous le pixel `x` — la conversion d'un clic en point d'insertion.
    //
    // COMMENT : on bascule sur la frontière suivante au MILIEU du glyphe, la convention universelle
    //   (cliquer sur la moitié gauche d'une lettre place le curseur AVANT elle). Le résultat est
    //   toujours une frontière de codepoint valide — jamais le milieu d'un caractère multi-octets —
    //   parce qu'on n'avance que codepoint par codepoint.
    // ------------------------------------------------------------------
    size_t indexAtX(std::string_view s, float x, float fontSize) const {
        if (s.empty() || x <= 0.0f) return 0;

        const float k = scale(fontSize);
        const char* p = s.data();
        const char* const end = s.data() + s.size();
        float acc = 0.0f;

        while (p < end) {
            const char* const before = p;
            const uint32_t cp = decodeUtf8Bounded(p, end);
            const float adv = rawAdvance(cp) * k;

            // Milieu de CE glyphe : au-delà, le curseur va après lui.
            if (x < acc + adv * 0.5f) {
                return static_cast<size_t>(before - s.data());
            }
            acc += adv;
        }
        return s.size();
    }

    // ------------------------------------------------------------------
    // Déplacement du curseur d'UN caractère (pas d'un octet) — le correctif du défaut UTF-8.
    //
    // Statiques : ne dépendent que de l'encodage, pas de la police. Un index tombé au milieu d'un
    // codepoint est recollé sur la frontière valide la plus proche dans la direction demandée : on
    // sort toujours d'un état incohérent, jamais on ne l'aggrave.
    // ------------------------------------------------------------------
    static size_t prevIndex(std::string_view s, size_t i) {
        if (i == 0) return 0;
        if (i > s.size()) i = s.size();
        --i;
        while (i > 0 && isUtf8Continuation(s[i])) --i;
        return i;
    }

    static size_t nextIndex(std::string_view s, size_t i) {
        if (i >= s.size()) return s.size();
        ++i;
        while (i < s.size() && isUtf8Continuation(s[i])) ++i;
        return i;
    }

private:
    float scale(float fontSize) const {
        // baseSize <= 0 serait une table corrompue : on ne met alors pas à l'échelle plutôt que de
        // diviser par zéro et propager des NaN dans des positions de curseur.
        return (baseSize > 0.0f) ? (fontSize / baseSize) : 1.0f;
    }

    float rawAdvance(uint32_t codepoint) const {
        const auto it = advances.find(codepoint);
        return (it != advances.end()) ? it->second : fallbackAdvance;
    }

    // Largeur des `limit` premiers octets (saturé à la taille de la chaîne).
    float widthUpTo(std::string_view s, size_t limit, float fontSize) const {
        if (s.empty() || limit == 0) return 0.0f;
        if (limit > s.size()) limit = s.size();

        const float k = scale(fontSize);
        const char* p = s.data();
        const char* const stop = s.data() + limit;
        float acc = 0.0f;
        while (p < stop) {
            const uint32_t cp = decodeUtf8Bounded(p, stop);
            acc += rawAdvance(cp) * k;
        }
        return acc;
    }
};

}  // namespace text
}  // namespace grove
