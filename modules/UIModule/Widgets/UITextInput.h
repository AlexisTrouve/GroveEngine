#pragma once

#include "../Core/UITextEditWidget.h"
#include "UIFrame.h"
#include <grove/text/TextMetrics.h>
#include <grove/text/TextEdit.h>
#include <cstdint>
#include <string>

namespace grove {

/**
 * @brief Text input filter types
 */
enum class TextInputFilter {
    None,           // No filtering
    Alphanumeric,   // Letters and numbers only
    Numeric,        // Numbers only (int)
    Float,          // Numbers with decimal point
    NoSpaces        // No whitespace characters
};

/**
 * @brief Text input visual state
 */
enum class TextInputState {
    Normal,
    Focused,
    Disabled
};

/**
 * @brief Style properties for text input
 */
struct TextInputStyle {
    uint32_t bgColor = 0x222222FF;
    uint32_t textColor = 0xFFFFFFFF;
    uint32_t placeholderColor = 0x888888FF;
    uint32_t cursorColor = 0xFFFFFFFF;
    uint32_t selectionColor = 0x4444AAAA;
    uint32_t borderColor = 0x666666FF;
    uint32_t focusBorderColor = 0x4488FFFF;
    float borderWidth = 2.0f;
};

/**
 * @brief Single-line text input widget
 *
 * Features:
 * - Text editing with cursor
 * - Text selection (future)
 * - Input filtering (numbers only, max length, etc.)
 * - Password mode (mask characters)
 * - Horizontal scroll for long text
 * - Placeholder text
 * - Copy/paste (future)
 *
 * Events Published:
 * - ui:text_changed → {widgetId, text}
 * - ui:text_submit → {widgetId, text} (Enter pressed)
 * - ui:focus_gained → {widgetId}
 * - ui:focus_lost → {widgetId}
 */
class UITextInput : public UITextEditWidget {
public:
    UITextInput() = default;
    ~UITextInput() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "textinput"; }
    // Libère les SIX entrées supplémentaires (la base ne lâche que le fond + les enfants) : sans ça,
    // cacher un champ laisse son texte, son curseur et son surlignage affichés. Verrouillé par IT_067.
    void releaseRenderEntries(UIRenderer& renderer) override;

    /**
     * @brief Check if a point is inside this text input
     */
    bool containsPoint(float px, float py) const;

    /**
     * @brief Handle mouse button event (for focus)
     * @return true if event was consumed
     */
    bool onMouseButton(int button, bool pressed, float x, float y) override;

    // Opaque au clic : delegue au predicat deja present sur ce widget.
    bool absorbsPoint(float x, float y) const override { return containsPoint(x, y); }

    /**
     * @brief Handle keyboard input when focused
     * @param keyCode Key code
     * @param character Unicode character (if printable)
     * @param ctrl Ctrl key modifier
     * @param shift Shift modifier — étend la sélection au lieu de déplacer le curseur
     * @return true if event was consumed
     */
    bool onKeyInput(int keyCode, uint32_t character, bool ctrl, bool shift = false) override;

    // ------------------------------------------------------------------
    // Sélection.
    //
    // MODÈLE : un unique `selectionAnchor` face à `cursorPosition`. La sélection est l'intervalle
    // [min, max) ; ancre == curseur signifie AUCUNE sélection. Un seul état à tenir cohérent, au
    // lieu d'un couple début/fin + un booléen « active » qu'il faudrait synchroniser à chaque
    // opération. L'ancre est le bord FIXE (celui posé au début du geste), le curseur le bord
    // mobile — c'est ce qui permet d'étendre dans les deux sens.
    // ------------------------------------------------------------------
    bool hasSelection() const { return edit.hasSelection(); }
    int selectionStart() const { return edit.selectionStart(); }
    int selectionEnd()   const { return edit.selectionEnd(); }

    /** @brief Annule la sélection en laissant le curseur où il est. */
    void clearSelection() { edit.clearSelection(); }

    /** @brief Sélectionne tout le contenu (Ctrl+A). */
    void selectAll();

    /**
     * @brief Texte actuellement sélectionné ("" s'il n'y a pas de sélection).
     *
     * Rend le texte RÉEL, jamais la version masquée du mode mot de passe : copier des astérisques
     * n'aurait aucun sens. (Un champ qui doit interdire la copie de son contenu doit le refuser
     * explicitement — c'est une décision de sécurité, pas un effet de bord du masquage.)
     */
    std::string selectedText() const override;

    /**
     * @brief Efface la sélection s'il y en a une. Retourne true si quelque chose a été supprimé.
     *
     * Point de passage unique de toute suppression d'intervalle : frappe, Backspace, Suppr et
     * Couper y convergent, pour qu'ils ne puissent pas diverger sur les cas limites (sélection
     * vide, bornes inversées, curseur laissé hors du texte).
     */
    bool deleteSelection() override;

    /**
     * @brief Gain focus (start receiving keyboard input)
     */
    void gainFocus() override;

    /**
     * @brief Lose focus (stop receiving keyboard input)
     */
    void loseFocus() override;

    /**
     * @brief Insert text at cursor position
     */
    void insertText(const std::string& str);

    /**
     * @brief Insert a (possibly multi-character) committed string, honoring the field's
     *        filter and maxLength. Returns true if anything was inserted.
     *
     * Used for the input:keyboard:text path (IME commit / paste / multi-byte UTF-8) where
     * a single event carries more than one character. With filter None the whole string
     * is inserted; with a restrictive filter only the passing (ASCII) characters are kept.
     */
    bool insertFilteredText(const std::string& str) override;

    /**
     * @brief Delete character before cursor (backspace)
     */
    void deleteCharBefore();

    /**
     * @brief Delete character after cursor (delete)
     */
    void deleteCharAfter();

    /**
     * @brief Move cursor left/right
     */
    void moveCursor(int offset);

    /**
     * @brief Set cursor to specific position
     */
    void setCursorPosition(int pos);

    /**
     * @brief Get visible text with scroll offset applied
     */
    std::string getVisibleText() const;

    /**
     * @brief Calculate pixel offset for cursor
     */
    float getCursorPixelOffset() const;

    // ------------------------------------------------------------------
    // LE MODÈLE D'ÉDITION — source de vérité du contenu, du curseur et de la sélection.
    //
    // Le widget est une VUE : il dessine, écoute la souris et le clavier, applique ses politiques de
    // présentation (filtre de saisie, mode mot de passe), et délègue TOUTE l'édition au modèle. C'est
    // ce qui lui permet de partager mot pour mot sa logique avec UITextArea au lieu d'en tenir une
    // seconde copie, condamnée à diverger.
    //
    // Les méthodes historiques (insertText, deleteCharBefore, moveCursor…) restent, en simples
    // relais : l'API publique du widget ne bouge pas, donc rien de ce qui l'utilise n'a à changer.
    // ------------------------------------------------------------------
    text::EditModel edit;

    /** @brief Contenu du champ. */
    const std::string& text() const override { return edit.text(); }

    // Contrat de saisie : un champ MONOLIGNE soumet sur Entree seule.
    const std::string& submitAction() const override { return onSubmit; }
    // Entree traverse le widget (elle n'insere rien) ; la soumission SUIT la frappe.
    bool swallowsSubmitKey() const override { return false; }
    bool submitsOn(int keyCode, bool /*ctrl*/) const override {
        return keyCode == 13 || keyCode == 10;
    }
    /** @brief Remplace le contenu (chargement JSON, binding, setState) ; curseur en fin. */
    void setText(const std::string& value) { edit.setText(value); }

    std::string placeholder = "Enter text...";
    TextInputFilter filter = TextInputFilter::None;
    bool passwordMode = false;
    bool enabled = true;
    float fontSize = 16.0f;
    std::string onSubmit;  // Action to publish on Enter

    // State-specific styles
    TextInputStyle normalStyle;
    TextInputStyle focusedStyle;
    TextInputStyle disabledStyle;

    // Current state
    TextInputState state = TextInputState::Normal;
    bool isFocused = false;
    // Position du curseur : lecture seule ici, la vérité est dans `edit`.
    int cursorPosition() const { return edit.cursor(); }
    bool draggingSelection = false;  // un appui a démarré un glisser-sélectionner dans ce champ
    float scrollOffset = 0.0f;     // Horizontal scroll for long text

    // DOUBLE-CLIC — sélection du mot.
    // Le widget mesure lui-même l'intervalle entre deux appuis (il reçoit deltaTime à chaque frame)
    // plutôt que d'attendre un événement « double-clic » du backend : InputModule publie des appuis
    // bruts, et faire remonter la notion de double-clic jusqu'à SDL obligerait chaque backend à
    // s'accorder sur le même seuil. La décision reste donc côté UI, où elle est testable.
    static constexpr float DOUBLE_CLICK_SECONDS = 0.4f;
    static constexpr float DOUBLE_CLICK_SLOP_PX = 4.0f;  // au-delà, c'est un nouveau clic ailleurs
    float timeSinceLastClick = 1.0e9f;  // grand = « aucun clic récent »
    float lastClickX = 0.0f;

    // Cursor blink animation
    float cursorBlinkTimer = 0.0f;
    bool cursorVisible = true;
    static constexpr float CURSOR_BLINK_INTERVAL = 0.5f;

    // Text measurement.
    // CHAR_WIDTH est le REPLI monospace historique, utilisé uniquement tant qu'aucune table d'avances
    // n'est arrivée (hôte sans renderer, tests headless, frames d'avant le chargement de police).
    // Dès que le renderer pousse `render:font:metrics`, la mesure passe par les avances RÉELLES —
    // sans quoi, sous une police proportionnelle, le curseur dérive du texte dessiné.
    static constexpr float CHAR_WIDTH = 8.0f;  // Average character width (fallback only)
    static constexpr float CURSOR_WIDTH = 2.0f;
    static constexpr float PADDING = 8.0f;

    // Métriques de la police courante, prêtées par UIContext à chaque update(). Non-possédant :
    // le contexte survit au widget. nullptr / table vide => repli CHAR_WIDTH.
    const text::Metrics* metrics = nullptr;

    // 9-slice FRAME — see UIFrame. Dresses the FIELD box; replaces both the flat bg and the border
    // rect (the border is what a nine-patch expresses natively). Tinted by the state bgColor, so the
    // focused/unfocused feedback re-tints the art for free.
    UIFrame frame;
    uint32_t m_frameId = 0;      // 9-slice entry — registered ONLY if a frame is ever active
    bool m_frameRegistered = false;

private:
    /**
     * @brief Get the appropriate style for current state
     */
    const TextInputStyle& getCurrentStyle() const;

    /**
     * @brief Check if character passes filter
     */
    bool passesFilter(uint32_t ch) const;

    /**
     * @brief Get display text (masked if password mode)
     */
    std::string getDisplayText() const;

    /**
     * @brief Update scroll offset to keep cursor visible
     */
    void updateScrollOffset();

    // Retained mode render IDs (m_renderId from base class is used for background)
    uint32_t m_borderRenderId = 0;       // Border element
    uint32_t m_textRenderId = 0;         // Text content element
    uint32_t m_placeholderRenderId = 0;  // Placeholder text element
    uint32_t m_cursorRenderId = 0;       // Cursor element
    uint32_t m_selectionRenderId = 0;    // Surlignage de sélection (DERRIÈRE le texte)

    /** @brief Index de caractère sous une abscisse ÉCRAN (clic, glisser). */
    int indexAtScreenX(float screenX) const;

    /**
     * @brief Largeur du préfixe de `shown` jusqu'à l'octet `index`, à la taille affichée.
     *
     * Unique endroit qui convertit un index en pixels : le curseur ET les deux bords du surlignage
     * en dépendent, donc les faire passer par la même fonction est ce qui garantit qu'un surlignage
     * ne peut pas se décaler du curseur qui l'a produit.
     */
    float measureTextTo(const std::string& shown, int index) const;
};

} // namespace grove
