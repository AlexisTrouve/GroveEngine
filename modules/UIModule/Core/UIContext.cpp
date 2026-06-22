#include "UIContext.h"
#include "UIWidget.h"
#include "../Widgets/UIButton.h"
#include "../Widgets/UISlider.h"
#include "../Widgets/UICheckbox.h"
#include "../Widgets/UITextInput.h"
#include "../Widgets/UIRadial.h"
#include <spdlog/spdlog.h>

namespace grove {

/**
 * @brief Perform hit testing to find the topmost widget at a point
 *
 * Recursively searches the widget tree from front to back (reverse order)
 * to find the topmost visible widget containing the point.
 *
 * @param widget Root widget to search from
 * @param x Point X coordinate
 * @param y Point Y coordinate
 * @return Topmost widget at point, or nullptr
 */
UIWidget* hitTest(UIWidget* widget, float x, float y) {
    if (!widget || !widget->visible) {
        return nullptr;
    }

    // A clipping container (scroll panel, window) hides its children outside its own rect — a point
    // outside the clip can't hit them, so skip the whole subtree. Mirrors the visual scissor (2a).
    const bool descend = !widget->clipsHitTest() ||
        (x >= widget->absX && x <= widget->absX + widget->width &&
         y >= widget->absY && y <= widget->absY + widget->height);

    // Check children first (front to back = reverse order for hit testing)
    if (descend) {
        for (auto it = widget->children.rbegin(); it != widget->children.rend(); ++it) {
            UIWidget* hit = hitTest(it->get(), x, y);
            if (hit) {
                return hit;
            }
        }
    }

    // Check this widget if it's interactive
    std::string type = widget->getType();

    if (type == "button") {
        UIButton* button = static_cast<UIButton*>(widget);
        if (button->containsPoint(x, y)) {
            return widget;
        }
    }
    else if (type == "slider") {
        UISlider* slider = static_cast<UISlider*>(widget);
        if (slider->containsPoint(x, y)) {
            return widget;
        }
    }
    else if (type == "checkbox") {
        UICheckbox* checkbox = static_cast<UICheckbox*>(widget);
        if (checkbox->containsPoint(x, y)) {
            return widget;
        }
    }
    else if (type == "textinput") {
        UITextInput* textInput = static_cast<UITextInput*>(widget);
        if (textInput->containsPoint(x, y)) {
            return widget;
        }
    }
    else if (type == "radial") {
        // Disque interactif (jusqu'à outerRadius) ; la dead-zone se résout en "annuler".
        UIRadial* radial = static_cast<UIRadial*>(widget);
        if (radial->containsPoint(x, y)) {
            return widget;
        }
    }

    return nullptr;
}

/**
 * @brief Update hover state for all widgets in tree
 *
 * Calls onMouseEnter/onMouseLeave for buttons based on hover state.
 *
 * @param widget Root widget
 * @param ctx UI context with hover state
 * @param prevHoveredId Previous frame's hovered widget ID
 */
void updateHoverState(UIWidget* widget, UIContext& ctx, const std::string& prevHoveredId) {
    if (!widget) return;

    // Check if this widget's hover state changed
    if (widget->getType() == "button") {
        UIButton* button = static_cast<UIButton*>(widget);

        bool wasHovered = (widget->id == prevHoveredId);
        bool isHovered = (widget->id == ctx.hoveredWidgetId);

        if (isHovered && !wasHovered) {
            button->onMouseEnter();
        } else if (!isHovered && wasHovered) {
            button->onMouseLeave();
        }
    }

    // Recurse to children
    for (auto& child : widget->children) {
        updateHoverState(child.get(), ctx, prevHoveredId);
    }
}

/**
 * @brief Dispatch mouse button event to widget tree
 *
 * Finds the widget under the mouse and delivers the event.
 *
 * @param widget Root widget
 * @param ctx UI context
 * @param button Mouse button (0 = left, 1 = right, 2 = middle)
 * @param pressed true if button pressed, false if released
 * @return Widget that handled the event (for action publishing), or nullptr
 */
UIWidget* dispatchMouseButton(UIWidget* widget, UIContext& ctx, int button, bool pressed) {
    // Hit test to find target widget
    UIWidget* target = hitTest(widget, ctx.mouseX, ctx.mouseY);

    if (!target) {
        return nullptr;
    }

    // Dispatch to appropriate widget type
    std::string type = target->getType();
    bool handled = false;

    if (type == "button") {
        UIButton* btn = static_cast<UIButton*>(target);
        handled = btn->onMouseButton(button, pressed, ctx.mouseX, ctx.mouseY);

        if (handled && !pressed && !btn->onClick.empty()) {
            return target;  // Return for action publishing
        }
    }
    else if (type == "slider") {
        UISlider* slider = static_cast<UISlider*>(target);
        handled = slider->onMouseButton(button, pressed, ctx.mouseX, ctx.mouseY);

        if (handled) {
            return target;  // Return for value_changed publishing
        }
    }
    else if (type == "checkbox") {
        UICheckbox* checkbox = static_cast<UICheckbox*>(target);
        handled = checkbox->onMouseButton(button, pressed, ctx.mouseX, ctx.mouseY);

        if (handled) {
            return target;  // Return for value_changed publishing
        }
    }
    else if (type == "textinput") {
        UITextInput* textInput = static_cast<UITextInput*>(target);
        handled = textInput->onMouseButton(button, pressed, ctx.mouseX, ctx.mouseY);

        if (handled) {
            return target;  // Return for focus handling in UIModule
        }
    }
    else if (type == "radial") {
        UIRadial* radial = static_cast<UIRadial*>(target);
        handled = radial->onMouseButton(button, pressed, ctx.mouseX, ctx.mouseY);

        if (handled && !pressed) {
            return target;  // release sur la roue -> UIModule résout action vs annuler
        }
    }

    return handled ? target : nullptr;
}

} // namespace grove
