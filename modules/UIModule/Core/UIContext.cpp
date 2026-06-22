#include "UIContext.h"
#include "UIWidget.h"
#include "../Widgets/UIButton.h"
#include "../Widgets/UISlider.h"
#include "../Widgets/UICheckbox.h"
#include "../Widgets/UITextInput.h"
#include "../Widgets/UIRadial.h"
#include "../Widgets/UIWindow.h"
#include "../Widgets/UITabs.h"
#include "../Widgets/UIDrawer.h"
#include "../Widgets/UIModal.h"
#include "../Widgets/UIList.h"
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

    // A clipping container (scroll panel, window) hides its children outside its clip rect — a point
    // outside it can't hit them, so skip the whole subtree. Mirrors the visual scissor (2a). The clip
    // rect is the widget's bounds by default, or a custom region (a window clips below its titlebar).
    bool descend = true;
    if (widget->clipsHitTest()) {
        float cx, cy, cw, ch;
        widget->hitClipRect(cx, cy, cw, ch);
        descend = (x >= cx && x <= cx + cw && y >= cy && y <= cy + ch);
    }

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
    else if (type == "window") {
        // Opaque: a click anywhere in the window's bounds is absorbed, so it never leaks to a
        // widget behind it. Content children were already tested above (clipped to the content
        // rect); reaching here means the title bar / chrome / empty content was clicked.
        UIWindow* window = static_cast<UIWindow*>(widget);
        if (window->pointInWindow(x, y)) {
            return widget;
        }
    }
    else if (type == "tabs") {
        // Opaque too: clicks in the tab bar / chrome are absorbed (the active page's children were
        // tested above, clipped to the content area). UIModule reads tabAt() to switch the page.
        UITabs* tabs = static_cast<UITabs*>(widget);
        if (tabs->pointInBounds(x, y)) {
            return widget;
        }
    }
    else if (type == "drawer") {
        // Opaque while on screen: absorb clicks in the (sliding) drawer rect; off-screen when
        // closed -> pointInBounds is false -> the click passes through.
        UIDrawer* drawer = static_cast<UIDrawer*>(widget);
        if (drawer->pointInBounds(x, y)) {
            return widget;
        }
    }
    else if (type == "modal") {
        // Focus-trap: while open, the backdrop absorbs EVERY click (dialog children were tested
        // above, clipped to the dialog); nothing behind the modal is reachable.
        UIModal* modal = static_cast<UIModal*>(widget);
        if (modal->pointInBounds(x, y)) {
            return widget;
        }
    }
    else if (type == "list") {
        // Opaque: rows are a self-managed retained pool (not children), so a click anywhere in the list
        // bounds is absorbed here; UIModule reads rowAt() to resolve + publish ui:list:selected.
        UIList* list = static_cast<UIList*>(widget);
        if (list->pointInBounds(x, y)) {
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
    else if (type == "tabs") {
        // Surface the tabs as the click target on PRESS; UIModule resolves the tab switch (it needs
        // to publish ui:tab:changed). On release / content, nothing extra.
        if (pressed) return target;
    }
    else if (type == "modal") {
        // Surface the modal on PRESS so UIModule can close it on an outside-the-dialog click.
        if (pressed) return target;
    }
    else if (type == "list") {
        // Surface the list on BOTH press and release: the press starts a possible scroll-drag (handled in
        // UIList::update), and UIModule resolves the row select/toggle on RELEASE (only if it wasn't a drag).
        return target;
    }

    return handled ? target : nullptr;
}

} // namespace grove
