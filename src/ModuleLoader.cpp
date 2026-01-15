#include <grove/ModuleLoader.h>
#include <grove/IModuleSystem.h>
#include <grove/platform/FileSystem.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <logger/Logger.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define PATH_SEPARATOR '\\'
#else
#include <unistd.h>
#define PATH_SEPARATOR '/'
#endif

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
    // Handle cleanup of previous library
    // - For reload (isReload=true): The caller has already destroyed the old module
    //   via reload(), so it's safe to unload the old library
    // - For fresh load (isReload=false): Old modules may still be alive, so we
    //   warn but don't auto-unload (caller should use separate loaders or manage lifecycle)
    if (libraryHandle) {
        if (isReload) {
            // Safe to unload - reload() destroyed the old module first
            logger->debug("🔄 Unloading previous library before loading new version");
            unload();
        } else {
            // Not safe to auto-unload - old modules may still be alive
            logger->warn("⚠️ Loading new module while previous handle still open. "
                        "Consider using separate ModuleLoader instances for independent modules.");
        }
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
        logger->debug("⏳ Waiting for library file to be fully written...");

        size_t lastSize = 0;
        size_t stableCount = 0;
        const int maxAttempts = 20; // 1 second max wait (20 * 50ms)
        const int stableRequired = 3; // Require 3 consecutive stable readings

        for (int i = 0; i < maxAttempts; i++) {
            size_t currentSize = grove::fs::fileSize(path);

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
        }

#ifdef _WIN32
        // Windows: Create unique temp filename in temp directory
        char tempDir[MAX_PATH];
        if (GetTempPathA(MAX_PATH, tempDir) == 0) {
            logger->warn("⚠️ Failed to get temp directory, loading directly");
        } else {
            char tempFile[MAX_PATH];
            if (GetTempFileNameA(tempDir, "grv", 0, tempFile) == 0) {
                logger->warn("⚠️ Failed to create temp file, loading directly");
            } else {
                // GetTempFileName creates the file, so we rename it to .dll
                tempPath = std::string(tempFile) + ".dll";
                DeleteFileA(tempFile);  // Remove the original temp file

                // Copy original .dll to temp location
                if (grove::fs::copyFile(path, tempPath)) {
                    // CRITICAL FIX: Verify the copy succeeded completely
                    auto origSize = grove::fs::fileSize(path);
                    auto copiedSize = grove::fs::fileSize(tempPath);

                    if (copiedSize != origSize) {
                        logger->error("❌ Incomplete copy: orig={} bytes, copied={} bytes", origSize, copiedSize);
                        DeleteFileA(tempPath.c_str());
                    } else if (origSize == 0) {
                        logger->error("❌ Source file is empty!");
                        DeleteFileA(tempPath.c_str());
                    } else {
                        actualPath = tempPath;
                        usedTempCopy = true;
                        logger->debug("🔄 Using temp copy for hot-reload: {} ({} bytes)", tempPath, copiedSize);
                    }
                } else {
                    logger->warn("⚠️ Failed to copy library, loading directly");
                    DeleteFileA(tempPath.c_str()); // Clean up failed temp file
                }
            }
        }
#else
        // Linux/Unix: Create unique temp filename
        char tempTemplate[] = "/tmp/grove_module_XXXXXX.so";
        int tempFd = mkstemps(tempTemplate, 3); // 3 for ".so"
        if (tempFd == -1) {
            logger->warn("⚠️ Failed to create temp file, loading directly (may use cached version)");
        } else {
            close(tempFd); // Close the fd, we just need the unique name
            tempPath = tempTemplate;

            // Copy original .so to temp location
            if (grove::fs::copyFile(path, tempPath)) {
                // CRITICAL FIX: Verify the copy succeeded completely
                auto origSize = grove::fs::fileSize(path);
                auto copiedSize = grove::fs::fileSize(tempPath);

                if (copiedSize != origSize) {
                    logger->error("❌ Incomplete copy: orig={} bytes, copied={} bytes", origSize, copiedSize);
                    unlink(tempPath.c_str());
                } else if (origSize == 0) {
                    logger->error("❌ Source file is empty!");
                    unlink(tempPath.c_str());
                } else {
                    actualPath = tempPath;
                    usedTempCopy = true;
                    logger->debug("🔄 Using temp copy for hot-reload: {} ({} bytes)", tempPath, copiedSize);
                }
            } else {
                logger->warn("⚠️ Failed to copy .so, loading directly (may use cached version)");
                unlink(tempPath.c_str()); // Clean up failed temp file
            }
        }
#endif
    }

    // Open library
#ifdef _WIN32
    libraryHandle = LoadLibraryA(actualPath.c_str());
    if (!libraryHandle) {
        DWORD errorCode = GetLastError();
        std::string error = "LoadLibrary failed with error code " + std::to_string(errorCode);

        // Clean up temp file if it was created
        if (usedTempCopy) {
            DeleteFileA(tempPath.c_str());
        }

        logLoadError(error);
        throw std::runtime_error("Failed to load module: " + error);
    }
#else
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
#endif

    // Store temp path for cleanup later
    if (usedTempCopy) {
        tempLibraryPath = tempPath;
        logger->debug("📝 Stored temp path for cleanup: {}", tempLibraryPath);
    }

    // Find createModule factory function
#ifdef _WIN32
    createFunc = reinterpret_cast<CreateModuleFunc>(GetProcAddress(static_cast<HMODULE>(libraryHandle), "createModule"));
    if (!createFunc) {
        DWORD errorCode = GetLastError();
        std::string error = "GetProcAddress failed with error code " + std::to_string(errorCode);
        FreeLibrary(static_cast<HMODULE>(libraryHandle));
        libraryHandle = nullptr;
        logLoadError("createModule symbol not found: " + error);
        throw std::runtime_error("Module missing createModule function: " + error);
    }
#else
    createFunc = reinterpret_cast<CreateModuleFunc>(dlsym(libraryHandle, "createModule"));
    if (!createFunc) {
        std::string error = dlerror();
        dlclose(libraryHandle);
        libraryHandle = nullptr;
        logLoadError("createModule symbol not found: " + error);
        throw std::runtime_error("Module missing createModule function: " + error);
    }
#endif

    // Create module instance
    IModule* modulePtr = createFunc();
    if (!modulePtr) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(libraryHandle));
#else
        dlclose(libraryHandle);
#endif
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
#ifdef _WIN32
    BOOL result = FreeLibrary(static_cast<HMODULE>(libraryHandle));
    if (!result) {
        DWORD errorCode = GetLastError();
        logger->error("❌ FreeLibrary failed with error code: {}", errorCode);
    }
#else
    int result = dlclose(libraryHandle);
    if (result != 0) {
        std::string error = dlerror();
        logger->error("❌ dlclose failed: {}", error);
    }
#endif

    // Clean up temp file if it was used
    if (!tempLibraryPath.empty()) {
        logger->debug("🧹 Cleaning up temp file: {}", tempLibraryPath);
#ifdef _WIN32
        if (DeleteFileA(tempLibraryPath.c_str())) {
            logger->debug("✅ Temp file deleted");
        } else {
            logger->warn("⚠️ Failed to delete temp file: {}", tempLibraryPath);
        }
#else
        if (unlink(tempLibraryPath.c_str()) == 0) {
            logger->debug("✅ Temp file deleted");
        } else {
            logger->warn("⚠️ Failed to delete temp file: {}", tempLibraryPath);
        }
#endif
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
