#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <spdlog/spdlog.h>
#include "IModule.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace grove {

/**
 * @brief Handles dynamic loading/unloading of module .so files
 *
 * ModuleLoader provides:
 * - Dynamic library loading with dlopen/LoadLibrary
 * - Module factory function resolution
 * - State preservation across reloads
 * - Hot-reload capability with <1ms latency
 * - Comprehensive error handling and logging
 *
 * Handle-leak prevention strategy (Windows):
 *   Windows Defender and the OS can hold a DLL open briefly after FreeLibrary
 *   returns, preventing immediate file deletion and causing EnumProcessModules
 *   to still list the module.  We address this in two ways:
 *     1. After FreeLibrary, verify the handle is gone via GetModuleHandleA and
 *        retry up to MAX_FREE_RETRIES times with a short sleep if not.
 *     2. Any temp file whose deletion failed is stored in `orphanedTempPaths`
 *        and retried at the start of the next unload cycle, so accumulated
 *        stale handles drain within 1-2 reloads.
 */
class ModuleLoader {
private:
    std::shared_ptr<spdlog::logger> logger;

    void* libraryHandle = nullptr;
    std::string libraryPath;
    std::string moduleName;
    std::string tempLibraryPath;  // Temp copy path for hot-reload cache bypass

    /**
     * Temp files whose deletion failed on the previous unload (e.g. because
     * Windows Defender still had them open).  They are retried at the
     * beginning of every subsequent unload() call so handles don't accumulate.
     */
    std::vector<std::string> orphanedTempPaths;

    /**
     * A DLL that is finished with but kept MAPPED for one extra reload cycle.
     *
     * WHY (deferred unload): a module can throw a std::exception from inside its
     * own DLL. On MinGW each module .dll statically links its OWN copy of the
     * exception type's vtable + destructor + the what() string buffer. The
     * caller catches that exception BY REFERENCE and then calls reload(), which
     * historically FreeLibrary'd the DLL immediately — WHILE the caught object
     * was still alive. When the caller's catch scope then exits, the runtime
     * runs ~exception() through a vtable now living in unmapped memory →
     * use-after-free → the flaky ChaosMonkey SIGSEGV (un-reproducible under gdb
     * because the fault depends on whether the freed page was reused yet).
     *
     * FIX: reload()/unload() do NOT FreeLibrary the old DLL synchronously. The
     * handle (and its temp-copy path) are parked here and freed one cycle later —
     * by which point the caller's catch block (and the exception object it held)
     * is long gone. Bounded to ~1 entry at steady state (current + one parked).
     */
    struct PendingUnload {
        void*       handle = nullptr;  ///< HMODULE/void* kept mapped until flushed
        std::string tempPath;          ///< temp copy to delete once truly freed
        std::string verifyPath;        ///< canonical path to poll out of the module list
    };
    std::vector<PendingUnload> pendingUnloads_;

    /**
     * @brief FreeLibrary/dlclose every parked handle and delete its temp copy.
     *
     * Called at the top of unload() (frees the handle parked last cycle, whose
     * exception object is now certainly dead) and from the destructor (drains
     * the final parked handle at teardown, when no exception can be in flight).
     */
    void flushDeferredUnload();

    /**
     * Number of successful hot-reloads performed by this loader instance.
     *
     * WHY: Used to scale the post-FreeLibrary wait time adaptively.
     * Under sustained reload cycles, Windows address space fragments faster —
     * giving the OS more time to complete teardown (AV hooks, DW2/SJLJ exception
     * frame deregistration) before the next LoadLibraryA reduces the probability
     * of SIGSEGV at reload ~8+ from address space exhaustion.
     */
    uint32_t reloadCount_ = 0;

    // Factory function signature: IModule* createModule()
    using CreateModuleFunc = IModule* (*)();
    CreateModuleFunc createFunc = nullptr;

    void logLoadStart(const std::string& path);
    void logLoadSuccess(float loadTime);
    void logLoadError(const std::string& error);
    void logUnloadStart();
    void logUnloadSuccess();

    /**
     * @brief Attempt to delete a temp library file with retry logic.
     *
     * On Windows the OS (or AV software) may keep the file open for a brief
     * period after FreeLibrary returns.  We retry up to `maxRetries` times
     * with `retryDelayMs` milliseconds between attempts.
     *
     * @param path       Full path to the temp file.
     * @param maxRetries Number of deletion attempts (default 5).
     * @param retryDelayMs Milliseconds to sleep between retries (default 20).
     * @return true if the file was deleted, false if it could not be removed.
     */
    bool deleteTempFileWithRetry(const std::string& path,
                                  int maxRetries = 5,
                                  int retryDelayMs = 20);

    /**
     * @brief Drain `orphanedTempPaths`, retrying deletions from prior cycles.
     *
     * Called at the top of unload() so that files which previously failed
     * to delete get a second chance once the OS has fully released them.
     */
    void retryOrphanedDeletions();

public:
    ModuleLoader();
    ~ModuleLoader();

    /**
     * @brief Load a module from .so / .dll file.
     * @param path     Path to the library file.
     * @param name     Module name for logging/identification.
     * @param isReload If true, use a unique temp copy to bypass OS DLL cache.
     * @return Unique pointer to the loaded module.
     * @throws std::runtime_error if loading fails.
     */
    std::unique_ptr<IModule> load(const std::string& path, const std::string& name, bool isReload = false);

    /**
     * @brief Unload the currently loaded module library.
     *
     * Closes the library handle, verifies the handle is gone (Windows),
     * deletes the temp copy if one was used, and drains any orphaned temp
     * files from previous cycles.
     */
    void unload();

    /**
     * @brief Hot-reload: save state → unload → reload → restore state.
     * @param currentModule Module with state to preserve.
     * @return New module instance with preserved state.
     * @throws std::runtime_error if reload fails.
     */
    std::unique_ptr<IModule> reload(std::unique_ptr<IModule> currentModule);

    /**
     * @brief Check if a module is currently loaded.
     */
    bool isLoaded() const { return libraryHandle != nullptr; }

    /**
     * @brief Get path to the source library (not the temp copy).
     */
    const std::string& getLoadedPath() const { return libraryPath; }

    /**
     * @brief Get name of the currently loaded module.
     */
    const std::string& getModuleName() const { return moduleName; }

    /**
     * @brief Wait for module to reach clean state (idle + no pending tasks).
     * @param module          Module to wait for.
     * @param moduleSystem    Module system to check for pending tasks.
     * @param timeoutSeconds  Maximum wait time (default: 5.0).
     * @return true if clean state reached before timeout.
     */
    bool waitForCleanState(
        IModule* module,
        class IModuleSystem* moduleSystem,
        float timeoutSeconds = 5.0f
    );
};

} // namespace grove
