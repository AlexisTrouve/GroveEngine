#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/JsonDataNode.h>
#include <memory>
#include <spdlog/logger.h>

namespace grove {

class UIContext;
class UITree;
class UIRenderer;
class UIWidget;
class UITooltipManager;

/**
 * @brief UI Module - Declarative UI system with JSON configuration
 *
 * Provides a retained-mode UI system with:
 * - JSON-based layout definition
 * - Widget hierarchy (Panel, Label, Button, etc.)
 * - Rendering via IIO topics (render:sprite, render:text)
 * - Input handling via IIO (input:mouse, input:keyboard)
 */
class UIModule : public IModule {
public:
    UIModule();
    ~UIModule() override;

    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "UIModule"; }
    bool isIdle() const override { return true; }

private:
    IIO* m_io = nullptr;
    std::shared_ptr<spdlog::logger> m_logger;

    // UI subsystems
    std::unique_ptr<UIContext> m_context;
    std::unique_ptr<UITree> m_tree;
    std::unique_ptr<UIRenderer> m_renderer;
    std::unique_ptr<UITooltipManager> m_tooltipManager;
    std::unique_ptr<UIWidget> m_root;

    // Configuration cache
    std::unique_ptr<JsonDataNode> m_configCache;

    // Stats
    uint64_t m_frameCount = 0;

    // Load layout from file path
    bool loadLayout(const std::string& layoutPath);

    // Load layout from inline JSON data
    bool loadLayoutData(const IDataNode& layoutData);

    // Process input from IIO
    void processInput();

    // Update UI state
    void updateUI(float deltaTime);

    // Render UI
    void renderUI();
};

} // namespace grove
