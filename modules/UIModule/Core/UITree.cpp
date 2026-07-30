#include "UITree.h"
#include "UILayout.h"
#include "../Widgets/UIPanel.h"
#include "../Widgets/UILabel.h"
#include "../Widgets/UIButton.h"
#include "../Widgets/UIImage.h"
#include "../Widgets/UIFlipbook.h"
#include "../Widgets/UISlider.h"
#include "../Widgets/UICheckbox.h"
#include "../Widgets/UIProgressBar.h"
#include "../Widgets/UITextInput.h"
#include "../Widgets/UITextArea.h"
#include "../Widgets/UIScrollPanel.h"
#include "../Widgets/UIRadial.h"
#include "../Widgets/UIWindow.h"
#include "../Widgets/UITabs.h"
#include "../Widgets/UIDrawer.h"
#include "../Widgets/UIModal.h"
#include "../Widgets/UIList.h"
#include <grove/JsonDataNode.h>   // parseWidgetBindings reads the raw json to find {{}} props + the `on` block
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

// QUOI : la TABLE des types de widgets reconnus dans un layout JSON.
//
// POURQUOI elle ne fait que ca : cette fonction contenait les DIX-SEPT fabriques en lambdas inline,
//   621 lignes. Ajouter un widget obligeait donc a editer une fonction geante d'un fichier central,
//   alors que tout le reste du widget vit dans son propre fichier -- un point de contention garanti
//   des que deux sessions touchent l'UI en parallele. Chaque fabrique est desormais un
//   `UIX::fromNode` chez son widget, et ce qui restait ici de commentaires explicatifs est parti
//   avec le code qu'il decrit.
//
// COMMENT : ajouter un widget = ecrire son fichier + UNE ligne ici.
void UITree::registerDefaultWidgets() {
    registerWidget("panel",        &UIPanel::fromNode);
    registerWidget("window",       &UIWindow::fromNode);
    registerWidget("tabs",         &UITabs::fromNode);
    registerWidget("drawer",       &UIDrawer::fromNode);
    registerWidget("modal",        &UIModal::fromNode);
    registerWidget("list",         &UIList::fromNode);
    registerWidget("label",        &UILabel::fromNode);
    registerWidget("button",       &UIButton::fromNode);
    registerWidget("image",        &UIImage::fromNode);
    registerWidget("flipbook",     &UIFlipbook::fromNode);
    registerWidget("slider",       &UISlider::fromNode);
    registerWidget("checkbox",     &UICheckbox::fromNode);
    registerWidget("progressbar",  &UIProgressBar::fromNode);
    registerWidget("textarea",     &UITextArea::fromNode);
    registerWidget("textinput",    &UITextInput::fromNode);
    registerWidget("scrollpanel",  &UIScrollPanel::fromNode);
    registerWidget("radial",       &UIRadial::fromNode);
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

// Record a widget's DATA BINDINGS (props whose value contains {{}}) and DECLARATIVE EVENTS (the `on`
// block) from its raw json — the widget half of the templating engine. Pure string capture; UIModule
// resolves them against the data scope later. Structural keys (children/template/on) are not props.
static void parseWidgetBindings(UIWidget* widget, const IDataNode& node) {
    const auto* jn = dynamic_cast<const JsonDataNode*>(&node);
    if (!jn) return;
    const auto& j = jn->getJsonData();
    if (!j.is_object()) return;

    // Bindable props: any scalar STRING value containing a {{...}} placeholder.
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& key = it.key();
        if (key == "on" || key == "children" || key == "template" || key == "repeat" || key == "if") continue;
        if (it.value().is_string()) {
            const std::string v = it.value().get<std::string>();
            if (v.find("{{") != std::string::npos) widget->bindings.push_back({key, v});
        }
    }

    // Conditional render: `"if":"{{flag}}"`.
    if (j.contains("if") && j["if"].is_string()) widget->ifBinding = j["if"].get<std::string>();

    // Repeater: "repeat":"{{<path>}}" + "template":{...}. Record the data path + the template (serialized
    // as a json string, re-parsed per item at expand time — keeps UIWidget free of any IDataNode coupling).
    if (j.contains("repeat") && j["repeat"].is_string() && j.contains("template") && j["template"].is_object()) {
        std::string rp = j["repeat"].get<std::string>();
        const size_t o = rp.find("{{"), c = rp.find("}}");
        if (o != std::string::npos && c != std::string::npos && c > o + 2) {
            std::string inner = rp.substr(o + 2, c - (o + 2));
            size_t a = inner.find_first_not_of(" \t"), b = inner.find_last_not_of(" \t");
            widget->repeatPath = (a == std::string::npos) ? "" : inner.substr(a, b - a + 1);
        }
        widget->repeatTemplateJson = j["template"].dump();
    }

    // Declarative events: "on": { "<signal>": { "event": "<topic>", "args": { "<k>": "{{...}}" } } }.
    if (j.contains("on") && j["on"].is_object()) {
        for (auto sit = j["on"].begin(); sit != j["on"].end(); ++sit) {
            const auto& spec = sit.value();
            if (!spec.is_object() || !spec.contains("event") || !spec["event"].is_string()) continue;
            UIWidget::EventBinding eb;
            eb.eventName = spec["event"].get<std::string>();
            if (spec.contains("args") && spec["args"].is_object()) {
                for (auto ait = spec["args"].begin(); ait != spec["args"].end(); ++ait) {
                    if (ait.value().is_string()) eb.args.push_back({ait.key(), ait.value().get<std::string>()});
                }
            }
            widget->eventBindings[sit.key()] = std::move(eb);
        }
    }
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

    // Create widget via factory.
    //
    // A factory that THROWS (a malformed property, a wrong JSON shape) must cost only ITS OWN widget.
    // Without this guard the exception unwinds the whole RECURSIVE parse: one mistyped array in a
    // corner of the file and the entire screen comes up blank, with nothing in the log to say which
    // widget was at fault — you then hunt your last code change instead of the layout. Dropping the
    // offender and naming it (type + id + what the parser said) is strictly more useful than losing
    // its siblings. This is NOT a silent fallback: the widget really is gone, and loudly so.
    std::unique_ptr<UIWidget> widget;
    try {
        widget = it->second(node);
    } catch (const std::exception& e) {
        spdlog::error("UITree: widget type '{}' (id '{}') failed to build and was DROPPED: {}",
                      type, node.getString("id", "<no id>"), e.what());
        return nullptr;
    } catch (...) {
        spdlog::error("UITree: widget type '{}' (id '{}') failed to build and was DROPPED "
                      "(non-standard exception)", type, node.getString("id", "<no id>"));
        return nullptr;
    }
    if (!widget) {
        spdlog::warn("UITree: Factory failed for type '{}'", type);
        return nullptr;
    }

    // Parse common properties
    parseCommonProperties(widget.get(), node);

    // Record data-bindings ({{}} props) + declarative events (`on`) for the templating engine.
    parseWidgetBindings(widget.get(), node);

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
    // Relative (percent) sizing — slice 1.1. Fraction of the parent content box on each axis
    // (root's parent = viewport). 0 = use the absolute width/height above. Resolved in UILayout.
    widget->widthPercent = static_cast<float>(node.getDouble("widthPercent", 0.0));
    widget->heightPercent = static_cast<float>(node.getDouble("heightPercent", 0.0));
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

    // Anchoring (slice 1.2): pins an absolutely-positioned widget to a point of the parent box.
    // Hyphenated names match the documented format; unknown/absent -> None (legacy x/y).
    std::string anchorStr = node.getString("anchor", "none");
    static const std::unordered_map<std::string, Anchor> anchorMap = {
        {"none", Anchor::None},
        {"top-left", Anchor::TopLeft},       {"top", Anchor::Top},       {"top-right", Anchor::TopRight},
        {"left", Anchor::Left},              {"center", Anchor::Center}, {"right", Anchor::Right},
        {"bottom-left", Anchor::BottomLeft}, {"bottom", Anchor::Bottom}, {"bottom-right", Anchor::BottomRight}
    };
    auto anchorIt = anchorMap.find(anchorStr);
    if (anchorIt != anchorMap.end()) {
        widget->anchor = anchorIt->second;
    }
    // Anchor offset: a pixel nudge applied after anchoring. Object {x,y}.
    if (auto* offsetNode = mutableNode.getChildReadOnly("anchorOffset")) {
        widget->anchorOffsetX = static_cast<float>(offsetNode->getDouble("x", 0.0));
        widget->anchorOffsetY = static_cast<float>(offsetNode->getDouble("y", 0.0));
    }
}

void UITree::parseLayoutProperties(UIWidget* widget, const IDataNode& layoutNode) {
    // Layout mode
    std::string modeStr = layoutNode.getString("type", "absolute");
    static const std::unordered_map<std::string, LayoutMode> modeMap = {
        {"vertical", LayoutMode::Vertical},
        {"horizontal", LayoutMode::Horizontal},
        {"stack", LayoutMode::Stack},
        {"absolute", LayoutMode::Absolute},
        {"grid", LayoutMode::Grid}
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

    // Grid (slice 1.3): columns + cell height (gap reuses `spacing`).
    widget->layoutProps.columns = layoutNode.getInt("columns", 1);
    widget->layoutProps.rowHeight = static_cast<float>(layoutNode.getDouble("rowHeight", 0.0));
}

} // namespace grove
