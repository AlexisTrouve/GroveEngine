#include <grove/ModuleFactory.h>
#include <filesystem>
#include <dlfcn.h>
#include <algorithm>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace fs = std::filesystem;

namespace warfactory {

ModuleFactory::ModuleFactory() {
    // Create logger with file and console output
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/module_factory.log", true);

    console_sink->set_level(spdlog::level::info);
    file_sink->set_level(spdlog::level::trace);

    logger = std::make_shared<spdlog::logger>("ModuleFactory",
        spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::debug);

    spdlog::register_logger(logger);

    logger->info("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=");
    logger->info("🏭 MODULE FACTORY INITIALIZED");
    logger->info("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=");
    logger->info("🔧 Dynamic module loading with dlopen/dlsym");
    logger->info("🔥 Hot-reload support available");
    logger->info("📁 Default modules directory: ./modules/");

    modulesDirectory = "./modules/";
}

ModuleFactory::~ModuleFactory() {
    logger->info("🏭 ModuleFactory destructor called");
    unloadAllModules();
    logger->trace("🏗️ ModuleFactory destroyed");
}

std::unique_ptr<IModule> ModuleFactory::loadModule(const std::string& modulePath) {
    logger->info("🏭 Loading module from path: '{}'", modulePath);

    if (!fs::exists(modulePath)) {
        logger->error("❌ Module file not found: '{}'", modulePath);
        throw std::runtime_error("Module file not found: " + modulePath);
    }

    if (!isValidModuleFile(modulePath)) {
        logger->error("❌ Invalid module file: '{}'", modulePath);
        throw std::runtime_error("Invalid module file: " + modulePath);
    }

    ModuleInfo info;
    info.path = modulePath;

    try {
        if (!loadSharedLibrary(modulePath, info)) {
            logger->error("❌ Failed to load shared library: '{}'", modulePath);
            throw std::runtime_error("Failed to load shared library: " + modulePath);
        }

        if (!resolveSymbols(info)) {
            logger->error("❌ Failed to resolve symbols: '{}'", modulePath);
            unloadSharedLibrary(info);
            throw std::runtime_error("Failed to resolve symbols: " + modulePath);
        }

        // Create module instance
        auto module = std::unique_ptr<IModule>(info.createFunc());
        if (!module) {
            logger->error("❌ Module creation function returned nullptr: '{}'", modulePath);
            unloadSharedLibrary(info);
            throw std::runtime_error("Module creation failed: " + modulePath);
        }

        // Verify module type consistency
        std::string actualType = module->getType();
        if (actualType != info.type) {
            logger->warn("⚠️ Module type mismatch: expected '{}', got '{}'", info.type, actualType);
        }

        // Register loaded module
        loadedModules[info.type] = info;
        availableModules[info.type] = modulePath;

        logModuleLoad(info.type, modulePath);
        logger->info("✅ Module '{}' loaded successfully from '{}'", info.type, modulePath);

        return module;

    } catch (const std::exception& e) {
        logModuleError("load", e.what());
        unloadSharedLibrary(info);
        throw;
    }
}

std::unique_ptr<IModule> ModuleFactory::createModule(const std::string& moduleType) {
    logger->info("🏭 Creating module of type: '{}'", moduleType);

    auto it = availableModules.find(moduleType);
    if (it == availableModules.end()) {
        logger->error("❌ Module type '{}' not available", moduleType);

        auto available = getAvailableModules();
        std::string availableStr = "[";
        for (size_t i = 0; i < available.size(); ++i) {
            availableStr += available[i];
            if (i < available.size() - 1) availableStr += ", ";
        }
        availableStr += "]";

        throw std::invalid_argument("Module type '" + moduleType + "' not available. Available: " + availableStr);
    }

    return loadModule(it->second);
}

void ModuleFactory::scanModulesDirectory(const std::string& directory) {
    logger->info("🔍 Scanning modules directory: '{}'", directory);

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        logger->warn("⚠️ Modules directory does not exist: '{}'", directory);
        return;
    }

    size_t foundCount = 0;

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && isValidModuleFile(entry.path().string())) {
            try {
                registerModule(entry.path().string());
                foundCount++;
            } catch (const std::exception& e) {
                logger->warn("⚠️ Failed to register module '{}': {}", entry.path().string(), e.what());
            }
        }
    }

    logger->info("✅ Scan complete: {} modules found in '{}'", foundCount, directory);
}

void ModuleFactory::registerModule(const std::string& modulePath) {
    logger->debug("📝 Registering module: '{}'", modulePath);

    if (!fs::exists(modulePath)) {
        throw std::runtime_error("Module file not found: " + modulePath);
    }

    if (!isValidModuleFile(modulePath)) {
        throw std::runtime_error("Invalid module file: " + modulePath);
    }

    // Extract module type from the path for registration
    std::string moduleType = extractModuleTypeFromPath(modulePath);

    // Quick validation - try to load and get type
    ModuleInfo tempInfo;
    tempInfo.path = modulePath;

    if (loadSharedLibrary(modulePath, tempInfo)) {
        if (resolveSymbols(tempInfo)) {
            // Get the actual type from the module
            typedef const char* (*GetTypeFunc)();
            auto getTypeFunc = (GetTypeFunc)dlsym(tempInfo.handle, "get_module_type");
            if (getTypeFunc) {
                moduleType = getTypeFunc();
            }
        }
        unloadSharedLibrary(tempInfo);
    }

    availableModules[moduleType] = modulePath;
    logger->debug("✅ Module '{}' registered from '{}'", moduleType, modulePath);
}

void ModuleFactory::unloadModule(const std::string& moduleType) {
    logger->info("🗑️ Unloading module: '{}'", moduleType);

    auto it = loadedModules.find(moduleType);
    if (it == loadedModules.end()) {
        logger->warn("⚠️ Module '{}' is not loaded", moduleType);
        return;
    }

    unloadSharedLibrary(it->second);
    loadedModules.erase(it);

    logModuleUnload(moduleType);
    logger->info("✅ Module '{}' unloaded successfully", moduleType);
}

void ModuleFactory::unloadAllModules() {
    logger->info("🗑️ Unloading all modules ({} loaded)", loadedModules.size());

    for (auto& [type, info] : loadedModules) {
        logger->debug("🗑️ Unloading module: '{}'", type);
        unloadSharedLibrary(info);
    }

    loadedModules.clear();
    logger->info("✅ All modules unloaded");
}

std::vector<std::string> ModuleFactory::getAvailableModules() const {
    std::vector<std::string> modules;
    modules.reserve(availableModules.size());

    for (const auto& [type, path] : availableModules) {
        modules.push_back(type);
    }

    std::sort(modules.begin(), modules.end());
    return modules;
}

std::vector<std::string> ModuleFactory::getLoadedModules() const {
    std::vector<std::string> modules;
    modules.reserve(loadedModules.size());

    for (const auto& [type, info] : loadedModules) {
        modules.push_back(type);
    }

    std::sort(modules.begin(), modules.end());
    return modules;
}

ModuleFactory::ModuleInfo ModuleFactory::getModuleInfo(const std::string& moduleType) const {
    auto it = loadedModules.find(moduleType);
    if (it != loadedModules.end()) {
        return it->second;
    }

    // Return empty info if not loaded
    return ModuleInfo{};
}

bool ModuleFactory::isModuleLoaded(const std::string& moduleType) const {
    return loadedModules.find(moduleType) != loadedModules.end();
}

bool ModuleFactory::isModuleAvailable(const std::string& moduleType) const {
    return availableModules.find(moduleType) != availableModules.end();
}

void ModuleFactory::setModulesDirectory(const std::string& directory) {
    logger->info("📁 Setting modules directory: '{}'", directory);
    modulesDirectory = directory;

    // Auto-scan new directory
    if (fs::exists(directory)) {
        scanModulesDirectory(directory);
    }
}

std::string ModuleFactory::getModulesDirectory() const {
    return modulesDirectory;
}

bool ModuleFactory::reloadModule(const std::string& moduleType) {
    logger->info("🔄 Reloading module: '{}'", moduleType);

    if (!hotReloadEnabled) {
        logger->warn("⚠️ Hot-reload is disabled");
        return false;
    }

    auto it = loadedModules.find(moduleType);
    if (it == loadedModules.end()) {
        logger->warn("⚠️ Module '{}' is not loaded, cannot reload", moduleType);
        return false;
    }

    std::string modulePath = it->second.path;

    try {
        unloadModule(moduleType);
        auto reloadedModule = loadModule(modulePath);

        logger->info("✅ Module '{}' reloaded successfully", moduleType);
        return true;

    } catch (const std::exception& e) {
        logger->error("❌ Failed to reload module '{}': {}", moduleType, e.what());
        return false;
    }
}

void ModuleFactory::enableHotReload(bool enable) {
    logger->info("🔧 Hot-reload {}", enable ? "enabled" : "disabled");
    hotReloadEnabled = enable;
}

bool ModuleFactory::isHotReloadEnabled() const {
    return hotReloadEnabled;
}

json ModuleFactory::getDetailedStatus() const {
    json status = {
        {"modules_directory", modulesDirectory},
        {"hot_reload_enabled", hotReloadEnabled},
        {"available_modules_count", availableModules.size()},
        {"loaded_modules_count", loadedModules.size()}
    };

    json availableList = json::array();
    for (const auto& [type, path] : availableModules) {
        availableList.push_back({
            {"type", type},
            {"path", path}
        });
    }
    status["available_modules"] = availableList;

    json loadedList = json::array();
    for (const auto& [type, info] : loadedModules) {
        loadedList.push_back({
            {"type", type},
            {"path", info.path},
            {"version", info.version},
            {"handle", reinterpret_cast<uintptr_t>(info.handle)}
        });
    }
    status["loaded_modules"] = loadedList;

    return status;
}

void ModuleFactory::validateModule(const std::string& modulePath) {
    logger->info("🔍 Validating module: '{}'", modulePath);

    if (!fs::exists(modulePath)) {
        throw std::runtime_error("Module file not found: " + modulePath);
    }

    if (!isValidModuleFile(modulePath)) {
        throw std::runtime_error("Invalid module file extension: " + modulePath);
    }

    ModuleInfo tempInfo;
    tempInfo.path = modulePath;

    if (!loadSharedLibrary(modulePath, tempInfo)) {
        throw std::runtime_error("Failed to load shared library: " + modulePath);
    }

    if (!resolveSymbols(tempInfo)) {
        unloadSharedLibrary(tempInfo);
        throw std::runtime_error("Failed to resolve required symbols: " + modulePath);
    }

    // Test module creation
    auto testModule = std::unique_ptr<IModule>(tempInfo.createFunc());
    if (!testModule) {
        unloadSharedLibrary(tempInfo);
        throw std::runtime_error("Module creation function returned nullptr");
    }

    // Test module type
    std::string moduleType = testModule->getType();
    if (moduleType.empty()) {
        tempInfo.destroyFunc(testModule.release());
        unloadSharedLibrary(tempInfo);
        throw std::runtime_error("Module getType() returned empty string");
    }

    // Cleanup
    tempInfo.destroyFunc(testModule.release());
    unloadSharedLibrary(tempInfo);

    logger->info("✅ Module validation passed: '{}' (type: '{}')", modulePath, moduleType);
}

void ModuleFactory::setLogLevel(spdlog::level::level_enum level) {
    logger->info("🔧 Setting log level to: {}", spdlog::level::to_string_view(level));
    logger->set_level(level);
}

// Private helper methods
std::shared_ptr<spdlog::logger> ModuleFactory::getFactoryLogger() {
    return logger;
}

bool ModuleFactory::loadSharedLibrary(const std::string& path, ModuleInfo& info) {
    logger->trace("📚 Loading shared library: '{}'", path);

    // Clear any existing error
    dlerror();

    // Load the shared library
    info.handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!info.handle) {
        const char* error = dlerror();
        logger->error("❌ dlopen failed for '{}': {}", path, error ? error : "unknown error");
        return false;
    }

    logger->trace("✅ Shared library loaded: '{}'", path);
    return true;
}

void ModuleFactory::unloadSharedLibrary(ModuleInfo& info) {
    if (info.handle) {
        logger->trace("🗑️ Unloading shared library: '{}'", info.path);

        int result = dlclose(info.handle);
        if (result != 0) {
            const char* error = dlerror();
            logger->warn("⚠️ dlclose warning for '{}': {}", info.path, error ? error : "unknown error");
        }

        info.handle = nullptr;
        info.createFunc = nullptr;
        info.destroyFunc = nullptr;
    }
}

bool ModuleFactory::resolveSymbols(ModuleInfo& info) {
    logger->trace("🔍 Resolving symbols for: '{}'", info.path);

    // Clear any existing error
    dlerror();

    // Resolve create_module function
    typedef IModule* (*CreateFunc)();
    auto createFunc = (CreateFunc)dlsym(info.handle, "create_module");
    const char* error = dlerror();
    if (error || !createFunc) {
        logger->error("❌ Failed to resolve 'create_module': {}", error ? error : "symbol not found");
        return false;
    }
    info.createFunc = createFunc;

    // Resolve destroy_module function
    typedef void (*DestroyFunc)(IModule*);
    auto destroyFunc = (DestroyFunc)dlsym(info.handle, "destroy_module");
    error = dlerror();
    if (error || !destroyFunc) {
        logger->error("❌ Failed to resolve 'destroy_module': {}", error ? error : "symbol not found");
        return false;
    }
    info.destroyFunc = destroyFunc;

    // Resolve get_module_type function
    typedef const char* (*GetTypeFunc)();
    auto getTypeFunc = (GetTypeFunc)dlsym(info.handle, "get_module_type");
    error = dlerror();
    if (error || !getTypeFunc) {
        logger->error("❌ Failed to resolve 'get_module_type': {}", error ? error : "symbol not found");
        return false;
    }
    info.type = getTypeFunc();

    // Resolve get_module_version function
    typedef const char* (*GetVersionFunc)();
    auto getVersionFunc = (GetVersionFunc)dlsym(info.handle, "get_module_version");
    error = dlerror();
    if (error || !getVersionFunc) {
        logger->warn("⚠️ Failed to resolve 'get_module_version': {}", error ? error : "symbol not found");
        info.version = "unknown";
    } else {
        info.version = getVersionFunc();
    }

    logger->trace("✅ All symbols resolved for '{}' (type: '{}', version: '{}')",
                 info.path, info.type, info.version);
    return true;
}

std::string ModuleFactory::extractModuleTypeFromPath(const std::string& path) const {
    fs::path p(path);
    std::string filename = p.stem().string(); // Remove extension

    // Remove common prefixes
    if (filename.find("lib") == 0) {
        filename = filename.substr(3);
    }
    if (filename.find("warfactory-") == 0) {
        filename = filename.substr(11);
    }

    return filename;
}

bool ModuleFactory::isValidModuleFile(const std::string& path) const {
    fs::path p(path);
    std::string extension = p.extension().string();

    // Check for valid shared library extensions
    return extension == ".so" || extension == ".dylib" || extension == ".dll";
}

void ModuleFactory::logModuleLoad(const std::string& type, const std::string& path) const {
    logger->debug("📦 Module loaded: type='{}', path='{}'", type, path);
}

void ModuleFactory::logModuleUnload(const std::string& type) const {
    logger->debug("📤 Module unloaded: type='{}'", type);
}

void ModuleFactory::logModuleError(const std::string& operation, const std::string& details) const {
    logger->error("❌ Module {} error: {}", operation, details);
}

} // namespace warfactory