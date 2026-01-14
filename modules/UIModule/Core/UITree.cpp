#include "UITree.h"
#include "UILayout.h"
#include "../Widgets/UIPanel.h"
#include "../Widgets/UILabel.h"
#include "../Widgets/UIButton.h"
#include "../Widgets/UIImage.h"
#include "../Widgets/UISlider.h"
#include "../Widgets/UICheckbox.h"
#include "../Widgets/UIProgressBar.h"
#include "../Widgets/UITextInput.h"
#include "../Widgets/UIScrollPanel.h"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <string>

namespace grove {

UITree::UITree() {
    registerDefaultWidgets();
}

void UITree::registerWidget(const std::string& type, WidgetFactory factory) {
    m_factories[type] = std::move(factory);
}

void UITree::registerDefaultWidgets() {
    // Register panel factory
    registerWidget("panel", [](const IDataNode& node) -> std::unique_ptr<UIWidget> {
        auto panel = std::make_unique<UIPanel>();

        // Parse style (const_cast safe for read-only operations)
        auto& mutableNode = const_cast<IDataNode&>(node);
        if (auto* style = mutableNode.getChildReadOnly("style")) {
            std::string bgColorStr = style->getString("bgColor", "0x333333FF");
            if (bgColorStr.size() >= 2 && (bgColorStr.substr(0, 2) == "0x" || bgColorStr.substr(0, 2) == "0X")) {
                panel->bgColor = static_cast<uint32_t>(std::stoul(bgColorStr, nullptr, 16));
            }
            panel->borderRadius = static_cast<float>(style->getDouble("borderRadius", 0.0));
        }

        return panel;
    });

    // Register label factory
    registerWidget("label", [](const IDataNode& node) -> std::unique_ptr<UIWidget> {
        auto label = std::make_unique<UILabel>();
        label->text = node.getString("text", "");

        // Parse style (const_cast safe for read-only operations)
        auto& mutableNode = const_cast<IDataNode&>(node);
        if (auto* style = mutableNode.getChildReadOnly("style")) {
            std::string colorStr = style->getString("color", "0xFFFFFFFF");
            if (colorStr.size() >= 2 && (colorStr.substr(0, 2) == "0x" || colorStr.substr(0, 2) == "0X")) {
                label->color = static_cast<uint32_t>(std::stoul(colorStr, nullptr, 16));
            }
            label->fontSize = static_cast<float>(style->getDouble("fontSize", 16.0));
        }

        return label;
    });

    // Register button factory
    registerWidget("button", [](const IDataNode& node) -> std::unique_ptr<UIWidget> {
        auto button = std::make_unique<UIButton>();
        button->text = node.getString("text", "");
        button->onClick = node.getString("onClick", "");
        button->enabled = node.getBool("enabled", true);

        // Helper lambda to parse a button style
        auto parseButtonStyle = [](IDataNode* styleNode, ButtonStyle& style) {
            if (!styleNode) return;

            std::string bgColorStr = styleNode->getString("bgColor", "");
            if (bgColorStr.size() >= 2 && (bgColorStr.substr(0, 2) == "0x" || bgColorStr.substr(0, 2) == "0X")) {
                style.bgColor = static_cast<uint32_t>(std::stoul(bgColorStr, nullptr, 16));
            }
            std::string textColorStr = styleNode->getString("textColor", "");
            if (textColorStr.size() >= 2 && (textColorStr.substr(0, 2) == "0x" || textColorStr.substr(0, 2) == "0X")) {
                style.textColor = static_cast<uint32_t>(std::stoul(textColorStr, nullptr, 16));
            }
            std::string borderColorStr = styleNode->getString("borderColor", "");
            if (borderColorStr.size() >= 2 && (borderColorStr.substr(0, 2) == "0x" || borderColorStr.substr(0, 2) == "0X")) {
                style.borderColor = static_cast<uint32_t>(std::stoul(borderColorStr, nullptr, 16));
            }
            style.borderWidth = static_cast<float>(styleNode->getDouble("borderWidth", style.borderWidth));
            style.borderRadius = static_cast<float>(styleNode->getDouble("borderRadius", style.borderRadius));
            style.textureId = styleNode->getInt("textureId", 0);
            style.useTexture = style.textureId > 0;
            if (style.textureId > 0) {
                spdlog::info("UIButton style parsed: textureId={}, useTexture={}", style.textureId, style.useTexture);
            }
        };

        // Parse style (const_cast safe for read-only operations)
        auto& mutableNode = const_cast<IDataNode&>(node);
        if (auto* style = mutableNode.getChildReadOnly("style")) {
            // Normal style
            if (auto* normalStyle = style->getChildReadOnly("normal")) {
                parseButtonStyle(normalStyle, button->normalStyle);
            }

            // Hover style
            if (auto* hoverStyle = style->getChildReadOnly("hover")) {
                parseButtonStyle(hoverStyle, button->hoverStyle);
                button->hoverStyleSet = true;
            }

            // Pressed style
            if (auto* pressedStyle = style->getChildReadOnly("pressed")) {
                parseButtonStyle(pressedStyle, button->pressedStyle);
                button->pressedStyleSet = true;
            }

            // Disabled style
            if (auto* disabledStyle = style->getChildReadOnly("disabled")) {
                parseButtonStyle(disabledStyle, button->disabledStyle);
            }

            // Font size from style root
            button->fontSize = static_cast<float>(style->getDouble("fontSize", 16.0));
        }

        // Auto-generate hover/pressed styles if not explicitly set
        button->generateDefaultStyles();

        return button;
    });

    // Register image factory
    registerWidget("image", [](const IDataNode& node) -> std::unique_ptr<UIWidget> {
        auto image = std::make_unique<UIImage>();
        image->textureId = node.getInt("textureId", 0);
        image->texturePath = node.getString("texturePath", "");

        auto& mutableNode = const_cast<IDataNode&>(node);
        if (auto* style = mutableNode.getChildReadOnly("style")) {
            std::string tintStr = style->getString("tintColor", "0xFFFFFFFF");
            if (tintStr.size() >= 2 && (tintStr.substr(0, 2) == "0x" || tintStr.substr(0, 2) == "0X")) {
                image->tintColor = static_cast<uint32_t>(std::stoul(tintStr, nullptr, 16));
            }
        }

        return image;
    });

    // Register slider factory
    registerWidget("slider", [](const IDataNode& node) -> std::unique_ptr<UIWidget> {
        auto slider = std::make_unique<UISlider>();
        slider->minValue = static_cast<float>(node.getDouble("min", 0.0));
        slider->maxValue = static_cast<float>(node.getDouble("max", 100.0));
        slider->value = static_cast<float>(node.getDouble("value", 50.0));
        slider->step = static_cast<float>(node.getDouble("step", 0.0));
        slider->horizontal = node.getBool("horizontal", true);
        slider->onChange = node.getString("onChange", "");

        auto& mutableNode = const_cast<IDataNode&>(node);
        if (auto* style = mutableNode.getChildReadOnly("style")) {
            std::string trackColorStr = style->getString("trackColor", "0x34495eFF");
            if (trackColorStr.size() >= 2 && (trackColorStr.substr(0, 2) == "0x" || trackColorStr.substr(0, 2) == "0X")) {
                slider->trackColor = static_cast<uint32_t>(std::stoul(trackColorStr, nullptr, 16));
            }
            std::string fillColorStr = style->getString("fillColor", "0x3498dbFF");
            if (fillColorStr.size() >= 2 && (fillColorStr.substr(0, 2) == "0x" || fillColorStr.substr(0, 2) == "0X")) {
                slider->fillColor = static_cast<uint32_t>(std::stoul(fillColorStr, nullptr, 16));
            }
            std::string handleColorStr = style->getString("handleColor", "0xecf0f1FF");
            if (handleColorStr.size() >= 2 && (handleColorStr.substr(0, 2) == "0x" || handleColorStr.substr(0, 2) == "0X")) {
                slider->handleColor = static_cast<uint32_t>(std::stoul(handleColorStr, nullptr, 16));
            }
            slider->handleSize = static_cast<float>(style->getDouble("handleSize", 16.0));
        }

        return slider;
    });

    // Register checkbox factory
    registerWidget("checkbox", [](const IDataNode& node) -> std::unique_ptr<UIWidget> {
        auto checkbox = std::make_unique<UICheckbox>();
        checkbox->checked = node.getBool("checked", false);
        checkbox->text = node.getString("text", "");
        checkbox->onChange = node.getString("onChange", "");

        auto& mutableNode = const_cast<IDataNode&>(node);
        if (auto* style = mutableNode.getChildReadOnly("style")) {
            std::string boxColorStr = style->getString("boxColor", "0x34495eFF");
            if (boxColorStr.size() >= 2 && (boxColorStr.substr(0, 2) == "0x" || boxColorStr.substr(0, 2) == "0X")) {
                checkbox->boxColor = static_cast<uint32_t>(std::stoul(boxColorStr, nullptr, 16));
            }
            std::string checkColorStr = style->getString("checkColor", "0x2ecc71FF");
            if (checkColorStr.size() >= 2 && (checkColorStr.substr(0, 2) == "0x" || checkColorStr.substr(0, 2) == "0X")) {
                checkbox->checkColor = static_cast<uint32_t>(std::stoul(checkColorStr, nullptr, 16));
            }
            std::string textColorStr = style->getString("textColor", "0xecf0f1FF");
            if (textColorStr.size() >= 2 && (textColorStr.substr(0, 2) == "0x" || textColorStr.substr(0, 2) == "0X")) {
                checkbox->textColor = static_cast<uint32_t>(std::stoul(textColorStr, nullptr, 16));
            }
            checkbox->boxSize = static_cast<float>(style->getDouble("boxSize", 24.0));
            checkbox->fontSize = static_cast<float>(style->getDouble("fontSize", 16.0));
            checkbox->spacing = static_cast<float>(style->getDouble("spacing", 8.0));
        }

        return checkbox;
    });

    // Register progressbar factory
    registerWidget("progressbar", [](const IDataNode& node) -> std::unique_ptr<UIWidget> {
        auto progressBar = std::make_unique<UIProgressBar>();
        progressBar->setProgress(static_cast<float>(node.getDouble("progress", 0.5)));
        progressBar->horizontal = node.getBool("horizontal", true);
        progressBar->showText = node.getBool("showText", false);

        auto& mutableNode = const_cast<IDataNode&>(node);
        if (auto* style = mutableNode.getChildReadOnly("style")) {
            std::string bgColorStr = style->getString("bgColor", "0x34495eFF");
            if (bgColorStr.size() >= 2 && (bgColorStr.substr(0, 2) == "0x" || bgColorStr.substr(0, 2) == "0X")) {
                progressBar->bgColor = static_cast<uint32_t>(std::stoul(bgColorStr, nullptr, 16));
            }
            std::string fillColorStr = style->getString("fillColor", "0x2ecc71FF");
            if (fillColorStr.size() >= 2 && (fillColorStr.substr(0, 2) == "0x" || fillColorStr.substr(0, 2) == "0X")) {
                progressBar->fillColor = static_cast<uint32_t>(std::stoul(fillColorStr, nullptr, 16));
            }
            std::string textColorStr = style->getString("textColor", "0xFFFFFFFF");
            if (textColorStr.size() >= 2 && (textColorStr.substr(0, 2) == "0x" || textColorStr.substr(0, 2) == "0X")) {
                progressBar->textColor = static_cast<uint32_t>(std::stoul(textColorStr, nullptr, 16));
            }
            progressBar->fontSize = static_cast<float>(style->getDouble("fontSize", 14.0));
        }

        return progressBar;
    });

    // Register textinput factory
    registerWidget("textinput", [](const IDataNode& node) -> std::unique_ptr<UIWidget> {
        auto textInput = std::make_unique<UITextInput>();
        textInput->text = node.getString("text", "");
        textInput->placeholder = node.getString("placeholder", "Enter text...");
        textInput->maxLength = node.getInt("maxLength", 256);
        textInput->passwordMode = node.getBool("passwordMode", false);
        textInput->onSubmit = node.getString("onSubmit", "");

        // Parse filter type
        std::string filterStr = node.getString("filter", "none");
        if (filterStr == "alphanumeric") {
            textInput->filter = TextInputFilter::Alphanumeric;
        } else if (filterStr == "numeric") {
            textInput->filter = TextInputFilter::Numeric;
        } else if (filterStr == "float") {
            textInput->filter = TextInputFilter::Float;
        } else if (filterStr == "nospaces") {
            textInput->filter = TextInputFilter::NoSpaces;
        } else {
            textInput->filter = TextInputFilter::None;
        }

        auto& mutableNode = const_cast<IDataNode&>(node);
        if (auto* style = mutableNode.getChildReadOnly("style")) {
            // Normal style
            std::string bgColorStr = style->getString("bgColor", "0x222222FF");
            if (bgColorStr.size() >= 2 && (bgColorStr.substr(0, 2) == "0x" || bgColorStr.substr(0, 2) == "0X")) {
                textInput->normalStyle.bgColor = static_cast<uint32_t>(std::stoul(bgColorStr, nullptr, 16));
            }
            std::string textColorStr = style->getString("textColor", "0xFFFFFFFF");
            if (textColorStr.size() >= 2 && (textColorStr.substr(0, 2) == "0x" || textColorStr.substr(0, 2) == "0X")) {
                textInput->normalStyle.textColor = static_cast<uint32_t>(std::stoul(textColorStr, nullptr, 16));
            }
            std::string borderColorStr = style->getString("borderColor", "0x666666FF");
            if (borderColorStr.size() >= 2 && (borderColorStr.substr(0, 2) == "0x" || borderColorStr.substr(0, 2) == "0X")) {
                textInput->normalStyle.borderColor = static_cast<uint32_t>(std::stoul(borderColorStr, nullptr, 16));
            }
            std::string focusBorderColorStr = style->getString("focusBorderColor", "0x4488FFFF");
            if (focusBorderColorStr.size() >= 2 && (focusBorderColorStr.substr(0, 2) == "0x" || focusBorderColorStr.substr(0, 2) == "0X")) {
                textInput->normalStyle.focusBorderColor = static_cast<uint32_t>(std::stoul(focusBorderColorStr, nullptr, 16));
            }

            // Copy normal style to focused and disabled
            textInput->focusedStyle = textInput->normalStyle;
            textInput->disabledStyle = textInput->normalStyle;
            textInput->disabledStyle.bgColor = 0x111111FF;
            textInput->disabledStyle.textColor = 0x666666FF;

            textInput->fontSize = static_cast<float>(style->getDouble("fontSize", 16.0));
        }

        return textInput;
    });

    // Register scrollpanel factory
    registerWidget("scrollpanel", [](const IDataNode& node) -> std::unique_ptr<UIWidget> {
        auto scrollPanel = std::make_unique<UIScrollPanel>();

        scrollPanel->scrollVertical = node.getBool("scrollVertical", true);
        scrollPanel->scrollHorizontal = node.getBool("scrollHorizontal", false);
        scrollPanel->showScrollbar = node.getBool("showScrollbar", true);
        scrollPanel->dragToScroll = node.getBool("dragToScroll", true);

        // Parse style
        auto& mutableNode = const_cast<IDataNode&>(node);
        if (auto* style = mutableNode.getChildReadOnly("style")) {
            std::string bgColorStr = style->getString("bgColor", "0x2a2a2aFF");
            if (bgColorStr.size() >= 2 && (bgColorStr.substr(0, 2) == "0x" || bgColorStr.substr(0, 2) == "0X")) {
                scrollPanel->bgColor = static_cast<uint32_t>(std::stoul(bgColorStr, nullptr, 16));
            }

            std::string borderColorStr = style->getString("borderColor", "0x444444FF");
            if (borderColorStr.size() >= 2 && (borderColorStr.substr(0, 2) == "0x" || borderColorStr.substr(0, 2) == "0X")) {
                scrollPanel->borderColor = static_cast<uint32_t>(std::stoul(borderColorStr, nullptr, 16));
            }

            std::string scrollbarColorStr = style->getString("scrollbarColor", "0x666666FF");
            if (scrollbarColorStr.size() >= 2 && (scrollbarColorStr.substr(0, 2) == "0x" || scrollbarColorStr.substr(0, 2) == "0X")) {
                scrollPanel->scrollbarColor = static_cast<uint32_t>(std::stoul(scrollbarColorStr, nullptr, 16));
            }

            scrollPanel->borderWidth = static_cast<float>(style->getDouble("borderWidth", 1.0));
            scrollPanel->scrollbarWidth = static_cast<float>(style->getDouble("scrollbarWidth", 8.0));
        }

        return scrollPanel;
    });
}

std::unique_ptr<UIWidget> UITree::loadFromJson(const IDataNode& layoutData) {
    m_root = parseWidget(layoutData);
    if (m_root) {
        m_root->computeAbsolutePosition();
    }
    return std::move(m_root);
}

UIWidget* UITree::findById(const std::string& id) {
    if (!m_root) return nullptr;
    return m_root->findById(id);
}

std::unique_ptr<UIWidget> UITree::parseWidget(const IDataNode& node) {
    std::string type = node.getString("type", "");
    if (type.empty()) {
        spdlog::warn("UITree: Widget missing 'type' property");
        return nullptr;
    }

    auto it = m_factories.find(type);
    if (it == m_factories.end()) {
        spdlog::warn("UITree: Unknown widget type '{}'", type);
        return nullptr;
    }

    // Create widget via factory
    auto widget = it->second(node);
    if (!widget) {
        spdlog::warn("UITree: Factory failed for type '{}'", type);
        return nullptr;
    }

    // Parse common properties
    parseCommonProperties(widget.get(), node);

    // Parse children recursively (const_cast safe for read-only operations)
    auto& mutableNode = const_cast<IDataNode&>(node);
    if (auto* children = mutableNode.getChildReadOnly("children")) {
        auto childNames = children->getChildNames();
        for (const auto& childName : childNames) {
            if (auto* childNode = children->getChildReadOnly(childName)) {
                if (auto child = parseWidget(*childNode)) {
                    widget->addChild(std::move(child));
                }
            }
        }
    }

    // Also check for array-style children (indexed by number)
    // JsonDataNode stores array elements as children with numeric keys
    int childIndex = 0;
    while (true) {
        std::string childKey = std::to_string(childIndex);
        // Check if there's a child with this numeric key inside "children"
        if (auto* childrenNode = mutableNode.getChildReadOnly("children")) {
            if (auto* childNode = childrenNode->getChildReadOnly(childKey)) {
                if (auto child = parseWidget(*childNode)) {
                    widget->addChild(std::move(child));
                }
                childIndex++;
                continue;
            }
        }
        break;
    }

    return widget;
}

void UITree::parseCommonProperties(UIWidget* widget, const IDataNode& node) {
    widget->id = node.getString("id", "");
    widget->tooltip = node.getString("tooltip", "");
    widget->x = static_cast<float>(node.getDouble("x", 0.0));
    widget->y = static_cast<float>(node.getDouble("y", 0.0));
    widget->width = static_cast<float>(node.getDouble("width", 0.0));
    widget->height = static_cast<float>(node.getDouble("height", 0.0));
    widget->visible = node.getBool("visible", true);

    // Parse layout properties (Phase 2)
    auto& mutableNode = const_cast<IDataNode&>(node);
    if (auto* layout = mutableNode.getChildReadOnly("layout")) {
        parseLayoutProperties(widget, *layout);
    }

    // Parse flex property (can be at root level)
    if (node.hasChild("flex")) {
        widget->layoutProps.flex = static_cast<float>(node.getDouble("flex", 0.0));
    }
}

void UITree::parseLayoutProperties(UIWidget* widget, const IDataNode& layoutNode) {
    // Layout mode
    std::string modeStr = layoutNode.getString("type", "absolute");
    static const std::unordered_map<std::string, LayoutMode> modeMap = {
        {"vertical", LayoutMode::Vertical},
        {"horizontal", LayoutMode::Horizontal},
        {"stack", LayoutMode::Stack},
        {"absolute", LayoutMode::Absolute}
    };
    auto modeIt = modeMap.find(modeStr);
    if (modeIt != modeMap.end()) {
        widget->layoutProps.mode = modeIt->second;
    }

    // Padding
    widget->layoutProps.padding = static_cast<float>(layoutNode.getDouble("padding", 0.0));
    widget->layoutProps.paddingTop = static_cast<float>(layoutNode.getDouble("paddingTop", 0.0));
    widget->layoutProps.paddingRight = static_cast<float>(layoutNode.getDouble("paddingRight", 0.0));
    widget->layoutProps.paddingBottom = static_cast<float>(layoutNode.getDouble("paddingBottom", 0.0));
    widget->layoutProps.paddingLeft = static_cast<float>(layoutNode.getDouble("paddingLeft", 0.0));

    // Margin
    widget->layoutProps.margin = static_cast<float>(layoutNode.getDouble("margin", 0.0));
    widget->layoutProps.marginTop = static_cast<float>(layoutNode.getDouble("marginTop", 0.0));
    widget->layoutProps.marginRight = static_cast<float>(layoutNode.getDouble("marginRight", 0.0));
    widget->layoutProps.marginBottom = static_cast<float>(layoutNode.getDouble("marginBottom", 0.0));
    widget->layoutProps.marginLeft = static_cast<float>(layoutNode.getDouble("marginLeft", 0.0));

    // Spacing
    widget->layoutProps.spacing = static_cast<float>(layoutNode.getDouble("spacing", 0.0));

    // Alignment
    std::string alignStr = layoutNode.getString("align", "start");
    static const std::unordered_map<std::string, Alignment> alignMap = {
        {"start", Alignment::Start},
        {"center", Alignment::Center},
        {"end", Alignment::End},
        {"stretch", Alignment::Stretch}
    };
    auto alignIt = alignMap.find(alignStr);
    if (alignIt != alignMap.end()) {
        widget->layoutProps.align = alignIt->second;
    }

    // Justification
    std::string justifyStr = layoutNode.getString("justify", "start");
    static const std::unordered_map<std::string, Justification> justifyMap = {
        {"start", Justification::Start},
        {"center", Justification::Center},
        {"end", Justification::End},
        {"spaceBetween", Justification::SpaceBetween},
        {"spaceAround", Justification::SpaceAround}
    };
    auto justifyIt = justifyMap.find(justifyStr);
    if (justifyIt != justifyMap.end()) {
        widget->layoutProps.justify = justifyIt->second;
    }

    // Size constraints
    widget->layoutProps.minWidth = static_cast<float>(layoutNode.getDouble("minWidth", 0.0));
    widget->layoutProps.minHeight = static_cast<float>(layoutNode.getDouble("minHeight", 0.0));
    widget->layoutProps.maxWidth = static_cast<float>(layoutNode.getDouble("maxWidth", -1.0));
    widget->layoutProps.maxHeight = static_cast<float>(layoutNode.getDouble("maxHeight", -1.0));

    // Flex
    widget->layoutProps.flex = static_cast<float>(layoutNode.getDouble("flex", 0.0));
}

} // namespace grove
