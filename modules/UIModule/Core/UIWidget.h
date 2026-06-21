#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include "UILayout.h"
#include "../Rendering/UIRenderer.h"   // releaseRenderEntries() calls renderer.unregisterEntry()

namespace grove {

class UIContext;

// Callback for when widget is destroyed (to notify renderer)
using WidgetDestroyCallback = std::function<void(uint32_t renderId)>;

/**
 * @brief Base interface for all UI widgets
 *
 * Retained-mode UI widget with hierarchical structure.
 * Each widget has position, size, visibility, and can have children.
 */
class UIWidget {
public:
    virtual ~UIWidget() {
        // Notify renderer to remove this widget's render entries
        if (m_renderId != 0 && m_destroyCallback) {
            m_destroyCallback(m_renderId);
        }
    }

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

    /**
     * @brief Publish render:*:remove for this widget's retained entries and RESET its registration,
     *        so a later re-show re-registers and re-publishes :add.
     *
     * WHY: a widget set visible=false stops calling render(), so without this its last-published
     *      retained entries linger on screen ("ghost rects"). Called when the widget becomes hidden
     *      (ui:set_visible false, or a self-close like the radial). Recurses to children — hiding a
     *      parent hides the whole subtree. Multi-entry widgets (button text, radial items) OVERRIDE
     *      to release their EXTRA ids too, then call this base (which drops the primary m_renderId).
     */
    virtual void releaseRenderEntries(UIRenderer& renderer) {
        if (m_renderId != 0) { renderer.unregisterEntry(m_renderId); m_renderId = 0; }
        m_registered = false;
        m_geometryDirty = true;   // re-show -> render() re-registers + re-publishes :add
        m_appearanceDirty = true;
        for (auto& child : children) child->releaseRenderEntries(renderer);
    }

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

    // ========================================================================
    // Retained Mode / Dirty Tracking
    // ========================================================================
protected:
    uint32_t m_renderId = 0;           // Unique ID for render system (0 = not registered)
    bool m_geometryDirty = true;       // Position/size changed, needs re-render
    bool m_appearanceDirty = true;     // Color/style changed, needs re-render
    bool m_registered = false;         // Has been registered with renderer
    WidgetDestroyCallback m_destroyCallback;  // Called on destruction

public:
    /**
     * @brief Get render ID (0 if not registered)
     */
    uint32_t getRenderId() const { return m_renderId; }

    /**
     * @brief Set render ID (called by UIRenderer on registration)
     */
    void setRenderId(uint32_t id) { m_renderId = id; }

    /**
     * @brief Check if widget needs re-rendering
     */
    bool isDirty() const { return m_geometryDirty || m_appearanceDirty; }

    /**
     * @brief Check if registered with renderer
     */
    bool isRegistered() const { return m_registered; }

    /**
     * @brief Mark as registered
     */
    void setRegistered(bool reg) { m_registered = reg; }

    /**
     * @brief Mark geometry as dirty (position, size changed)
     */
    void markGeometryDirty() {
        m_geometryDirty = true;
    }

    /**
     * @brief Mark appearance as dirty (color, style changed)
     */
    void markAppearanceDirty() {
        m_appearanceDirty = true;
    }

    /**
     * @brief Clear dirty flags after rendering
     */
    void clearDirtyFlags() {
        m_geometryDirty = false;
        m_appearanceDirty = false;
    }

    /**
     * @brief Set callback for widget destruction
     */
    void setDestroyCallback(WidgetDestroyCallback callback) {
        m_destroyCallback = std::move(callback);
    }
};

} // namespace grove
