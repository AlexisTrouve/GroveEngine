#include "UITextArea.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"

#include <algorithm>
#include <cctype>

namespace grove {

// ============================================================================
// Cycle de vie / état
// ============================================================================

void UITextArea::update(UIContext& ctx, float deltaTime) {
    metrics = &ctx.fontMetrics;

    // GLISSER-SÉLECTIONNER : même patron que le champ monoligne — l'ancre reste à l'appui, le curseur
    // suit le pointeur, donc l'intervalle balayé EST la sélection. Ici le pointeur porte aussi une
    // composante verticale, donc le glisser traverse les lignes.
    if (draggingSelection) {
        if (ctx.mouseDown) {
            const int idx = indexAtScreenPos(ctx.mouseX, ctx.mouseY);
            if (idx != edit.cursor()) {
                edit.setCursor(idx, /*extend=*/true);
                ensureCursorVisible();
                cursorBlinkTimer = 0.0f;
                cursorVisible = true;
            }
        } else {
            draggingSelection = false;
        }
    }

    if (!enabled) {
        state = TextInputState::Disabled;
        isFocused = false;
    } else if (isFocused) {
        state = TextInputState::Focused;
        cursorBlinkTimer += deltaTime;
        if (cursorBlinkTimer >= CURSOR_BLINK_INTERVAL) {
            cursorBlinkTimer = 0.0f;
            cursorVisible = !cursorVisible;
        }
    } else {
        state = TextInputState::Normal;
        cursorVisible = false;
    }

    updateChildren(ctx, deltaTime);
}

void UITextArea::gainFocus() {
    if (!isFocused) {
        isFocused = true;
        cursorBlinkTimer = 0.0f;
        cursorVisible = true;
    }
}

void UITextArea::loseFocus() {
    if (isFocused) {
        isFocused = false;
        cursorVisible = false;
    }
}

const TextInputStyle& UITextArea::getCurrentStyle() const {
    switch (state) {
        case TextInputState::Focused:  return focusedStyle;
        case TextInputState::Disabled: return disabledStyle;
        default:                       return normalStyle;
    }
}

bool UITextArea::passesFilter(uint32_t ch) const {
    switch (filter) {
        case TextInputFilter::Alphanumeric: return std::isalnum(ch) != 0 || ch == '\n';
        case TextInputFilter::Numeric:      return std::isdigit(ch) != 0 || ch == '-' || ch == '\n';
        case TextInputFilter::Float:        return std::isdigit(ch) != 0 || ch == '.' || ch == '-' || ch == '\n';
        case TextInputFilter::NoSpaces:     return std::isspace(ch) == 0 || ch == '\n';
        case TextInputFilter::None:
        default:                            return true;
    }
}

// ============================================================================
// Géométrie
// ============================================================================

void UITextArea::hitClipRect(float& x, float& y, float& w, float& h) const {
    x = absX; y = absY; w = width; h = height;
}

bool UITextArea::containsPoint(float px, float py) const {
    return px >= absX && px < absX + width && py >= absY && py < absY + height;
}

float UITextArea::measureInLine(const std::string& line, size_t bytesIntoLine) const {
    if (metrics != nullptr && !metrics->empty()) {
        return metrics->xAtIndex(line, bytesIntoLine, fontSize);
    }
    return std::min(bytesIntoLine, line.size()) * CHAR_WIDTH;
}

void UITextArea::visibleLineRange(int& first, int& last) const {
    const float usable = std::max(0.0f, height - 2 * PADDING);
    first = std::max(0, static_cast<int>(scrollY / lineHeight));
    const int visibleCount = static_cast<int>(usable / lineHeight) + 1;
    last = std::min(edit.lineCount() - 1, first + visibleCount);
}

int UITextArea::indexAtScreenPos(float screenX, float screenY) const {
    // QUOI : convertit un point écran en index d'octet.
    // COMMENT : la ligne vient du Y (division par la hauteur de ligne, défilement compris), la colonne
    //   du X, mesurée DANS cette ligne seulement. Passer par indexAtX garantit une frontière de
    //   codepoint — un clic ne peut donc pas couper un accent, comme dans le champ monoligne.
    const float localY = screenY - (absY + PADDING) + scrollY;
    int line = static_cast<int>(localY / lineHeight);
    line = std::clamp(line, 0, edit.lineCount() - 1);

    const size_t lineBegin = edit.startOfLine(line);
    const size_t lineFinish = edit.lineEnd(static_cast<int>(lineBegin));
    const std::string lineText = edit.text().substr(lineBegin, lineFinish - lineBegin);

    const float localX = screenX - (absX + PADDING);
    size_t col = 0;
    if (metrics != nullptr && !metrics->empty()) {
        col = metrics->indexAtX(lineText, localX, fontSize);
    } else {
        const int approx = static_cast<int>((localX + CHAR_WIDTH * 0.5f) / CHAR_WIDTH);
        col = static_cast<size_t>(std::clamp(approx, 0, static_cast<int>(lineText.size())));
    }
    return static_cast<int>(lineBegin + col);
}

void UITextArea::ensureCursorVisible() {
    // Défilement vertical seulement : on ramène la LIGNE du curseur dans la boîte. Le défilement
    // horizontal est hors périmètre (cf. l'en-tête) — une ligne trop large est coupée par le clip.
    const int line = edit.lineNumberAt(edit.cursor());
    const float lineTop = line * lineHeight;
    const float usable = std::max(0.0f, height - 2 * PADDING);

    if (lineTop < scrollY) {
        scrollY = lineTop;
    } else if (lineTop + lineHeight > scrollY + usable) {
        scrollY = lineTop + lineHeight - usable;
    }
    scrollY = std::max(0.0f, scrollY);
}

// ============================================================================
// Entrée
// ============================================================================

bool UITextArea::onMouseButton(int button, bool pressed, float x, float y) {
    if (!enabled) return false;

    if (button == 0 && !pressed) {
        draggingSelection = false;
        return false;
    }

    if (button == 0 && pressed && containsPoint(x, y)) {
        edit.setCursor(indexAtScreenPos(x, y));
        edit.clearSelection();
        draggingSelection = true;
        ensureCursorVisible();
        cursorBlinkTimer = 0.0f;
        cursorVisible = true;
        return true;
    }
    return false;
}

bool UITextArea::insertFilteredText(const std::string& str) {
    if (str.empty()) return false;

    std::string toInsert;
    if (filter == TextInputFilter::None) {
        toInsert = str;
    } else {
        for (char c : str) {
            const uint32_t ch = static_cast<unsigned char>(c);
            if ((ch >= 32 || ch == '\n') && passesFilter(ch)) toInsert += c;
        }
        if (toInsert.empty()) return false;
    }

    const bool changed = edit.insert(toInsert);
    ensureCursorVisible();
    return changed;
}

bool UITextArea::onKeyInput(int keyCode, uint32_t character, bool ctrl, bool shift) {
    if (!isFocused || !enabled) return false;

    cursorBlinkTimer = 0.0f;
    cursorVisible = true;

    // Codes d'édition — même dialecte que UITextInput, plus Haut/Bas qui n'ont de sens qu'ici.
    // Backspace=8, Suppr=127, Entrée=13, Gauche=37, Droite=39, Début=36, Fin=35, Haut=38, Bas=40.
    switch (keyCode) {
        case 8:   edit.deleteBefore(); ensureCursorVisible(); return true;
        case 127: edit.deleteAfter();  ensureCursorVisible(); return true;

        case 13:
        case 10:
            // ENTRÉE INSÈRE UN SAUT DE LIGNE — l'attente universelle dans une zone de texte. La
            // SOUMISSION passe donc à Ctrl+Entrée, que l'UIModule reconnaît et transforme en
            // ui:text_submit. On ne consomme pas ici le cas Ctrl : c'est au module de publier.
            if (ctrl) return false;
            edit.insert("\n");
            ensureCursorVisible();
            return true;

        case 37: edit.moveCursor(-1, shift);      ensureCursorVisible(); return true;
        case 39: edit.moveCursor(1, shift);       ensureCursorVisible(); return true;
        // Début/Fin agissent sur la LIGNE (et non sur tout le texte) : c'est la différence de
        // sémantique qui distingue une zone de texte d'un champ monoligne.
        case 36: edit.moveToLineStart(shift);     ensureCursorVisible(); return true;
        case 35: edit.moveToLineEnd(shift);       ensureCursorVisible(); return true;
        case 38: edit.moveCursorByLine(-1, shift); ensureCursorVisible(); return true;
        case 40: edit.moveCursorByLine(1, shift);  ensureCursorVisible(); return true;
        default: break;
    }

    if (ctrl && character == 'a') {
        edit.selectAll();
        return true;
    }

    if (character >= 32 && character < 127) {
        if (ctrl) return false;   // un raccourci n'est pas une frappe
        if (passesFilter(character)) {
            edit.insert(std::string(1, static_cast<char>(character)));
            ensureCursorVisible();
            return true;
        }
    }
    return false;
}

// ============================================================================
// Rendu
// ============================================================================

void UITextArea::releaseRenderEntries(UIRenderer& renderer) {
    for (const LineEntry& e : m_linePool) {
        renderer.unregisterEntry(e.textId);
        renderer.unregisterEntry(e.selectionId);
    }
    m_linePool.clear();
    if (m_cursorRenderId != 0) renderer.unregisterEntry(m_cursorRenderId);
    if (m_placeholderRenderId != 0) renderer.unregisterEntry(m_placeholderRenderId);
    m_cursorRenderId = 0;
    m_placeholderRenderId = 0;
    UIWidget::releaseRenderEntries(renderer);
}

void UITextArea::render(UIRenderer& renderer) {
    if (!m_registered) {
        m_renderId = renderer.registerEntry();            // fond
        m_placeholderRenderId = renderer.registerEntry();
        m_cursorRenderId = renderer.registerEntry();
        m_registered = true;
    }

    const TextInputStyle& style = getCurrentStyle();

    // Couches réservées d'un bloc, dans l'ordre de profondeur — le renderer FIGE la couche d'une
    // entrée à sa première publication, donc une entrée publiée « cachée » avec un 0 littéral y
    // resterait pour toujours (le bug qui rendait le curseur du champ monoligne invisible).
    const int bgLayer        = renderer.nextLayer();
    const int selectionLayer = renderer.nextLayer();
    const int textLayer      = renderer.nextLayer();
    const int cursorLayer    = renderer.nextLayer();

    renderer.updateRect(m_renderId, absX, absY, width, height, style.bgColor, bgLayer);

    // Le contenu est clippé à la boîte : les lignes hors champ ne débordent pas sur les voisins.
    renderer.pushClip(absX, absY, width, height);

    const bool showPlaceholder = edit.text().empty() && !placeholder.empty() && !isFocused;
    if (showPlaceholder) {
        renderer.updateText(m_placeholderRenderId, absX + PADDING, absY + PADDING + lineHeight * 0.5f,
                            placeholder, fontSize, style.placeholderColor, textLayer);
    } else {
        renderer.updateText(m_placeholderRenderId, 0, 0, "", fontSize, 0, textLayer);
    }

    int firstLine = 0, lastLine = 0;
    visibleLineRange(firstLine, lastLine);
    const int needed = std::max(0, lastLine - firstLine + 1);

    // Le pool suit la HAUTEUR de la boîte, jamais le nombre de lignes du texte : un journal de
    // 10 000 lignes n'enregistre qu'une vingtaine d'entrées (même principe que la liste virtualisée).
    while (static_cast<int>(m_linePool.size()) < needed) {
        LineEntry e;
        e.selectionId = renderer.registerEntry();
        e.textId = renderer.registerEntry();
        m_linePool.push_back(e);
    }

    const int selFrom = edit.selectionStart();
    const int selTo = edit.selectionEnd();
    const bool hasSel = edit.hasSelection();

    for (size_t slot = 0; slot < m_linePool.size(); ++slot) {
        const LineEntry& e = m_linePool[slot];
        const int line = firstLine + static_cast<int>(slot);

        if (showPlaceholder || line > lastLine || line >= edit.lineCount()) {
            // Emplacement inutilisé cette frame : replié à taille nulle, mais TOUJOURS sur sa couche.
            renderer.updateText(e.textId, 0, 0, "", fontSize, 0, textLayer);
            renderer.updateRect(e.selectionId, 0, 0, 0, 0, 0, selectionLayer);
            continue;
        }

        const size_t lineBegin = edit.startOfLine(line);
        const size_t lineFinish = edit.lineEnd(static_cast<int>(lineBegin));
        const std::string lineText = edit.text().substr(lineBegin, lineFinish - lineBegin);

        const float y = absY + PADDING + line * lineHeight - scrollY;

        // Surlignage : intersection de la sélection avec CETTE ligne. Une sélection multi-lignes
        // devient donc un bandeau par ligne, ce qui est exactement ce qu'on veut voir.
        if (hasSel && selTo > static_cast<int>(lineBegin) && selFrom < static_cast<int>(lineFinish)) {
            const size_t from = static_cast<size_t>(std::max(selFrom, static_cast<int>(lineBegin))) - lineBegin;
            const size_t to   = static_cast<size_t>(std::min(selTo, static_cast<int>(lineFinish))) - lineBegin;
            const float x0 = measureInLine(lineText, from);
            const float x1 = measureInLine(lineText, to);
            renderer.updateRect(e.selectionId, absX + PADDING + x0, y,
                                std::max(1.0f, x1 - x0), lineHeight,
                                style.selectionColor, selectionLayer);
        } else {
            renderer.updateRect(e.selectionId, 0, 0, 0, 0, 0, selectionLayer);
        }

        // Le texte est ancré au CENTRE vertical de sa ligne (convention de render:text).
        renderer.updateText(e.textId, absX + PADDING, y + lineHeight * 0.5f, lineText,
                            fontSize, style.textColor, textLayer);
    }

    if (isFocused && cursorVisible && !showPlaceholder) {
        const int line = edit.lineNumberAt(edit.cursor());
        const size_t lineBegin = edit.startOfLine(line);
        const size_t lineFinish = edit.lineEnd(static_cast<int>(lineBegin));
        const std::string lineText = edit.text().substr(lineBegin, lineFinish - lineBegin);
        const float cx = measureInLine(lineText, static_cast<size_t>(edit.cursor()) - lineBegin);
        const float cy = absY + PADDING + line * lineHeight - scrollY;
        renderer.updateRect(m_cursorRenderId, absX + PADDING + cx, cy,
                            CURSOR_WIDTH, lineHeight, style.cursorColor, cursorLayer);
    } else {
        renderer.updateRect(m_cursorRenderId, 0, 0, 0, 0, 0, cursorLayer);
    }

    renderChildren(renderer);
    renderer.popClip();
}

}  // namespace grove
