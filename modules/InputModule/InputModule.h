#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/ITaskScheduler.h>
#include "Core/InputState.h"
#include "Core/InputConverter.h"
#include "Backends/SDLBackend.h"

#include <memory>
#include <vector>
#include <mutex>
#include <string>
#include <SDL.h>

namespace grove {

class InputModule : public IModule {
public:
    InputModule();
    ~InputModule() override;

    // IModule interface
    void setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) override;
    void process(const IDataNode& input) override;
    void shutdown() override;

    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;

    std::string getType() const override { return "input_module"; }
    bool isIdle() const override { return true; }

    // API specific to InputModule
    void feedEvent(const void* nativeEvent);  // Thread-safe injection from main loop

private:
    IIO* m_io = nullptr;
    std::unique_ptr<InputState> m_state;
    std::unique_ptr<InputConverter> m_converter;
    std::unique_ptr<IDataNode> m_config;

    // Event buffer (thread-safe)
    std::vector<SDL_Event> m_eventBuffer;
    std::mutex m_bufferMutex;

    // Config options
    std::string m_backend = "sdl";
    bool m_enableMouse = true;
    bool m_enableKeyboard = true;
    bool m_enableGamepad = false;

    // Stats
    uint64_t m_frameCount = 0;
    uint64_t m_eventsProcessed = 0;
};

} // namespace grove

// Export functions for module loading
extern "C" {
    #ifdef _WIN32
        __declspec(dllexport) grove::IModule* createModule();
        __declspec(dllexport) void destroyModule(grove::IModule* module);
    #else
        grove::IModule* createModule();
        void destroyModule(grove::IModule* module);
    #endif
}
