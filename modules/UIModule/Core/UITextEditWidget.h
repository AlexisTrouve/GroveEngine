#pragma once

#include "UIWidget.h"
#include <string>

namespace grove {

/**
 * @brief Base commune aux widgets de SAISIE (UITextInput monoligne, UITextArea multiligne).
 *
 * QUOI     : l'API qu'un widget éditable expose au routeur clavier — sélection, insertion, touches,
 *            contenu, action de soumission.
 *
 * POURQUOI : le routage clavier de UIModule était écrit DEUX FOIS, une branche par type concret,
 *            pour un corps quasi identique (copier / couper / coller, insertion, publication de
 *            `ui:text_changed` et `ui:text_submit`). Un troisième widget éditable aurait demandé une
 *            troisième copie.
 *
 *            POURQUOI PAS sur `UIWidget` : `selectedText()` ne veut rien dire pour un libellé ou une
 *            image. Le plan de ce chantier s'était donné la règle « un virtuel sur la base doit avoir
 *            un sens pour TOUT widget » ; une base intermédiaire la respecte, une base fourre-tout
 *            l'aurait violée. Le module caste UNE fois vers ce contrat au lieu de deux fois vers des
 *            types concrets.
 *
 * COMMENT  : purement des relais. Chaque widget implémente ces méthodes avec ce qu'il possède déjà —
 *            `UITextInput` les avait toutes, `UITextArea` délègue à son `EditModel`. Aucune logique
 *            n'est déplacée, seule la façade est déclarée.
 *
 * ⚠️ Ce contrat ne publie RIEN. Comme tout widget, un éditable n'a pas d'IIO : il rend compte, et
 *    UIModule publie (cf. docs/UI_ARCHITECTURE.md).
 */
class UITextEditWidget : public UIWidget {
public:
    ~UITextEditWidget() override = default;

    // Tout widget de saisie prend le focus clavier au clic.
    bool acceptsFocus() const override { return true; }

    /** @brief Texte sélectionné, "" s'il n'y a pas de sélection. */
    virtual std::string selectedText() const = 0;

    /** @brief Efface la sélection. true si quelque chose a été supprimé. */
    virtual bool deleteSelection() = 0;

    /** @brief Insère une chaîne (commit IME, collage, UTF-8) en honorant filtre et longueur max. */
    virtual bool insertFilteredText(const std::string& str) = 0;

    /** @brief Traite une touche d'édition. true si consommée (donc contenu potentiellement changé). */
    virtual bool onKeyInput(int keyCode, uint32_t character, bool ctrl, bool shift) = 0;

    /** @brief Contenu courant. */
    virtual const std::string& text() const = 0;

    /** @brief Action publiée sur `ui:action` à la soumission ("" = aucune). */
    virtual const std::string& submitAction() const = 0;

    /**
     * @brief Cette touche SOUMET-elle le contenu ?
     *
     * C'est la SEULE divergence réelle entre les deux widgets : un champ monoligne soumet sur
     * Entrée ; une zone multiligne insère un saut de ligne, donc la soumission passe à Ctrl+Entrée.
     */
    virtual bool submitsOn(int keyCode, bool ctrl) const = 0;

    /**
     * @brief La touche de soumission doit-elle etre AVALEE (ne jamais atteindre le widget) ?
     *
     * POURQUOI ce second predicat, et pas seulement `submitsOn` : les deux widgets ne soumettent pas
     * au meme MOMENT du flux, et ce n'est pas un accident.
     *   - Zone multiligne (true) : Ctrl+Entree doit etre avale, sinon la touche descend dans le
     *     widget et insere un saut de ligne EN PLUS de soumettre.
     *   - Champ monoligne (false) : Entree traverse le widget normalement (elle n'insere rien), et la
     *     soumission suit la frappe -- le champ publie donc `ui:text_changed` AVANT `ui:text_submit`.
     *
     * Fusionner ces deux flux sans ce predicat change le comportement de l'un ou de l'autre. Le
     * declarer le rend explicite plutot que dependant de l'ordre des lignes dans le module.
     */
    virtual bool swallowsSubmitKey() const = 0;
};

}  // namespace grove
