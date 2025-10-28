#pragma once

#include "IUI_Enums.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <GL/gl.h>

#include <map>
#include <vector>
#include <string>
#include <functional>
#include <chrono>

namespace grove {

/**
 * @brief ImGui implementation of IUI interface
 *
 * Provides full windowing system with docking, tabs, splits, and floating windows.
 * Handles hybrid percentage + pixel sizing with automatic constraint enforcement.
 */
class ImGuiUI : public IUI {
private:
    // ========================================
    // CORE STATE
    // ========================================

    GLFWwindow* window = nullptr;
    bool initialized = false;
    bool should_close = false;
    int frame_count = 0;

    // Screen/parent sizes for percentage calculations
    ImVec2 screen_size = {1400, 900};
    ImVec2 previous_screen_size = {0, 0};

    // ========================================
    // WINDOW MANAGEMENT
    // ========================================

    struct WindowInfo {
        std::string id;
        std::string title;
        std::string parent;
        DockPosition dock_position = DockPosition::CENTER;
        bool is_open = true;
        bool is_floating = false;
        bool resizable = true;
        bool closeable = true;

        // Size system
        ImVec2 size = {400, 300};
        ImVec2 min_size = {100, 100};
        ImVec2 max_size = {2000, 1500};
        ImVec2 position = {0, 0};

        // Percentage tracking
        std::string size_width_percent = "";
        std::string size_height_percent = "";
        std::string min_width_percent = "";
        std::string min_height_percent = "";
        std::string max_width_percent = "";
        std::string max_height_percent = "";

        // Content
        DataType data_type = DataType::CUSTOM;
        json content_data;
    };

    std::map<std::string, WindowInfo> windows;

    struct DockInfo {
        std::string id;
        DockType type = DockType::DOCK;
        DockPosition position = DockPosition::LEFT;
        std::string parent;
        bool collapsible = true;
        bool resizable = true;
        ImVec2 size = {300, 200};
        ImVec2 min_size = {100, 100};
        ImVec2 max_size = {1000, 800};
        std::vector<std::string> child_windows;
    };

    std::map<std::string, DockInfo> docks;

    // ========================================
    // CALLBACKS
    // ========================================

    std::map<RequestType, std::function<void(const json&)>> request_callbacks;
    std::map<std::string, std::function<void(const json&)>> custom_request_callbacks;

    // ========================================
    // MESSAGE SYSTEM
    // ========================================

    struct LogMessage {
        EventLevel level;
        std::string message;
        std::chrono::steady_clock::time_point timestamp;
    };

    std::vector<LogMessage> log_messages;
    static constexpr size_t MAX_LOG_MESSAGES = 100;

public:
    ImGuiUI() = default;
    ~ImGuiUI() override { shutdown(); }

    // ========================================
    // LIFECYCLE IMPLEMENTATION
    // ========================================

    void initialize(const json& config) override {
        if (initialized) return;

        // Initialize GLFW
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        // OpenGL 3.3 Core
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        // Create window
        std::string title = config.value("title", "Warfactory ImGui UI");
        auto window_size = config.value("window_size", json{{"width", 1400}, {"height", 900}});
        if (window_size.is_object()) {
            screen_size.x = window_size.value("width", 1400);
            screen_size.y = window_size.value("height", 900);
        } else {
            screen_size.x = 1400;
            screen_size.y = 900;
        }

        window = glfwCreateWindow(
            static_cast<int>(screen_size.x),
            static_cast<int>(screen_size.y),
            title.c_str(), nullptr, nullptr
        );

        if (!window) {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1); // VSync

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // Note: Docking features depend on ImGui docking branch
        // Using manual docking simulation for compatibility

        // Basic style setup
        ImGui::StyleColorsDark();

        // Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 330 core");

        initialized = true;
    }

    bool update() override {
        if (!initialized || !window) return false;

        if (glfwWindowShouldClose(window)) {
            should_close = true;
            return false;
        }

        // Update screen size for percentage calculations
        int fb_width, fb_height;
        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        ImVec2 new_screen_size = {static_cast<float>(fb_width), static_cast<float>(fb_height)};

        // Detect screen size changes and recalculate if needed
        if (new_screen_size.x != previous_screen_size.x || new_screen_size.y != previous_screen_size.y) {
            if (frame_count > 0) { // Skip first frame (initialization)
                debug("🔄 Screen size changed: " + std::to_string((int)new_screen_size.x) + "x" + std::to_string((int)new_screen_size.y));
                recalculateAllSizes();
            }
            previous_screen_size = screen_size;
        }
        screen_size = new_screen_size;

        frame_count++;

        // Poll events
        glfwPollEvents();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render all windows
        renderAllWindows();

        // Render ImGui
        ImGui::Render();

        // OpenGL rendering
        glViewport(0, 0, static_cast<int>(screen_size.x), static_cast<int>(screen_size.y));
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        return !should_close;
    }

    void shutdown() override {
        if (!initialized) return;

        if (window) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();

            glfwDestroyWindow(window);
            glfwTerminate();
            window = nullptr;
        }

        initialized = false;
    }

private:
    // ========================================
    // SIZE CALCULATION HELPERS
    // ========================================

    /**
     * @brief Parse size value - handles both pixels and percentages
     */
    float parseSize(const json& size_value, float parent_size, float default_size) {
        try {
            if (size_value.is_number()) {
                return size_value.get<float>();
            }

            if (size_value.is_string()) {
                std::string size_str = size_value.get<std::string>();
                if (!size_str.empty() && size_str.back() == '%') {
                    float percent = std::stof(size_str.substr(0, size_str.length() - 1));
                    return (percent / 100.0f) * parent_size;
                } else {
                    // String but not percentage - try to parse as number
                    return std::stof(size_str);
                }
            }
        } catch (...) {
            // Any JSON or parsing error - return default
        }

        // Neither number nor string or error - return default
        return default_size;
    }

    /**
     * @brief Calculate effective size with hybrid constraints
     */
    ImVec2 calculateEffectiveSize(const WindowInfo& win, ImVec2 parent_size) {
        // Use already parsed sizes (converted in showData)
        float target_width = win.size.x;
        float target_height = win.size.y;

        // Calculate constraint bounds
        float min_width = win.min_size.x;
        float min_height = win.min_size.y;
        float max_width = win.max_size.x;
        float max_height = win.max_size.y;

        // Apply constraints (clamp)
        float final_width = std::max(min_width, std::min(target_width, max_width));
        float final_height = std::max(min_height, std::min(target_height, max_height));

        return {final_width, final_height};
    }

    /**
     * @brief Calculate window position based on docking
     */
    ImVec2 calculateDockedPosition(const WindowInfo& win, ImVec2 size) {
        if (win.parent.empty() || win.is_floating) {
            // For windows without parent, use explicit position or calculate smart default
            debug("🔍 Checking position for '" + win.id + "': pos=" +
                  std::to_string(win.position.x) + "," + std::to_string(win.position.y) +
                  " floating=" + (win.is_floating ? "true" : "false"));

            // Only use explicit position if it was actually set by user (not just default values)
            if (win.position.x > 10 && win.position.y > 10) {
                debug("📌 Using explicit position for '" + win.id + "'");
                return win.position; // Use explicit position
            } else {
                // Simple approach: use actual window sizes from economy_main window
                float left_edge_end = 252;  // Real end of economy_main (we know it's 252px wide)
                float top_edge_end = 88;    // Real end of toolbar + margin

                // Find the right sidebar start by looking for info_panel_main
                float right_edge_start = 1050; // We know info_panel starts at 1050px

                debug("🔧 Simple positioning for window '" + win.id + "': left_end=" +
                      std::to_string(left_edge_end) + "px, right_start=" +
                      std::to_string(right_edge_start) + "px, top_end=" +
                      std::to_string(top_edge_end) + "px");

                // Position directly against the real edge of existing windows
                float x = left_edge_end;  // Directly touching end of left sidebar (252px)
                float y = top_edge_end;   // Directly below toolbar (88px)

                // If window would overlap with right sidebar, push it left to touch right edge
                if (x + size.x > right_edge_start) {
                    x = right_edge_start - size.x; // Touch right sidebar windows
                }

                debug("🎯 Calculated position for '" + win.id + "': " +
                      std::to_string(x) + "," + std::to_string(y) +
                      " (touching real window edges)");

                return {x, y};
            }
        }

        // Find parent dock
        auto dock_it = docks.find(win.parent);
        if (dock_it == docks.end()) {
            return {0, 0}; // Parent dock not found
        }

        const DockInfo& dock = dock_it->second;

        // Calculate dock area based on position
        switch (dock.position) {
            case DockPosition::LEFT:
                return {0, 80}; // Left edge but below toolbar (72px + margin)
            case DockPosition::RIGHT:
                return {screen_size.x - dock.size.x, 80}; // Right edge but below toolbar
            case DockPosition::TOP:
                // Top edge - if dock spans full width, start at 0, else offset
                if (dock.size.x >= screen_size.x * 0.9f) {
                    return {0, 0}; // Full width toolbar starts at screen edge
                } else {
                    return {280, 0}; // Partial width toolbar starts after sidebar
                }
            case DockPosition::BOTTOM:
                return {0, screen_size.y - dock.size.y}; // Bottom edge
            case DockPosition::CENTER:
            default:
                return {screen_size.x * 0.5f - size.x * 0.5f, screen_size.y * 0.5f - size.y * 0.5f}; // Center
        }
    }

    // ========================================
    // RECALCULATION METHODS
    // ========================================

    void recalculateAllSizes() {
        // Recalculate dock sizes
        for (auto& [dock_id, dock] : docks) {
            // Recalculate dock size if it uses percentages
            recalculateDockSize(dock);
        }

        // Recalculate window sizes
        for (auto& [window_id, win] : windows) {
            recalculateWindowSize(win);
        }
    }

    void recalculateDockSize(DockInfo& dock) {
        // Re-parse dock size with new screen size
        // This would need the original JSON config, for now just log
        debug("📐 Recalculating dock: " + dock.id);
        // TODO: Store original percentage strings to recalculate properly
    }

    void recalculateWindowSize(WindowInfo& win) {
        // Re-parse window size with new screen/parent sizes
        debug("📐 Recalculating window: " + win.id);

        // Recalculate width if percentage
        if (!win.size_width_percent.empty()) {
            float parent_width = screen_size.x;
            if (!win.parent.empty() && docks.find(win.parent) != docks.end()) {
                parent_width = docks[win.parent].size.x;
            }
            win.size.x = parseSize(win.size_width_percent, parent_width, 400);
        }

        // Recalculate height if percentage
        if (!win.size_height_percent.empty()) {
            float parent_height = screen_size.y;
            if (!win.parent.empty() && docks.find(win.parent) != docks.end()) {
                parent_height = docks[win.parent].size.y;
            }
            win.size.y = parseSize(win.size_height_percent, parent_height, 300);
        }
    }

    // ========================================
    // RENDERING IMPLEMENTATION
    // ========================================

    void renderAllWindows() {
        // Log screen size for debugging (only first frame to avoid spam)
        if (frame_count == 1) {
            debug("🖥️ Screen Size: " + std::to_string((int)screen_size.x) + "x" + std::to_string((int)screen_size.y) + "px");
            info("🏗️ Manual docking system active (simulated docking layout)");
        }

        for (auto& [window_id, win] : windows) {
            if (!win.is_open) continue;

            if (frame_count <= 5) { // Log first 5 frames for each window
                debug("🪟 Window: " + window_id + " (" + win.title + ")");
                debug("  📐 Target Size: " + std::to_string((int)win.size.x) + "x" + std::to_string((int)win.size.y) + "px");
                debug("  📏 Size %: width='" + win.size_width_percent + "' height='" + win.size_height_percent + "'");
                debug("  ⚖️ Constraints: min=" + std::to_string((int)win.min_size.x) + "x" + std::to_string((int)win.min_size.y) +
                      " max=" + std::to_string((int)win.max_size.x) + "x" + std::to_string((int)win.max_size.y));
                debug("  🔗 Docking: parent='" + win.parent + "' position=" + std::to_string((int)win.dock_position));
            }

            // Calculate effective size with constraints
            ImVec2 effective_size = calculateEffectiveSize(win, screen_size);
            if (frame_count <= 5) {
                debug("  ✅ Effective Size: " + std::to_string((int)effective_size.x) + "x" + std::to_string((int)effective_size.y) + "px");
            }

            // Set window constraints
            ImGui::SetNextWindowSizeConstraints(win.min_size, win.max_size);

            // Set window size
            if (win.is_floating) {
                // For floating windows, force initial size and position
                ImGuiCond size_condition = (frame_count <= 3) ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
                ImGui::SetNextWindowSize(effective_size, size_condition);

                // Calculate smart position that avoids dock overlaps
                ImVec2 floating_position = calculateDockedPosition(win, effective_size);
                ImGuiCond position_condition = (frame_count <= 3) ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
                ImGui::SetNextWindowPos(floating_position, position_condition);

                if (frame_count <= 5) {
                    debug("  🎈 Floating Position: " + std::to_string((int)floating_position.x) + "," + std::to_string((int)floating_position.y));
                }
            } else {
                // For docked windows, calculate position and force it during initial frames
                ImVec2 dock_position = calculateDockedPosition(win, effective_size);

                ImGuiCond condition = (frame_count <= 3) ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
                ImGui::SetNextWindowSize(effective_size, condition);
                ImGui::SetNextWindowPos(dock_position, condition);

                if (frame_count <= 5) {
                    debug("  📍 Docked Position: " + std::to_string((int)dock_position.x) + "," + std::to_string((int)dock_position.y));
                }
            }

            // Window flags
            ImGuiWindowFlags flags = ImGuiWindowFlags_None;
            if (!win.resizable) flags |= ImGuiWindowFlags_NoResize;

            // Render window
            if (ImGui::Begin(win.title.c_str(), win.closeable ? &win.is_open : nullptr, flags)) {
                // Log actual ImGui window size after rendering (first 5 frames only)
                if (frame_count <= 5) {
                    ImVec2 current_size = ImGui::GetWindowSize();
                    ImVec2 current_pos = ImGui::GetWindowPos();
                    debug("  🎯 ImGui Actual: pos=" + std::to_string((int)current_pos.x) + "," + std::to_string((int)current_pos.y) +
                          " size=" + std::to_string((int)current_size.x) + "x" + std::to_string((int)current_size.y) + "px");
                }

                renderWindowContent(win);
            }
            ImGui::End();
        }
    }

    void renderWindowContent(const WindowInfo& win) {
        switch (win.data_type) {
            case DataType::ECONOMY:
                renderEconomyContent(win.content_data);
                break;
            case DataType::MAP:
                renderMapContent(win.content_data);
                break;
            case DataType::INVENTORY:
                renderInventoryContent(win.content_data);
                break;
            case DataType::CONSOLE:
                renderConsoleContent(win.content_data);
                break;
            case DataType::PERFORMANCE:
                renderPerformanceContent(win.content_data);
                break;
            case DataType::COMPANIES:
                renderCompaniesContent(win.content_data);
                break;
            case DataType::ALERTS:
                renderAlertsContent(win.content_data);
                break;
            case DataType::SETTINGS:
                renderSettingsContent(win.content_data);
                break;
            default:
                renderGenericContent(win.content_data);
                break;
        }
    }

public:
    // ========================================
    // IUI INTERFACE IMPLEMENTATION - DATA DISPLAY
    // ========================================

    void showData(DataType dataType, const json& data) override {
        // Extract window configuration
        json window_config = data.value("window", json{});
        json content = data.value("content", data);

        // Generate ID if not provided
        std::string window_id = window_config.value("id", "window_" + std::to_string(windows.size()));

        // Create or update window info
        WindowInfo& win = windows[window_id];
        win.id = window_id;
        win.title = window_config.value("title", toString(dataType));
        win.data_type = dataType;
        win.content_data = content;
        win.is_open = true;

        // Parse parent first (needed for size calculations)
        win.parent = window_config.value("parent", "");

        // Parse size configuration with percentage support
        if (window_config.contains("size")) {
            auto size_config = window_config["size"];
            if (size_config.is_object()) {
                if (size_config.contains("width")) {
                    auto width_val = size_config["width"];
                    if (width_val.is_string()) {
                        win.size_width_percent = width_val.get<std::string>();
                        debug("🔧 Processing width percentage '" + win.size_width_percent +
                              "' for window '" + win.id + "' with parent='" + win.parent + "'");

                        // Calculate parent size for percentage - use dock size if docked
                        float parent_width = screen_size.x;
                        if (!win.parent.empty() && docks.find(win.parent) != docks.end()) {
                            parent_width = docks[win.parent].size.x;
                            debug("🔍 Found parent dock '" + win.parent + "' with width=" +
                                  std::to_string((int)parent_width) + "px");
                        } else if (!win.parent.empty()) {
                            debug("❌ Parent dock '" + win.parent + "' not found! Using screen width.");
                        }

                        win.size.x = parseSize(width_val, parent_width, 400);
                    } else if (width_val.is_number()) {
                        win.size.x = width_val.get<float>();
                    } else {
                        win.size.x = 400;  // Default fallback
                    }
                }

                if (size_config.contains("height")) {
                    auto height_val = size_config["height"];
                    if (height_val.is_string()) {
                        win.size_height_percent = height_val.get<std::string>();
                        float parent_height = screen_size.y;
                        if (!win.parent.empty() && docks.find(win.parent) != docks.end()) {
                            parent_height = docks[win.parent].size.y;
                        }
                        win.size.y = parseSize(height_val, parent_height, 300);
                    } else if (height_val.is_number()) {
                        win.size.y = height_val.get<float>();
                    } else {
                        win.size.y = 300;  // Default fallback
                    }
                }
            }
        }

        // Parse constraints
        if (window_config.contains("min_size")) {
            auto min_config = window_config["min_size"];
            if (min_config.is_object()) {
                if (min_config.contains("width")) {
                    win.min_size.x = parseSize(min_config["width"], screen_size.x, 100);
                } else {
                    win.min_size.x = 100;
                }
                if (min_config.contains("height")) {
                    win.min_size.y = parseSize(min_config["height"], screen_size.y, 100);
                } else {
                    win.min_size.y = 100;
                }
            }
        }

        if (window_config.contains("max_size")) {
            auto max_config = window_config["max_size"];
            if (max_config.is_object()) {
                if (max_config.contains("width")) {
                    win.max_size.x = parseSize(max_config["width"], screen_size.x, 2000);
                } else {
                    win.max_size.x = 2000;
                }
                if (max_config.contains("height")) {
                    win.max_size.y = parseSize(max_config["height"], screen_size.y, 1500);
                } else {
                    win.max_size.y = 1500;
                }
            }
        }

        // Parse other properties
        win.is_floating = window_config.value("floating", false);
        win.resizable = window_config.value("resizable", true);
        win.closeable = window_config.value("closeable", true);

        // Parse dock position if specified
        if (window_config.contains("dock")) {
            std::string dock_str = window_config["dock"].get<std::string>();
            if (dock_str == "left") win.dock_position = DockPosition::LEFT;
            else if (dock_str == "right") win.dock_position = DockPosition::RIGHT;
            else if (dock_str == "top") win.dock_position = DockPosition::TOP;
            else if (dock_str == "bottom") win.dock_position = DockPosition::BOTTOM;
            else if (dock_str == "tab") win.dock_position = DockPosition::CENTER; // tabs go in center
            else win.dock_position = DockPosition::CENTER;
        }

        if (window_config.contains("position")) {
            auto pos = window_config["position"];
            if (pos.is_object()) {
                win.position.x = pos.value("x", 0);
                win.position.y = pos.value("y", 0);
            }
        }
    }

    void showDataCustom(const std::string& customType, const json& data) override {
        // Treat as generic data with custom type in title
        json modified_data = data;
        if (!modified_data.contains("window")) {
            modified_data["window"] = json{};
        }
        if (!modified_data["window"].contains("title")) {
            modified_data["window"]["title"] = customType;
        }

        showData(DataType::CUSTOM, modified_data);
    }

    // ========================================
    // IUI INTERFACE IMPLEMENTATION - REQUESTS & EVENTS
    // ========================================

    void onRequest(RequestType requestType, std::function<void(const json&)> callback) override;
    void onRequestCustom(const std::string& customType, std::function<void(const json&)> callback) override;
    void showEvent(EventLevel level, const std::string& message) override;

    // ========================================
    // WINDOW MANAGEMENT IMPLEMENTATION
    // ========================================

    void createDock(const std::string& dockId, DockType type, DockPosition position, const json& config = {}) override;
    void createSplit(const std::string& dockId, Orientation orientation, const json& config = {}) override;
    void closeWindow(const std::string& windowId) override;
    void focusWindow(const std::string& windowId) override;

    // ========================================
    // STATE MANAGEMENT
    // ========================================

    json getState() const override;
    void setState(const json& state) override;

private:
    // ========================================
    // CONTENT RENDERING IMPLEMENTATIONS
    // ========================================

    void renderEconomyContent(const json& content);
    void renderMapContent(const json& content);
    void renderInventoryContent(const json& content);
    void renderConsoleContent(const json& content);
    void renderPerformanceContent(const json& content);
    void renderCompaniesContent(const json& content);
    void renderAlertsContent(const json& content);
    void renderSettingsContent(const json& content);
    void renderGenericContent(const json& content);
    void renderLogConsole();
};

} // namespace grove