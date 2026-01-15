/**
 * TestControllerModule - Simulates game logic for integration tests
 *
 * This module:
 * - Subscribes to UI events (clicks, actions, value changes)
 * - Responds to UI interactions
 * - Updates UI state via IIO messages
 * - Demonstrates bidirectional UI ↔ Game communication
 */

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/JsonDataNode.h>

#include <memory>
#include <string>
#include <iostream>

extern "C" {

class TestControllerModule : public grove::IModule {
public:
    TestControllerModule() = default;
    ~TestControllerModule() override = default;

    void setConfiguration(const grove::IDataNode& config, grove::IIO* io, grove::ITaskScheduler* scheduler) override {
        m_io = io;
        m_uiInstanceId = config.getString("uiInstanceId", "ui_module");

        std::cout << "[TestController] Initializing...\n";

        // Subscribe to UI events
        if (m_io) {
            m_io->subscribe("ui:click");
            m_io->subscribe("ui:action");
            m_io->subscribe("ui:value_changed");
            m_io->subscribe("ui:text_changed");
            m_io->subscribe("ui:text_submit");
            m_io->subscribe("ui:hover");
            m_io->subscribe("ui:focus_gained");
            m_io->subscribe("ui:focus_lost");
        }

        std::cout << "[TestController] Subscribed to UI events\n";
    }

    void process(const grove::IDataNode& input) override {
        if (!m_io) return;

        m_frameCount++;

        // Process incoming UI events
        while (m_io->hasMessages() > 0) {
            auto msg = m_io->pullMessage();

            if (msg.topic == "ui:click") {
                handleClick(*msg.data);
            }
            else if (msg.topic == "ui:action") {
                handleAction(*msg.data);
            }
            else if (msg.topic == "ui:value_changed") {
                handleValueChanged(*msg.data);
            }
            else if (msg.topic == "ui:text_changed") {
                handleTextChanged(*msg.data);
            }
            else if (msg.topic == "ui:text_submit") {
                handleTextSubmit(*msg.data);
            }
            else if (msg.topic == "ui:hover") {
                handleHover(*msg.data);
            }
            else if (msg.topic == "ui:focus_gained") {
                handleFocusGained(*msg.data);
            }
            else if (msg.topic == "ui:focus_lost") {
                handleFocusLost(*msg.data);
            }
        }

        // Simulate some game logic
        if (m_frameCount % 120 == 0) {  // Every 2 seconds at 60fps
            // Could update UI here
            std::cout << "[TestController] Heartbeat at frame " << m_frameCount << "\n";
        }
    }

    const grove::IDataNode& getConfiguration() override {
        if (!m_config) {
            m_config = std::make_unique<grove::JsonDataNode>("config");
        }
        return *m_config;
    }

    std::unique_ptr<grove::IDataNode> getHealthStatus() override {
        auto health = std::make_unique<grove::JsonDataNode>("health");
        health->setString("status", "healthy");
        health->setString("module", "TestControllerModule");
        health->setInt("frameCount", static_cast<int>(m_frameCount));
        health->setInt("clicksReceived", m_clickCount);
        health->setInt("actionsReceived", m_actionCount);
        return health;
    }

    void shutdown() override {
        std::cout << "[TestController] Shutting down...\n";
        std::cout << "  Total frames: " << m_frameCount << "\n";
        std::cout << "  Clicks received: " << m_clickCount << "\n";
        std::cout << "  Actions received: " << m_actionCount << "\n";
        std::cout << "  Value changes: " << m_valueChangeCount << "\n";
        m_io = nullptr;
    }

    std::unique_ptr<grove::IDataNode> getState() override {
        auto state = std::make_unique<grove::JsonDataNode>("state");
        state->setInt("frameCount", static_cast<int>(m_frameCount));
        state->setInt("clickCount", m_clickCount);
        state->setInt("actionCount", m_actionCount);
        state->setInt("valueChangeCount", m_valueChangeCount);
        return state;
    }

    void setState(const grove::IDataNode& state) override {
        m_frameCount = static_cast<uint64_t>(state.getInt("frameCount", 0));
        m_clickCount = state.getInt("clickCount", 0);
        m_actionCount = state.getInt("actionCount", 0);
        m_valueChangeCount = state.getInt("valueChangeCount", 0);
    }

    std::string getType() const override {
        return "TestControllerModule";
    }

    bool isIdle() const override {
        return true;
    }

private:
    grove::IIO* m_io = nullptr;
    std::string m_uiInstanceId;
    std::unique_ptr<grove::JsonDataNode> m_config;

    // Stats
    uint64_t m_frameCount = 0;
    int m_clickCount = 0;
    int m_actionCount = 0;
    int m_valueChangeCount = 0;
    int m_textChangeCount = 0;

    void handleClick(const grove::IDataNode& data) {
        m_clickCount++;
        std::string widgetId = data.getString("widgetId", "");
        double x = data.getDouble("x", 0.0);
        double y = data.getDouble("y", 0.0);

        std::cout << "[TestController] Click on '" << widgetId
                  << "' at (" << x << ", " << y << ")\n";

        // Example: Update UI visibility based on click
        if (widgetId == "btn_toggle") {
            auto visibilityMsg = std::make_unique<grove::JsonDataNode>("set_visible");
            visibilityMsg->setString("id", "hidden_panel");
            visibilityMsg->setBool("visible", true);
            m_io->publish("ui:set_visible", std::move(visibilityMsg));
        }
    }

    void handleAction(const grove::IDataNode& data) {
        m_actionCount++;
        std::string action = data.getString("action", "");
        std::string widgetId = data.getString("widgetId", "");

        std::cout << "[TestController] Action: '" << action
                  << "' from '" << widgetId << "'\n";

        // Example: Handle game actions
        if (action == "game:start") {
            std::cout << "  → Starting game...\n";
        }
        else if (action == "game:quit") {
            std::cout << "  → Quitting game...\n";
        }
        else if (action == "settings:volume") {
            double volume = data.getDouble("value", 50.0);
            std::cout << "  → Setting volume to " << volume << "\n";
        }
    }

    void handleValueChanged(const grove::IDataNode& data) {
        m_valueChangeCount++;
        std::string widgetId = data.getString("widgetId", "");

        if (data.hasChild("value")) {
            double value = data.getDouble("value", 0.0);
            std::cout << "[TestController] Value changed: '" << widgetId
                      << "' = " << value << "\n";
        }
        else if (data.hasChild("checked")) {
            bool checked = data.getBool("checked", false);
            std::cout << "[TestController] Checkbox changed: '" << widgetId
                      << "' = " << (checked ? "checked" : "unchecked") << "\n";
        }
    }

    void handleTextChanged(const grove::IDataNode& data) {
        m_textChangeCount++;
        std::string widgetId = data.getString("widgetId", "");
        std::string text = data.getString("text", "");

        std::cout << "[TestController] Text changed: '" << widgetId
                  << "' = \"" << text << "\"\n";
    }

    void handleTextSubmit(const grove::IDataNode& data) {
        std::string widgetId = data.getString("widgetId", "");
        std::string text = data.getString("text", "");

        std::cout << "[TestController] Text submitted: '" << widgetId
                  << "' = \"" << text << "\"\n";

        // Example: Process username input
        if (widgetId == "username_input") {
            std::cout << "  → Username entered: " << text << "\n";
        }
    }

    void handleHover(const grove::IDataNode& data) {
        std::string widgetId = data.getString("widgetId", "");
        bool enter = data.getBool("enter", false);

        if (enter && !widgetId.empty()) {
            // Only log enter events to reduce spam
            // std::cout << "[TestController] Hover: '" << widgetId << "'\n";
        }
    }

    void handleFocusGained(const grove::IDataNode& data) {
        std::string widgetId = data.getString("widgetId", "");
        std::cout << "[TestController] Focus gained: '" << widgetId << "'\n";
    }

    void handleFocusLost(const grove::IDataNode& data) {
        std::string widgetId = data.getString("widgetId", "");
        std::cout << "[TestController] Focus lost: '" << widgetId << "'\n";
    }
};

// Module factory functions (standard GroveEngine interface)
grove::IModule* createModule() {
    return new TestControllerModule();
}

void destroyModule(grove::IModule* module) {
    delete module;
}

} // extern "C"
