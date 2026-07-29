#pragma once

// ============================================================================
// Frontières de MOT — pures, sans police, sans GPU. Header-only.
//
// QUOI : « quel mot y a-t-il sous cet index ? » — l'intervalle [début, fin) du mot contenant une
//        position donnée. C'est ce que fait un double-clic dans n'importe quel champ de saisie.
//
// POURQUOI ici et pas dans le widget : c'est de la SEGMENTATION de texte, pas du dessin ni de
//        l'interaction. Un jeu qui veut découper un libellé, un futur champ multiligne, un moteur de
//        recherche in-game en ont le même besoin. Et surtout : isolé, ça se teste exhaustivement sans
//        monter un module, une fenêtre ou une police — la même raison qui a sorti la mesure de texte
//        du renderer (cf. TextMetrics.h).
//
// COMMENT : un mot est un run de caractères « de mot » ; tout le reste (espaces, ponctuation) forme
//        des runs de séparateurs, eux aussi sélectionnables d'un bloc. La classification est
//        volontairement SIMPLE et documentée plutôt que « correcte au sens Unicode » :
//
//          - ASCII : lettres, chiffres et '_' sont des caractères de mot.
//          - Latin-1 (0xC0..0xFF sauf 0xD7/0xF7, les signes multiplié/divisé) : caractères de mot.
//            C'est ce qui fait que « café » et « prénom » se sélectionnent d'un seul geste en
//            français — sans ça, un double-clic sur « café » ne prendrait que « caf ».
//          - Tout codepoint >= 0x100 : caractère de mot. Choix ASSUMÉ : pour les écritures non
//            latines on préfère sélectionner trop (le run entier) que couper à chaque caractère.
//            Une vraie segmentation Unicode (UAX #29) demanderait des tables ; ce n'est pas le
//            problème que ce moteur résout aujourd'hui.
//
//        Les bornes rendues sont TOUJOURS des frontières de codepoint, puisqu'on n'avance que
//        codepoint par codepoint — un double-clic ne peut donc pas couper un accent en deux.
// ============================================================================

#include <grove/text/TextMetrics.h>
#include <grove/text/Utf8.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace grove {
namespace text {

struct WordRange {
    size_t start = 0;
    size_t end = 0;
    bool empty() const { return start >= end; }
};

// Un codepoint fait-il partie d'un mot ? Voir la note de classification en tête de fichier.
inline bool isWordCodepoint(uint32_t cp) {
    if (cp < 0x80) {
        return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
               cp == '_';
    }
    if (cp < 0xC0) return false;              // 0x80..0xBF : signes/contrôles Latin-1
    if (cp == 0xD7 || cp == 0xF7) return false;  // × et ÷ ne sont pas des lettres
    return true;                               // lettres accentuées + tout le reste de l'Unicode
}

// Décode le codepoint qui COMMENCE à `pos` (qui doit être une frontière). N'avance pas `pos`.
inline uint32_t codepointAt(std::string_view s, size_t pos) {
    if (pos >= s.size()) return 0;
    const char* p = s.data() + pos;
    const char* const end = s.data() + s.size();
    // Décodage borné : on ne lit jamais au-delà de la vue.
    const unsigned char b0 = static_cast<unsigned char>(*p);
    if (b0 < 0x80) return b0;

    int extra;
    uint32_t cp;
    if      ((b0 & 0xE0) == 0xC0) { extra = 1; cp = b0 & 0x1Fu; }
    else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0Fu; }
    else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07u; }
    else return b0;

    ++p;
    for (int i = 0; i < extra && p < end; ++i) {
        const unsigned char bc = static_cast<unsigned char>(*p);
        if ((bc & 0xC0) != 0x80) return cp;
        cp = (cp << 6) | (bc & 0x3Fu);
        ++p;
    }
    return cp;
}

// ----------------------------------------------------------------------------
// Mot (ou run de séparateurs) contenant `index`.
//
// Règle de bord : un index posé À LA FIN d'un mot appartient à ce mot — double-cliquer juste après
// le 'é' de « café » sélectionne « café », pas l'espace qui suit. C'est le comportement attendu
// partout, et sans cette règle un double-clic en fin de champ ne sélectionnerait jamais rien.
// ----------------------------------------------------------------------------
inline WordRange wordBoundsAt(std::string_view s, size_t index) {
    WordRange out;
    if (s.empty()) return out;
    if (index > s.size()) index = s.size();

    // Se recoller sur une frontière de codepoint, puis choisir le caractère de référence : celui
    // sous l'index, ou — si l'index est en fin de chaîne/de mot — celui qui le précède.
    size_t probe = index;
    if (probe < s.size() && isUtf8Continuation(s[probe])) {
        probe = Metrics::prevIndex(s, probe);
    }

    size_t refPos = probe;
    if (refPos >= s.size()) {
        refPos = Metrics::prevIndex(s, s.size());
    }
    bool wordClass = isWordCodepoint(codepointAt(s, refPos));

    // Si l'index tombe sur un séparateur mais que le caractère PRÉCÉDENT est un mot, on prend le mot
    // (règle de bord ci-dessus).
    if (probe < s.size() && !wordClass && probe > 0) {
        const size_t before = Metrics::prevIndex(s, probe);
        if (isWordCodepoint(codepointAt(s, before))) {
            refPos = before;
            wordClass = true;
        }
    }

    // Étendre à gauche tant que la classe ne change pas.
    size_t start = refPos;
    while (start > 0) {
        const size_t prev = Metrics::prevIndex(s, start);
        if (isWordCodepoint(codepointAt(s, prev)) != wordClass) break;
        start = prev;
    }

    // Puis à droite.
    size_t end = refPos;
    while (end < s.size()) {
        if (isWordCodepoint(codepointAt(s, end)) != wordClass) break;
        end = Metrics::nextIndex(s, end);
    }

    out.start = start;
    out.end = end;
    return out;
}

}  // namespace text
}  // namespace grove
