#pragma once

// ============================================================================
// Modèle d'ÉDITION de texte — pur, sans police, sans widget, sans GPU. Header-only.
//
// QUOI : le tampon, le curseur, la sélection, et les opérations qui les font évoluer (insérer,
//        effacer, se déplacer, sélectionner un mot, naviguer entre lignes).
//
// POURQUOI ce fichier existe : `UITextInput` et le futur `UITextArea` ont besoin EXACTEMENT de la
//        même logique — index UTF-8, ancre de sélection, remplacement à la frappe, bornes de mot.
//        Deux implémentations divergeraient : un correctif appliqué à l'une manquerait à l'autre, et
//        c'est précisément ce qui a produit les défauts déjà rencontrés dans ce chantier (la
//        suppression qui recassait l'UTF-8 que l'insertion gérait, l'ancre laissée derrière par
//        deleteCharBefore). Un seul modèle, testé unitairement, deux vues minces au-dessus.
//
//        L'alternative — greffer un `multiline: true` sur le widget existant — était plus courte à
//        écrire aujourd'hui et plus coûteuse à tenir ensuite : chaque méthode se serait ramifiée sur
//        un booléen. La modularité prime (doctrine §III.1).
//
// COMMENT : les index sont des OCTETS, toujours posés sur une frontière de codepoint — c'est
//        l'invariant central, tenu par `setCursor` qui recolle toute position, de sorte qu'aucun
//        appelant (clic, restauration d'état, désérialisation) ne puisse laisser le curseur au
//        milieu d'un caractère.
//
//        Ce qui N'EST PAS ici, volontairement : le FILTRE de saisie (numérique, sans espaces…) et le
//        mode mot de passe. Ce sont des politiques de présentation, propres au widget ; le modèle,
//        lui, édite du texte. Il garde `maxLength` en revanche, parce que la limite doit s'appliquer
//        ATOMIQUEMENT avec l'insertion (remplacer une sélection dans un champ plein doit rester
//        possible — l'opération ne rallonge pas le texte).
//
// Multiligne : le modèle ne connaît que le caractère '\n'. Il ne fait ni retour à la ligne
//        automatique ni mise en page — ce sont des questions de LARGEUR, donc de police, donc de vue.
// ============================================================================

#include <grove/text/TextMetrics.h>
#include <grove/text/TextWords.h>

#include <algorithm>
#include <cstddef>
#include <string>

namespace grove {
namespace text {

class EditModel {
public:
    // ------------------------------------------------------------------
    // État. Public en lecture : les vues en ont besoin à chaque frame pour dessiner. Toute MUTATION
    // passe par les méthodes ci-dessous, qui sont ce qui tient les invariants.
    // ------------------------------------------------------------------
    const std::string& text() const { return m_text; }
    int cursor() const { return m_cursor; }
    int anchor() const { return m_anchor; }

    int maxLength = 256;

    // Remplace tout le contenu (chargement, binding, setState). Le curseur atterrit en fin, ce que
    // fait tout champ qu'on vient de remplir par programme.
    void setText(const std::string& value) {
        m_text = value;
        m_cursor = static_cast<int>(m_text.size());
        m_anchor = m_cursor;
    }

    // ------------------------------------------------------------------
    // Sélection : une ancre FIXE face au curseur MOBILE. Ancre == curseur => aucune sélection.
    // Un seul état à tenir cohérent, au lieu d'un couple début/fin + un booléen « active ».
    // ------------------------------------------------------------------
    bool hasSelection() const { return m_anchor != m_cursor; }
    int selectionStart() const { return m_anchor < m_cursor ? m_anchor : m_cursor; }
    int selectionEnd()   const { return m_anchor < m_cursor ? m_cursor : m_anchor; }

    std::string selectedText() const {
        if (!hasSelection()) return "";
        const int from = selectionStart();
        return m_text.substr(static_cast<size_t>(from),
                             static_cast<size_t>(selectionEnd() - from));
    }

    void clearSelection() { m_anchor = m_cursor; }

    void selectAll() {
        m_anchor = 0;
        m_cursor = static_cast<int>(m_text.size());
    }

    /** @brief Sélectionne le mot (ou le run de séparateurs) contenant `index`. */
    void selectWordAt(int index) {
        const WordRange w = wordBoundsAt(m_text, static_cast<size_t>(std::max(0, index)));
        if (w.empty()) return;
        m_anchor = static_cast<int>(w.start);
        m_cursor = static_cast<int>(w.end);
    }

    /**
     * @brief Supprime la sélection s'il y en a une. Retourne true si quelque chose a disparu.
     *
     * Point de passage UNIQUE de toute suppression d'intervalle — frappe sur sélection, Backspace,
     * Suppr et Couper y convergent, pour qu'ils ne puissent pas diverger sur les cas limites
     * (bornes inversées, curseur laissé hors du texte).
     */
    bool deleteSelection() {
        if (!hasSelection()) return false;
        const int from = selectionStart();
        const int to = selectionEnd();
        m_text.erase(static_cast<size_t>(from), static_cast<size_t>(to - from));
        m_cursor = from;
        m_anchor = from;
        return true;
    }

    // ------------------------------------------------------------------
    // Déplacement du curseur.
    //
    // `extend` = la touche Maj est tenue : le curseur bouge, l'ancre reste, donc la sélection
    // s'étend. Sans Maj sur une sélection existante, la première pression se contente de la REPLIER
    // sur le bord visé : la flèche « sort » de la sélection, elle ne saute pas par-dessus. C'est la
    // convention partout, et l'oublier se remarque immédiatement à l'usage.
    // ------------------------------------------------------------------
    void setCursor(int pos, bool extend = false) {
        const int clamped = std::clamp(pos, 0, static_cast<int>(m_text.size()));
        size_t snapped = static_cast<size_t>(clamped);
        // Recollage sur une frontière de codepoint : l'invariant central du modèle, tenu ICI, une
        // seule fois, pour que personne d'autre n'ait à y penser.
        if (snapped < m_text.size() && isUtf8Continuation(m_text[snapped])) {
            snapped = Metrics::prevIndex(m_text, snapped);
        }
        m_cursor = static_cast<int>(snapped);
        if (!extend) m_anchor = m_cursor;
    }

    /** @brief Déplace de `chars` CARACTÈRES (pas d'octets) — négatif vers la gauche. */
    void moveCursor(int chars, bool extend = false) {
        if (!extend && hasSelection()) {
            // Repli sur le bord visé, sans avancer d'un caractère de plus.
            setCursor(chars < 0 ? selectionStart() : selectionEnd(), false);
            return;
        }
        size_t pos = static_cast<size_t>(m_cursor);
        for (int i = 0; i < chars; ++i)  pos = Metrics::nextIndex(m_text, pos);
        for (int i = 0; i > chars; --i)  pos = Metrics::prevIndex(m_text, pos);
        setCursor(static_cast<int>(pos), extend);
    }

    void moveToTextStart(bool extend = false) { setCursor(0, extend); }
    void moveToTextEnd(bool extend = false)   { setCursor(static_cast<int>(m_text.size()), extend); }

    // ------------------------------------------------------------------
    // Modification.
    // ------------------------------------------------------------------

    /**
     * @brief Insère `s` au curseur, en REMPLAÇANT la sélection s'il y en a une.
     * @return true si le texte a changé.
     *
     * La suppression de la sélection précède la vérification de maxLength : remplacer une sélection
     * dans un champ déjà plein doit rester possible, puisque l'opération ne rallonge pas le texte.
     * Le retour compare le TEXTE et non sa longueur — remplacer « abc » par « ZZZ » ne change pas la
     * longueur, et un appelant qui se fierait à elle n'émettrait jamais son événement de changement.
     */
    bool insert(const std::string& s) {
        const std::string before = m_text;
        deleteSelection();

        if (m_text.size() + s.size() > static_cast<size_t>(std::max(0, maxLength))) {
            return m_text != before;  // la seule chose qui a pu changer est la suppression
        }

        m_text.insert(static_cast<size_t>(m_cursor), s);
        m_cursor += static_cast<int>(s.size());
        m_anchor = m_cursor;
        return m_text != before;
    }

    /** @brief Backspace : supprime la sélection, sinon le CARACTÈRE avant le curseur. */
    bool deleteBefore() {
        if (deleteSelection()) return true;
        if (m_cursor <= 0) return false;
        const size_t cur = static_cast<size_t>(m_cursor);
        const size_t prev = Metrics::prevIndex(m_text, cur);
        m_text.erase(prev, cur - prev);
        m_cursor = static_cast<int>(prev);
        m_anchor = m_cursor;   // toute mutation rétablit l'invariant : pas d'ancre orpheline
        return true;
    }

    /** @brief Suppr : supprime la sélection, sinon le CARACTÈRE après le curseur. */
    bool deleteAfter() {
        if (deleteSelection()) return true;
        if (m_cursor >= static_cast<int>(m_text.size())) return false;
        const size_t cur = static_cast<size_t>(m_cursor);
        const size_t next = Metrics::nextIndex(m_text, cur);
        m_text.erase(cur, next - cur);
        m_anchor = m_cursor;
        return true;
    }

    // ------------------------------------------------------------------
    // LIGNES. Le modèle ne connaît que '\n' : pas de retour à la ligne automatique, pas de mise en
    // page — ce sont des questions de largeur, donc de police, donc de vue.
    // ------------------------------------------------------------------

    /** @brief Index du premier octet de la ligne contenant `index`. */
    size_t lineStart(int index) const {
        size_t i = static_cast<size_t>(std::clamp(index, 0, static_cast<int>(m_text.size())));
        while (i > 0 && m_text[i - 1] != '\n') --i;
        return i;
    }

    /** @brief Index du '\n' terminant la ligne contenant `index` (ou la fin du texte). */
    size_t lineEnd(int index) const {
        size_t i = static_cast<size_t>(std::clamp(index, 0, static_cast<int>(m_text.size())));
        while (i < m_text.size() && m_text[i] != '\n') ++i;
        return i;
    }

    /** @brief Numéro (0-based) de la ligne contenant `index`. */
    int lineNumberAt(int index) const {
        const size_t limit = static_cast<size_t>(std::clamp(index, 0, static_cast<int>(m_text.size())));
        int n = 0;
        for (size_t i = 0; i < limit; ++i) {
            if (m_text[i] == '\n') ++n;
        }
        return n;
    }

    /** @brief Nombre de lignes (toujours >= 1 ; un texte vide compte une ligne vide). */
    int lineCount() const {
        int n = 1;
        for (char c : m_text) {
            if (c == '\n') ++n;
        }
        return n;
    }

    /** @brief Début de la ligne numéro `line` (saturé aux bornes). */
    size_t startOfLine(int line) const {
        if (line <= 0) return 0;
        int seen = 0;
        for (size_t i = 0; i < m_text.size(); ++i) {
            if (m_text[i] == '\n') {
                if (++seen == line) return i + 1;
            }
        }
        return m_text.size();
    }

    /**
     * @brief Monte ou descend de `deltaLines` lignes en conservant la COLONNE (en caractères).
     *
     * Colonne comptée en CARACTÈRES et non en octets : sur une ligne accentuée, un décompte en
     * octets ferait dériver la position d'une ligne à l'autre et pourrait viser le milieu d'un
     * codepoint. La cible est saturée à la fin de la ligne d'arrivée, comme partout — descendre
     * depuis une longue ligne vers une courte pose le curseur en bout de la courte.
     */
    void moveCursorByLine(int deltaLines, bool extend = false) {
        const int currentLine = lineNumberAt(m_cursor);
        const int targetLine = std::clamp(currentLine + deltaLines, 0, lineCount() - 1);
        if (targetLine == currentLine) {
            // Déjà sur la première/dernière ligne : le geste attendu est d'aller au bord.
            setCursor(deltaLines < 0 ? 0 : static_cast<int>(m_text.size()), extend);
            return;
        }

        const size_t curLineStart = lineStart(m_cursor);
        int column = 0;
        for (size_t i = curLineStart; i < static_cast<size_t>(m_cursor); ) {
            i = Metrics::nextIndex(m_text, i);
            ++column;
        }

        const size_t target = startOfLine(targetLine);
        const size_t targetEnd = lineEnd(static_cast<int>(target));
        size_t pos = target;
        for (int c = 0; c < column && pos < targetEnd; ++c) {
            pos = Metrics::nextIndex(m_text, pos);
        }
        setCursor(static_cast<int>(std::min(pos, targetEnd)), extend);
    }

    void moveToLineStart(bool extend = false) { setCursor(static_cast<int>(lineStart(m_cursor)), extend); }
    void moveToLineEnd(bool extend = false)   { setCursor(static_cast<int>(lineEnd(m_cursor)), extend); }

private:
    std::string m_text;
    int m_cursor = 0;
    int m_anchor = 0;
};

}  // namespace text
}  // namespace grove
