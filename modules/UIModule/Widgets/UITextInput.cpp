#include "UITextInput.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <grove/text/TextMetrics.h>
#include <grove/text/TextWords.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <spdlog/spdlog.h>

namespace grove {

void UITextInput::update(UIContext& ctx, float deltaTime) {
    // Emprunte les avances de la police courante au contexte. Elles arrivent du renderer sur
    // `render:font:metrics` et peuvent changer en cours de partie (render:font) — on relit donc la
    // référence à chaque frame plutôt que de la capturer une fois à la construction.
    metrics = &ctx.fontMetrics;

    // Horloge du double-clic. Saturée pour qu'un champ resté longtemps inactif ne finisse pas par
    // déborder en float après des heures de jeu.
    if (timeSinceLastClick < 1.0e6f) timeSinceLastClick += deltaTime;

    // GLISSER-SÉLECTIONNER : tant que le bouton reste enfoncé après un appui dans ce champ, on suit
    // le pointeur avec le curseur — l'ancre étant restée là où l'appui a eu lieu, l'intervalle
    // balayé EST la sélection. Le geste vit ici plutôt que dans onMouseButton parce qu'il s'étend
    // sur plusieurs frames ; c'est le même patron que le drag du slider.
    // Le drag n'est armé que par un appui SUR ce champ, donc deux champs ne peuvent pas se disputer
    // le pointeur, et un glisser parti d'ailleurs ne vient pas sélectionner ici.
    if (draggingSelection) {
        if (ctx.mouseDown) {
            const int idx = indexAtScreenX(ctx.mouseX);
            if (idx != cursorPosition()) {
                edit.setCursor(idx, /*extend=*/true);   // l'ancre ne bouge pas : la sélection s'étend
                updateScrollOffset();
                cursorBlinkTimer = 0.0f;
                cursorVisible = true;
            }
        } else {
            draggingSelection = false;    // filet de sécurité si le relâchement n'est pas passé ici
        }
    }

    // Update state based on enabled/focused flags
    if (!enabled) {
        state = TextInputState::Disabled;
        isFocused = false;
    } else if (isFocused) {
        state = TextInputState::Focused;

        // Update cursor blink animation
        cursorBlinkTimer += deltaTime;
        if (cursorBlinkTimer >= CURSOR_BLINK_INTERVAL) {
            cursorBlinkTimer = 0.0f;
            cursorVisible = !cursorVisible;
        }
    } else {
        state = TextInputState::Normal;
        cursorVisible = false;
    }

    // Update children (text inputs typically don't have children, but support it)
    updateChildren(ctx, deltaTime);
}

void UITextInput::render(UIRenderer& renderer) {
    // Register with renderer on first render (need 5 entries: bg, border, text, placeholder, cursor)
    if (!m_registered) {
        m_renderId = renderer.registerEntry();            // Background
        m_borderRenderId = renderer.registerEntry();      // Border
        m_textRenderId = renderer.registerEntry();        // Text content
        m_placeholderRenderId = renderer.registerEntry(); // Placeholder text
        m_cursorRenderId = renderer.registerEntry();      // Cursor
        m_selectionRenderId = renderer.registerEntry();   // Surlignage de sélection
        m_registered = true;
        // Set destroy callback to unregister all entries
        setDestroyCallback([&renderer,
                           borderId = m_borderRenderId,
                           textId = m_textRenderId,
                           placeholderId = m_placeholderRenderId,
                           cursorId = m_cursorRenderId,
                           selectionId = m_selectionRenderId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(borderId);
            renderer.unregisterEntry(textId);
            renderer.unregisterEntry(placeholderId);
            renderer.unregisterEntry(cursorId);
            renderer.unregisterEntry(selectionId);
        });
    }

    const TextInputStyle& style = getCurrentStyle();

    // QUOI : toutes les couches de ce widget sont réservées ICI, d'un coup, dans l'ordre de
    //   profondeur voulu — et NON au fil des branches d'affichage.
    //
    // POURQUOI (bug réel, trouvé par IT_063) : le renderer retained FIGE la couche d'une entrée à sa
    //   PREMIÈRE publication (`// Keep original layer (don't update it)`, UIRenderer.cpp). Or ce
    //   widget publiait ses entrées cachées avec un `0` littéral en couche — et la toute première
    //   frame passe par la branche « placeholder » (champ vide, non focalisé), donc le TEXTE, le
    //   CURSEUR et le surlignage étaient enregistrés à la couche 0... définitivement. Le fond du
    //   champ étant à ~1001, ces trois éléments se retrouvaient DERRIÈRE lui.
    //   Conséquence jamais remarquée : le curseur de saisie n'était en fait jamais visible. (Le
    //   texte, lui, s'en sortait par accident — il part dans TextPass, une passe qui s'exécute
    //   après SpritePass quelles que soient les couches ; le curseur, un rect, n'a pas cette chance.)
    //
    // COMMENT : réserver les six couches dans un ordre fixe rend la profondeur indépendante de la
    //   branche prise à la première frame. Le surlignage passe entre le cadre et le texte : au-dessus
    //   du fond (donc visible), sous le texte (donc lisible).
    const int bgLayer        = renderer.nextLayer();
    const int borderLayer    = renderer.nextLayer();
    const int selectionLayer = renderer.nextLayer();
    const int textLayer      = renderer.nextLayer();
    const int placeholderLayer = renderer.nextLayer();
    const int cursorLayer    = renderer.nextLayer();

    // 9-slice CHROME: one composed border replaces BOTH the flat background and the border strip —
    // a field box is exactly what a nine-patch draws well. Tinted by the state bgColor, so focus
    // feedback re-tints the art. Lazy registration: a frameless input costs nothing extra.
    if (frame.active()) {
        if (!m_frameRegistered) {
            m_frameId = renderer.registerEntry();
            m_frameRegistered = true;
        }
        frame.emit(renderer, m_frameId, absX, absY, width, height, style.bgColor, bgLayer);
        renderer.updateRect(m_renderId, 0, 0, 0, 0, 0, bgLayer);          // flat bg idle
        renderer.updateRect(m_borderRenderId, 0, 0, 0, 0, 0, borderLayer); // border idle
    } else {
        // Flat look: background, then the border strip along the bottom edge.
        renderer.updateRect(m_renderId, absX, absY, width, height, style.bgColor, bgLayer);
        const uint32_t borderColor = isFocused ? style.focusBorderColor : style.borderColor;
        renderer.updateRect(m_borderRenderId, absX, absY + height - style.borderWidth,
                            width, style.borderWidth, borderColor, borderLayer);
        if (m_frameRegistered) UIFrame::collapse(renderer, m_frameId, bgLayer);
    }

    // Calculate text area
    float textX = absX + PADDING;
    float textY = absY + height * 0.5f;

    // Render text or placeholder
    bool showPlaceholder = text().empty() && !placeholder.empty() && !isFocused;

    if (showPlaceholder) {
        // Show placeholder, hide text and cursor.
        // NB : même caché, chaque élément est publié avec SA couche définitive — c'est cette première
        // publication qui la fige (cf. le bloc de réservation ci-dessus).
        renderer.updateText(m_placeholderRenderId, textX, textY, placeholder,
                           fontSize, style.placeholderColor, placeholderLayer);
        renderer.updateText(m_textRenderId, 0, 0, "", fontSize, 0, textLayer);
        renderer.updateRect(m_cursorRenderId, 0, 0, 0, 0, 0, cursorLayer);
        renderer.updateRect(m_selectionRenderId, 0, 0, 0, 0, 0, selectionLayer);
    } else {
        // Show actual text, hide placeholder
        renderer.updateText(m_placeholderRenderId, 0, 0, "", fontSize, 0, placeholderLayer);

        // SURLIGNAGE DE SÉLECTION — émis AVANT le texte pour qu'il passe DESSOUS : les couches sont
        // attribuées dans l'ordre d'émission (nextLayer), donc l'ordre du code EST l'ordre de
        // profondeur. `selectionColor` existait dans le style depuis toujours et n'avait jamais
        // servi — la sélection n'existait pas.
        if (hasSelection()) {
            const std::string shown = getDisplayText();
            const float x0 = measureTextTo(shown, selectionStart());
            const float x1 = measureTextTo(shown, selectionEnd());
            renderer.updateRect(m_selectionRenderId,
                                textX + x0 - scrollOffset, absY + PADDING,
                                x1 - x0, height - 2 * PADDING,
                                style.selectionColor, selectionLayer);
        } else {
            renderer.updateRect(m_selectionRenderId, 0, 0, 0, 0, 0, selectionLayer);
        }

        std::string visibleText = getVisibleText();

        if (!visibleText.empty()) {
            renderer.updateText(m_textRenderId, textX - scrollOffset, textY, visibleText,
                               fontSize, style.textColor, textLayer);
        } else {
            renderer.updateText(m_textRenderId, 0, 0, "", fontSize, 0, textLayer);
        }

        // Render cursor if focused and visible
        if (isFocused && cursorVisible) {
            float cursorX = textX + getCursorPixelOffset() - scrollOffset;
            renderer.updateRect(m_cursorRenderId, cursorX, absY + PADDING,
                               CURSOR_WIDTH, height - 2 * PADDING,
                               style.cursorColor, cursorLayer);
        } else {
            // Hide cursor — replié à taille nulle, mais TOUJOURS sur sa couche définitive.
            renderer.updateRect(m_cursorRenderId, 0, 0, 0, 0, 0, cursorLayer);
        }
    }

    // Render children on top
    renderChildren(renderer);
}

bool UITextInput::containsPoint(float px, float py) const {
    return px >= absX && px < absX + width &&
           py >= absY && py < absY + height;
}

int UITextInput::indexAtScreenX(float screenX) const {
    // Ramène une abscisse ÉCRAN dans l'espace du texte (origine à absX + PADDING, décalée du
    // défilement) puis demande à la police quelle frontière de caractère est la plus proche.
    // indexAtX ne rend jamais un index au milieu d'un codepoint : un clic sur un mot accentué ne
    // peut donc pas casser la frappe suivante. Sans table d'avances (pas de renderer, tests
    // headless), on retombe sur la division monospace.
    const std::string shown = getDisplayText();
    const float localX = screenX - (absX + PADDING) + scrollOffset;

    if (metrics != nullptr && !metrics->empty()) {
        return static_cast<int>(metrics->indexAtX(shown, localX, fontSize));
    }
    const int approx = static_cast<int>((localX + CHAR_WIDTH * 0.5f) / CHAR_WIDTH);
    return std::clamp(approx, 0, static_cast<int>(shown.size()));
}

void UITextInput::selectAll() { edit.selectAll(); }

std::string UITextInput::selectedText() const { return edit.selectedText(); }

bool UITextInput::deleteSelection() {
    // Relais vers le modèle ; seul le défilement reste une affaire de vue.
    if (!edit.deleteSelection()) return false;
    updateScrollOffset();
    return true;
}

bool UITextInput::onMouseButton(int button, bool pressed, float x, float y) {
    if (!enabled) return false;

    // Relâchement : fin d'un éventuel glisser-sélectionner. On ne touche PAS à la sélection — elle
    // est le résultat du geste et doit survivre au relâchement.
    if (button == 0 && !pressed) {
        draggingSelection = false;
        return false;  // rien à consommer : le focus a déjà été pris à l'appui
    }

    if (button == 0 && pressed) {  // Left mouse button down
        if (containsPoint(x, y)) {
            // QUOI : place le curseur sous le clic (le TODO d'origine : on ne savait que prendre le
            //   focus, donc on ne pouvait éditer qu'en bout de chaîne, aux flèches).
            // COMMENT : on ramène le x écran dans l'espace du texte — origine à absX + PADDING, et
            //   décalé du défilement horizontal — puis on demande à la police quelle frontière de
            //   caractère est la plus proche. indexAtX ne rend jamais un index au milieu d'un
            //   codepoint, donc un clic sur un mot accentué ne peut pas casser la frappe suivante.
            //   Sans table d'avances (pas de renderer, tests headless), on retombe sur la division
            //   monospace — approximatif, mais c'est déjà mieux que « toujours à la fin ».
            const bool isDoubleClick = (timeSinceLastClick <= DOUBLE_CLICK_SECONDS) &&
                                       (std::abs(x - lastClickX) <= DOUBLE_CLICK_SLOP_PX);
            timeSinceLastClick = 0.0f;
            lastClickX = x;

            setCursorPosition(indexAtScreenX(x));

            if (isDoubleClick) {
                // DOUBLE-CLIC : sélectionner le MOT sous le curseur. La segmentation est pure et
                // partagée (grove::text::wordBoundsAt) — elle traite les lettres accentuées comme des
                // caractères de mot, sinon un double-clic sur « café » ne prendrait que « caf ».
                // Ses bornes tombent toujours sur des frontières de codepoint : la sélection ne peut
                // donc pas couper un accent en deux.
                const std::string shown = getDisplayText();
                const text::WordRange w =
                    text::wordBoundsAt(shown, static_cast<size_t>(cursorPosition()));
                if (!w.empty()) {
                    edit.selectWordAt(cursorPosition());
                    updateScrollOffset();
                }
                draggingSelection = false;  // un double-clic ne s'étend pas au glisser
            } else {
                // L'appui POSE l'ancre et arme le glisser : tant que le bouton reste enfoncé,
                // update() déplacera le curseur, donc l'intervalle balayé devient la sélection. Un
                // clic net (appui + relâchement sans bouger) laisse ancre == curseur, c'est-à-dire
                // AUCUNE sélection — pas de cas particulier à écrire pour ça.
                edit.clearSelection();   // un clic net ne sélectionne rien : l'ancre suit le curseur
                draggingSelection = true;
            }

            cursorBlinkTimer = 0.0f;  // le curseur doit être VISIBLE là où on vient de cliquer
            cursorVisible = true;
            return true;  // Will trigger focus in UIModule
        }
    }

    return false;
}

bool UITextInput::onKeyInput(int keyCode, uint32_t character, bool ctrl, bool shift) {
    if (!isFocused || !enabled) return false;

    // Reset cursor blink on input
    cursorBlinkTimer = 0.0f;
    cursorVisible = true;

    // La règle « flèche nue vs flèche avec Maj » vit maintenant DANS le modèle
    // (EditModel::moveCursor / setCursor avec `extend`) : avec Maj le curseur bouge et l'ancre reste,
    // sans Maj sur une sélection existante la première pression se contente de la REPLIER sur le bord
    // visé. La factoriser là-bas est ce qui garantit que le futur textarea se comportera pareil.
    // Ici ne reste que le défilement horizontal, qui est une affaire de vue.

    // Handle special keys
    // Key codes (SDL-like): Backspace=8, Delete=127, Enter=13, Left=37, Right=39, Home=36, End=35

    if (keyCode == 8) {  // Backspace — sur une sélection, efface l'INTERVALLE (géré par le modèle)
        deleteCharBefore();
        return true;
    }
    else if (keyCode == 127) {  // Delete
        deleteCharAfter();
        return true;
    }
    else if (keyCode == 13 || keyCode == 10) {  // Enter/Return
        // Submit action - will be published by UIModule
        return true;
    }
    else if (keyCode == 37) {  // Left arrow
        edit.moveCursor(-1, shift);
        updateScrollOffset();
        return true;
    }
    else if (keyCode == 39) {  // Right arrow
        edit.moveCursor(1, shift);
        updateScrollOffset();
        return true;
    }
    else if (keyCode == 36) {  // Home — champ MONOLIGNE : début de ligne == début du texte
        edit.moveToTextStart(shift);
        updateScrollOffset();
        return true;
    }
    else if (keyCode == 35) {  // End
        edit.moveToTextEnd(shift);
        updateScrollOffset();
        return true;
    }
    else if (ctrl && character == 'a') {
        selectAll();
        return true;
    }

    // Handle printable characters
    if (character >= 32 && character < 127) {
        // Un raccourci Ctrl+lettre n'est PAS une frappe : sans ce garde, Ctrl+B insérerait un 'b'.
        if (ctrl) return false;
        if (passesFilter(character)) {
            std::string charStr(1, static_cast<char>(character));
            insertText(charStr);
            return true;
        }
    }

    return false;
}

void UITextInput::gainFocus() {
    if (!isFocused) {
        isFocused = true;
        cursorBlinkTimer = 0.0f;
        cursorVisible = true;
        spdlog::info("🎯 UITextInput '{}' gainFocus() called - isFocused={}", id, isFocused);
    }
}

void UITextInput::loseFocus() {
    if (isFocused) {
        isFocused = false;
        cursorVisible = false;
    }
}

void UITextInput::insertText(const std::string& str) {
    edit.insert(str);
    updateScrollOffset();
}

bool UITextInput::insertFilteredText(const std::string& str) {
    // QUOI : insère une chaîne entière (commit IME / coller / UTF-8 multi-octets) en respectant le
    //   FILTRE du champ.
    // POURQUOI le filtre reste ici et non dans le modèle : « ce champ n'accepte que des chiffres »
    //   est une politique de présentation, propre à ce widget ; le modèle, lui, édite du texte. Un
    //   textarea ou une console de debug n'en veulent pas.
    // COMMENT : filtre None → on passe tout d'un bloc au modèle ; filtre restrictif → on ne garde que
    //   les caractères imprimables qui passent (ASCII ; le filtrage par codepoint Unicode reste un
    //   follow-up). Le modèle décide ensuite du remplacement de sélection et de maxLength.
    if (str.empty()) return false;

    std::string toInsert;
    if (filter == TextInputFilter::None) {
        toInsert = str;
    } else {
        for (char c : str) {
            const uint32_t ch = static_cast<unsigned char>(c);
            if (ch >= 32 && passesFilter(ch)) toInsert += c;
        }
        if (toInsert.empty()) return false;
    }

    const bool changed = edit.insert(toInsert);
    updateScrollOffset();
    return changed;
}

void UITextInput::deleteCharBefore() {
    if (edit.deleteBefore()) updateScrollOffset();
}

void UITextInput::deleteCharAfter() {
    if (edit.deleteAfter()) updateScrollOffset();
}

void UITextInput::moveCursor(int offset) {
    edit.moveCursor(offset);
    updateScrollOffset();
}

void UITextInput::setCursorPosition(int pos) {
    edit.setCursor(pos);
    updateScrollOffset();
}

std::string UITextInput::getVisibleText() const {
    std::string displayText = getDisplayText();

    // Simple approach: return full text (scrolling handled by offset)
    // In a real implementation, we'd clip to visible characters only
    return displayText;
}

float UITextInput::measureTextTo(const std::string& shown, int index) const {
    const size_t idx = static_cast<size_t>(std::max(0, index));
    if (metrics != nullptr && !metrics->empty()) {
        return metrics->xAtIndex(shown, idx, fontSize);
    }
    return std::min(idx, shown.size()) * CHAR_WIDTH;
}

float UITextInput::getCursorPixelOffset() const {
    // QUOI : position pixel du curseur, mesurée sur le texte RÉELLEMENT affiché.
    // POURQUOI : l'ancienne version rendait `cursorPosition * CHAR_WIDTH`, soit une hypothèse
    //   monospace 8px héritée de la police intégrée. Sous une TTF proportionnelle (un 'i' plus étroit
    //   qu'un 'M'), le curseur dérivait du point d'insertion, de plus en plus à mesure qu'on tape.
    // COMMENT : on mesure le préfixe jusqu'au curseur avec les avances de la police courante. Le
    //   texte mesuré est celui qui est DESSINÉ (donc masqué en mode mot de passe), sinon le curseur
    //   d'un champ de mot de passe suivrait le texte clair.
    //   Table absente/vide (pas de renderer, tests headless) => repli monospace strictement identique
    //   à l'ancien comportement.
    return measureTextTo(getDisplayText(), cursorPosition());
}

const TextInputStyle& UITextInput::getCurrentStyle() const {
    switch (state) {
        case TextInputState::Focused:
            return focusedStyle;
        case TextInputState::Disabled:
            return disabledStyle;
        case TextInputState::Normal:
        default:
            return normalStyle;
    }
}

bool UITextInput::passesFilter(uint32_t ch) const {
    switch (filter) {
        case TextInputFilter::None:
            return true;

        case TextInputFilter::Alphanumeric:
            return std::isalnum(ch);

        case TextInputFilter::Numeric:
            return std::isdigit(ch) || ch == '-';  // Allow negative numbers

        case TextInputFilter::Float:
            return std::isdigit(ch) || ch == '.' || ch == '-';

        case TextInputFilter::NoSpaces:
            return !std::isspace(ch);

        default:
            return true;
    }
}

std::string UITextInput::getDisplayText() const {
    if (passwordMode && !text().empty()) {
        // Mask all characters
        return std::string(text().length(), '*');
    }
    return text();
}

void UITextInput::updateScrollOffset() {
    float cursorPixelPos = getCursorPixelOffset();
    float textAreaWidth = width - 2 * PADDING;

    // Marge de confort au bord droit : on garde environ une largeur de caractère derrière le curseur
    // pour que le prochain glyphe ne naisse pas collé au cadre. Avec une police réelle on prend la
    // largeur de l'espace À LA TAILLE AFFICHÉE — le CHAR_WIDTH figé donnait une marge dérisoire à
    // grande taille de police et exagérée à petite.
    const float margin = (metrics != nullptr && !metrics->empty())
                             ? metrics->advanceOf(static_cast<uint32_t>(' '), fontSize)
                             : CHAR_WIDTH;

    // Scroll to keep cursor visible
    if (cursorPixelPos - scrollOffset > textAreaWidth - margin) {
        // Cursor would be off the right edge
        scrollOffset = cursorPixelPos - textAreaWidth + margin;
    } else if (cursorPixelPos < scrollOffset) {
        // Cursor would be off the left edge
        scrollOffset = cursorPixelPos;
    }

    // Clamp scroll offset
    scrollOffset = std::max(0.0f, scrollOffset);
}

} // namespace grove
