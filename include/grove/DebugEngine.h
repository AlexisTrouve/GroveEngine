#pragma once

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <spdlog/spdlog.h>

#include "IEngine.h"
#include "IModuleSystem.h"
#include "IIO.h"
#include "IDataNode.h"

namespace grove {

/**
 * @brief Debug engine implementation with comprehensive logging
 *
 * DebugEngine provides maximum visibility into engine operations:
 * - Verbose logging of all operations
 * - Step-by-step execution capabilities
 * - Module isolation and debugging
 * - Performance metrics and timing
 * - IIO health monitoring and reporting
 * - Detailed socket management logging
 */
class DebugEngine : public IEngine {
private:
    std::shared_ptr<spdlog::logger> logger;
    std::atomic<bool> running{false};
    std::atomic<bool> debugPaused{false};

    // Module management
    std::vector<std::unique_ptr<IModuleSystem>> moduleSystems;
    std::vector<std::string> moduleNames;

    // Socket management
    std::unique_ptr<IIO> coordinatorSocket;
    std::vector<std::unique_ptr<IIO>> clientSockets;

    // Performance tracking
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    std::chrono::high_resolution_clock::time_point engineStartTime;
    size_t frameCount = 0;

    // Configuration
    std::unique_ptr<IDataNode> engineConfig;

    // Helper methods
    void logEngineStart();
    void logEngineShutdown();
    void logFrameStart(float deltaTime);
    void logFrameEnd(float frameTime);
    void logModuleHealth();
    void logSocketHealth();
    void processModuleSystems(float deltaTime);
    void processClientMessages();
    void processCoordinatorMessages();
    float calculateDeltaTime();
    void validateConfiguration();

public:
    DebugEngine();
    virtual ~DebugEngine();

    // IEngine implementation
    void initialize() override;
    void run() override;
    void step(float deltaTime) override;
    void shutdown() override;
    void loadModules(const std::string& configPath) override;
    void registerMainSocket(std::unique_ptr<IIO> coordinatorSocket) override;
    void registerNewClientSocket(std::unique_ptr<IIO> clientSocket) override;
    EngineType getType() const override;

    // Debug-specific methods
    void pauseExecution();
    void resumeExecution();
    void stepSingleFrame();
    bool isPaused() const;
    std::unique_ptr<IDataNode> getDetailedStatus() const;
    void setLogLevel(spdlog::level::level_enum level);
};

} // namespace grove