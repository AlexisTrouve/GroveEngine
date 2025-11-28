#pragma once

#include "../Core/UIWidget.h"
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
class UITextInput : public UIWidget {
public:
    UITextInput() = default;
    ~UITextInput() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "textinput"; }

    /**
     * @brief Check if a point is inside this text input
     */
    bool containsPoint(float px, float py) const;

    /**
     * @brief Handle mouse button event (for focus)
     * @return true if event was consumed
     */
    bool onMouseButton(int button, bool pressed, float x, float y);

    /**
     * @brief Handle keyboard input when focused
     * @param keyCode Key code
     * @param character Unicode character (if printable)
     * @param ctrl Ctrl key modifier
     * @return true if event was consumed
     */
    bool onKeyInput(int keyCode, uint32_t character, bool ctrl);

    /**
     * @brief Gain focus (start receiving keyboard input)
     */
    void gainFocus();

    /**
     * @brief Lose focus (stop receiving keyboard input)
     */
    void loseFocus();

    /**
     * @brief Insert text at cursor position
     */
    void insertText(const std::string& str);

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

    // Text input properties
    std::string text;
    std::string placeholder = "Enter text...";
    int maxLength = 256;
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
    int cursorPosition = 0;        // Index in text string
    float scrollOffset = 0.0f;     // Horizontal scroll for long text

    // Cursor blink animation
    float cursorBlinkTimer = 0.0f;
    bool cursorVisible = true;
    static constexpr float CURSOR_BLINK_INTERVAL = 0.5f;

    // Text measurement (approximate)
    static constexpr float CHAR_WIDTH = 8.0f;  // Average character width
    static constexpr float CURSOR_WIDTH = 2.0f;
    static constexpr float PADDING = 8.0f;

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
};

} // namespace grove
