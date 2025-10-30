#pragma once

#include <string>
#include <memory>
#include <functional>
#include <dlfcn.h>
#include <spdlog/spdlog.h>
#include "IModule.h"

namespace grove {

/**
 * @brief Handles dynamic loading/unloading of module .so files
 *
 * ModuleLoader provides:
 * - Dynamic library loading with dlopen
 * - Module factory function resolution
 * - State preservation across reloads
 * - Hot-reload capability with <1ms latency
 * - Comprehensive error handling and logging
 */
class ModuleLoader {
private:
    std::shared_ptr<spdlog::logger> logger;

    void* libraryHandle = nullptr;
    std::string libraryPath;
    std::string moduleName;

    // Factory function signature: IModule* createModule()
    using CreateModuleFunc = IModule* (*)();
    CreateModuleFunc createFunc = nullptr;

    void logLoadStart(const std::string& path);
    void logLoadSuccess(float loadTime);
    void logLoadError(const std::string& error);
    void logUnloadStart();
    void logUnloadSuccess();

public:
    ModuleLoader();
    ~ModuleLoader();

    /**
     * @brief Load a module from .so file
     * @param path Path to .so file
     * @param moduleName Name for logging/identification
     * @return Unique pointer to loaded module
     * @throws std::runtime_error if loading fails
     */
    std::unique_ptr<IModule> load(const std::string& path, const std::string& name);

    /**
     * @brief Unload currently loaded module
     * Closes the library handle and frees resources
     */
    void unload();

    /**
     * @brief Hot-reload: save state, unload, reload, restore state
     * @param currentModule Module with state to preserve
     * @return New module instance with preserved state
     * @throws std::runtime_error if reload fails
     *
     * This is the core hot-reload operation:
     * 1. Extract state from old module
     * 2. Unload old .so
     * 3. Load new .so (recompiled)
     * 4. Inject state into new module
     */
    std::unique_ptr<IModule> reload(std::unique_ptr<IModule> currentModule);

    /**
     * @brief Check if a module is currently loaded
     */
    bool isLoaded() const { return libraryHandle != nullptr; }

    /**
     * @brief Get path to currently loaded module
     */
    const std::string& getLoadedPath() const { return libraryPath; }

    /**
     * @brief Get name of currently loaded module
     */
    const std::string& getModuleName() const { return moduleName; }
};

} // namespace grove
