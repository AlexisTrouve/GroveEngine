#include "warfactory/ImGuiUI.h"
#include <sstream>
#include <iomanip>
#include <iostream>

namespace warfactory {

// ========================================
// IUI INTERFACE IMPLEMENTATION - REQUESTS & EVENTS
// ========================================

void ImGuiUI::onRequest(RequestType requestType, std::function<void(const json&)> callback) {
    request_callbacks[requestType] = callback;
}

void ImGuiUI::onRequestCustom(const std::string& customType, std::function<void(const json&)> callback) {
    custom_request_callbacks[customType] = callback;
}

void ImGuiUI::showEvent(EventLevel level, const std::string& message) {
    LogMessage log_msg;
    log_msg.level = level;
    log_msg.message = message;
    log_msg.timestamp = std::chrono::steady_clock::now();

    log_messages.push_back(log_msg);

    // Keep only last MAX_LOG_MESSAGES
    if (log_messages.size() > MAX_LOG_MESSAGES) {
        log_messages.erase(log_messages.begin());
    }

    // Also output to console for debugging
    const char* level_str = toString(level);
    std::cout << "[" << level_str << "] " << message << std::endl;
}

// ========================================
// WINDOW MANAGEMENT IMPLEMENTATION
// ========================================

void ImGuiUI::createDock(const std::string& dockId, DockType type, DockPosition position, const json& config) {
    DockInfo& dock = docks[dockId];
    dock.id = dockId;
    dock.type = type;
    dock.position = position;
    dock.parent = config.value("parent", "");

    // Parse size with percentage support
    if (config.contains("size")) {
        auto size_config = config["size"];
        if (size_config.is_object()) {
            if (size_config.contains("width")) {
                dock.size.x = parseSize(size_config["width"], screen_size.x, 300);
            } else {
                dock.size.x = 300;  // Default
            }
            if (size_config.contains("height")) {
                dock.size.y = parseSize(size_config["height"], screen_size.y, 200);
            } else {
                dock.size.y = 200;  // Default
            }
        }
    }

    if (config.contains("min_size")) {
        auto min_config = config["min_size"];
        if (min_config.is_object()) {
            if (min_config.contains("width")) {
                dock.min_size.x = parseSize(min_config["width"], screen_size.x, 100);
            } else {
                dock.min_size.x = 100;
            }
            if (min_config.contains("height")) {
                dock.min_size.y = parseSize(min_config["height"], screen_size.y, 100);
            } else {
                dock.min_size.y = 100;
            }
        }
    }

    if (config.contains("max_size")) {
        auto max_config = config["max_size"];
        if (max_config.is_object()) {
            if (max_config.contains("width")) {
                dock.max_size.x = parseSize(max_config["width"], screen_size.x, 1000);
            } else {
                dock.max_size.x = 1000;
            }
            if (max_config.contains("height")) {
                dock.max_size.y = parseSize(max_config["height"], screen_size.y, 800);
            } else {
                dock.max_size.y = 800;
            }
        }
    }

    dock.collapsible = config.value("collapsible", true);
    dock.resizable = config.value("resizable", true);

    // Debug logging for dock creation
    showEvent(EventLevel::DEBUG, "🏗️ Created dock '" + dockId + "': " + std::string(toString(type)) +
              " size=" + std::to_string((int)dock.size.x) + "x" + std::to_string((int)dock.size.y) + "px");

    showEvent(EventLevel::INFO, "Created " + std::string(toString(type)) + " dock: " + dockId);
}

void ImGuiUI::createSplit(const std::string& dockId, Orientation orientation, const json& config) {
    // Create as a split dock
    json split_config = config;
    split_config["orientation"] = toString(orientation);
    createDock(dockId, DockType::SPLIT, DockPosition::CENTER, split_config);
}

void ImGuiUI::closeWindow(const std::string& windowId) {
    auto it = windows.find(windowId);
    if (it != windows.end()) {
        it->second.is_open = false;
        showEvent(EventLevel::INFO, "Closed window: " + windowId);
    }
}

void ImGuiUI::focusWindow(const std::string& windowId) {
    auto it = windows.find(windowId);
    if (it != windows.end()) {
        ImGui::SetWindowFocus(it->second.title.c_str());
        showEvent(EventLevel::DEBUG, "Focused window: " + windowId);
    }
}

// ========================================
// STATE MANAGEMENT
// ========================================

json ImGuiUI::getState() const {
    json state;
    state["frame_count"] = frame_count;
    state["window_open"] = !should_close;
    state["screen_size"] = {{"width", screen_size.x}, {"height", screen_size.y}};

    // Save window states
    json window_states = json::object();
    for (const auto& [id, win] : windows) {
        window_states[id] = {
            {"is_open", win.is_open},
            {"size", {{"width", win.size.x}, {"height", win.size.y}}},
            {"position", {{"x", win.position.x}, {"y", win.position.y}}},
            {"floating", win.is_floating}
        };
    }
    state["windows"] = window_states;

    return state;
}

void ImGuiUI::setState(const json& state) {
    if (state.contains("windows")) {
        for (const auto& [id, win_state] : state["windows"].items()) {
            auto it = windows.find(id);
            if (it != windows.end()) {
                auto& win = it->second;
                win.is_open = win_state.value("is_open", true);

                if (win_state.contains("size")) {
                    auto size_state = win_state["size"];
                    if (size_state.is_object()) {
                        win.size.x = size_state.value("width", win.size.x);
                        win.size.y = size_state.value("height", win.size.y);
                    }
                }

                if (win_state.contains("position")) {
                    auto pos_state = win_state["position"];
                    if (pos_state.is_object()) {
                        win.position.x = pos_state.value("x", win.position.x);
                        win.position.y = pos_state.value("y", win.position.y);
                    }
                }

                win.is_floating = win_state.value("floating", win.is_floating);
            }
        }
    }
}

// ========================================
// CONTENT RENDERING IMPLEMENTATIONS
// ========================================

void ImGuiUI::renderEconomyContent(const json& content) {
    ImGui::Text("💰 Economy Dashboard");
    ImGui::Separator();

    if (content.contains("prices")) {
        ImGui::Text("Market Prices:");
        ImGui::BeginTable("prices_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg);

        ImGui::TableSetupColumn("Item");
        ImGui::TableSetupColumn("Price");
        ImGui::TableSetupColumn("Trend");
        ImGui::TableHeadersRow();

        for (const auto& [item, price] : content["prices"].items()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", item.c_str());
            ImGui::TableNextColumn();
            if (price.is_number()) {
                ImGui::Text("%.2f", price.get<float>());
            } else {
                ImGui::Text("%s", price.dump().c_str());
            }
            ImGui::TableNextColumn();

            // Show trend if available
            if (content.contains("trends") && content["trends"].contains(item)) {
                std::string trend = content["trends"][item];
                if (trend[0] == '+') {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", trend.c_str());
                } else if (trend[0] == '-') {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", trend.c_str());
                } else {
                    ImGui::Text("%s", trend.c_str());
                }
            } else {
                ImGui::Text("--");
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();

    // Action buttons
    if (ImGui::Button("🔄 Refresh Prices")) {
        if (request_callbacks.count(RequestType::GET_PRICES)) {
            request_callbacks[RequestType::GET_PRICES]({});
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("📊 Market Analysis")) {
        if (custom_request_callbacks.count("market_analysis")) {
            custom_request_callbacks["market_analysis"]({});
        }
    }
}

void ImGuiUI::renderMapContent(const json& content) {
    ImGui::Text("🗺️ Global Map");
    ImGui::Separator();

    if (content.contains("current_chunk")) {
        auto chunk = content["current_chunk"];
        if (chunk.is_object()) {
            ImGui::Text("Current Chunk: (%d, %d)", chunk.value("x", 0), chunk.value("y", 0));
        }
    }

    if (content.contains("tiles")) {
        ImGui::Text("Map Display:");

        // Navigation controls
        if (ImGui::Button("⬆️")) {
            if (request_callbacks.count(RequestType::GET_CHUNK)) {
                request_callbacks[RequestType::GET_CHUNK]({{"action", "move_up"}});
            }
        }

        if (ImGui::Button("⬅️")) {
            if (request_callbacks.count(RequestType::GET_CHUNK)) {
                request_callbacks[RequestType::GET_CHUNK]({{"action", "move_left"}});
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("➡️")) {
            if (request_callbacks.count(RequestType::GET_CHUNK)) {
                request_callbacks[RequestType::GET_CHUNK]({{"action", "move_right"}});
            }
        }

        if (ImGui::Button("⬇️")) {
            if (request_callbacks.count(RequestType::GET_CHUNK)) {
                request_callbacks[RequestType::GET_CHUNK]({{"action", "move_down"}});
            }
        }

        // Simple tile grid representation
        ImGui::Text("Tile Grid (sample):");
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 8; x++) {
                if (x > 0) ImGui::SameLine();

                // Generate simple tile representation
                char tile_str[2] = ".";  // Null-terminated string
                if ((x + y) % 3 == 0) tile_str[0] = 'I';  // Iron
                else if ((x + y) % 5 == 0) tile_str[0] = 'C';  // Copper
                else if ((x + y) % 7 == 0) tile_str[0] = 'T';  // Tree

                ImGui::Button(tile_str, ImVec2(20, 20));
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("🔄 Refresh Map")) {
        if (request_callbacks.count(RequestType::GET_CHUNK)) {
            request_callbacks[RequestType::GET_CHUNK]({{"type", "refresh"}});
        }
    }
}

void ImGuiUI::renderInventoryContent(const json& content) {
    ImGui::Text("🎒 Inventory");
    ImGui::Separator();

    if (content.contains("items")) {
        ImGui::BeginTable("inventory_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg);
        ImGui::TableSetupColumn("Item");
        ImGui::TableSetupColumn("Quantity");
        ImGui::TableSetupColumn("Reserved");
        ImGui::TableHeadersRow();

        for (const auto& item : content["items"]) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", item.value("name", "Unknown").c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", item.value("quantity", 0));
            ImGui::TableNextColumn();
            ImGui::Text("%d", item.value("reserved", 0));
        }

        ImGui::EndTable();
    }
}

void ImGuiUI::renderConsoleContent(const json& content) {
    ImGui::Text("🖥️ Console");
    ImGui::Separator();

    // Console output area
    ImGui::BeginChild("console_output", ImVec2(0, -30), true);

    if (content.contains("logs")) {
        for (const auto& log : content["logs"]) {
            std::string level = log.value("level", "info");
            std::string message = log.value("message", "");
            std::string timestamp = log.value("timestamp", "");

            // Color based on level
            if (level == "error") {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "[%s] %s - %s",
                                 timestamp.c_str(), level.c_str(), message.c_str());
            } else if (level == "warning") {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[%s] %s - %s",
                                 timestamp.c_str(), level.c_str(), message.c_str());
            } else if (level == "success") {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[%s] %s - %s",
                                 timestamp.c_str(), level.c_str(), message.c_str());
            } else {
                ImGui::Text("[%s] %s - %s", timestamp.c_str(), level.c_str(), message.c_str());
            }
        }
    }

    ImGui::EndChild();

    // Command input
    static char command_buffer[256] = "";
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##command", command_buffer, sizeof(command_buffer),
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (custom_request_callbacks.count("console_command")) {
            custom_request_callbacks["console_command"]({{"command", std::string(command_buffer)}});
        }
        command_buffer[0] = '\0';  // Clear buffer
    }
}

void ImGuiUI::renderPerformanceContent(const json& content) {
    ImGui::Text("📊 Performance Monitor");
    ImGui::Separator();

    if (content.contains("fps")) {
        ImGui::Text("FPS: %d", content.value("fps", 0));
    }
    if (content.contains("frame_time")) {
        ImGui::Text("Frame Time: %s", content.value("frame_time", "0ms").c_str());
    }
    if (content.contains("memory_usage")) {
        ImGui::Text("Memory: %s", content.value("memory_usage", "0MB").c_str());
    }
    if (content.contains("entities")) {
        ImGui::Text("Entities: %d", content.value("entities", 0));
    }

    // Real-time FPS display
    ImGui::Spacing();
    ImGui::Text("Real-time FPS: %.1f", ImGui::GetIO().Framerate);
}

void ImGuiUI::renderCompaniesContent(const json& content) {
    ImGui::Text("🏢 Companies");
    ImGui::Separator();

    for (const auto& [company_name, company_data] : content.items()) {
        if (ImGui::CollapsingHeader(company_name.c_str())) {
            if (company_data.contains("cash")) {
                ImGui::Text("💰 Cash: $%d", company_data.value("cash", 0));
            }
            if (company_data.contains("status")) {
                ImGui::Text("📊 Status: %s", company_data.value("status", "unknown").c_str());
            }
            if (company_data.contains("strategy")) {
                ImGui::Text("🎯 Strategy: %s", company_data.value("strategy", "none").c_str());
            }
        }
    }
}

void ImGuiUI::renderAlertsContent(const json& content) {
    ImGui::Text("⚠️ Alerts");
    ImGui::Separator();

    if (content.contains("urgent_alerts")) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "🚨 URGENT:");
        for (const auto& alert : content["urgent_alerts"]) {
            if (alert.is_string()) {
                ImGui::BulletText("%s", alert.get<std::string>().c_str());
            } else {
                ImGui::BulletText("%s", alert.dump().c_str());
            }
        }
    }

    if (content.contains("warnings")) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "⚠️ Warnings:");
        for (const auto& warning : content["warnings"]) {
            if (warning.is_string()) {
                ImGui::BulletText("%s", warning.get<std::string>().c_str());
            } else {
                ImGui::BulletText("%s", warning.dump().c_str());
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("✅ Acknowledge All")) {
        if (custom_request_callbacks.count("acknowledge_alerts")) {
            custom_request_callbacks["acknowledge_alerts"]({});
        }
    }
}

void ImGuiUI::renderSettingsContent(const json& content) {
    ImGui::Text("⚙️ Settings");
    ImGui::Separator();

    if (content.contains("graphics")) {
        if (ImGui::CollapsingHeader("🖥️ Graphics")) {
            auto graphics = content["graphics"];
            if (graphics.is_object()) {
                ImGui::Text("Resolution: %s", graphics.value("resolution", "Unknown").c_str());
                bool fullscreen = graphics.value("fullscreen", false);
                if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
                    // Handle setting change
                }
                bool vsync = graphics.value("vsync", true);
                if (ImGui::Checkbox("VSync", &vsync)) {
                    // Handle setting change
                }
            }
        }
    }

    if (content.contains("audio")) {
        if (ImGui::CollapsingHeader("🔊 Audio")) {
            auto audio = content["audio"];
            if (audio.is_object()) {
                float master_vol = audio.value("master_volume", 1.0f);
                if (ImGui::SliderFloat("Master Volume", &master_vol, 0.0f, 1.0f)) {
                    // Handle setting change
                }
                float effects_vol = audio.value("effects_volume", 1.0f);
                if (ImGui::SliderFloat("Effects Volume", &effects_vol, 0.0f, 1.0f)) {
                    // Handle setting change
                }
            }
        }
    }
}

void ImGuiUI::renderGenericContent(const json& content) {
    ImGui::Text("📄 Data");
    ImGui::Separator();

    // Generic JSON display
    std::ostringstream oss;
    oss << content.dump(2);  // Pretty print with 2-space indent
    ImGui::TextWrapped("%s", oss.str().c_str());
}

void ImGuiUI::renderLogConsole() {
    // Always visible log console at bottom
    ImGui::SetNextWindowSize(ImVec2(screen_size.x, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(0, screen_size.y - 200), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("📜 System Log", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::BeginChild("log_scroll", ImVec2(0, 150), true);

        for (const auto& log_msg : log_messages) {
            auto duration = log_msg.timestamp.time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() % 100000;

            const char* level_str = toString(log_msg.level);

            // Color based on level
            ImVec4 color = {1.0f, 1.0f, 1.0f, 1.0f};  // Default white
            switch (log_msg.level) {
                case EventLevel::ERROR: color = {1.0f, 0.0f, 0.0f, 1.0f}; break;
                case EventLevel::WARNING: color = {1.0f, 1.0f, 0.0f, 1.0f}; break;
                case EventLevel::SUCCESS: color = {0.0f, 1.0f, 0.0f, 1.0f}; break;
                case EventLevel::DEBUG: color = {0.7f, 0.7f, 0.7f, 1.0f}; break;
                case EventLevel::INFO:
                default: color = {1.0f, 1.0f, 1.0f, 1.0f}; break;
            }

            ImGui::TextColored(color, "[%05lld] [%s] %s",
                             millis, level_str, log_msg.message.c_str());
        }

        // Auto-scroll to bottom
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace warfactory