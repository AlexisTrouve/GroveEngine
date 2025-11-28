#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "UILayout.h"

namespace grove {

class UIContext;
class UIRenderer;

/**
 * @brief Base interface for all UI widgets
 *
 * Retained-mode UI widget with hierarchical structure.
 * Each widget has position, size, visibility, and can have children.
 */
class UIWidget {
public:
    virtual ~UIWidget() = default;

    /**
     * @brief Update widget state
     * @param ctx UI context with input state
     * @param deltaTime Time since last update
     */
    virtual void update(UIContext& ctx, float deltaTime) = 0;

    /**
     * @brief Render widget via UIRenderer
     * @param renderer Renderer that publishes to IIO
     */
    virtual void render(UIRenderer& renderer) = 0;

    /**
     * @brief Get widget type name
     * @return Type string (e.g., "panel", "label", "button")
     */
    virtual std::string getType() const = 0;

    // Identity
    std::string id;
    std::string tooltip;  // Tooltip text (empty = no tooltip)

    // Position and size (relative to parent)
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool visible = true;

    // Layout properties (Phase 2)
    LayoutProperties layoutProps;

    // Hierarchy
    UIWidget* parent = nullptr;
    std::vector<std::unique_ptr<UIWidget>> children;

    // Computed absolute position (after layout)
    float absX = 0.0f;
    float absY = 0.0f;

    /**
     * @brief Compute absolute position from parent chain
     */
    void computeAbsolutePosition() {
        if (parent) {
            absX = parent->absX + x;
            absY = parent->absY + y;
        } else {
            absX = x;
            absY = y;
        }
        for (auto& child : children) {
            child->computeAbsolutePosition();
        }
    }

    /**
     * @brief Add a child widget
     * @param child Widget to add
     */
    void addChild(std::unique_ptr<UIWidget> child) {
        child->parent = this;
        children.push_back(std::move(child));
    }

    /**
     * @brief Find widget by ID recursively
     * @param targetId ID to search for
     * @return Widget pointer or nullptr
     */
    UIWidget* findById(const std::string& targetId) {
        if (id == targetId) {
            return this;
        }
        for (auto& child : children) {
            if (UIWidget* found = child->findById(targetId)) {
                return found;
            }
        }
        return nullptr;
    }

protected:
    /**
     * @brief Update all children
     */
    void updateChildren(UIContext& ctx, float deltaTime) {
        for (auto& child : children) {
            if (child->visible) {
                child->update(ctx, deltaTime);
            }
        }
    }

    /**
     * @brief Render all children
     */
    void renderChildren(UIRenderer& renderer) {
        for (auto& child : children) {
            if (child->visible) {
                child->render(renderer);
            }
        }
    }
};

} // namespace grove
