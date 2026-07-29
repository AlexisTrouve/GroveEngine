#pragma once

// ============================================================================
// Retour à la ligne automatique — PUR, sans police, sans GPU, header-only.
//
// QUOI : découpe un texte en LIGNES VISUELLES. Une ligne visuelle est ce qu'on voit sur une rangée
//        à l'écran ; une ligne LOGIQUE est ce qui sépare deux '\n'. Sans wrap les deux coïncident ;
//        avec wrap, une ligne logique trop large en produit plusieurs.
//
// POURQUOI cette distinction porte tout le reste : dès qu'un texte se replie, chaque traduction
//        entre (ligne, colonne) et pixels doit passer par les lignes VISUELLES — le clic, le
//        curseur, les flèches Haut/Bas, le surlignage, le défilement. Se tromper de notion à un seul
//        de ces endroits produit un décalage que rien ne rattrape.
//
// COMMENT : l'avance des glyphes est un CALLABLE, donc ce fichier ne sait rien de la police ni du
//        GPU — même contrat que TextFit.h et TextMetrics.h, et c'est ce qui le rend testable
//        headless avec une métrique factice.
//
//        Règle de coupe : on coupe à la dernière OPPORTUNITÉ (juste après une espace) qui tienne.
//        Un mot plus large que la boîte n'a aucune opportunité — on le coupe alors au caractère,
//        sinon il déborderait indéfiniment. On garantit AU MOINS un codepoint par ligne visuelle :
//        c'est ce qui rend l'algorithme total, même pour une largeur absurde (une boîte de 2px).
//
//        `maxWidth <= 0` signifie AUCUN repli : seules les coupures dures ('\n') comptent. C'est le
//        patron « défaut à coût zéro » du moteur — un appelant qui ne veut pas de wrap obtient
//        exactement le découpage logique, sans chemin de code particulier.
//
//        Les espaces en fin de ligne visuelle sont EXCLUES du texte rendu mais l'index de reprise
//        les saute : elles ne doivent ni pousser la coupe, ni réapparaître en début de ligne
//        suivante (l'alinéa fantôme classique).
// ============================================================================

#include <grove/text/Utf8.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace grove {
namespace text {

struct VisualLine {
    size_t begin = 0;       // premier octet de la ligne visuelle (dans le texte source)
    size_t end = 0;         // fin exclusive du texte À DESSINER (espaces de coupe exclues)
    size_t next = 0;        // premier octet de la ligne visuelle SUIVANTE (espaces de coupe sautées)
    bool hardBreak = false; // cette ligne se termine par un '\n' explicite

    size_t length() const { return end > begin ? end - begin : 0; }
};

namespace detail {

// Une espace est une opportunité de coupe. Tabulation incluse ; le '\n' est traité à part (coupure
// dure). On ne cherche pas à implémenter les règles de coupe Unicode (UAX #14) : ce serait des
// tables, et ce n'est pas le problème que ce moteur résout.
inline bool isBreakSpace(uint32_t cp) { return cp == ' ' || cp == '\t'; }

}  // namespace detail

// ----------------------------------------------------------------------------
// `advanceOf(codepoint) -> float` rend l'avance d'un glyphe DANS LES MÊMES UNITÉS que maxWidth
// (donc déjà mise à l'échelle de la taille affichée).
//
// Le résultat contient TOUJOURS au moins une ligne (un texte vide = une ligne vide), pour que la
// vue n'ait pas de cas particulier : « la ligne 0 existe » est un invariant.
// ----------------------------------------------------------------------------
template <class AdvanceFn>
inline std::vector<VisualLine> wrapText(std::string_view text, float maxWidth, AdvanceFn advanceOf) {
    std::vector<VisualLine> out;

    const char* const base = text.data();
    const char* const end = base + text.size();
    const char* p = base;

    // Début de la ligne visuelle en cours.
    const char* lineBegin = base;
    // Dernière opportunité de coupe rencontrée : `opp` = fin du texte à dessiner (avant l'espace),
    // `oppNext` = où reprendre (après la ou les espaces).
    const char* opp = nullptr;
    const char* oppNext = nullptr;
    float width = 0.0f;

    auto push = [&](const char* drawEnd, const char* nextStart, bool hard) {
        VisualLine v;
        v.begin = static_cast<size_t>(lineBegin - base);
        v.end = static_cast<size_t>(drawEnd - base);
        v.next = static_cast<size_t>(nextStart - base);
        v.hardBreak = hard;
        out.push_back(v);
    };

    while (p < end) {
        // --- Coupure DURE : elle prime toujours, quelle que soit la largeur. ---
        if (*p == '\n') {
            push(p, p + 1, true);
            lineBegin = p + 1;
            p = p + 1;
            opp = nullptr; oppNext = nullptr;
            width = 0.0f;
            continue;
        }

        const char* const glyphStart = p;
        const uint32_t cp = decodeUtf8Bounded(p, end);
        const float adv = advanceOf(cp);

        if (detail::isBreakSpace(cp)) {
            // L'opportunité s'ouvre AVANT la PREMIÈRE espace de la suite : c'est là que le texte
            // dessiné doit s'arrêter. On consomme ensuite la suite ENTIÈRE d'un bloc — sinon chaque
            // espace suivante écraserait `opp` et la coupe tomberait après la dernière, laissant des
            // espaces traînantes en fin de ligne visuelle (« aa   » au lieu de « aa »).
            opp = glyphStart;
            width += adv;
            while (p < end && *p != '\n') {
                const char* const s0 = p;
                const uint32_t c2 = decodeUtf8Bounded(p, end);
                if (!detail::isBreakSpace(c2)) { p = s0; break; }
                width += advanceOf(c2);
            }
            oppNext = p;   // on reprendra APRÈS toute la suite : pas d'alinéa fantôme
            continue;
        }

        // --- Débordement ? ---
        if (maxWidth > 0.0f && width + adv > maxWidth && glyphStart > lineBegin) {
            if (opp != nullptr && opp > lineBegin) {
                // Coupe propre, à la dernière espace.
                push(opp, oppNext, false);
                lineBegin = oppNext;
                p = oppNext;
            } else {
                // Aucune opportunité : le mot est plus large que la boîte. On coupe au caractère —
                // sinon il déborderait indéfiniment. `glyphStart > lineBegin` garantit qu'on a posé
                // au moins un codepoint, donc l'algorithme progresse toujours.
                push(glyphStart, glyphStart, false);
                lineBegin = glyphStart;
                p = glyphStart;
            }
            opp = nullptr; oppNext = nullptr;
            width = 0.0f;
            continue;
        }

        width += adv;
    }

    // Dernière ligne (ou la seule, si le texte est vide).
    push(end, end, false);
    return out;
}

// ----------------------------------------------------------------------------
// Ligne visuelle contenant un index d'octet. Rend l'index dans `lines` ; jamais hors bornes.
//
// Un index posé exactement sur une frontière de coupe appartient à la ligne SUIVANTE — c'est ce qui
// place le curseur au début de la ligne d'arrivée après un repli, et non en bout de la précédente.
// ----------------------------------------------------------------------------
inline size_t visualLineAt(const std::vector<VisualLine>& lines, size_t byteIndex) {
    if (lines.empty()) return 0;
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
        if (byteIndex < lines[i + 1].begin) return i;
    }
    return lines.size() - 1;
}

}  // namespace text
}  // namespace grove
