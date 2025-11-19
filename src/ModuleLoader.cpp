#include <grove/ModuleLoader.h>
#include <grove/IModuleSystem.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <filesystem>
#include <thread>
#include <logger/Logger.h>

namespace grove {

ModuleLoader::ModuleLoader() {
    logger = stillhammer::createDomainLogger("ModuleLoader", "engine");
    logger->info("🔧 ModuleLoader initialized");
}

ModuleLoader::~ModuleLoader() {
    if (libraryHandle) {
        if (logger) {
            logger->warn("⚠️ ModuleLoader destroyed with library still loaded - forcing unload");
        }
        unload();
    }
}

std::unique_ptr<IModule> ModuleLoader::load(const std::string& path, const std::string& name, bool isReload) {
    // CRITICAL FIX: Unload any previously loaded library before loading a new one
    // This prevents library handle leaks and temp file accumulation
    if (libraryHandle) {
        logger->debug("🔄 Unloading previous library before loading new one");
        unload();
    }

    logLoadStart(path);

    auto loadStartTime = std::chrono::high_resolution_clock::now();

    // CRITICAL FIX: Linux caches .so files by path in dlopen
    // Even after dlclose, subsequent dlopen of the same path will load from cache
    // Solution: Create a temporary copy with unique name for hot-reload scenarios

    std::string actualPath = path;
    std::string tempPath;
    bool usedTempCopy = false;

    // If we're reloading, use a temp copy to bypass cache
    if (isReload) {
        // CRITICAL FIX: Wait for file to be fully written after compilation
        // The FileWatcher may detect the change while the compiler is still writing
        logger->debug("⏳ Waiting for .so file to be fully written...");

        size_t lastSize = 0;
        size_t stableCount = 0;
        const int maxAttempts = 20; // 1 second max wait (20 * 50ms)
        const int stableRequired = 3; // Require 3 consecutive stable readings

        for (int i = 0; i < maxAttempts; i++) {
            try {
                size_t currentSize = std::filesystem::file_size(path);

                if (currentSize > 0 && currentSize == lastSize) {
                    stableCount++;
                    if (stableCount >= stableRequired) {
                        logger->debug("✅ File size stable at {} bytes (after {}ms)", currentSize, i * 50);
                        break;
                    }
                } else {
                    stableCount = 0; // Reset if size changed
                }

                lastSize = currentSize;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } catch (const std::filesystem::filesystem_error& e) {
                // File might not exist yet or be locked
                logger->debug("⏳ Waiting for file access... ({})", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        // Create unique temp filename
        char tempTemplate[] = "/tmp/grove_module_XXXXXX.so";
        int tempFd = mkstemps(tempTemplate, 3); // 3 for ".so"
        if (tempFd == -1) {
            logger->warn("⚠️ Failed to create temp file, loading directly (may use cached version)");
        } else {
            close(tempFd); // Close the fd, we just need the unique name
            tempPath = tempTemplate;

            // Copy original .so to temp location using std::filesystem
            try {
                std::filesystem::copy_file(path, tempPath,
                    std::filesystem::copy_options::overwrite_existing);

                // CRITICAL FIX: Verify the copy succeeded completely
                auto origSize = std::filesystem::file_size(path);
                auto copiedSize = std::filesystem::file_size(tempPath);

                if (copiedSize != origSize) {
                    logger->error("❌ Incomplete copy: orig={} bytes, copied={} bytes", origSize, copiedSize);
                    unlink(tempPath.c_str());
                    throw std::runtime_error("Incomplete file copy detected");
                }

                if (origSize == 0) {
                    logger->error("❌ Source file is empty!");
                    unlink(tempPath.c_str());
                    throw std::runtime_error("Source .so file is empty");
                }

                actualPath = tempPath;
                usedTempCopy = true;
                logger->debug("🔄 Using temp copy for hot-reload: {} ({} bytes)", tempPath, copiedSize);
            } catch (const std::filesystem::filesystem_error& e) {
                logger->warn("⚠️ Failed to copy .so ({}), loading directly (may use cached version)", e.what());
                unlink(tempPath.c_str()); // Clean up failed temp file
            }
        }
    }

    // Open library with RTLD_NOW (resolve all symbols immediately)
    libraryHandle = dlopen(actualPath.c_str(), RTLD_NOW);
    if (!libraryHandle) {
        std::string error = dlerror();

        // Clean up temp file if it was created
        if (usedTempCopy) {
            unlink(tempPath.c_str());
        }

        logLoadError(error);
        throw std::runtime_error("Failed to load module: " + error);
    }

    // Store temp path for cleanup later
    if (usedTempCopy) {
        tempLibraryPath = tempPath;
        logger->debug("📝 Stored temp path for cleanup: {}", tempLibraryPath);
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

    // Clean up temp file if it was used
    if (!tempLibraryPath.empty()) {
        logger->debug("🧹 Cleaning up temp file: {}", tempLibraryPath);
        if (unlink(tempLibraryPath.c_str()) == 0) {
            logger->debug("✅ Temp file deleted");
        } else {
            logger->warn("⚠️ Failed to delete temp file: {}", tempLibraryPath);
        }
        tempLibraryPath.clear();
    }

    libraryHandle = nullptr;
    createFunc = nullptr;
    libraryPath.clear();
    moduleName.clear();

    logUnloadSuccess();
}

bool ModuleLoader::waitForCleanState(IModule* module, IModuleSystem* moduleSystem, float timeoutSeconds) {
    logger->info("⏳ Waiting for clean state (timeout: {:.1f}s)", timeoutSeconds);

    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastLogTime = startTime;

    while (true) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(currentTime - startTime).count();

        // Check timeout
        if (elapsed >= timeoutSeconds) {
            logger->error("❌ Clean state timeout after {:.1f}s", elapsed);
            return false;
        }

        // Check clean state conditions
        bool moduleIdle = module->isIdle();
        int pendingTasks = moduleSystem->getPendingTaskCount(moduleName);

        if (moduleIdle && pendingTasks == 0) {
            logger->info("✅ Clean state reached after {:.3f}s", elapsed);
            return true;
        }

        // Log progress every 500ms
        float timeSinceLastLog = std::chrono::duration<float>(currentTime - lastLogTime).count();
        if (timeSinceLastLog >= 0.5f) {
            logger->info("⏳ Waiting... ({:.1f}s) - module idle: {}, pending tasks: {}",
                        elapsed, moduleIdle, pendingTasks);
            lastLogTime = currentTime;
        }

        // Sleep briefly to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
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

    // Step 4: Load new library and restore state (use temp copy to bypass cache)
    logger->debug("📥 Step 4/4: Loading new library with cache bypass");
    auto newModule = load(pathToReload, nameToReload, true);  // isReload = true
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
    if (logger) {
        logger->info("📥 Loading module from: {}", path);
    }
}

void ModuleLoader::logLoadSuccess(float loadTime) {
    if (logger) {
        logger->info("✅ Module '{}' loaded successfully in {:.3f}ms", moduleName, loadTime);
        logger->debug("📍 Library path: {}", libraryPath);
        logger->debug("🔗 Library handle: {}", libraryHandle);
    }
}

void ModuleLoader::logLoadError(const std::string& error) {
    if (logger) {
        logger->error("❌ Failed to load module: {}", error);
    }
}

void ModuleLoader::logUnloadStart() {
    if (logger) {
        logger->info("🔓 Unloading module '{}'", moduleName);
        logger->debug("📍 Library path: {}", libraryPath);
    }
}

void ModuleLoader::logUnloadSuccess() {
    if (logger) {
        logger->info("✅ Module unloaded successfully");
    }
}

} // namespace grove
