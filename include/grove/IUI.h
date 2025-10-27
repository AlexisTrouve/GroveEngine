#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <functional>

namespace warfactory {

using json = nlohmann::json;

/**
 * @brief Pure Generic UI Interface - Zero assumptions about content
 *
 * Completely data-agnostic. Implementation decides how to handle each data type.
 */
class IUI {
public:
    virtual ~IUI() = default;

    // ========================================
    // LIFECYCLE
    // ========================================

    /**
     * @brief Initialize UI system
     * @param config Generic config, implementation interprets
     */
    virtual void initialize(const json& config) = 0;

    /**
     * @brief Update/render one frame
     * @return true to continue, false to quit
     */
    virtual bool update() = 0;

    /**
     * @brief Clean shutdown
     */
    virtual void shutdown() = 0;

    // ========================================
    // GENERIC DATA FLOW
    // ========================================

    /**
     * @brief Display any data of any type with layout/windowing info
     * @param dataType "economy", "map", "inventory", "status", whatever
     * @param data JSON with content + layout:
     * {
     *   "content": {...},              // Actual data to display
     *   "window": {                    // Window/layout configuration
     *     "id": "economy_main",        // Unique window ID
     *     "title": "Economy Dashboard",
     *     "parent": "main_dock",       // Parent window/dock ID (optional)
     *     "dock": "left",              // Dock position: "left", "right", "top", "bottom", "center", "tab"
     *     "size": {"width": 400, "height": 300},
     *     "position": {"x": 100, "y": 50},
     *     "floating": false,           // true = floating window, false = docked
     *     "resizable": true,
     *     "closeable": true
     *   }
     * }
     */
    virtual void showData(const std::string& dataType, const json& data) = 0;

    /**
     * @brief Handle any user request of any type
     * @param requestType "get_prices", "move_unit", "save_game", whatever
     * @param callback Function to call when request happens
     */
    virtual void onRequest(const std::string& requestType, std::function<void(const json&)> callback) = 0;

    /**
     * @brief Show any event/message
     * @param level "info", "error", "debug", whatever
     * @param message Human readable text
     */
    virtual void showEvent(const std::string& level, const std::string& message) = 0;

    // ========================================
    // WINDOW MANAGEMENT
    // ========================================

    /**
     * @brief Create or configure a dock/container window
     * @param dockId Unique dock identifier
     * @param config Dock configuration:
     * {
     *   "type": "dock",                // "dock", "tabbed", "split"
     *   "orientation": "horizontal",   // "horizontal", "vertical" (for splits)
     *   "parent": "main_window",       // Parent dock (for nested docks)
     *   "position": "left",            // Initial position
     *   "size": {"width": 300},        // Initial size
     *   "collapsible": true,           // Can be collapsed
     *   "tabs": true                   // Enable tabbed interface
     * }
     */
    virtual void createDock(const std::string& dockId, const json& config) = 0;

    /**
     * @brief Close/remove window or dock
     * @param windowId Window or dock ID to close
     */
    virtual void closeWindow(const std::string& windowId) = 0;

    /**
     * @brief Focus/bring to front a specific window
     * @param windowId Window ID to focus
     */
    virtual void focusWindow(const std::string& windowId) = 0;

    // ========================================
    // GENERIC STATE
    // ========================================

    /**
     * @brief Get current UI state
     * @return JSON state, implementation defines structure
     */
    virtual json getState() const = 0;

    /**
     * @brief Restore UI state
     * @param state JSON state from previous getState()
     */
    virtual void setState(const json& state) = 0;
};

} // namespace warfactory