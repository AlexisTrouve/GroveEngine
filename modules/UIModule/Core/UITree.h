#pragma once

#include "UIWidget.h"
#include <grove/IDataNode.h>
#include <memory>
#include <functional>
#include <unordered_map>

namespace grove {

class UIPanel;
class UILabel;

/**
 * @brief Factory function type for creating widgets
 */
using WidgetFactory = std::function<std::unique_ptr<UIWidget>(const IDataNode&)>;

/**
 * @brief Manages the UI widget tree and JSON parsing
 *
 * Parses JSON layout files into a hierarchy of UIWidget objects.
 * Supports widget factory registration for extensibility.
 */
class UITree {
public:
    UITree();
    ~UITree() = default;

    /**
     * @brief Register a widget factory for a type
     * @param type Widget type name (e.g., "panel", "label")
     * @param factory Factory function
     */
    void registerWidget(const std::string& type, WidgetFactory factory);

    /**
     * @brief Load UI tree from JSON data
     * @param layoutData JSON layout data
     * @return Root widget or nullptr on error
     */
    std::unique_ptr<UIWidget> loadFromJson(const IDataNode& layoutData);

    /**
     * @brief Find widget by ID in the tree
     * @param id Widget ID
     * @return Widget pointer or nullptr
     */
    UIWidget* findById(const std::string& id);

    /**
     * @brief Get the root widget
     */
    UIWidget* getRoot() { return m_root.get(); }

    /**
     * @brief Set the root widget
     */
    void setRoot(std::unique_ptr<UIWidget> root) { m_root = std::move(root); }

private:
    std::unique_ptr<UIWidget> m_root;
    std::unordered_map<std::string, WidgetFactory> m_factories;

    /**
     * @brief Parse a widget and its children recursively
     */
    std::unique_ptr<UIWidget> parseWidget(const IDataNode& node);

    /**
     * @brief Parse common widget properties
     */
    void parseCommonProperties(UIWidget* widget, const IDataNode& node);

    /**
     * @brief Parse layout properties from JSON
     */
    void parseLayoutProperties(UIWidget* widget, const IDataNode& layoutNode);

    /**
     * @brief Register default widget types
     */
    void registerDefaultWidgets();
};

} // namespace grove
