#pragma once

// ============================================================================
// UITextArea — champ de saisie MULTILIGNE.
//
// QUOI : une zone de texte à plusieurs lignes, avec curseur, sélection, défilement vertical, et
//        Entrée qui insère un saut de ligne.
//
// POURQUOI un widget SÉPARÉ plutôt qu'un `multiline: true` sur UITextInput : greffer un booléen
//        aurait ramifié chaque méthode du champ monoligne (rendu, clic, navigation, défilement) sur
//        une condition, et ce genre de widget finit illisible. Ici les deux vues partagent leur
//        logique par le bas — `grove::text::EditModel` — et non par un drapeau. La modularité prime
//        (doctrine §III.1) ; le modèle partagé est ce qui rend le découpage gratuit plutôt que
//        duplicant.
//
// CE QUE CE WIDGET NE FAIT PAS (assumé et documenté) :
//   - Pas de RETOUR À LA LIGNE AUTOMATIQUE. Une ligne est un run entre deux '\n'. Le wrap est une
//     question de largeur, donc de police, donc de mesure par ligne à chaque changement de taille —
//     un chantier à part entière, pas un détail de ce widget.
//   - Pas de défilement HORIZONTAL. Une ligne plus large que la boîte est coupée par le clip.
//   Les deux sont des suites naturelles, pas des oublis.
//
// SÉMANTIQUE D'ENTRÉE : Entrée INSÈRE un saut de ligne (c'est l'attente universelle dans une zone de
//        texte) — la soumission passe donc à **Ctrl+Entrée**, qui publie `ui:text_submit`. Le champ
//        monoligne, lui, ne bouge pas : Entrée y soumet toujours.
// ============================================================================

#include "../Core/UIWidget.h"
#include "UITextInput.h"   // TextInputStyle / TextInputFilter — partagés avec le champ monoligne
#include <grove/text/TextEdit.h>
#include <grove/text/TextMetrics.h>

#include <cstdint>
#include <string>
#include <vector>

namespace grove {

class UITextArea : public UIWidget {
public:
    UITextArea() = default;
    ~UITextArea() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "textarea"; }

    // Le contenu clippe ses enfants : un textarea est un conteneur au sens du hit-test (§3.1 du
    // handoff UI), pour qu'un clic hors de sa boîte ne descende pas dedans.
    bool clipsHitTest() const override { return true; }
    void hitClipRect(float& x, float& y, float& w, float& h) const override;

    void releaseRenderEntries(UIRenderer& renderer) override;

    bool containsPoint(float px, float py) const;
    bool onMouseButton(int button, bool pressed, float x, float y);

    /**
     * @brief Touche d'édition. Même dialecte que UITextInput (Backspace=8, Entrée=13, flèches…),
     *        plus Haut=38 / Bas=40 qui n'ont de sens qu'ici.
     * @return true si l'événement a été consommé.
     */
    bool onKeyInput(int keyCode, uint32_t character, bool ctrl, bool shift = false);

    /** @brief Insère une chaîne en respectant le filtre du champ. */
    bool insertFilteredText(const std::string& str);

    void gainFocus();
    void loseFocus();

    // ------------------------------------------------------------------
    // Le modèle partagé — même source de vérité que UITextInput.
    // ------------------------------------------------------------------
    text::EditModel edit;

    const std::string& text() const { return edit.text(); }
    void setText(const std::string& value) { edit.setText(value); }

    std::string placeholder = "";
    TextInputFilter filter = TextInputFilter::None;
    bool enabled = true;
    float fontSize = 16.0f;
    float lineHeight = 20.0f;   // hauteur d'une ligne à l'écran
    std::string onSubmit;       // action publiée sur Ctrl+Entrée

    TextInputStyle normalStyle;
    TextInputStyle focusedStyle;
    TextInputStyle disabledStyle;

    TextInputState state = TextInputState::Normal;
    bool isFocused = false;
    bool draggingSelection = false;

    float scrollY = 0.0f;       // défilement vertical, en pixels

    float cursorBlinkTimer = 0.0f;
    bool cursorVisible = true;
    static constexpr float CURSOR_BLINK_INTERVAL = 0.5f;
    static constexpr float CURSOR_WIDTH = 2.0f;
    static constexpr float PADDING = 8.0f;
    static constexpr float CHAR_WIDTH = 8.0f;   // repli monospace, cf. UITextInput

    // Métriques de la police courante, prêtées par UIContext à chaque update (non-possédant).
    const text::Metrics* metrics = nullptr;

    /** @brief Index d'octet sous un point ÉCRAN (clic, glisser). */
    int indexAtScreenPos(float screenX, float screenY) const;

    /** @brief Première et dernière ligne visibles dans la boîte, défilement compris. */
    void visibleLineRange(int& first, int& last) const;

private:
    const TextInputStyle& getCurrentStyle() const;
    bool passesFilter(uint32_t ch) const;

    /** @brief Largeur du préfixe d'une ligne jusqu'à `bytesIntoLine`, à la taille affichée. */
    float measureInLine(const std::string& line, size_t bytesIntoLine) const;

    /** @brief Fait défiler pour que le curseur reste visible. */
    void ensureCursorVisible();

    /**
     * @brief Réserve/récupère les entrées de rendu d'une ligne visible.
     *
     * POURQUOI un POOL : le nombre d'entrées suit la HAUTEUR DE LA BOÎTE, jamais le nombre de lignes
     * du texte — un journal de 10 000 lignes n'enregistre qu'une vingtaine d'entrées, comme la liste
     * virtualisée. Sans ça, coller un gros texte enregistrerait des milliers d'entrées retained.
     */
    struct LineEntry {
        uint32_t textId = 0;
        uint32_t selectionId = 0;
    };
    std::vector<LineEntry> m_linePool;

    uint32_t m_cursorRenderId = 0;
    uint32_t m_placeholderRenderId = 0;
};

}  // namespace grove
