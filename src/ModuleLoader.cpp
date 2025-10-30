#include <grove/ModuleLoader.h>
#include <chrono>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace grove {

ModuleLoader::ModuleLoader() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::debug);

    logger = std::make_shared<spdlog::logger>("ModuleLoader", console_sink);
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::debug);

    spdlog::register_logger(logger);

    logger->info("🔧 ModuleLoader initialized");
}

ModuleLoader::~ModuleLoader() {
    if (libraryHandle) {
        logger->warn("⚠️ ModuleLoader destroyed with library still loaded - forcing unload");
        unload();
    }
}

std::unique_ptr<IModule> ModuleLoader::load(const std::string& path, const std::string& name) {
    logLoadStart(path);

    auto loadStartTime = std::chrono::high_resolution_clock::now();

    // Open library with RTLD_NOW (resolve all symbols immediately)
    libraryHandle = dlopen(path.c_str(), RTLD_NOW);
    if (!libraryHandle) {
        std::string error = dlerror();
        logLoadError(error);
        throw std::runtime_error("Failed to load module: " + error);
    }

    // Find createModule factory function
    createFunc = reinterpret_cast<CreateModuleFunc>(dlsym(libraryHandle, "createModule"));
    if (!createFunc) {
        std::string error = dlerror();
        dlclose(libraryHandle);
        libraryHandle = nullptr;
        logLoadError("createModule symbol not found: " + error);
        throw std::runtime_error("Module missing createModule function: " + error);
    }

    // Create module instance
    IModule* modulePtr = createFunc();
    if (!modulePtr) {
        dlclose(libraryHandle);
        libraryHandle = nullptr;
        createFunc = nullptr;
        logLoadError("createModule returned null");
        throw std::runtime_error("createModule returned null");
    }

    libraryPath = path;
    moduleName = name;

    auto loadEndTime = std::chrono::high_resolution_clock::now();
    float loadTime = std::chrono::duration<float, std::milli>(loadEndTime - loadStartTime).count();

    logLoadSuccess(loadTime);

    return std::unique_ptr<IModule>(modulePtr);
}

void ModuleLoader::unload() {
    if (!libraryHandle) {
        logger->debug("🔍 Unload called but no library loaded");
        return;
    }

    logUnloadStart();

    // Close library
    int result = dlclose(libraryHandle);
    if (result != 0) {
        std::string error = dlerror();
        logger->error("❌ dlclose failed: {}", error);
    }

    libraryHandle = nullptr;
    createFunc = nullptr;
    libraryPath.clear();
    moduleName.clear();

    logUnloadSuccess();
}

std::unique_ptr<IModule> ModuleLoader::reload(std::unique_ptr<IModule> currentModule) {
    logger->info("🔄 Hot-reload starting for module '{}'", moduleName);

    auto reloadStartTime = std::chrono::high_resolution_clock::now();

    if (!currentModule) {
        logger->error("❌ Cannot reload: current module is null");
        throw std::runtime_error("Cannot reload null module");
    }

    if (!libraryHandle) {
        logger->error("❌ Cannot reload: no library loaded");
        throw std::runtime_error("Cannot reload: no library loaded");
    }

    // Step 1: Extract state from old module
    logger->debug("📦 Step 1/4: Extracting state from old module");
    auto state = currentModule->getState();
    logger->debug("✅ State extracted successfully");

    // Step 2: Destroy old module before unloading library
    logger->debug("💀 Step 2/4: Destroying old module instance");
    currentModule.reset();
    logger->debug("✅ Old module destroyed");

    // Step 3: Unload old library
    logger->debug("🔓 Step 3/4: Unloading old library");
    std::string pathToReload = libraryPath;
    std::string nameToReload = moduleName;
    unload();
    logger->debug("✅ Old library unloaded");

    // Step 4: Load new library and restore state
    logger->debug("📥 Step 4/4: Loading new library");
    auto newModule = load(pathToReload, nameToReload);
    logger->debug("✅ New library loaded");

    // Step 5: Restore state to new module
    logger->debug("🔁 Restoring state to new module");
    newModule->setState(*state);
    logger->debug("✅ State restored successfully");

    auto reloadEndTime = std::chrono::high_resolution_clock::now();
    float reloadTime = std::chrono::duration<float, std::milli>(reloadEndTime - reloadStartTime).count();

    logger->info("✅ Hot-reload completed in {:.3f}ms", reloadTime);

    return newModule;
}

// Private logging helpers
void ModuleLoader::logLoadStart(const std::string& path) {
    logger->info("📥 Loading module from: {}", path);
}

void ModuleLoader::logLoadSuccess(float loadTime) {
    logger->info("✅ Module '{}' loaded successfully in {:.3f}ms", moduleName, loadTime);
    logger->debug("📍 Library path: {}", libraryPath);
    logger->debug("🔗 Library handle: {}", libraryHandle);
}

void ModuleLoader::logLoadError(const std::string& error) {
    logger->error("❌ Failed to load module: {}", error);
}

void ModuleLoader::logUnloadStart() {
    logger->info("🔓 Unloading module '{}'", moduleName);
    logger->debug("📍 Library path: {}", libraryPath);
}

void ModuleLoader::logUnloadSuccess() {
    logger->info("✅ Module unloaded successfully");
}

} // namespace grove
