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

    // Drain any remaining orphaned temp files on destruction so we don't leave
    // files behind when the process exits.
    retryOrphanedDeletions();
}

// ============================================================================
// Private helpers
// ============================================================================

bool ModuleLoader::deleteTempFileWithRetry(const std::string& path,
                                            int maxRetries,
                                            int retryDelayMs) {
#ifdef _WIN32
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (DeleteFileA(path.c_str())) {
            if (attempt > 0) {
                logger->debug("✅ Temp file deleted on attempt {} : {}", attempt + 1, path);
            } else {
                logger->debug("✅ Temp file deleted: {}", path);
            }
            return true;
        }

        DWORD err = GetLastError();

        // ERROR_FILE_NOT_FOUND (2) means already gone — treat as success.
        if (err == ERROR_FILE_NOT_FOUND) {
            logger->debug("✅ Temp file already gone: {}", path);
            return true;
        }

        // Any other error: the OS or AV still has the file open.  Sleep and
        // retry so the handle can be released.
        logger->debug("⏳ Temp file still locked (attempt {}/{}, err={}), retrying in {}ms: {}",
                      attempt + 1, maxRetries, err, retryDelayMs, path);
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
    }

    // All retries exhausted.
    logger->warn("⚠️ Could not delete temp file after {} attempts: {}", maxRetries, path);
    return false;
#else
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (unlink(path.c_str()) == 0) {
            logger->debug("✅ Temp file deleted: {}", path);
            return true;
        }

        if (errno == ENOENT) {
            // Already deleted — that's fine.
            logger->debug("✅ Temp file already gone: {}", path);
            return true;
        }

        logger->debug("⏳ Temp file still locked (attempt {}/{}, errno={}), retrying in {}ms: {}",
                      attempt + 1, maxRetries, errno, retryDelayMs, path);
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
    }

    logger->warn("⚠️ Could not delete temp file after {} attempts: {}", maxRetries, path);
    return false;
#endif
}

void ModuleLoader::retryOrphanedDeletions() {
    if (orphanedTempPaths.empty()) return;

    logger->debug("🧹 Retrying {} orphaned temp file deletion(s)...", orphanedTempPaths.size());

    // Build a new list of files that still couldn't be deleted.
    std::vector<std::string> stillFailed;

    for (const auto& orphan : orphanedTempPaths) {
        // Use a single attempt here; if it still fails it stays in the list.
        if (!deleteTempFileWithRetry(orphan, 1, 0)) {
            stillFailed.push_back(orphan);
        }
    }

    if (!stillFailed.empty()) {
        logger->warn("⚠️ {} orphaned temp file(s) still pending deletion", stillFailed.size());
    }

    orphanedTempPaths = std::move(stillFailed);
}

// ============================================================================
// Public API
// ============================================================================

std::unique_ptr<IModule> ModuleLoader::load(const std::string& path, const std::string& name, bool isReload) {
    // Handle cleanup of previous library.
    // - For reload (isReload=true): The caller has already destroyed the old module
    //   via reload(), so it's safe to unload the old library.
    // - For fresh load (isReload=false): Old modules may still be alive, so we
    //   warn but don't auto-unload (caller should use separate loaders or manage
    //   lifecycle).
    if (libraryHandle) {
        if (isReload) {
            // Safe to unload — reload() or the caller destroyed the module first.
            logger->debug("🔄 Unloading previous library before loading new version");
            unload();
        } else {
            logger->warn("⚠️ Loading new module while previous handle still open. "
                         "Consider using separate ModuleLoader instances for independent modules.");
        }
    }

    logLoadStart(path);

    auto loadStartTime = std::chrono::high_resolution_clock::now();

    // CRITICAL FIX: Linux caches .so files by path in dlopen.
    // Even after dlclose, subsequent dlopen of the same path loads from cache.
    // Solution: Create a temporary copy with a unique name for hot-reload.

    std::string actualPath = path;
    std::string tempPath;
    bool usedTempCopy = false;

    if (isReload) {
        // CRITICAL FIX: Wait for the file to be fully written after compilation.
        // The FileWatcher may detect the change while the compiler is still writing.
        logger->debug("⏳ Waiting for library file to be fully written...");

        size_t lastSize = 0;
        size_t stableCount = 0;
        const int maxAttempts = 20;   // 1 second max (20 × 50 ms)
        const int stableRequired = 3; // 3 consecutive identical sizes = stable

        for (int i = 0; i < maxAttempts; i++) {
            size_t currentSize = grove::fs::fileSize(path);

            if (currentSize > 0 && currentSize == lastSize) {
                stableCount++;
                if (stableCount >= stableRequired) {
                    logger->debug("✅ File size stable at {} bytes (after {}ms)",
                                  currentSize, i * 50);
                    break;
                }
            } else {
                stableCount = 0; // Reset on size change
            }

            lastSize = currentSize;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

#ifdef _WIN32
        // Windows: Create a unique temp filename in the system temp directory.
        // GetTempFileNameA creates a zero-byte placeholder file; we rename it
        // to .dll before writing the actual DLL so Windows loads it correctly.
        char tempDir[MAX_PATH];
        if (GetTempPathA(MAX_PATH, tempDir) == 0) {
            logger->warn("⚠️ Failed to get temp directory, loading directly");
        } else {
            char tempFile[MAX_PATH];
            if (GetTempFileNameA(tempDir, "grv", 0, tempFile) == 0) {
                logger->warn("⚠️ Failed to create temp file placeholder, loading directly");
            } else {
                // GetTempFileName creates the placeholder; append .dll and remove
                // the placeholder so we can create grv<N>.tmp.dll cleanly.
                tempPath = std::string(tempFile) + ".dll";
                DeleteFileA(tempFile); // Remove the .tmp placeholder

                if (grove::fs::copyFile(path, tempPath)) {
                    // Verify the copy completed fully before loading.
                    auto origSize   = grove::fs::fileSize(path);
                    auto copiedSize = grove::fs::fileSize(tempPath);

                    if (copiedSize != origSize) {
                        logger->error("❌ Incomplete copy: orig={} bytes, copied={} bytes",
                                      origSize, copiedSize);
                        DeleteFileA(tempPath.c_str());
                    } else if (origSize == 0) {
                        logger->error("❌ Source file is empty!");
                        DeleteFileA(tempPath.c_str());
                    } else {
                        actualPath   = tempPath;
                        usedTempCopy = true;
                        logger->debug("🔄 Using temp copy for hot-reload: {} ({} bytes)",
                                      tempPath, copiedSize);
                    }
                } else {
                    logger->warn("⚠️ Failed to copy library, loading directly");
                    DeleteFileA(tempPath.c_str());
                }
            }
        }
#else
        // Linux/Unix: mkstemps gives us a unique path like /tmp/grove_module_XXXXXX.so
        char tempTemplate[] = "/tmp/grove_module_XXXXXX.so";
        int tempFd = mkstemps(tempTemplate, 3); // 3 == length of ".so"
        if (tempFd == -1) {
            logger->warn("⚠️ Failed to create temp file, loading directly (may use cached version)");
        } else {
            close(tempFd); // We only needed the unique name
            tempPath = tempTemplate;

            if (grove::fs::copyFile(path, tempPath)) {
                auto origSize   = grove::fs::fileSize(path);
                auto copiedSize = grove::fs::fileSize(tempPath);

                if (copiedSize != origSize) {
                    logger->error("❌ Incomplete copy: orig={} bytes, copied={} bytes",
                                  origSize, copiedSize);
                    unlink(tempPath.c_str());
                } else if (origSize == 0) {
                    logger->error("❌ Source file is empty!");
                    unlink(tempPath.c_str());
                } else {
                    actualPath   = tempPath;
                    usedTempCopy = true;
                    logger->debug("🔄 Using temp copy for hot-reload: {} ({} bytes)",
                                  tempPath, copiedSize);
                }
            } else {
                logger->warn("⚠️ Failed to copy .so, loading directly (may use cached version)");
                unlink(tempPath.c_str());
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

        if (usedTempCopy) {
            DeleteFileA(tempPath.c_str());
        }

        logLoadError(error);
        throw std::runtime_error("Failed to load module: " + error);
    }
#else
    libraryHandle = dlopen(actualPath.c_str(), RTLD_NOW);
    if (!libraryHandle) {
        std::string error = dlerror();

        if (usedTempCopy) {
            unlink(tempPath.c_str());
        }

        logLoadError(error);
        throw std::runtime_error("Failed to load module: " + error);
    }
#endif

    // Record the temp path so unload() can clean it up.
    if (usedTempCopy) {
        tempLibraryPath = tempPath;
        logger->debug("📝 Stored temp path for cleanup: {}", tempLibraryPath);
    }

    // Find the createModule factory function.
#ifdef _WIN32
    createFunc = reinterpret_cast<CreateModuleFunc>(
        GetProcAddress(static_cast<HMODULE>(libraryHandle), "createModule"));
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

    // Create the module instance via the factory.
    IModule* modulePtr = createFunc();
    if (!modulePtr) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(libraryHandle));
#else
        dlclose(libraryHandle);
#endif
        libraryHandle = nullptr;
        createFunc    = nullptr;
        logLoadError("createModule returned null");
        throw std::runtime_error("createModule returned null");
    }

    libraryPath = path;
    moduleName  = name;

    auto loadEndTime = std::chrono::high_resolution_clock::now();
    float loadTime   = std::chrono::duration<float, std::milli>(loadEndTime - loadStartTime).count();

    logLoadSuccess(loadTime);

    return std::unique_ptr<IModule>(modulePtr);
}

void ModuleLoader::unload() {
    // --- Phase 0: drain orphaned temp files from previous cycles ---------------
    // Retry any temp files that couldn't be deleted last time.  Doing this
    // before the new unload means failures drain within at most 2 reload cycles,
    // keeping the orphan count at ≤1 at any point in time.
    retryOrphanedDeletions();

    if (!libraryHandle) {
        logger->debug("🔍 Unload called but no library loaded");
        return;
    }

    logUnloadStart();

    // --- Phase 1: release the library handle -----------------------------------
#ifdef _WIN32
    // Save the handle before clearing — we need it to verify unload below.
    HMODULE handleToFree = static_cast<HMODULE>(libraryHandle);
    BOOL freeResult = FreeLibrary(handleToFree);

    if (!freeResult) {
        DWORD errorCode = GetLastError();
        logger->error("❌ FreeLibrary failed with error code: {}", errorCode);
        // Even on failure, null out the handle to avoid double-free.
        libraryHandle = nullptr;
        createFunc    = nullptr;
        libraryPath.clear();
        moduleName.clear();
        logUnloadSuccess();
        return;
    }

    // --- Phase 1b: verify the module is no longer in the process module list --
    // Windows Defender / the OS can keep a DLL open momentarily after
    // FreeLibrary returns TRUE.  If EnumProcessModules still sees it, wait a
    // little and re-check.  We do NOT call FreeLibrary again — that would
    // double-decrement the refcount on a DLL we already freed.
    //
    // Note: GetModuleHandleA returns NULL for the temp path once the OS has
    // actually unmapped it, which is what we want to confirm.
    {
        const int maxVerifyRetries  = 10;
        const int verifyRetryDelayMs = 10; // 10 ms × 10 = 100 ms max wait

        for (int i = 0; i < maxVerifyRetries; ++i) {
            // GetModuleHandleA does NOT increment the refcount (unlike
            // GetModuleHandleEx without GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT).
            // If it returns non-null the module is still loaded.
            if (!tempLibraryPath.empty()) {
                HMODULE stillLoaded = GetModuleHandleA(tempLibraryPath.c_str());
                if (stillLoaded == NULL) {
                    // Module is gone from the process module list — good.
                    if (i > 0) {
                        logger->debug("✅ Module unloaded from process list after {}ms wait",
                                      i * verifyRetryDelayMs);
                    }
                    break;
                }
                logger->debug("⏳ Module still in process list (attempt {}/{}), waiting {}ms...",
                              i + 1, maxVerifyRetries, verifyRetryDelayMs);
                std::this_thread::sleep_for(std::chrono::milliseconds(verifyRetryDelayMs));
            } else {
                // No temp copy — we can't check by path easily; just break.
                break;
            }
        }
    }
#else
    int dlResult = dlclose(libraryHandle);
    if (dlResult != 0) {
        std::string error = dlerror();
        logger->error("❌ dlclose failed: {}", error);
    }
#endif

    libraryHandle = nullptr;
    createFunc    = nullptr;
    libraryPath.clear();
    moduleName.clear();

    // --- Phase 2: delete the temp file ----------------------------------------
    // Use retry logic to handle the case where the OS (or AV) still has the
    // file open briefly after FreeLibrary / dlclose returned.
    if (!tempLibraryPath.empty()) {
        logger->debug("🧹 Cleaning up temp file: {}", tempLibraryPath);

        if (!deleteTempFileWithRetry(tempLibraryPath)) {
            // Couldn't delete even after retries.  Store for the next cycle.
            logger->warn("⚠️ Queuing temp file for deferred deletion: {}", tempLibraryPath);
            orphanedTempPaths.push_back(tempLibraryPath);
        }

        tempLibraryPath.clear();
    }

    logUnloadSuccess();
}

bool ModuleLoader::waitForCleanState(IModule* module, IModuleSystem* moduleSystem, float timeoutSeconds) {
    logger->info("⏳ Waiting for clean state (timeout: {:.1f}s)", timeoutSeconds);

    auto startTime   = std::chrono::high_resolution_clock::now();
    auto lastLogTime = startTime;

    while (true) {
        auto  currentTime = std::chrono::high_resolution_clock::now();
        float elapsed     = std::chrono::duration<float>(currentTime - startTime).count();

        if (elapsed >= timeoutSeconds) {
            logger->error("❌ Clean state timeout after {:.1f}s", elapsed);
            return false;
        }

        bool moduleIdle    = module->isIdle();
        int  pendingTasks  = moduleSystem->getPendingTaskCount(moduleName);

        if (moduleIdle && pendingTasks == 0) {
            logger->info("✅ Clean state reached after {:.3f}s", elapsed);
            return true;
        }

        float timeSinceLastLog = std::chrono::duration<float>(currentTime - lastLogTime).count();
        if (timeSinceLastLog >= 0.5f) {
            logger->info("⏳ Waiting... ({:.1f}s) - module idle: {}, pending tasks: {}",
                         elapsed, moduleIdle, pendingTasks);
            lastLogTime = currentTime;
        }

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
    auto newModule = load(pathToReload, nameToReload, true);
    logger->debug("✅ New library loaded");

    // Step 5: Restore state to new module
    logger->debug("🔁 Restoring state to new module");
    newModule->setState(*state);
    logger->debug("✅ State restored successfully");

    auto  reloadEndTime = std::chrono::high_resolution_clock::now();
    float reloadTime    = std::chrono::duration<float, std::milli>(reloadEndTime - reloadStartTime).count();

    logger->info("✅ Hot-reload completed in {:.3f}ms", reloadTime);

    return newModule;
}

// ============================================================================
// Private logging helpers
// ============================================================================

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
