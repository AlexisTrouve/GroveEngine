#include <grove/ModuleLoader.h>
#include <grove/IModuleSystem.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <grove/platform/FileSystem.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <logger/Logger.h>

// Process-lifetime unique counter for temp file naming.
//
// WHY: Using only PID as a temp file suffix means every reload attempt reuses
// the same candidate filename if the previous copy could not be deleted (e.g.
// AV scanner still has the file open).  GetTempFileNameA() adds a 4-digit
// hex discriminator, but under rapid reload cycles the OS reuses those slots.
// An atomic counter that never wraps during a single process run guarantees
// each reload gets a genuinely unique name, eliminating filename collision as
// a cause of copyFile failures and the subsequent direct-load fallback that
// leads to address-space exhaustion (SIGSEGV after ~23 cycles).
static std::atomic<uint64_t> g_tempFileSeq{0};

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
        unload();  // parks the current DLL + flushes any older parked one
    }

    // Drain the final parked DLL. At teardown no caller catch handler can be in
    // flight, so FreeLibrary on the last deferred handle is safe to do now.
    flushDeferredUnload();

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

    // Move stillFailed back into orphanedTempPaths before emitting the
    // diagnostic so we can report the post-move accumulated total (M).
    orphanedTempPaths = std::move(stillFailed);

    // Diagnostic: report how many are still locked and the total accumulated.
    // WHY: Visible in logs during chaos monkey runs to confirm whether
    // address space pressure is building up from irremovable temp DLLs.
    // N = still-locked count, M = total accumulated in orphanedTempPaths.
    if (!orphanedTempPaths.empty()) {
        logger->warn("⚠️ [{} orphaned temp files still locked after retry — total accumulated: {}]",
                     orphanedTempPaths.size(), orphanedTempPaths.size());
    }
}

void ModuleLoader::flushDeferredUnload() {
    // QUOI : libérer pour de bon (FreeLibrary / dlclose) les DLLs PARQUÉES au
    //        cycle précédent, puis supprimer leur copie temporaire.
    //
    // POURQUOI : on a volontairement DIFFÉRÉ leur démappage (voir PendingUnload
    //        dans le header). Une DLL parquée est une DLL dont le module a peut-être
    //        levé une exception que l'appelant a attrapée PAR RÉFÉRENCE avant de
    //        rappeler reload(). Si on l'avait FreeLibrary tout de suite, le ~exception()
    //        exécuté à la sortie du catch de l'appelant sauterait dans une vtable
    //        démappée → use-after-free (le SIGSEGV flaky de ChaosMonkey, invisible
    //        sous gdb). En la libérant ICI — au tout début du PROCHAIN unload(), ou
    //        à la destruction — on a la garantie que la portée catch de l'appelant
    //        s'est refermée depuis longtemps : l'objet exception est mort, le
    //        FreeLibrary est donc sûr.
    //
    // COMMENT : pour chaque entrée parquée — FreeLibrary, puis (Windows) on attend
    //        que la DLL quitte la liste des modules du process (poll GetModuleHandleA,
    //        ~0ms si FreeLibrary démappe tout de suite) AVANT de supprimer la copie
    //        temp. Plus de sleep aveugle adaptatif : c'était un pansement sur l'UAF
    //        d'exception, désormais corrigé à la racine (voir le else ci-dessous).
    //        Enfin, suppression de la copie temp (mise en orphelin si encore verrouillée).
    if (pendingUnloads_.empty()) return;

    for (auto& p : pendingUnloads_) {
        if (!p.handle) continue;

#ifdef _WIN32
        HMODULE handleToFree = static_cast<HMODULE>(p.handle);
        BOOL freeResult = FreeLibrary(handleToFree);
        if (!freeResult) {
            DWORD errorCode = GetLastError();
            logger->error("❌ FreeLibrary (deferred) failed with error code: {}", errorCode);
        } else {
            // NO blind post-FreeLibrary sleep. The historical inline path slept an
            // adaptive 150–500ms here "to let DW2/SJLJ deregistration + AV hooks
            // finish before the next LoadLibraryA". That was a SYMPTOMATIC mitigation
            // for the cross-DLL exception use-after-free that deferred unload now fixes
            // at the root (see PendingUnload): a bigger wait merely made the freed
            // vtable page more likely to be already remapped when ~exception() ran, so
            // the flaky crash fired less often — it never addressed the cause. With the
            // UAF gone, the only real requirement is that the DLL be fully unmapped
            // before we delete its temp file / the address is reused. We gate on that
            // directly — polling GetModuleHandleA until the module leaves the process
            // list — instead of a fixed blind wait. In the common case FreeLibrary
            // unmaps synchronously so the first poll returns NULL (~0ms), cutting
            // recovery latency from ~800ms to ~300ms (ChaosMonkey: 150 recoveries in
            // <60s instead of timing out at 121s). The poll still tolerates the rare
            // case where an AV scanner briefly keeps the module listed.
            if (!p.verifyPath.empty()) {
                const int maxVerifyRetries   = 50;
                const int verifyRetryDelayMs = 10;  // up to 500ms ONLY if it lingers
                for (int i = 0; i < maxVerifyRetries; ++i) {
                    if (GetModuleHandleA(p.verifyPath.c_str()) == NULL) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(verifyRetryDelayMs));
                }
            }
        }
#else
        if (dlclose(p.handle) != 0) {
            logger->error("❌ dlclose (deferred) failed: {}", dlerror());
        }
#endif

        // Now that the DLL is truly unmapped, delete its temp copy (orphan it for
        // a later retry if the OS/AV still holds the file open).
        if (!p.tempPath.empty()) {
            logger->debug("🧹 Cleaning up deferred temp file: {}", p.tempPath);
            if (!deleteTempFileWithRetry(p.tempPath)) {
                logger->warn("⚠️ Queuing temp file for deferred deletion: {}", p.tempPath);
                orphanedTempPaths.push_back(p.tempPath);
            }
        }
    }

    pendingUnloads_.clear();
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

    // Aggressively drain orphaned temp DLLs before creating the new temp copy.
    // WHY: Orphaned locked DLLs fragment the Windows address space - each
    // locked temp file occupies a region that LoadLibraryA cannot reclaim.
    // Cleaning them here (in addition to the drain in unload()) gives us a
    // second chance to free those regions before we allocate another one,
    // directly reducing the risk of SIGSEGV at reload 8+ from exhaustion.
    retryOrphanedDeletions();
    if (!orphanedTempPaths.empty()) {
        logger->warn("⚠️ [{} orphaned temp DLLs persist before loading — address space may be fragmented]",
                     orphanedTempPaths.size());
    }

    logLoadStart(path);

    auto loadStartTime = std::chrono::high_resolution_clock::now();

    // CRITICAL FIX: Linux caches .so files by path in dlopen.
    // Even after dlclose, subsequent dlopen of the same path loads from cache.
    // Solution: Create a temporary copy with a unique name for hot-reload.

    std::string actualPath = path;
    std::string tempPath;
    bool usedTempCopy = false;

    // WHY: On Windows, LoadLibraryA holds an exclusive write-lock on the DLL file.
    // Even for the initial (non-reload) load, we must load from a temp copy so that
    // the original file path (build/tests/libTestModule.dll) remains writable by the
    // build tool (ninja/gcc ld.exe). Without this, the first compilation attempt in
    // test_04_race_condition always fails with ld.exe "Permission denied", because
    // the initial LoadLibraryA maps the file and prevents overwriting.
    //
    // On Linux, dlopen with RTLD_NOW does not lock the .so file for writing —
    // mmap() is used internally, but the file path can still be replaced. So the
    // Linux path only needs temp copies when isReload=true (to bypass dlopen cache).
    //
    // FIX: on Windows, always enter the temp-copy block. On Linux, only for reload.
    // The file-stability wait inside the block is still gated on isReload (initial
    // loads don't need it — the file is already fully present).
#ifdef _WIN32
    // Windows: always use a temp copy so the original DLL path stays writable.
    if (true) {
        if (isReload) {
            // For reloads: wait for the new DLL to finish writing before copying.
            logger->debug("⏳ Waiting for library file to be fully written...");

            size_t lastSize = 0;
            size_t stableCount = 0;
            const int maxAttempts = 20;
            const int stableRequired = 3;

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
                    stableCount = 0;
                }
                lastSize = currentSize;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        // For initial loads: the file is already fully present; no stability wait needed.

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
                    // First copy attempt failed — Windows may still have the source
                    // DLL file-mapped from the previous FreeLibrary cycle.  Retry
                    // with exponential backoff so the OS has time to release the
                    // lock, rather than immediately falling back to direct load
                    // (which causes address-space exhaustion after ~23 cycles and
                    // ultimately a SIGSEGV).
                    DeleteFileA(tempPath.c_str()); // Remove the stale placeholder

                    bool copySucceeded = false;
                    for (int retry = 0; retry < 10 && !copySucceeded; ++retry) {
                        // On the first retry, sleep before trying again.
                        // Backoff schedule: 100ms, 150ms, 200ms, 250ms, 300ms…
                        // Start at 100ms because Windows file locks after FreeLibrary
                        // typically persist for 50–200ms (AV scanner involvement).
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(100 + 50 * retry));

                        // Generate a fresh, unique temp filename each iteration.
                        // We cannot reuse the old tempFile path because the OS may
                        // still see it as locked even though DeleteFileA returned.
                        char retryTempFile[MAX_PATH];
                        if (GetTempFileNameA(tempDir, "grv", 0, retryTempFile) == 0) {
                            logger->debug("⏳ Retry {}/10: GetTempFileNameA failed", retry + 1);
                            continue;
                        }

                        // GetTempFileNameA creates a zero-byte placeholder; rename it
                        // to .dll so Windows will accept it as a loadable module.
                        std::string retryTempPath = std::string(retryTempFile) + ".dll";
                        DeleteFileA(retryTempFile); // Remove placeholder before copy

                        if (!grove::fs::copyFile(path, retryTempPath)) {
                            // Copy failed — placeholder may or may not exist; clean up.
                            DeleteFileA(retryTempPath.c_str());
                            logger->debug("⏳ Retry {}/10: copyFile failed", retry + 1);
                            continue;
                        }

                        // Verify byte-exact completeness before trusting this copy.
                        auto origSize   = grove::fs::fileSize(path);
                        auto copiedSize = grove::fs::fileSize(retryTempPath);

                        if (copiedSize != origSize || origSize == 0) {
                            logger->debug("⏳ Retry {}/10: size mismatch orig={} copied={}",
                                          retry + 1, origSize, copiedSize);
                            DeleteFileA(retryTempPath.c_str());
                            continue;
                        }

                        // Success — promote the retry copy as the path to load.
                        actualPath   = retryTempPath;
                        tempPath     = retryTempPath; // ensures cleanup on unload
                        usedTempCopy = true;
                        copySucceeded = true;
                        logger->debug("🔄 Temp copy succeeded on retry {}/10: {} ({} bytes)",
                                      retry + 1, retryTempPath, copiedSize);
                    }

                    if (!copySucceeded) {
                        // All 10 PID-based retries exhausted.
                        //
                        // WHY ONE MORE ATTEMPT: GetTempFileNameA's 4-digit hex
                        // discriminator has a limited namespace and can be reused
                        // across rapid reload cycles, especially if old temp files
                        // are still locked by AV software.  The global g_tempFileSeq
                        // counter is guaranteed unique for the process lifetime, so
                        // this attempt picks a filename that has never been used in
                        // this process, bypassing any OS-level reuse.
                        //
                        // Only fall back to direct load if this final attempt also
                        // fails — direct load causes address-space fragmentation and
                        // SIGSEGV after ~23 cycles, so we must avoid it if at all
                        // possible.
                        uint64_t seqNum = g_tempFileSeq.fetch_add(1, std::memory_order_relaxed);

                        // Build a unique path in the system temp dir.
                        // Format: <tempDir>\grvU<pid>_<seq>.dll
                        // Both PID and monotonic seq are embedded so it is unique
                        // even if multiple ModuleLoader instances run in parallel.
                        char finalTempPath[MAX_PATH];
                        snprintf(finalTempPath, sizeof(finalTempPath),
                                 "%sgrvU%lu_%llu.dll",
                                 tempDir,
                                 static_cast<unsigned long>(GetCurrentProcessId()),
                                 static_cast<unsigned long long>(seqNum));

                        logger->debug("🔄 Final unique-seq attempt: {}", finalTempPath);

                        if (grove::fs::copyFile(path, finalTempPath)) {
                            auto origSize   = grove::fs::fileSize(path);
                            auto copiedSize = grove::fs::fileSize(finalTempPath);

                            if (copiedSize == origSize && origSize > 0) {
                                // Success — use this copy instead of falling back.
                                actualPath   = finalTempPath;
                                tempPath     = finalTempPath;
                                usedTempCopy = true;
                                copySucceeded = true;
                                logger->debug("✅ Final unique-seq copy succeeded: {} ({} bytes)",
                                              finalTempPath, copiedSize);
                            } else {
                                logger->debug("⚠️ Final unique-seq copy size mismatch: orig={} copied={}",
                                              origSize, copiedSize);
                                DeleteFileA(finalTempPath);
                            }
                        } else {
                            logger->debug("⚠️ Final unique-seq copyFile failed");
                            DeleteFileA(finalTempPath); // clean up any partial write
                        }

                        if (!copySucceeded) {
                            // Truly last resort: load the original DLL directly.
                            // This risks address-space reuse and SIGSEGV after
                            // ~23 cycles, but is better than refusing to load at all.
                            logger->warn("⚠️ Failed to copy library after 10 retries + seq attempt, "
                                         "loading directly (SIGSEGV risk at sustained reload counts)");
                        }
                    }
                }
            }
        }
#else
    {  // Linux: opener matching the Windows `if (true) {` above, so the brace after #endif balances on both platforms (the brace straddle broke the Linux build).
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
    // Load the library, retrying ONLY on transient file-lock errors.
    // QUOI : tenter LoadLibraryA ; si l'OS répond par un verrou TRANSITOIRE
    //        (ERROR_SHARING_VIOLATION 32 / ERROR_ACCESS_DENIED 5 / ERROR_LOCK_VIOLATION 33),
    //        attendre brièvement et réessayer.
    // POURQUOI : la copie temp vient d'être écrite ; un scanner antivirus (Defender)
    //        peut la tenir ouverte le temps de la scanner → le mapping échoue avec un
    //        verrou transitoire. Sous forte charge parallèle (plusieurs tests
    //        hot-reload concurrents + scan AV), ça arrive ponctuellement (vu : ctest -j4,
    //        ChaosMonkey, LoadLibrary err=32). Un retry borné est la réponse correcte à
    //        une condition transitoire — préférable à l'ancien sleep aveugle de ~500ms
    //        imposé à CHAQUE reload (il « couvrait » ce cas en ralentissant tout). Ici le
    //        chemin nominal (pas de verrou) ne paie rien : succès au 1er essai.
    // COMMENT : jusqu'à 20 essais espacés de 30ms (≈600ms max, et SEULEMENT si verrou).
    //        Toute autre erreur (DLL invalide / introuvable) échoue immédiatement.
    DWORD loadError = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
        libraryHandle = LoadLibraryA(actualPath.c_str());
        if (libraryHandle) break;
        loadError = GetLastError();
        const bool transientLock = (loadError == ERROR_SHARING_VIOLATION ||
                                    loadError == ERROR_ACCESS_DENIED ||
                                    loadError == ERROR_LOCK_VIOLATION);
        if (!transientLock) break;  // genuine load failure — don't waste retries
        logger->debug("⏳ LoadLibrary transient lock (err={}, attempt {}/20), retrying in 30ms: {}",
                      loadError, attempt + 1, actualPath);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    if (!libraryHandle) {
        std::string error = "LoadLibrary failed with error code " + std::to_string(loadError);

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

    // --- Phase 0.5: tear down IIO callbacks that live in THIS module's DLL ------
    // QUOI : purger les abonnements (Subscription::handler) de l'instance IntraIO
    //        qui porte le nom de ce module, AVANT de démapper sa DLL.
    //
    // POURQUOI : un module enregistre ses abonnements via une lambda compilée DANS
    //        sa propre DLL ; le std::function qui l'enveloppe garde un pointeur
    //        interne (manager/invoker) vers du code de cette DLL. Le singleton
    //        IntraIOManager garde l'instance IntraIO du module vivante jusqu'à la
    //        destruction statique (exit()). Si on FreeLibrary/dlclose plus bas SANS
    //        détruire ces std::function d'abord, c'est ~IntraIOManager → ~IntraIO
    //        qui les détruit à exit() — DLL déjà démappée → l'invoker saute dans du
    //        code libéré → SIGSEGV. Prouvé au gdb sur IT_015 :
    //        "#0 ?? () ← ~IntraIO ← ~IntraIOManager", adresse #0 dans une DLL
    //        absente de la liste des modules chargés.
    //
    // COMMENT : tant que la DLL est encore mappée (avant FreeLibrary), on récupère
    //        l'instance IntraIO dont l'id == moduleName (convention du codebase :
    //        un module ↔ une instance IIO homonyme) et on vide ses subscriptions.
    //        Détruire les std::function ici exécute leur manager-pointer alors qu'il
    //        est encore valide. On utilise tryGetLiveInstance() et NON getInstance() :
    //        getInstance() est un singleton Meyers qui CRÉERAIT le manager (et son
    //        thread) s'il n'existe pas — désastreux quand unload() tourne depuis
    //        ~ModuleLoader à exit(). tryGetLiveInstance() renvoie nullptr si aucun
    //        manager n'est vivant → on saute proprement. Couvre aussi le reload :
    //        reload() appelle unload() avant FreeLibrary de l'ancienne DLL.
    if (!moduleName.empty()) {
        if (auto* manager = IntraIOManager::tryGetLiveInstance()) {
            if (auto ioInstance = manager->getInstance(moduleName)) {
                ioInstance->clearAllSubscriptions();
            }
            // Also drop the manager-side routing (TopicTree / instancePatterns) for
            // this module. clearAllSubscriptions() above only clears the IntraIO-side
            // handler vectors; without this the manager kept routing to a queue that
            // the unloaded module no longer drains (lost messages + stale-entry leak
            // across reloads).
            manager->clearInstanceSubscriptions(moduleName);
        }
    }

    // --- Phase 1: free the PREVIOUSLY parked DLL (deferred unload) --------------
    // If a module threw and the caller caught it by reference before calling
    // reload() again, that exception object was destroyed when the earlier catch
    // scope exited — so the parked DLL is no longer referenced and FreeLibrary is
    // safe now. (See PendingUnload / flushDeferredUnload for the full
    // use-after-free rationale; it also carries the adaptive post-free wait +
    // process-module-list verify that used to live here.)
    flushDeferredUnload();

    // --- Phase 1b: PARK the current DLL instead of freeing it synchronously -----
    // QUOI : on ne FreeLibrary/dlclose PAS la DLL courante ici ; on garde son
    //        handle (et sa copie temp) MAPPÉ, à libérer au prochain unload() / à
    //        la destruction.
    // POURQUOI : reload() est typiquement appelé depuis le catch de l'appelant qui
    //        vient d'attraper PAR RÉFÉRENCE une exception levée par CE module. Cet
    //        objet exception porte une vtable + un destructeur compilés DANS cette
    //        DLL. La démapper maintenant ferait sauter le ~exception() (exécuté à la
    //        fermeture du catch, APRÈS le retour de reload()) dans du code libéré →
    //        use-after-free (le SIGSEGV flaky de ChaosMonkey). En différant d'un
    //        cycle, la DLL reste mappée au-delà de la portée catch de l'appelant.
    // COMMENT : on mémorise handle + chemin temp + chemin canonique (pour vérifier
    //        plus tard sa sortie de la liste des modules) puis on remet le loader en
    //        état « déchargé ». Borné à ≤1 entrée parquée en régime établi.
    {
        PendingUnload parked;
        parked.handle   = libraryHandle;
        parked.tempPath = tempLibraryPath;  // deleted by flushDeferredUnload next cycle
#ifdef _WIN32
        // Canonicalize the path we loaded from (temp copy if any, else the
        // original — covers the direct-load fallback) so a later GetModuleHandleA
        // can find it in the process module list. Windows stores module handles
        // under their absolute path, not relative paths like "./foo.dll".
        const std::string& rawVerifyPath = !tempLibraryPath.empty() ? tempLibraryPath : libraryPath;
        std::string verifyPath = rawVerifyPath;
        if (!rawVerifyPath.empty()) {
            char fullPath[MAX_PATH];
            if (GetFullPathNameA(rawVerifyPath.c_str(), MAX_PATH, fullPath, nullptr) != 0) {
                verifyPath = fullPath;
            }
        }
        parked.verifyPath = std::move(verifyPath);
#endif
        pendingUnloads_.push_back(std::move(parked));
        logger->debug("🅿️ Parked DLL for deferred unload (pending={})", pendingUnloads_.size());
    }

    libraryHandle = nullptr;
    createFunc    = nullptr;
    libraryPath.clear();
    moduleName.clear();

    // --- Phase 2: the temp file is now OWNED by the parked entry ----------------
    // We deferred FreeLibrary, so the temp .dll is still memory-mapped and cannot
    // be deleted yet; flushDeferredUnload() deletes it once it truly frees the
    // handle next cycle. Here we only relinquish our copy of the path (ownership
    // moved into the parked entry above) so we don't double-track or re-delete it.
    tempLibraryPath.clear();

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

    // Step 1b: RE-HOME the state into a host-owned object BEFORE unloading the DLL.
    // QUOI : recopier l'état dans un JsonDataNode fraîchement construit ICI (dans
    //        grove_impl, lié dans l'exécutable hôte) au lieu de transporter l'objet
    //        rendu par l'ancien module.
    // POURQUOI : grove_impl est une lib STATIC, donc l'ancienne DLL du module embarque
    //        SA PROPRE copie de la vtable/type_info de JsonDataNode. `state` a été
    //        alloué par l'ancienne DLL → son pointeur de vtable pointe DANS cette DLL.
    //        unload() (step 3) la FreeLibrary ; tout accès virtuel/RTTI ultérieur sur
    //        *state (le dynamic_cast + getJsonData() de setState au step 5) lirait alors
    //        une vtable démappée → SIGSEGV intermittent (la réutilisation d'adresses rend
    //        le crash flaky, d'où l'impossibilité de le reproduire sous gdb).
    // COMMENT : tant que l'ancienne DLL est encore mappée, on downcast vers le type
    //        concret JsonDataNode (le dynamic_cast lit la vtable encore valide) et on
    //        deep-copie son JSON dans un JsonDataNode construit ici. Cette construction
    //        non-virtuelle utilise la copie de la vtable de l'image hôte (jamais
    //        déchargée) → l'objet survit à l'unload. Si l'état n'est pas un JsonDataNode
    //        (cas non rencontré dans ce codebase) on le laisse tel quel.
    if (auto* jsonState = dynamic_cast<JsonDataNode*>(state.get())) {
        state = std::make_unique<JsonDataNode>("state", jsonState->getJsonData());
        logger->debug("✅ State re-homed into host-owned node (survives DLL unload)");
    }

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

    // Count successful reloads (informational — surfaced in the completion log
    // and useful for diagnostics). No longer drives any timing: the old adaptive
    // post-FreeLibrary wait that read this was removed once deferred unload fixed
    // the underlying exception use-after-free at the root.
    ++reloadCount_;

    logger->info("✅ Hot-reload completed in {:.3f}ms (reloadCount now {})",
                 reloadTime, reloadCount_);

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
