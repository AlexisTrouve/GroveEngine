#include "UITextInput.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <algorithm>
#include <cctype>

namespace grove {

void UITextInput::update(UIContext& ctx, float deltaTime) {
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
    const TextInputStyle& style = getCurrentStyle();

    // Render background
    renderer.drawRect(absX, absY, width, height, style.bgColor);

    // Render border
    uint32_t borderColor = isFocused ? style.focusBorderColor : style.borderColor;
    // TODO: Implement proper border rendering
    // For now, render as thin line at bottom
    renderer.drawRect(absX, absY + height - style.borderWidth,
                     width, style.borderWidth, borderColor);

    // Calculate text area
    float textX = absX + PADDING;
    float textY = absY + height * 0.5f;
    float textAreaWidth = width - 2 * PADDING;

    // Render text or placeholder
    if (text.empty() && !placeholder.empty() && !isFocused) {
        // Show placeholder
        renderer.drawText(textX, textY, placeholder, fontSize, style.placeholderColor);
    } else {
        // Show actual text
        std::string displayText = getDisplayText();
        std::string visibleText = getVisibleText();

        if (!visibleText.empty()) {
            renderer.drawText(textX - scrollOffset, textY, visibleText,
                            fontSize, style.textColor);
        }

        // Render cursor if focused and visible
        if (isFocused && cursorVisible) {
            float cursorX = textX + getCursorPixelOffset() - scrollOffset;
            renderer.drawRect(cursorX, absY + PADDING,
                            CURSOR_WIDTH, height - 2 * PADDING,
                            style.cursorColor);
        }
    }

    // Render children on top
    renderChildren(renderer);
}

bool UITextInput::containsPoint(float px, float py) const {
    return px >= absX && px < absX + width &&
           py >= absY && py < absY + height;
}

bool UITextInput::onMouseButton(int button, bool pressed, float x, float y) {
    if (!enabled) return false;

    if (button == 0 && pressed) {  // Left mouse button down
        if (containsPoint(x, y)) {
            // TODO: Calculate click position and set cursor there
            // For now, just focus
            return true;  // Will trigger focus in UIModule
        }
    }

    return false;
}

bool UITextInput::onKeyInput(int keyCode, uint32_t character, bool ctrl) {
    if (!isFocused || !enabled) return false;

    // Reset cursor blink on input
    cursorBlinkTimer = 0.0f;
    cursorVisible = true;

    // Handle special keys
    // Key codes (SDL-like): Backspace=8, Delete=127, Enter=13, Left=37, Right=39, Home=36, End=35

    if (keyCode == 8) {  // Backspace
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
        moveCursor(-1);
        return true;
    }
    else if (keyCode == 39) {  // Right arrow
        moveCursor(1);
        return true;
    }
    else if (keyCode == 36) {  // Home
        setCursorPosition(0);
        return true;
    }
    else if (keyCode == 35) {  // End
        setCursorPosition(static_cast<int>(text.length()));
        return true;
    }
    else if (ctrl && keyCode == 'a') {
        // Select all (future feature)
        return true;
    }
    else if (ctrl && keyCode == 'c') {
        // Copy (future feature)
        return true;
    }
    else if (ctrl && keyCode == 'v') {
        // Paste (future feature)
        return true;
    }

    // Handle printable characters
    if (character >= 32 && character < 127) {
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
    }
}

void UITextInput::loseFocus() {
    if (isFocused) {
        isFocused = false;
        cursorVisible = false;
    }
}

void UITextInput::insertText(const std::string& str) {
    if (text.length() + str.length() > static_cast<size_t>(maxLength)) {
        return;  // Would exceed max length
    }

    text.insert(cursorPosition, str);
    cursorPosition += static_cast<int>(str.length());
    updateScrollOffset();
}

void UITextInput::deleteCharBefore() {
    if (cursorPosition > 0) {
        text.erase(cursorPosition - 1, 1);
        cursorPosition--;
        updateScrollOffset();
    }
}

void UITextInput::deleteCharAfter() {
    if (cursorPosition < static_cast<int>(text.length())) {
        text.erase(cursorPosition, 1);
        updateScrollOffset();
    }
}

void UITextInput::moveCursor(int offset) {
    int newPos = cursorPosition + offset;
    newPos = std::clamp(newPos, 0, static_cast<int>(text.length()));
    setCursorPosition(newPos);
}

void UITextInput::setCursorPosition(int pos) {
    cursorPosition = std::clamp(pos, 0, static_cast<int>(text.length()));
    updateScrollOffset();
}

std::string UITextInput::getVisibleText() const {
    std::string displayText = getDisplayText();

    // Simple approach: return full text (scrolling handled by offset)
    // In a real implementation, we'd clip to visible characters only
    return displayText;
}

float UITextInput::getCursorPixelOffset() const {
    // Approximate pixel position of cursor
    return cursorPosition * CHAR_WIDTH;
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
    if (passwordMode && !text.empty()) {
        // Mask all characters
        return std::string(text.length(), '*');
    }
    return text;
}

void UITextInput::updateScrollOffset() {
    float cursorPixelPos = getCursorPixelOffset();
    float textAreaWidth = width - 2 * PADDING;

    // Scroll to keep cursor visible
    if (cursorPixelPos - scrollOffset > textAreaWidth - CHAR_WIDTH) {
        // Cursor would be off the right edge
        scrollOffset = cursorPixelPos - textAreaWidth + CHAR_WIDTH;
    } else if (cursorPixelPos < scrollOffset) {
        // Cursor would be off the left edge
        scrollOffset = cursorPixelPos;
    }

    // Clamp scroll offset
    scrollOffset = std::max(0.0f, scrollOffset);
}

} // namespace grove
