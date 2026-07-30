#include <grove/IDataNode.h>
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
    metricsEpoch = ctx.fontMetricsEpoch;
    // La mise en page doit être à jour AVANT tout calcul de position (le glisser ci-dessous en
    // dépend), donc elle est rafraîchie en tête d'update et non au moment du rendu.
    refreshLayout();

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

// ============================================================================
// Mise en page : du texte logique aux LIGNES VISUELLES
// ============================================================================

void UITextArea::refreshLayout() {
    // QUOI : recalcule le découpage en lignes visuelles si (et seulement si) quelque chose dont il
    //   dépend a changé.
    // POURQUOI la signature EXACTE plutôt qu'une heuristique : une mise en page périmée est un bug
    //   purement visuel — le texte s'affiche à la mauvaise place, le clic vise à côté — et aucun test
    //   headless ne l'attrape si l'on ne sait pas déjà quoi comparer. C'est le risque documenté de
    //   tout cache de layout (cf. ui-framework.md §8). On préfère donc quatre comparaisons exactes.
    // COMMENT : la révision du texte est O(1) (compteur du modèle) ; largeur, taille de police et
    //   époque des métriques couvrent les trois autres entrées du calcul.
    const float usable = std::max(0.0f, width - 2 * PADDING);
    const uint32_t epoch = metricsEpoch;

    if (m_layoutRevision == edit.revision() &&
        m_layoutWidth == usable &&
        m_layoutFontSize == fontSize &&
        m_layoutMetricsEpoch == epoch &&
        m_layoutWrap == wrap) {
        return;
    }

    // `maxWidth <= 0` signifie « aucun repli » côté wrapText : c'est exactement ce que veut
    // `wrap: false`, sans chemin de code particulier.
    const float budget = wrap ? usable : 0.0f;

    if (metrics != nullptr && !metrics->empty()) {
        const text::Metrics* m = metrics;
        const float size = fontSize;
        m_visual = text::wrapText(edit.text(), budget,
                                  [m, size](uint32_t cp) { return m->advanceOf(cp, size); });
    } else {
        // Repli monospace, comme partout ailleurs dans ces widgets.
        m_visual = text::wrapText(edit.text(), budget, [](uint32_t) { return CHAR_WIDTH; });
    }

    m_layoutRevision = edit.revision();
    m_layoutWidth = usable;
    m_layoutFontSize = fontSize;
    m_layoutMetricsEpoch = epoch;
    m_layoutWrap = wrap;
}

std::string UITextArea::visualLineText(int v) const {
    if (v < 0 || v >= static_cast<int>(m_visual.size())) return "";
    const text::VisualLine& line = m_visual[static_cast<size_t>(v)];
    return edit.text().substr(line.begin, line.length());
}

float UITextArea::cursorXInVisualLine() const {
    const size_t v = text::visualLineAt(m_visual, static_cast<size_t>(edit.cursor()));
    const text::VisualLine& line = m_visual[v];
    const size_t into = static_cast<size_t>(edit.cursor()) > line.begin
                            ? static_cast<size_t>(edit.cursor()) - line.begin : 0;
    return measureInLine(visualLineText(static_cast<int>(v)), std::min(into, line.length()));
}

void UITextArea::moveCursorByVisualLine(int delta, bool extend) {
    // QUOI : monter/descendre d'une ligne VISUELLE — ce que l'utilisateur voit bouger.
    // POURQUOI pas les lignes logiques : avec le repli, une ligne logique peut occuper cinq rangées ;
    //   une flèche Bas qui sauterait la ligne logique entière ferait bondir le curseur de cinq
    //   rangées d'un coup. La navigation suit donc ce qui est AFFICHÉ.
    // COMMENT : on conserve l'ABSCISSE (et non un numéro de colonne) puis on redemande l'index à
    //   cette abscisse dans la ligne d'arrivée — sous une police proportionnelle, c'est ce qui garde
    //   le curseur visuellement aligné, ce qu'un décompte de caractères ne ferait pas.
    if (m_visual.empty()) return;

    const size_t cur = text::visualLineAt(m_visual, static_cast<size_t>(edit.cursor()));
    const int target = static_cast<int>(cur) + delta;

    if (target < 0) { edit.moveToTextStart(extend); return; }
    if (target >= static_cast<int>(m_visual.size())) { edit.moveToTextEnd(extend); return; }

    const float x = cursorXInVisualLine();
    const text::VisualLine& dst = m_visual[static_cast<size_t>(target)];
    const std::string dstText = visualLineText(target);

    size_t col;
    if (metrics != nullptr && !metrics->empty()) {
        col = metrics->indexAtX(dstText, x, fontSize);
    } else {
        const int approx = static_cast<int>((x + CHAR_WIDTH * 0.5f) / CHAR_WIDTH);
        col = static_cast<size_t>(std::clamp(approx, 0, static_cast<int>(dstText.size())));
    }
    edit.setCursor(static_cast<int>(dst.begin + col), extend);
}

void UITextArea::moveToVisualLineStart(bool extend) {
    if (m_visual.empty()) return;
    const size_t v = text::visualLineAt(m_visual, static_cast<size_t>(edit.cursor()));
    edit.setCursor(static_cast<int>(m_visual[v].begin), extend);
}

void UITextArea::moveToVisualLineEnd(bool extend) {
    if (m_visual.empty()) return;
    const size_t v = text::visualLineAt(m_visual, static_cast<size_t>(edit.cursor()));
    edit.setCursor(static_cast<int>(m_visual[v].end), extend);
}

void UITextArea::visibleLineRange(int& first, int& last) const {
    const float usable = std::max(0.0f, height - 2 * PADDING);
    first = std::max(0, static_cast<int>(scrollY / lineHeight));
    const int visibleCount = static_cast<int>(usable / lineHeight) + 1;
    const int total = static_cast<int>(m_visual.size());
    last = std::min(total > 0 ? total - 1 : 0, first + visibleCount);
}

int UITextArea::indexAtScreenPos(float screenX, float screenY) const {
    // QUOI : convertit un point écran en index d'octet.
    // COMMENT : la ligne VISUELLE vient du Y (division par la hauteur de ligne, défilement compris),
    //   la colonne du X, mesurée DANS cette ligne visuelle seulement. Passer par indexAtX garantit une
    //   frontière de codepoint — un clic ne peut donc pas couper un accent.
    if (m_visual.empty()) return 0;

    const float localY = screenY - (absY + PADDING) + scrollY;
    int v = static_cast<int>(localY / lineHeight);
    v = std::clamp(v, 0, static_cast<int>(m_visual.size()) - 1);

    const text::VisualLine& line = m_visual[static_cast<size_t>(v)];
    const std::string lineText = visualLineText(v);

    const float localX = screenX - (absX + PADDING);
    size_t col;
    if (metrics != nullptr && !metrics->empty()) {
        col = metrics->indexAtX(lineText, localX, fontSize);
    } else {
        const int approx = static_cast<int>((localX + CHAR_WIDTH * 0.5f) / CHAR_WIDTH);
        col = static_cast<size_t>(std::clamp(approx, 0, static_cast<int>(lineText.size())));
    }
    return static_cast<int>(line.begin + col);
}

void UITextArea::ensureCursorVisible() {
    // Défilement vertical seulement : on ramène la LIGNE du curseur dans la boîte. Le défilement
    // horizontal est hors périmètre (cf. l'en-tête) — une ligne trop large est coupée par le clip.
    const int line = static_cast<int>(text::visualLineAt(m_visual, static_cast<size_t>(edit.cursor())));
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
    refreshLayout();      // le texte a changé : le repli aussi
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
        case 8:   edit.deleteBefore(); refreshLayout(); ensureCursorVisible(); return true;
        case 127: edit.deleteAfter();  refreshLayout(); ensureCursorVisible(); return true;

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
        case 36: moveToVisualLineStart(shift);     ensureCursorVisible(); return true;
        case 35: moveToVisualLineEnd(shift);       ensureCursorVisible(); return true;
        case 38: moveCursorByVisualLine(-1, shift); ensureCursorVisible(); return true;
        case 40: moveCursorByVisualLine(1, shift);  ensureCursorVisible(); return true;
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
            refreshLayout();
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

    refreshLayout();   // le rendu ne doit jamais dessiner une mise en page perimee

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
        const int v = firstLine + static_cast<int>(slot);

        if (showPlaceholder || v > lastLine || v >= static_cast<int>(m_visual.size())) {
            // Emplacement inutilisé cette frame : replié à taille nulle, mais TOUJOURS sur sa couche.
            renderer.updateText(e.textId, 0, 0, "", fontSize, 0, textLayer);
            renderer.updateRect(e.selectionId, 0, 0, 0, 0, 0, selectionLayer);
            continue;
        }

        const text::VisualLine& line = m_visual[static_cast<size_t>(v)];
        const std::string lineText = visualLineText(v);
        const float y = absY + PADDING + v * lineHeight - scrollY;

        // Surlignage : intersection de la sélection avec CETTE ligne visuelle. Une sélection qui
        // traverse un repli devient donc un bandeau par rangée, ce qui est exactement ce qu'on veut
        // voir. On borne sur `end` (texte dessiné) et non sur `next`, pour ne pas surligner les
        // espaces avalées par la coupe.
        const int lineFrom = static_cast<int>(line.begin);
        const int lineTo = static_cast<int>(line.end);
        if (hasSel && selTo > lineFrom && selFrom < lineTo) {
            const size_t from = static_cast<size_t>(std::max(selFrom, lineFrom)) - line.begin;
            const size_t to   = static_cast<size_t>(std::min(selTo, lineTo)) - line.begin;
            const float x0 = measureInLine(lineText, from);
            const float x1 = measureInLine(lineText, to);
            renderer.updateRect(e.selectionId, absX + PADDING + x0, y,
                                std::max(1.0f, x1 - x0), lineHeight,
                                style.selectionColor, selectionLayer);
        } else {
            renderer.updateRect(e.selectionId, 0, 0, 0, 0, 0, selectionLayer);
        }

        // Le texte est ancré au CENTRE vertical de sa rangée (convention de render:text).
        renderer.updateText(e.textId, absX + PADDING, y + lineHeight * 0.5f, lineText,
                            fontSize, style.textColor, textLayer);
    }

    if (isFocused && cursorVisible && !showPlaceholder) {
        const int v = static_cast<int>(text::visualLineAt(m_visual, static_cast<size_t>(edit.cursor())));
        const float cx = cursorXInVisualLine();
        const float cy = absY + PADDING + v * lineHeight - scrollY;
        renderer.updateRect(m_cursorRenderId, absX + PADDING + cx, cy,
                            CURSOR_WIDTH, lineHeight, style.cursorColor, cursorLayer);
    } else {
        renderer.updateRect(m_cursorRenderId, 0, 0, 0, 0, 0, cursorLayer);
    }

    renderChildren(renderer);
    renderer.popClip();
}


// Champ de saisie MULTILIGNE. Partage le modele d'edition et les styles du champ monoligne ; ce
// qui change est la VUE (une entree de rendu par ligne visible) et la semantique d'Entree (elle
// insere un saut de ligne, donc la soumission passe a Ctrl+Entree).
std::unique_ptr<UIWidget> UITextArea::fromNode(const IDataNode& node) {
    auto area = std::make_unique<UITextArea>();
    area->setText(node.getString("text", ""));
    area->placeholder = node.getString("placeholder", "");
    area->edit.maxLength = node.getInt("maxLength", 4096);   // une zone de texte est plus longue
    area->onSubmit = node.getString("onSubmit", "");
    area->wrap = node.getBool("wrap", true);   // repli automatique par defaut

    const std::string filterStr = node.getString("filter", "none");
    if (filterStr == "alphanumeric")   area->filter = TextInputFilter::Alphanumeric;
    else if (filterStr == "numeric")   area->filter = TextInputFilter::Numeric;
    else if (filterStr == "float")     area->filter = TextInputFilter::Float;
    else if (filterStr == "nospaces")  area->filter = TextInputFilter::NoSpaces;
    else                               area->filter = TextInputFilter::None;

    auto& mutableNode = const_cast<IDataNode&>(node);
    if (auto* style = mutableNode.getChildReadOnly("style")) {
        auto colour = [&](const char* key, uint32_t fallback) -> uint32_t {
            const std::string v = style->getString(key, "");
            if (v.size() >= 2 && (v.substr(0, 2) == "0x" || v.substr(0, 2) == "0X")) {
                return static_cast<uint32_t>(std::stoul(v, nullptr, 16));
            }
            return fallback;
        };
        area->normalStyle.bgColor        = colour("bgColor", area->normalStyle.bgColor);
        area->normalStyle.textColor      = colour("textColor", area->normalStyle.textColor);
        area->normalStyle.selectionColor = colour("selectionColor", area->normalStyle.selectionColor);
        area->normalStyle.cursorColor    = colour("cursorColor", area->normalStyle.cursorColor);
        area->fontSize   = static_cast<float>(style->getDouble("fontSize", 16.0));
        area->lineHeight = static_cast<float>(style->getDouble("lineHeight", area->fontSize + 4.0));
    }
    // Les états focalisé/désactivé héritent du normal, sauf mention contraire — un textarea sans
    // styles explicites reste lisible plutôt que noir sur noir.
    area->focusedStyle = area->normalStyle;
    area->disabledStyle = area->normalStyle;
    return area;
}

}  // namespace grove
