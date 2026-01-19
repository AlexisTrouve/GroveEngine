#include "UIModule.h"
#include "Core/UIContext.h"
#include "Core/UITree.h"
#include "Core/UIWidget.h"
#include "Core/UITooltip.h"
#include "Rendering/UIRenderer.h"
#include "Widgets/UIButton.h"
#include "Widgets/UISlider.h"
#include "Widgets/UICheckbox.h"
#include "Widgets/UITextInput.h"
#include "Widgets/UIScrollPanel.h"
#include "Widgets/UILabel.h"

#include <grove/JsonDataNode.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

// Forward declarations for hit testing functions in UIContext.cpp
namespace grove {
    UIWidget* hitTest(UIWidget* widget, float x, float y);
    void updateHoverState(UIWidget* widget, UIContext& ctx, const std::string& prevHoveredId);
    UIWidget* dispatchMouseButton(UIWidget* widget, UIContext& ctx, int button, bool pressed);
}

namespace grove {

UIModule::UIModule() = default;
UIModule::~UIModule() = default;

void UIModule::setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) {
    m_io = io;

    // Setup logger
    m_logger = spdlog::get("UIModule");
    if (!m_logger) {
        m_logger = spdlog::stdout_color_mt("UIModule");
    }

    m_logger->info("Initializing UIModule");

    // Initialize subsystems
    m_context = std::make_unique<UIContext>();
    m_tree = std::make_unique<UITree>();
    m_renderer = std::make_unique<UIRenderer>(io);
    m_tooltipManager = std::make_unique<UITooltipManager>();

    // Read screen size from config
    m_context->screenWidth = static_cast<float>(config.getInt("windowWidth", 1280));
    m_context->screenHeight = static_cast<float>(config.getInt("windowHeight", 720));

    // Set base UI layer
    int baseLayer = config.getInt("baseLayer", 1000);
    m_renderer->setBaseLayer(baseLayer);

    // Load layout if specified
    std::string layoutFile = config.getString("layoutFile", "");
    if (!layoutFile.empty()) {
        if (loadLayout(layoutFile)) {
            m_logger->info("Loaded layout from: {}", layoutFile);
        } else {
            m_logger->error("Failed to load layout: {}", layoutFile);
        }
    }

    // Check for inline layout data (const_cast safe for read-only operations)
    auto& mutableConfig = const_cast<IDataNode&>(config);
    if (auto* layoutData = mutableConfig.getChildReadOnly("layout")) {
        if (loadLayoutData(*layoutData)) {
            m_logger->info("Loaded inline layout data");
        }
    }

    // Subscribe to input topics with callbacks
    if (m_io) {
        m_io->subscribe("input:mouse:move", [this](const Message& msg) {
            m_context->mouseX = static_cast<float>(msg.data->getDouble("x", 0.0));
            m_context->mouseY = static_cast<float>(msg.data->getDouble("y", 0.0));
        });

        m_io->subscribe("input:mouse:button", [this](const Message& msg) {
            bool pressed = msg.data->getBool("pressed", false);
            if (pressed && !m_context->mouseDown) {
                m_context->mousePressed = true;
            }
            if (!pressed && m_context->mouseDown) {
                m_context->mouseReleased = true;
            }
            m_context->mouseDown = pressed;
        });

        m_io->subscribe("input:mouse:wheel", [this](const Message& msg) {
            m_context->mouseWheelDelta = static_cast<float>(msg.data->getDouble("delta", 0.0));
        });

        m_io->subscribe("input:keyboard", [this](const Message& msg) {
            m_context->keyPressed = true;
            m_context->keyCode = msg.data->getInt("keyCode", 0);
            m_context->keyChar = static_cast<char>(msg.data->getInt("char", 0));
        });

        m_io->subscribe("ui:load", [this](const Message& msg) {
            std::string layoutPath = msg.data->getString("path", "");
            if (!layoutPath.empty()) {
                loadLayout(layoutPath);
            }
        });

        m_io->subscribe("ui:set_visible", [this](const Message& msg) {
            std::string widgetId = msg.data->getString("id", "");
            bool visible = msg.data->getBool("visible", true);
            if (m_root) {
                if (UIWidget* widget = m_root->findById(widgetId)) {
                    widget->visible = visible;
                }
            }
        });

        m_io->subscribe("ui:set_text", [this](const Message& msg) {
            // Timestamp on receive
            auto now = std::chrono::high_resolution_clock::now();
            auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

            std::string widgetId = msg.data->getString("id", "");
            std::string text = msg.data->getString("text", "");

            // Extract original timestamp if present
            double t0 = msg.data->getDouble("_timestamp_publish", 0);
            if (t0 > 0) {
                double latency = (micros - t0) / 1000.0; // Convert to milliseconds
                m_logger->info("⏱️ [T3] UIModule received ui:set_text at {} µs (latency from T0: {:.2f} ms)", micros, latency);
            } else {
                m_logger->info("⏱️ [T3] UIModule received ui:set_text at {} µs", micros);
            }

            if (m_root) {
                if (UIWidget* widget = m_root->findById(widgetId)) {
                    // Only labels support text updates
                    if (widget->getType() == "label") {
                        UILabel* label = static_cast<UILabel*>(widget);
                        label->text = text;
                        m_logger->info("Updated text for label '{}': '{}'", widgetId, text);
                    } else {
                        m_logger->warn("Widget '{}' is not a label, cannot set text", widgetId);
                    }
                }
            }
        });
    }

    m_logger->info("UIModule initialized");
}

void UIModule::process(const IDataNode& input) {
    float deltaTime = static_cast<float>(input.getDouble("deltaTime", 0.016));

    // Begin new frame
    m_context->beginFrame();
    m_renderer->beginFrame();

    // Process input messages from IIO
    processInput();

    // Update UI logic
    updateUI(deltaTime);

    // Render UI
    renderUI();

    m_frameCount++;
}

void UIModule::processInput() {
    if (!m_io) return;

    // Pull and dispatch all pending messages (callbacks invoked automatically)
    while (m_io->hasMessages() > 0) {
        m_io->pullAndDispatch();
    }
}

void UIModule::updateUI(float deltaTime) {
    if (!m_root) return;

    // Store previous hover state
    std::string prevHoveredId = m_context->hoveredWidgetId;

    // Perform hit testing to update hover state
    UIWidget* hoveredWidget = hitTest(m_root.get(), m_context->mouseX, m_context->mouseY);
    if (hoveredWidget && !hoveredWidget->id.empty()) {
        m_context->hoveredWidgetId = hoveredWidget->id;
    } else {
        m_context->hoveredWidgetId.clear();
    }

    // Update hover state (calls onMouseEnter/onMouseLeave)
    updateHoverState(m_root.get(), *m_context, prevHoveredId);

    // Publish hover event if changed
    if (m_context->hoveredWidgetId != prevHoveredId && m_io) {
        auto hoverEvent = std::make_unique<JsonDataNode>("hover");
        hoverEvent->setString("widgetId", m_context->hoveredWidgetId);
        hoverEvent->setBool("enter", !m_context->hoveredWidgetId.empty());
        m_io->publish("ui:hover", std::move(hoverEvent));
    }

    // Handle mouse wheel for scroll panels
    if (m_context->mouseWheelDelta != 0.0f && hoveredWidget) {
        // Find the first scrollpanel parent or self
        UIWidget* widget = hoveredWidget;
        while (widget) {
            if (widget->getType() == "scrollpanel") {
                UIScrollPanel* scrollPanel = static_cast<UIScrollPanel*>(widget);
                scrollPanel->handleMouseWheel(m_context->mouseWheelDelta);
                break;
            }
            widget = widget->parent;
        }
    }

    // Handle mouse button events
    if (m_context->mousePressed || m_context->mouseReleased) {
        UIWidget* clickedWidget = dispatchMouseButton(
            m_root.get(), *m_context,
            0, // Left button
            m_context->mousePressed
        );

        if (clickedWidget && m_io) {
            // Publish click event
            auto clickEvent = std::make_unique<JsonDataNode>("click");
            clickEvent->setString("widgetId", clickedWidget->id);
            clickEvent->setDouble("x", m_context->mouseX);
            clickEvent->setDouble("y", m_context->mouseY);
            m_io->publish("ui:click", std::move(clickEvent));

            // Publish type-specific events
            std::string widgetType = clickedWidget->getType();

            m_logger->info("🖱️ Widget clicked: id='{}', type='{}', mousePressed={}",
                clickedWidget->id, widgetType, m_context->mousePressed);

            // Handle focus for text inputs
            if (widgetType == "textinput" && m_context->mousePressed) {
                UITextInput* textInput = static_cast<UITextInput*>(clickedWidget);

                // Lose focus on previous widget
                if (!m_context->focusedWidgetId.empty() && m_context->focusedWidgetId != textInput->id) {
                    if (UIWidget* prevFocused = m_root->findById(m_context->focusedWidgetId)) {
                        if (prevFocused->getType() == "textinput") {
                            static_cast<UITextInput*>(prevFocused)->loseFocus();
                        }
                    }

                    auto lostFocusEvent = std::make_unique<JsonDataNode>("focus_lost");
                    lostFocusEvent->setString("widgetId", m_context->focusedWidgetId);
                    m_io->publish("ui:focus_lost", std::move(lostFocusEvent));
                }

                // Gain focus
                textInput->gainFocus();
                m_context->setFocus(textInput->id);

                auto gainedFocusEvent = std::make_unique<JsonDataNode>("focus_gained");
                gainedFocusEvent->setString("widgetId", textInput->id);
                m_io->publish("ui:focus_gained", std::move(gainedFocusEvent));

                m_logger->info("TextInput '{}' gained focus", textInput->id);
            }
            else if (widgetType == "button") {
                // Publish action event if button has onClick
                UIButton* btn = static_cast<UIButton*>(clickedWidget);
                if (!btn->onClick.empty() && m_context->mouseReleased) {
                    auto actionEvent = std::make_unique<JsonDataNode>("action");
                    actionEvent->setString("action", btn->onClick);
                    actionEvent->setString("widgetId", btn->id);
                    m_io->publish("ui:action", std::move(actionEvent));
                    m_logger->info("Button '{}' clicked, action: {}", btn->id, btn->onClick);
                }
            }
            else if (widgetType == "slider") {
                // Publish value_changed event for slider
                UISlider* slider = static_cast<UISlider*>(clickedWidget);
                auto valueEvent = std::make_unique<JsonDataNode>("value");
                valueEvent->setString("widgetId", slider->id);
                valueEvent->setDouble("value", slider->getValue());
                valueEvent->setDouble("min", slider->minValue);
                valueEvent->setDouble("max", slider->maxValue);

                // Add timestamp for latency measurement
                auto now = std::chrono::high_resolution_clock::now();
                auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                valueEvent->setDouble("_timestamp_publish", static_cast<double>(micros));

                m_logger->info("⏱️ [T0] UIModule publishing ui:value_changed at {} µs", micros);
                m_io->publish("ui:value_changed", std::move(valueEvent));

                // Publish onChange action if specified
                if (!slider->onChange.empty()) {
                    auto actionEvent = std::make_unique<JsonDataNode>("action");
                    actionEvent->setString("action", slider->onChange);
                    actionEvent->setString("widgetId", slider->id);
                    actionEvent->setDouble("value", slider->getValue());
                    m_io->publish("ui:action", std::move(actionEvent));
                }
            }
            else if (widgetType == "checkbox") {
                // Publish value_changed event for checkbox
                UICheckbox* checkbox = static_cast<UICheckbox*>(clickedWidget);
                if (m_context->mouseReleased) {  // Only on click release
                    auto valueEvent = std::make_unique<JsonDataNode>("value");
                    valueEvent->setString("widgetId", checkbox->id);
                    valueEvent->setBool("checked", checkbox->checked);
                    m_io->publish("ui:value_changed", std::move(valueEvent));

                    // Publish onChange action if specified
                    if (!checkbox->onChange.empty()) {
                        auto actionEvent = std::make_unique<JsonDataNode>("action");
                        actionEvent->setString("action", checkbox->onChange);
                        actionEvent->setString("widgetId", checkbox->id);
                        actionEvent->setBool("checked", checkbox->checked);
                        m_io->publish("ui:action", std::move(actionEvent));
                    }

                    m_logger->info("Checkbox '{}' toggled to {}", checkbox->id, checkbox->checked);
                }
            }
        }
    }

    // Handle keyboard input for focused widget
    if (m_context->keyPressed && !m_context->focusedWidgetId.empty()) {
        if (UIWidget* focusedWidget = m_root->findById(m_context->focusedWidgetId)) {
            if (focusedWidget->getType() == "textinput") {
                UITextInput* textInput = static_cast<UITextInput*>(focusedWidget);

                // Get character and ctrl state from context
                uint32_t character = static_cast<uint32_t>(m_context->keyChar);
                bool ctrl = false;  // TODO: Add ctrl modifier to UIContext

                bool handled = textInput->onKeyInput(m_context->keyCode, character, ctrl);

                if (handled) {
                    // Publish text_changed event
                    auto textChangedEvent = std::make_unique<JsonDataNode>("text_changed");
                    textChangedEvent->setString("widgetId", textInput->id);
                    textChangedEvent->setString("text", textInput->text);
                    m_io->publish("ui:text_changed", std::move(textChangedEvent));

                    // Check if Enter was pressed (submit)
                    if (m_context->keyCode == 13 || m_context->keyCode == 10) {
                        auto submitEvent = std::make_unique<JsonDataNode>("text_submit");
                        submitEvent->setString("widgetId", textInput->id);
                        submitEvent->setString("text", textInput->text);
                        m_io->publish("ui:text_submit", std::move(submitEvent));

                        // Publish onSubmit action if specified
                        if (!textInput->onSubmit.empty()) {
                            auto actionEvent = std::make_unique<JsonDataNode>("action");
                            actionEvent->setString("action", textInput->onSubmit);
                            actionEvent->setString("widgetId", textInput->id);
                            actionEvent->setString("text", textInput->text);
                            m_io->publish("ui:action", std::move(actionEvent));
                        }

                        m_logger->info("TextInput '{}' submitted: '{}'", textInput->id, textInput->text);
                    }
                }
            }
        }
    }

    // Update all widgets
    m_root->update(*m_context, deltaTime);

    // Update tooltips
    if (m_tooltipManager) {
        m_tooltipManager->update(hoveredWidget, *m_context, deltaTime);
    }
}

void UIModule::renderUI() {
    if (m_root && m_root->visible) {
        m_root->render(*m_renderer);
    }

    // Render tooltips on top of everything
    if (m_tooltipManager && m_tooltipManager->isVisible()) {
        m_tooltipManager->render(*m_renderer, m_context->screenWidth, m_context->screenHeight);
    }
}

bool UIModule::loadLayout(const std::string& layoutPath) {
    std::ifstream file(layoutPath);
    if (!file.is_open()) {
        m_logger->error("Cannot open layout file: {}", layoutPath);
        return false;
    }

    try {
        nlohmann::json jsonData;
        file >> jsonData;

        // Convert to JsonDataNode
        auto layoutNode = std::make_unique<JsonDataNode>("layout", jsonData);
        return loadLayoutData(*layoutNode);
    }
    catch (const std::exception& e) {
        m_logger->error("Failed to parse layout JSON: {}", e.what());
        return false;
    }
}

bool UIModule::loadLayoutData(const IDataNode& layoutData) {
    m_root = m_tree->loadFromJson(layoutData);
    if (m_root) {
        m_root->computeAbsolutePosition();
        m_logger->info("Layout loaded: root id='{}', type='{}'",
                       m_root->id, m_root->getType());
        return true;
    }
    return false;
}

void UIModule::shutdown() {
    m_logger->info("UIModule shutting down, {} frames processed", m_frameCount);

    m_root.reset();
    m_tree.reset();
    m_renderer.reset();
    m_context.reset();
}

std::unique_ptr<IDataNode> UIModule::getState() {
    auto state = std::make_unique<JsonDataNode>("state");
    state->setInt("frameCount", static_cast<int>(m_frameCount));
    return state;
}

void UIModule::setState(const IDataNode& state) {
    m_frameCount = static_cast<uint64_t>(state.getInt("frameCount", 0));
    m_logger->info("State restored: frameCount={}", m_frameCount);
}

const IDataNode& UIModule::getConfiguration() {
    if (!m_configCache) {
        m_configCache = std::make_unique<JsonDataNode>("config");
        m_configCache->setDouble("screenWidth", m_context ? m_context->screenWidth : 1280.0);
        m_configCache->setDouble("screenHeight", m_context ? m_context->screenHeight : 720.0);
    }
    return *m_configCache;
}

std::unique_ptr<IDataNode> UIModule::getHealthStatus() {
    auto health = std::make_unique<JsonDataNode>("health");
    health->setString("status", "running");
    health->setInt("frameCount", static_cast<int>(m_frameCount));
    health->setBool("hasRoot", m_root != nullptr);
    return health;
}

} // namespace grove

// ============================================================================
// C Export (required for dlopen/LoadLibrary)
// Skip when building as static library to avoid multiple definition errors
// ============================================================================

#ifndef GROVE_MODULE_STATIC

#ifdef _WIN32
#define GROVE_MODULE_EXPORT __declspec(dllexport)
#else
#define GROVE_MODULE_EXPORT
#endif

extern "C" {

GROVE_MODULE_EXPORT grove::IModule* createModule() {
    return new grove::UIModule();
}

GROVE_MODULE_EXPORT void destroyModule(grove::IModule* module) {
    delete module;
}

}

#endif // GROVE_MODULE_STATIC
