// ============================================================================
// test_05_memory_leak.cpp - Memory Leak Hunter
// ============================================================================
// Tests that repeated hot-reload cycles do not leak memory
//
// Strategy:
// - Load the same .so file 200 times (no recompilation)
// - Measure memory usage every 5 seconds
// - Verify temp file cleanup
// - Check for library handle leaks
//
// Success criteria:
// - < 10 MB total memory growth
// - < 50 KB average memory per reload
// - Temp files cleaned up (≤ 2 at any time)
// - No increase in mapped .so count
// - 100% reload success rate
// ============================================================================

#include "grove/ModuleLoader.h"
#include "grove/SequentialModuleSystem.h"
#include "grove/JsonDataNode.h"
#include "../helpers/SystemUtils.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <filesystem>
#include <cmath>

using namespace grove;
namespace fs = std::filesystem;

// ============================================================================
// Configuration
// ============================================================================

const int TOTAL_RELOADS = 200;
const int RELOAD_INTERVAL_MS = 500;
const int MEMORY_CHECK_INTERVAL_MS = 5000;
const float TARGET_FPS = 60.0f;
const int MAX_TEST_TIME_SECONDS = 180;

// ============================================================================
// State tracking
// ============================================================================

struct MemorySnapshot {
    int reloadCount;
    size_t memoryBytes;
    int tempFiles;
    int mappedLibs;
    std::chrono::steady_clock::time_point timestamp;
};

std::atomic<bool> g_running{true};
std::atomic<int> g_reloadCount{0};
std::atomic<int> g_reloadSuccesses{0};
std::atomic<int> g_reloadFailures{0};
std::atomic<int> g_crashes{0};
std::vector<MemorySnapshot> g_snapshots;
std::mutex g_snapshotMutex;

// ============================================================================
// Reload Scheduler Thread
// ============================================================================

void reloadSchedulerThread(ModuleLoader& loader, SequentialModuleSystem* moduleSystem,
                          const fs::path& modulePath, std::mutex& moduleSystemMutex) {
    std::cout << "  Starting ReloadScheduler thread...\n";

    while (g_running && g_reloadCount < TOTAL_RELOADS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(RELOAD_INTERVAL_MS));

        if (!g_running) break;

        try {
            std::lock_guard<std::mutex> lock(moduleSystemMutex);

            // Extract current module and save state
            auto module = moduleSystem->extractModule();
            auto state = module->getState();
            // Re-home state into a host-owned JsonDataNode BEFORE load() unloads the old
            // DLL. `state` was allocated by the old module DLL (its vtable lives there);
            // load(isReload=true) below FreeLibrary's that DLL, after which setState()'s
            // virtual/RTTI access on *state dereferences an unmapped vtable -> SIGSEGV.
            // (config just below was already deep-copied this way; state was the straggler.)
            if (auto* js = dynamic_cast<JsonDataNode*>(state.get())) {
                state = std::make_unique<JsonDataNode>("state", js->getJsonData());
            }
            auto config = std::make_unique<JsonDataNode>("config",
                dynamic_cast<const JsonDataNode&>(module->getConfiguration()).getJsonData());

            // CRITICAL: Destroy old module BEFORE reloading to avoid use-after-free
            // The loader.load() will unload the old .so, so we must destroy the module first
            module.reset();

            // Reload the same .so file
            auto newModule = loader.load(modulePath.string(), "LeakTestModule", true);

            g_reloadCount++;

            if (newModule) {
                // Restore state
                newModule->setConfiguration(*config, nullptr, nullptr);
                newModule->setState(*state);

                // Register new module
                moduleSystem->registerModule("LeakTestModule", std::move(newModule));
                g_reloadSuccesses++;
            } else {
                // Reload failed - we can't recover (old module destroyed)
                g_reloadFailures++;
            }
        } catch (...) {
            g_crashes++;
            g_reloadFailures++;
            g_reloadCount++;
        }
    }

    std::cout << "  ReloadScheduler thread finished.\n";
}

// ============================================================================
// Memory Monitor Thread
// ============================================================================

void memoryMonitorThread() {
    std::cout << "  Starting MemoryMonitor thread...\n";

    auto startTime = std::chrono::steady_clock::now();

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(MEMORY_CHECK_INTERVAL_MS));

        if (!g_running) break;

        MemorySnapshot snapshot;
        snapshot.reloadCount = g_reloadCount;
        snapshot.memoryBytes = getCurrentMemoryUsage();
        snapshot.tempFiles = countTempFiles("/tmp/grove_module_*");
        snapshot.mappedLibs = getMappedLibraryCount();
        snapshot.timestamp = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(g_snapshotMutex);
            g_snapshots.push_back(snapshot);
        }

        // Print progress
        float memoryMB = snapshot.memoryBytes / (1024.0f * 1024.0f);
        int progress = (snapshot.reloadCount * 100) / TOTAL_RELOADS;

        std::cout << "\n  Progress: " << snapshot.reloadCount << " reloads (" << progress << "%)\n";
        std::cout << "    Memory: " << std::fixed << std::setprecision(1) << memoryMB << " MB\n";
        std::cout << "    Temp files: " << snapshot.tempFiles << "\n";
        std::cout << "    Mapped .so: " << snapshot.mappedLibs << "\n";
    }

    std::cout << "  MemoryMonitor thread finished.\n";
}

// ============================================================================
// Engine Thread
// ============================================================================

void engineThread(SequentialModuleSystem* moduleSystem, std::mutex& moduleSystemMutex) {
    std::cout << "  Starting Engine thread (60 FPS)...\n";

    const float frameTime = 1.0f / TARGET_FPS;
    auto lastFrame = std::chrono::steady_clock::now();
    int frameCount = 0;

    while (g_running && g_reloadCount < TOTAL_RELOADS) {
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastFrame).count();

        if (deltaTime >= frameTime) {
            try {
                std::lock_guard<std::mutex> lock(moduleSystemMutex);
                moduleSystem->processModules(deltaTime);
                frameCount++;
            } catch (...) {
                g_crashes++;
            }

            lastFrame = now;
        } else {
            // Sleep for remaining time
            int sleepMs = static_cast<int>((frameTime - deltaTime) * 1000);
            if (sleepMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            }
        }
    }

    std::cout << "  Engine thread finished (" << frameCount << " frames processed).\n";
}

// ============================================================================
// Main Test
// ============================================================================

int main() {
    std::cout << "================================================================================\n";
    std::cout << "TEST: Memory Leak Hunter - " << TOTAL_RELOADS << " Reload Cycles\n";
    std::cout << "================================================================================\n\n";

    // Find module path
    fs::path modulePath = "./libLeakTestModule.so";
#ifdef _WIN32
    modulePath = "./libLeakTestModule.dll";
#endif
    if (!fs::exists(modulePath)) {
        std::cerr << "❌ Module not found: " << modulePath << "\n";
        return 1;
    }

    std::cout << "Setup:\n";
    std::cout << "  Module path:     " << modulePath << "\n";
    std::cout << "  Total reloads:   " << TOTAL_RELOADS << "\n";
    std::cout << "  Interval:        " << RELOAD_INTERVAL_MS << "ms\n";
    std::cout << "  Expected time:   ~" << (TOTAL_RELOADS * RELOAD_INTERVAL_MS / 1000) << "s\n\n";

    // Create module loader and system
    ModuleLoader loader;
    auto moduleSystem = std::make_unique<SequentialModuleSystem>();
    std::mutex moduleSystemMutex;

    // Disable verbose logging for performance
    moduleSystem->setLogLevel(spdlog::level::err);

    // Load initial module
    try {
        auto module = loader.load(modulePath.string(), "LeakTestModule", false);
        if (!module) {
            std::cerr << "❌ Failed to load LeakTestModule\n";
            return 1;
        }

        nlohmann::json configJson = nlohmann::json::object();
        auto config = std::make_unique<JsonDataNode>("config", configJson);
        module->setConfiguration(*config, nullptr, nullptr);
        moduleSystem->registerModule("LeakTestModule", std::move(module));
        std::cout << "✓ Initial module loaded\n\n";
    } catch (const std::exception& e) {
        std::cerr << "❌ Failed to load initial module: " << e.what() << "\n";
        return 1;
    }

    // Baseline memory
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    size_t baselineMemory = getCurrentMemoryUsage();
    int baselineMappedLibs = getMappedLibraryCount();
    float baselineMB = baselineMemory / (1024.0f * 1024.0f);

    std::cout << "Baseline memory: " << std::fixed << std::setprecision(1) << baselineMB << " MB\n";
    std::cout << "Baseline mapped .so: " << baselineMappedLibs << "\n\n";

    // Start threads
    auto startTime = std::chrono::steady_clock::now();

    std::thread reloadThread(reloadSchedulerThread, std::ref(loader), moduleSystem.get(),
                            modulePath, std::ref(moduleSystemMutex));
    std::thread monitorThread(memoryMonitorThread);
    std::thread engThread(engineThread, moduleSystem.get(), std::ref(moduleSystemMutex));

    // Wait for completion or timeout
    auto deadline = startTime + std::chrono::seconds(MAX_TEST_TIME_SECONDS);

    while (g_running && g_reloadCount < TOTAL_RELOADS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (std::chrono::steady_clock::now() > deadline) {
            std::cout << "\n⚠️  Test timeout after " << MAX_TEST_TIME_SECONDS << " seconds\n";
            break;
        }
    }

    // Stop threads
    g_running = false;

    reloadThread.join();
    monitorThread.join();
    engThread.join();

    auto endTime = std::chrono::steady_clock::now();
    float durationSeconds = std::chrono::duration<float>(endTime - startTime).count();

    // Final measurements
    size_t finalMemory = getCurrentMemoryUsage();
    int finalMappedLibs = getMappedLibraryCount();
    int finalTempFiles = countTempFiles("/tmp/grove_module_*");

    float finalMB = finalMemory / (1024.0f * 1024.0f);
    float growthMB = (finalMemory - baselineMemory) / (1024.0f * 1024.0f);

    // ========================================================================
    // Results Summary
    // ========================================================================

    std::cout << "\n================================================================================\n";
    std::cout << "MEMORY LEAK HUNTER SUMMARY\n";
    std::cout << "================================================================================\n\n";

    std::cout << "Duration: " << static_cast<int>(durationSeconds) << "s\n\n";

    std::cout << "Reloads:\n";
    std::cout << "  Total:        " << g_reloadCount << "\n";
    std::cout << "  Successes:    " << g_reloadSuccesses;
    if (g_reloadCount > 0) {
        float successRate = (g_reloadSuccesses * 100.0f) / g_reloadCount;
        std::cout << " (" << std::fixed << std::setprecision(1) << successRate << "%)";
    }
    std::cout << "\n";
    std::cout << "  Failures:     " << g_reloadFailures << "\n\n";

    std::cout << "Memory Analysis:\n";
    std::cout << "  Baseline:     " << std::fixed << std::setprecision(1) << baselineMB << " MB\n";
    std::cout << "  Final:        " << finalMB << " MB\n";
    std::cout << "  Growth:       " << growthMB << " MB";

    if (growthMB < 10.0f) {
        std::cout << " ✅";
    } else {
        std::cout << " ❌";
    }
    std::cout << "\n";

    float memoryPerReloadKB = 0.0f;
    if (g_reloadCount > 0) {
        memoryPerReloadKB = (growthMB * 1024.0f) / g_reloadCount;
    }
    std::cout << "  Per reload:   " << std::fixed << std::setprecision(1) << memoryPerReloadKB << " KB";
    if (memoryPerReloadKB < 50.0f) {
        std::cout << " ✅";
    } else {
        std::cout << " ❌";
    }
    std::cout << "\n\n";

    std::cout << "Resource Cleanup:\n";
    std::cout << "  Temp files:   " << finalTempFiles;
    if (finalTempFiles <= 2) {
        std::cout << " ✅";
    } else {
        std::cout << " ❌";
    }
    std::cout << "\n";

    std::cout << "  Mapped .so:   " << finalMappedLibs;
    if (finalMappedLibs <= baselineMappedLibs + 2) {
        std::cout << " (stable) ✅";
    } else {
        std::cout << " (leak: +" << (finalMappedLibs - baselineMappedLibs) << ") ❌";
    }
    std::cout << "\n\n";

    std::cout << "Stability:\n";
    std::cout << "  Crashes:      " << g_crashes;
    if (g_crashes == 0) {
        std::cout << " ✅";
    } else {
        std::cout << " ❌";
    }
    std::cout << "\n\n";

    // ========================================================================
    // Validation
    // ========================================================================

    std::cout << "Validating results...\n";

    bool passed = true;

    // 1. Memory growth < 10 MB
    if (growthMB > 10.0f) {
        std::cout << "  ❌ Memory growth too high: " << growthMB << " MB (need < 10 MB)\n";
        passed = false;
    } else {
        std::cout << "  ✓ Memory growth: " << growthMB << " MB (< 10 MB)\n";
    }

    // 2. Memory per reload < 50 KB
    if (memoryPerReloadKB > 50.0f) {
        std::cout << "  ❌ Memory per reload too high: " << memoryPerReloadKB << " KB (need < 50 KB)\n";
        passed = false;
    } else {
        std::cout << "  ✓ Memory per reload: " << memoryPerReloadKB << " KB (< 50 KB)\n";
    }

    // 3. Temp files cleaned
    if (finalTempFiles > 2) {
        std::cout << "  ❌ Temp files not cleaned up: " << finalTempFiles << " (need ≤ 2)\n";
        passed = false;
    } else {
        std::cout << "  ✓ Temp files cleaned: " << finalTempFiles << " (≤ 2)\n";
    }

    // 4. No .so handle leaks
    if (finalMappedLibs > baselineMappedLibs + 2) {
        std::cout << "  ❌ Library handle leak: +" << (finalMappedLibs - baselineMappedLibs) << "\n";
        passed = false;
    } else {
        std::cout << "  ✓ No .so handle leaks\n";
    }

    // 5. Reload success rate
    float successRate = g_reloadCount > 0 ? (g_reloadSuccesses * 100.0f) / g_reloadCount : 0.0f;
    if (successRate < 100.0f) {
        std::cout << "  ❌ Reload success rate: " << std::fixed << std::setprecision(1)
                  << successRate << "% (need 100%)\n";
        passed = false;
    } else {
        std::cout << "  ✓ Reload success rate: 100%\n";
    }

    // 6. No crashes
    if (g_crashes > 0) {
        std::cout << "  ❌ Crashes detected: " << g_crashes << "\n";
        passed = false;
    } else {
        std::cout << "  ✓ No crashes\n";
    }

    std::cout << "\n================================================================================\n";
    if (passed) {
        std::cout << "Result: ✅ PASSED\n";
    } else {
        std::cout << "Result: ❌ FAILED\n";
    }
    std::cout << "================================================================================\n";

    return passed ? 0 : 1;
}
