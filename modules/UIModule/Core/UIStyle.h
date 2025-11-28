#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <vector>

namespace grove {

class IDataNode;

/**
 * @brief Style properties that can be applied to widgets
 *
 * Contains all visual properties like colors, sizes, padding, etc.
 * Can be partially defined (unset values use parent/default).
 */
struct WidgetStyle {
    // Colors (0 = not set)
    uint32_t bgColor = 0;
    uint32_t textColor = 0;
    uint32_t borderColor = 0;
    uint32_t accentColor = 0;  // For fills, checks, etc.

    // Sizes (-1 = not set)
    float fontSize = -1.0f;
    float padding = -1.0f;
    float margin = -1.0f;
    float borderWidth = -1.0f;
    float borderRadius = -1.0f;

    // Specific widget properties
    float handleSize = -1.0f;     // For sliders
    float boxSize = -1.0f;        // For checkboxes
    float spacing = -1.0f;        // For checkboxes (text spacing)

    /**
     * @brief Check if a property is set
     */
    bool hasBgColor() const { return bgColor != 0; }
    bool hasTextColor() const { return textColor != 0; }
    bool hasBorderColor() const { return borderColor != 0; }
    bool hasAccentColor() const { return accentColor != 0; }
    bool hasFontSize() const { return fontSize >= 0.0f; }
    bool hasPadding() const { return padding >= 0.0f; }
    bool hasMargin() const { return margin >= 0.0f; }
    bool hasBorderWidth() const { return borderWidth >= 0.0f; }
    bool hasBorderRadius() const { return borderRadius >= 0.0f; }

    /**
     * @brief Merge another style on top of this one
     * Only overwrites properties that are set in the other style
     */
    void merge(const WidgetStyle& other);

    /**
     * @brief Parse from JSON data node
     */
    void parseFromJson(const IDataNode& styleData);
};

/**
 * @brief Theme definition with named colors and widget styles
 *
 * A theme contains:
 * - Named color palette (e.g., "primary", "secondary", "background")
 * - Default styles per widget type (e.g., "button", "panel")
 * - Style variants (e.g., "button:hover", "button:pressed")
 */
class UITheme {
public:
    UITheme() = default;
    UITheme(const std::string& name) : m_name(name) {}

    /**
     * @brief Get theme name
     */
    const std::string& getName() const { return m_name; }

    /**
     * @brief Define a named color in the palette
     */
    void setColor(const std::string& name, uint32_t color);

    /**
     * @brief Get a named color from the palette
     * @return Color value, or 0 if not found
     */
    uint32_t getColor(const std::string& name) const;

    /**
     * @brief Resolve color references (e.g., "$primary" -> actual color)
     */
    uint32_t resolveColor(const std::string& colorRef) const;

    /**
     * @brief Set style for a widget type
     * @param widgetType Type of widget (e.g., "button", "panel")
     * @param style Style to apply
     */
    void setWidgetStyle(const std::string& widgetType, const WidgetStyle& style);

    /**
     * @brief Set style for a widget variant
     * @param widgetType Type of widget (e.g., "button")
     * @param variant Variant name (e.g., "hover", "pressed")
     * @param style Style to apply
     */
    void setWidgetVariantStyle(const std::string& widgetType, const std::string& variant, const WidgetStyle& style);

    /**
     * @brief Get style for a widget type
     * @return Style, or empty style if not found
     */
    const WidgetStyle& getWidgetStyle(const std::string& widgetType) const;

    /**
     * @brief Get style for a widget variant
     * @return Style, or empty style if not found
     */
    const WidgetStyle& getWidgetVariantStyle(const std::string& widgetType, const std::string& variant) const;

    /**
     * @brief Load theme from JSON
     */
    bool loadFromJson(const IDataNode& themeData);

private:
    std::string m_name;
    std::unordered_map<std::string, uint32_t> m_colors;
    std::unordered_map<std::string, WidgetStyle> m_widgetStyles;
    std::unordered_map<std::string, WidgetStyle> m_variantStyles;  // Key: "widgetType:variant"

    static WidgetStyle s_emptyStyle;

    /**
     * @brief Make variant key from widget type and variant name
     */
    static std::string makeVariantKey(const std::string& widgetType, const std::string& variant);
};

/**
 * @brief Style manager - holds current theme and provides style resolution
 */
class UIStyleManager {
public:
    UIStyleManager() = default;

    /**
     * @brief Set the current theme
     */
    void setTheme(std::unique_ptr<UITheme> theme);

    /**
     * @brief Get the current theme
     */
    UITheme* getTheme() const { return m_currentTheme.get(); }

    /**
     * @brief Resolve a complete style for a widget
     *
     * Resolution order:
     * 1. Widget inline style
     * 2. Theme widget type style
     * 3. Default style
     *
     * @param widgetType Type of widget
     * @param inlineStyle Style defined inline in JSON
     * @return Resolved style
     */
    WidgetStyle resolveStyle(const std::string& widgetType, const WidgetStyle& inlineStyle) const;

    /**
     * @brief Resolve a variant style
     * @param widgetType Type of widget
     * @param variant Variant name
     * @param inlineStyle Inline variant style
     * @return Resolved style
     */
    WidgetStyle resolveVariantStyle(const std::string& widgetType, const std::string& variant, const WidgetStyle& inlineStyle) const;

private:
    std::unique_ptr<UITheme> m_currentTheme;

    /**
     * @brief Get default style for a widget type
     */
    WidgetStyle getDefaultStyle(const std::string& widgetType) const;
};

} // namespace grove
