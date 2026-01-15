#include "UIStyle.h"
#include <grove/IDataNode.h>
#include <spdlog/spdlog.h>

namespace grove {

// Static member initialization
WidgetStyle UITheme::s_emptyStyle;

// =============================================================================
// WidgetStyle
// =============================================================================

void WidgetStyle::merge(const WidgetStyle& other) {
    if (other.hasBgColor()) bgColor = other.bgColor;
    if (other.hasTextColor()) textColor = other.textColor;
    if (other.hasBorderColor()) borderColor = other.borderColor;
    if (other.hasAccentColor()) accentColor = other.accentColor;
    if (other.hasFontSize()) fontSize = other.fontSize;
    if (other.hasPadding()) padding = other.padding;
    if (other.hasMargin()) margin = other.margin;
    if (other.hasBorderWidth()) borderWidth = other.borderWidth;
    if (other.hasBorderRadius()) borderRadius = other.borderRadius;
    if (other.handleSize >= 0.0f) handleSize = other.handleSize;
    if (other.boxSize >= 0.0f) boxSize = other.boxSize;
    if (other.spacing >= 0.0f) spacing = other.spacing;
}

void WidgetStyle::parseFromJson(const IDataNode& styleData) {
    // Parse colors (hex strings)
    auto parseColor = [](const IDataNode& node, const std::string& key) -> uint32_t {
        std::string colorStr = node.getString(key, "");
        if (colorStr.size() >= 2 && (colorStr.substr(0, 2) == "0x" || colorStr.substr(0, 2) == "0X")) {
            return static_cast<uint32_t>(std::stoul(colorStr, nullptr, 16));
        }
        return 0;
    };

    bgColor = parseColor(styleData, "bgColor");
    textColor = parseColor(styleData, "textColor");
    borderColor = parseColor(styleData, "borderColor");
    accentColor = parseColor(styleData, "accentColor");

    // Parse sizes
    fontSize = static_cast<float>(styleData.getDouble("fontSize", -1.0));
    padding = static_cast<float>(styleData.getDouble("padding", -1.0));
    margin = static_cast<float>(styleData.getDouble("margin", -1.0));
    borderWidth = static_cast<float>(styleData.getDouble("borderWidth", -1.0));
    borderRadius = static_cast<float>(styleData.getDouble("borderRadius", -1.0));
    handleSize = static_cast<float>(styleData.getDouble("handleSize", -1.0));
    boxSize = static_cast<float>(styleData.getDouble("boxSize", -1.0));
    spacing = static_cast<float>(styleData.getDouble("spacing", -1.0));
}

// =============================================================================
// UITheme
// =============================================================================

void UITheme::setColor(const std::string& name, uint32_t color) {
    m_colors[name] = color;
}

uint32_t UITheme::getColor(const std::string& name) const {
    auto it = m_colors.find(name);
    return (it != m_colors.end()) ? it->second : 0;
}

uint32_t UITheme::resolveColor(const std::string& colorRef) const {
    // Check if it's a color reference (starts with $)
    if (!colorRef.empty() && colorRef[0] == '$') {
        std::string colorName = colorRef.substr(1);
        return getColor(colorName);
    }

    // Otherwise parse as hex color
    if (colorRef.size() >= 2 && (colorRef.substr(0, 2) == "0x" || colorRef.substr(0, 2) == "0X")) {
        return static_cast<uint32_t>(std::stoul(colorRef, nullptr, 16));
    }

    return 0;
}

void UITheme::setWidgetStyle(const std::string& widgetType, const WidgetStyle& style) {
    m_widgetStyles[widgetType] = style;
}

void UITheme::setWidgetVariantStyle(const std::string& widgetType, const std::string& variant, const WidgetStyle& style) {
    m_variantStyles[makeVariantKey(widgetType, variant)] = style;
}

const WidgetStyle& UITheme::getWidgetStyle(const std::string& widgetType) const {
    auto it = m_widgetStyles.find(widgetType);
    return (it != m_widgetStyles.end()) ? it->second : s_emptyStyle;
}

const WidgetStyle& UITheme::getWidgetVariantStyle(const std::string& widgetType, const std::string& variant) const {
    auto it = m_variantStyles.find(makeVariantKey(widgetType, variant));
    return (it != m_variantStyles.end()) ? it->second : s_emptyStyle;
}

bool UITheme::loadFromJson(const IDataNode& themeData) {
    m_name = themeData.getString("name", "unnamed");

    // Load color palette
    auto& mutableTheme = const_cast<IDataNode&>(themeData);
    if (auto* colorsNode = mutableTheme.getChildReadOnly("colors")) {
        auto colorNames = colorsNode->getChildNames();
        for (const auto& colorName : colorNames) {
            if (auto* colorNode = colorsNode->getChildReadOnly(colorName)) {
                std::string colorStr = colorNode->getString("", "");
                if (colorStr.empty()) {
                    // Try as direct value
                    colorStr = colorsNode->getString(colorName, "");
                }
                uint32_t color = resolveColor(colorStr);
                if (color != 0) {
                    setColor(colorName, color);
                }
            }
        }
    }

    // Load widget styles
    auto widgetTypes = {"panel", "label", "button", "image", "slider", "checkbox", "progressbar"};
    for (const auto& widgetType : widgetTypes) {
        if (auto* widgetStyleNode = mutableTheme.getChildReadOnly(widgetType)) {
            WidgetStyle style;
            style.parseFromJson(*widgetStyleNode);

            // Resolve color references
            if (style.hasBgColor() == false) {
                std::string bgColorRef = widgetStyleNode->getString("bgColor", "");
                if (!bgColorRef.empty()) {
                    style.bgColor = resolveColor(bgColorRef);
                }
            }
            if (style.hasTextColor() == false) {
                std::string textColorRef = widgetStyleNode->getString("textColor", "");
                if (!textColorRef.empty()) {
                    style.textColor = resolveColor(textColorRef);
                }
            }
            if (style.hasAccentColor() == false) {
                std::string accentColorRef = widgetStyleNode->getString("accentColor", "");
                if (!accentColorRef.empty()) {
                    style.accentColor = resolveColor(accentColorRef);
                }
            }

            setWidgetStyle(widgetType, style);

            // Load variant styles (hover, pressed, etc.)
            auto variants = {"normal", "hover", "pressed", "disabled", "checked", "unchecked"};
            for (const auto& variant : variants) {
                if (auto* variantNode = widgetStyleNode->getChildReadOnly(variant)) {
                    WidgetStyle variantStyle;
                    variantStyle.parseFromJson(*variantNode);

                    // Resolve color references for variant
                    if (variantStyle.hasBgColor() == false) {
                        std::string bgColorRef = variantNode->getString("bgColor", "");
                        if (!bgColorRef.empty()) {
                            variantStyle.bgColor = resolveColor(bgColorRef);
                        }
                    }

                    setWidgetVariantStyle(widgetType, variant, variantStyle);
                }
            }
        }
    }

    spdlog::info("Theme '{}' loaded with {} colors and {} widget styles",
                 m_name, m_colors.size(), m_widgetStyles.size());

    return true;
}

std::string UITheme::makeVariantKey(const std::string& widgetType, const std::string& variant) {
    return widgetType + ":" + variant;
}

// =============================================================================
// UIStyleManager
// =============================================================================

void UIStyleManager::setTheme(std::unique_ptr<UITheme> theme) {
    m_currentTheme = std::move(theme);
}

WidgetStyle UIStyleManager::resolveStyle(const std::string& widgetType, const WidgetStyle& inlineStyle) const {
    // Start with default
    WidgetStyle resolved = getDefaultStyle(widgetType);

    // Apply theme style if available
    if (m_currentTheme) {
        resolved.merge(m_currentTheme->getWidgetStyle(widgetType));
    }

    // Apply inline style (highest priority)
    resolved.merge(inlineStyle);

    return resolved;
}

WidgetStyle UIStyleManager::resolveVariantStyle(const std::string& widgetType, const std::string& variant, const WidgetStyle& inlineStyle) const {
    // Start with base widget style
    WidgetStyle resolved = resolveStyle(widgetType, WidgetStyle());

    // Apply theme variant style
    if (m_currentTheme) {
        resolved.merge(m_currentTheme->getWidgetVariantStyle(widgetType, variant));
    }

    // Apply inline variant style
    resolved.merge(inlineStyle);

    return resolved;
}

WidgetStyle UIStyleManager::getDefaultStyle(const std::string& widgetType) const {
    WidgetStyle style;

    // Set some sensible defaults per widget type
    if (widgetType == "panel") {
        style.bgColor = 0x333333FF;
        style.padding = 10.0f;
    }
    else if (widgetType == "label") {
        style.textColor = 0xFFFFFFFF;
        style.fontSize = 16.0f;
    }
    else if (widgetType == "button") {
        style.bgColor = 0x444444FF;
        style.textColor = 0xFFFFFFFF;
        style.fontSize = 16.0f;
        style.padding = 10.0f;
    }
    else if (widgetType == "slider") {
        style.bgColor = 0x34495eFF;  // track
        style.accentColor = 0x3498dbFF;  // fill
        style.handleSize = 16.0f;
    }
    else if (widgetType == "checkbox") {
        style.bgColor = 0x34495eFF;  // box
        style.accentColor = 0x2ecc71FF;  // check
        style.textColor = 0xFFFFFFFF;
        style.fontSize = 16.0f;
        style.boxSize = 24.0f;
        style.spacing = 8.0f;
    }
    else if (widgetType == "progressbar") {
        style.bgColor = 0x34495eFF;
        style.accentColor = 0x2ecc71FF;  // fill
        style.textColor = 0xFFFFFFFF;
        style.fontSize = 14.0f;
    }

    return style;
}

} // namespace grove
