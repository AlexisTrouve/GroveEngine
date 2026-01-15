#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <functional>

namespace grove {

using json = nlohmann::json;

// ========================================
// ENUMS FOR TYPE SAFETY
// ========================================

/**
 * @brief Data types for UI display
 */
enum class DataType {
    ECONOMY,
    MAP,
    INVENTORY,
    CONSOLE,
    PERFORMANCE,
    COMPANIES,
    ALERTS,
    PRODUCTION,
    LOGISTICS,
    PLAYER,
    SETTINGS,
    DEBUG,
    CUSTOM // For extending with string fallback
};

/**
 * @brief Request types from UI
 */
enum class RequestType {
    GET_PRICES,
    GET_CHUNK,
    MOVE_PLAYER,
    SAVE_GAME,
    LOAD_GAME,
    CLOSE_WINDOW,
    FOCUS_WINDOW,
    UPDATE_SETTINGS,
    EXECUTE_COMMAND,
    CUSTOM // For extending with string fallback
};

/**
 * @brief Event/message levels
 */
enum class EventLevel {
    INFO,
    SUCCESS,
    WARNING,
    ERROR,
    DEBUG,
    TRACE
};

/**
 * @brief Dock types for window management
 */
enum class DockType {
    DOCK,      // Standard dockable panel
    SPLIT,     // Horizontal/vertical split
    TABBED,    // Tabbed container
    FLOATING   // Floating window
};

/**
 * @brief Dock positions
 */
enum class DockPosition {
    LEFT,
    RIGHT,
    TOP,
    BOTTOM,
    CENTER,
    TAB        // Add as tab to parent
};

/**
 * @brief Split orientations
 */
enum class Orientation {
    HORIZONTAL,
    VERTICAL
};

/**
 * @brief Pure Generic UI Interface - Type-safe with enums
 */
class IUI {
public:
    virtual ~IUI() = default;

    // ========================================
    // LIFECYCLE
    // ========================================

    virtual void initialize(const json& config) = 0;
    virtual bool update() = 0;
    virtual void shutdown() = 0;

    // ========================================
    // GENERIC DATA FLOW - ENUM VERSIONS
    // ========================================

    /**
     * @brief Display data with type-safe enum
     * @param dataType Enum data type
     * @param data JSON with content + optional window config:
     * {
     *   "content": {...},                    // Actual data to display
     *   "window": {                          // Window configuration (optional)
     *     "id": "window_id",
     *     "title": "Window Title",
     *     "parent": "parent_dock_id",
     *     "dock": "left|right|top|bottom|center|tab",
     *
     *     // SIZE SYSTEM - Hybrid percentage + absolute constraints
     *     "size": {"width": "20%", "height": 300},           // Target: 20% of parent width, 300px height
     *     "size": {"width": 400, "height": "50%"},           // Target: 400px width, 50% of parent height
     *     "size": {"width": "30%", "height": "40%"},         // Target: 30% width, 40% height
     *
     *     "min_size": {"width": 200, "height": 150},         // ABSOLUTE minimum in pixels (always respected)
     *     "max_size": {"width": 800, "height": "80%"},       // Maximum: 800px width OR 80% of parent (whichever smaller)
     *
     *     "position": {"x": 100, "y": 50},
     *     "floating": false,
     *     "resizable": true,
     *     "closeable": true,
     *     "collapsible": false
     *   }
     * }
     */
    virtual void showData(DataType dataType, const json& data) = 0;

    /**
     * @brief Display custom data type (fallback to string)
     * @param customType Custom data type name
     * @param data JSON data
     */
    virtual void showDataCustom(const std::string& customType, const json& data) = 0;

    /**
     * @brief Handle user request with type-safe enum
     * @param requestType Enum request type
     * @param callback Function to call when request happens
     */
    virtual void onRequest(RequestType requestType, std::function<void(const json&)> callback) = 0;

    /**
     * @brief Handle custom request type (fallback to string)
     * @param customType Custom request type name
     * @param callback Function to call when request happens
     */
    virtual void onRequestCustom(const std::string& customType, std::function<void(const json&)> callback) = 0;

    /**
     * @brief Show event with type-safe level
     * @param level Event level enum
     * @param message Human readable text
     */
    virtual void showEvent(EventLevel level, const std::string& message) = 0;

    // ========================================
    // WINDOW MANAGEMENT - ENUM VERSIONS
    // ========================================

    /**
     * @brief Create dock with type-safe enums
     * @param dockId Unique dock identifier
     * @param type Dock type enum
     * @param position Dock position enum
     * @param config Additional configuration:
     * {
     *   "parent": "parent_dock_id",                      // Parent dock (optional)
     *
     *   // HYBRID SIZE SYSTEM
     *   "size": {"width": "25%", "height": 200},        // Target: 25% of parent width, 200px height
     *   "min_size": {"width": 200, "height": 100},      // ABSOLUTE minimum pixels (overrides percentage)
     *   "max_size": {"width": "50%", "height": 600},    // Maximum: 50% of parent OR 600px (whichever smaller)
     *
     *   "orientation": "horizontal",                     // For SPLIT type
     *   "collapsible": true,                            // Can be collapsed
     *   "resizable": true,                              // Can be resized
     *   "tabs": true                                    // Enable tabbed interface
     * }
     */
    virtual void createDock(const std::string& dockId, DockType type, DockPosition position, const json& config = {}) = 0;

    /**
     * @brief Create split dock with orientation
     * @param dockId Unique dock identifier
     * @param orientation Split orientation
     * @param config Additional configuration:
     * {
     *   "parent": "parent_dock_id",                      // Parent dock (optional)
     *   "size": {"width": 300, "height": 200},          // Initial size
     *   "min_size": {"width": 100, "height": 50},       // Minimum split size in pixels
     *   "max_size": {"width": 1000, "height": 800},     // Maximum split size in pixels
     *   "split_ratio": 0.5,                             // Split ratio (0.0 to 1.0)
     *   "min_panel_size": 80,                           // Minimum size for each panel in split
     *   "resizable": true                               // Can be resized by dragging splitter
     * }
     */
    virtual void createSplit(const std::string& dockId, Orientation orientation, const json& config = {}) = 0;

    /**
     * @brief Close window or dock
     * @param windowId Window/dock ID to close
     */
    virtual void closeWindow(const std::string& windowId) = 0;

    /**
     * @brief Focus window
     * @param windowId Window ID to focus
     */
    virtual void focusWindow(const std::string& windowId) = 0;

    // ========================================
    // GENERIC STATE
    // ========================================

    virtual json getState() const = 0;
    virtual void setState(const json& state) = 0;

    // ========================================
    // CONVENIENCE METHODS WITH ENUMS
    // ========================================

    void info(const std::string& message) {
        showEvent(EventLevel::INFO, message);
    }

    void success(const std::string& message) {
        showEvent(EventLevel::SUCCESS, message);
    }

    void warning(const std::string& message) {
        showEvent(EventLevel::WARNING, message);
    }

    void error(const std::string& message) {
        showEvent(EventLevel::ERROR, message);
    }

    void debug(const std::string& message) {
        showEvent(EventLevel::DEBUG, message);
    }
};

// ========================================
// ENUM TO STRING CONVERSIONS (for implementations)
// ========================================

/**
 * @brief Convert DataType enum to string (for implementations that need strings)
 */
constexpr const char* toString(DataType type) {
    switch (type) {
        case DataType::ECONOMY: return "economy";
        case DataType::MAP: return "map";
        case DataType::INVENTORY: return "inventory";
        case DataType::CONSOLE: return "console";
        case DataType::PERFORMANCE: return "performance";
        case DataType::COMPANIES: return "companies";
        case DataType::ALERTS: return "alerts";
        case DataType::PRODUCTION: return "production";
        case DataType::LOGISTICS: return "logistics";
        case DataType::PLAYER: return "player";
        case DataType::SETTINGS: return "settings";
        case DataType::DEBUG: return "debug";
        case DataType::CUSTOM: return "custom";
        default: return "unknown";
    }
}

constexpr const char* toString(RequestType type) {
    switch (type) {
        case RequestType::GET_PRICES: return "get_prices";
        case RequestType::GET_CHUNK: return "get_chunk";
        case RequestType::MOVE_PLAYER: return "move_player";
        case RequestType::SAVE_GAME: return "save_game";
        case RequestType::LOAD_GAME: return "load_game";
        case RequestType::CLOSE_WINDOW: return "close_window";
        case RequestType::FOCUS_WINDOW: return "focus_window";
        case RequestType::UPDATE_SETTINGS: return "update_settings";
        case RequestType::EXECUTE_COMMAND: return "execute_command";
        case RequestType::CUSTOM: return "custom";
        default: return "unknown";
    }
}

constexpr const char* toString(EventLevel level) {
    switch (level) {
        case EventLevel::INFO: return "info";
        case EventLevel::SUCCESS: return "success";
        case EventLevel::WARNING: return "warning";
        case EventLevel::ERROR: return "error";
        case EventLevel::DEBUG: return "debug";
        case EventLevel::TRACE: return "trace";
        default: return "unknown";
    }
}

constexpr const char* toString(DockType type) {
    switch (type) {
        case DockType::DOCK: return "dock";
        case DockType::SPLIT: return "split";
        case DockType::TABBED: return "tabbed";
        case DockType::FLOATING: return "floating";
        default: return "unknown";
    }
}

constexpr const char* toString(DockPosition pos) {
    switch (pos) {
        case DockPosition::LEFT: return "left";
        case DockPosition::RIGHT: return "right";
        case DockPosition::TOP: return "top";
        case DockPosition::BOTTOM: return "bottom";
        case DockPosition::CENTER: return "center";
        case DockPosition::TAB: return "tab";
        default: return "unknown";
    }
}

constexpr const char* toString(Orientation orient) {
    switch (orient) {
        case Orientation::HORIZONTAL: return "horizontal";
        case Orientation::VERTICAL: return "vertical";
        default: return "unknown";
    }
}

} // namespace grove