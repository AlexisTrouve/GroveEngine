#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "IModule.h"

using json = nlohmann::json;

namespace grove {

/**
 * @brief Factory for loading and creating modules from shared libraries (.so files)
 *
 * ModuleFactory handles dynamic loading of module implementations from .so files.
 * It manages symbol resolution, error handling, and module lifecycle.
 *
 * Features:
 * - Dynamic loading of .so files with dlopen/dlsym
 * - Automatic symbol resolution for module entry points
 * - Hot-reload support with proper cleanup
 * - Comprehensive error reporting and logging
 * - Module registration and discovery
 * - Thread-safe operations
 *
 * Expected module .so structure:
 * - extern "C" IModule* create_module()
 * - extern "C" void destroy_module(IModule*)
 * - extern "C" const char* get_module_type()
 * - extern "C" const char* get_module_version()
 */
class ModuleFactory {
public:
    struct ModuleInfo {
        std::string path;
        std::string type;
        std::string version;
        void* handle = nullptr;
        std::function<IModule*()> createFunc;
        std::function<void(IModule*)> destroyFunc;
    };

    ModuleFactory();
    ~ModuleFactory();

    // Module loading
    std::unique_ptr<IModule> loadModule(const std::string& modulePath);
    std::unique_ptr<IModule> createModule(const std::string& moduleType);

    // Module discovery and registration
    void scanModulesDirectory(const std::string& directory);
    void registerModule(const std::string& modulePath);
    void unloadModule(const std::string& moduleType);
    void unloadAllModules();

    // Information and diagnostics
    std::vector<std::string> getAvailableModules() const;
    std::vector<std::string> getLoadedModules() const;
    ModuleInfo getModuleInfo(const std::string& moduleType) const;
    bool isModuleLoaded(const std::string& moduleType) const;
    bool isModuleAvailable(const std::string& moduleType) const;

    // Configuration
    void setModulesDirectory(const std::string& directory);
    std::string getModulesDirectory() const;

    // Hot-reload support
    bool reloadModule(const std::string& moduleType);
    void enableHotReload(bool enable);
    bool isHotReloadEnabled() const;

    // Diagnostics and debugging
    json getDetailedStatus() const;
    void validateModule(const std::string& modulePath);
    void setLogLevel(spdlog::level::level_enum level);

private:
    std::shared_ptr<spdlog::logger> logger;
    std::string modulesDirectory;
    bool hotReloadEnabled = false;

    // Module registry
    std::unordered_map<std::string, ModuleInfo> loadedModules;
    std::unordered_map<std::string, std::string> availableModules; // type -> path

    // Helper methods
    std::shared_ptr<spdlog::logger> getFactoryLogger();
    bool loadSharedLibrary(const std::string& path, ModuleInfo& info);
    void unloadSharedLibrary(ModuleInfo& info);
    bool resolveSymbols(ModuleInfo& info);
    std::string extractModuleTypeFromPath(const std::string& path) const;
    bool isValidModuleFile(const std::string& path) const;
    void logModuleLoad(const std::string& type, const std::string& path) const;
    void logModuleUnload(const std::string& type) const;
    void logModuleError(const std::string& operation, const std::string& details) const;
};

} // namespace grove