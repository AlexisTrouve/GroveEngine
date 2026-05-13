/**
 * Scenario 10: Multi-Version Module Coexistence Test
 *
 * Tests ability to load multiple versions of the same module simultaneously
 * with canary deployment, progressive migration, and instant rollback.
 *
 * Phases:
 * - Phase 0: Setup baseline (v1 with 100 entities)
 * - Phase 1: Canary deployment (10% v2, 90% v1)
 * - Phase 2: Progressive migration (v1 → v2: 30%, 50%, 80%, 100%)
 * - Phase 3: Auto garbage collection (unload v1)
 * - Phase 4: Emergency rollback (v2 → v1)
 * - Phase 5: Three-way coexistence (20% v1, 30% v2, 50% v3)
 */

#include "grove/IModule.h"
#include "grove/JsonDataNode.h"
#include "../helpers/TestMetrics.h"
#include "../helpers/TestAssertions.h"
#include "../helpers/TestReporter.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <iostream>
#include <map>
#include <vector>
#include <chrono>
#include <thread>
#include <memory>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

// Cross-platform dlopen wrappers
#ifdef _WIN32
inline void* grove_dlopen(const char* path, int flags) {
    (void)flags;
    return LoadLibraryA(path);
}
inline void* grove_dlsym(void* handle, const char* symbol) {
    return (void*)GetProcAddress((HMODULE)handle, symbol);
}
inline int grove_dlclose(void* handle) {
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
}
inline const char* grove_dlerror() {
    static thread_local char buf[256];
    DWORD err = GetLastError();
    snprintf(buf, sizeof(buf), "Windows error code: %lu", err);
    return buf;
}
#define RTLD_NOW 0
#define RTLD_LOCAL 0
#else
#define grove_dlopen dlopen
#define grove_dlsym dlsym
#define grove_dlclose dlclose
#define grove_dlerror dlerror
#endif

using namespace grove;
using json = nlohmann::json;

// Version-specific module handle
struct VersionHandle {
    void* dlHandle = nullptr;
    IModule* instance = nullptr;
    std::string modulePath;
    int version = 0;
    float trafficPercent = 0.0f; // % of traffic routed to this version
    std::chrono::steady_clock::time_point lastUsed;
    size_t processedEntities = 0;

    bool isLoaded() const { return dlHandle != nullptr && instance != nullptr; }
};

// Multi-version test engine
class MultiVersionTestEngine {
public:
    MultiVersionTestEngine() {
        logger_ = spdlog::default_logger();
        logger_->set_level(spdlog::level::info);
    }

    ~MultiVersionTestEngine() {
        // Copy keys to avoid iterator invalidation during unload
        std::vector<std::string> keys;
        for (const auto& [key, handle] : versions_) {
            keys.push_back(key);
        }
        for (const auto& key : keys) {
            unloadVersion(key);
        }
    }

    // Load a specific version of a module
    bool loadModuleVersion(const std::string& moduleName, int version, const std::string& path) {
        std::string key = moduleName + ":v" + std::to_string(version);

        if (versions_.count(key) > 0) {
            logger_->warn("Version {} already loaded", key);
            return false;
        }

        void* dlHandle = grove_dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!dlHandle) {
            logger_->error("Failed to load {}: {}", key, grove_dlerror());
            return false;
        }

        auto createFunc = (IModule* (*)())grove_dlsym(dlHandle, "createModule");
        if (!createFunc) {
            logger_->error("Failed to find createModule in {}: {}", key, grove_dlerror());
            grove_dlclose(dlHandle);
            return false;
        }

        IModule* instance = createFunc();
        if (!instance) {
            logger_->error("createModule returned nullptr for {}", key);
            grove_dlclose(dlHandle);
            return false;
        }

        VersionHandle handle;
        handle.dlHandle = dlHandle;
        handle.instance = instance;
        handle.modulePath = path;
        handle.version = instance->getVersion();
        handle.trafficPercent = 0.0f;
        handle.lastUsed = std::chrono::steady_clock::now();

        versions_[key] = handle;

        // Initialize module
        json configJson;
        configJson["entityCount"] = 100;
        auto config = std::make_unique<JsonDataNode>("config", configJson);
        instance->setConfiguration(*config, nullptr, nullptr);

        logger_->info("✓ Loaded {} (actual version: {})", key, handle.version);

        return true;
    }

    // Unload a specific version
    bool unloadVersion(const std::string& key) {
        auto it = versions_.find(key);
        if (it == versions_.end()) return false;

        auto& handle = it->second;
        if (handle.instance) {
            handle.instance->shutdown();
            auto destroyFunc = (void (*)(IModule*))grove_dlsym(handle.dlHandle, "destroyModule");
            if (destroyFunc) {
                destroyFunc(handle.instance);
            }
        }

        if (handle.dlHandle) {
            grove_dlclose(handle.dlHandle);
        }

        versions_.erase(it);
        logger_->info("✓ Unloaded {}", key);
        return true;
    }

    // Set traffic split across versions
    bool setTrafficSplit(const std::string& moduleName, const std::map<int, float>& weights) {
        // Validate weights sum to ~1.0
        float sum = 0.0f;
        for (const auto& [version, weight] : weights) {
            sum += weight;
        }

        if (std::abs(sum - 1.0f) > 0.01f) {
            logger_->error("Traffic weights must sum to 1.0 (got {})", sum);
            return false;
        }

        // Apply weights
        for (const auto& [version, weight] : weights) {
            std::string key = moduleName + ":v" + std::to_string(version);
            if (versions_.count(key) > 0) {
                versions_[key].trafficPercent = weight * 100.0f;
            }
        }

        logger_->info("✓ Traffic split configured:");
        for (const auto& [version, weight] : weights) {
            logger_->info("  v{}: {}%", version, weight * 100.0f);
        }

        return true;
    }

    // Get current traffic split
    std::map<int, float> getTrafficSplit(const std::string& moduleName) const {
        std::map<int, float> split;
        for (const auto& [key, handle] : versions_) {
            if (key.find(moduleName + ":v") == 0) {
                split[handle.version] = handle.trafficPercent;
            }
        }
        return split;
    }

    // Migrate state from one version to another
    bool migrateState(const std::string& moduleName, int fromVersion, int toVersion) {
        std::string fromKey = moduleName + ":v" + std::to_string(fromVersion);
        std::string toKey = moduleName + ":v" + std::to_string(toVersion);

        if (versions_.count(fromKey) == 0 || versions_.count(toKey) == 0) {
            logger_->error("Cannot migrate: version not loaded");
            return false;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        // Extract state from source version
        auto oldState = versions_[fromKey].instance->getState();

        // Migrate to target version
        bool success = versions_[toKey].instance->migrateStateFrom(fromVersion, *oldState);

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        if (success) {
            logger_->info("✓ State migrated v{} → v{} in {}ms", fromVersion, toVersion, duration.count());
        } else {
            logger_->error("✗ State migration v{} → v{} failed", fromVersion, toVersion);
        }

        return success;
    }

    // Check if version is loaded
    bool isVersionLoaded(const std::string& moduleName, int version) const {
        std::string key = moduleName + ":v" + std::to_string(version);
        return versions_.count(key) > 0;
    }

    // Get entity count for a version
    size_t getEntityCount(const std::string& moduleName, int version) const {
        std::string key = moduleName + ":v" + std::to_string(version);
        auto it = versions_.find(key);
        if (it == versions_.end()) return 0;

        auto state = it->second.instance->getState();
        const auto* jsonState = dynamic_cast<const JsonDataNode*>(state.get());
        if (jsonState) {
            const auto& jsonData = jsonState->getJsonData();
            if (jsonData.contains("entityCount")) {
                return jsonData["entityCount"];
            }
        }
        return 0;
    }

    // Process all versions (simulates traffic routing)
    void processAllVersions(float deltaTime) {
        json inputJson;
        inputJson["deltaTime"] = deltaTime;
        auto input = std::make_unique<JsonDataNode>("input", inputJson);

        for (auto& [key, handle] : versions_) {
            if (handle.isLoaded() && handle.trafficPercent > 0.0f) {
                handle.instance->process(*input);
                handle.lastUsed = std::chrono::steady_clock::now();
            }
        }
    }

    // Auto garbage collection of unused versions
    void autoGC(float unusedThresholdSeconds = 10.0f) {
        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> toUnload;

        for (const auto& [key, handle] : versions_) {
            if (handle.trafficPercent == 0.0f) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - handle.lastUsed);
                if (elapsed.count() >= unusedThresholdSeconds) {
                    toUnload.push_back(key);
                }
            }
        }

        for (const auto& key : toUnload) {
            logger_->info("Auto GC: unloading unused version {}", key);
            unloadVersion(key);
        }
    }

    // Get all loaded versions for a module
    std::vector<int> getLoadedVersions(const std::string& moduleName) const {
        std::vector<int> versions;
        for (const auto& [key, handle] : versions_) {
            if (key.find(moduleName + ":v") == 0) {
                versions.push_back(handle.version);
            }
        }
        return versions;
    }

private:
    std::map<std::string, VersionHandle> versions_;
    std::shared_ptr<spdlog::logger> logger_;
};

// Global reporter pointer for atexit handler — needed because ASSERT_* macros
// call std::exit(1) which bypasses normal control flow (try/catch/return).
// atexit() IS called by std::exit(), so this guarantees the report is always printed.
static TestReporter* g_reporterForAtexit = nullptr;

static void atexitReportHandler() {
    // Print final report if it hasn't been printed yet
    // (i.e., when std::exit was triggered by a failing ASSERT_*)
    if (g_reporterForAtexit) {
        g_reporterForAtexit->printFinalReport();
    }
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "TEST: Multi-Version Module Coexistence\n";
    std::cout << "================================================================================\n\n";

    // static: ensures reporter is alive during atexit handlers
    // (atexit fires after main() returns, when locals are already destroyed)
    static TestReporter reporter("Multi-Version Module Coexistence");

    // Register atexit handler so printFinalReport() is called even if an
    // ASSERT_* macro triggers std::exit(1) — which bypasses catch blocks.
    g_reporterForAtexit = &reporter;
    std::atexit(atexitReportHandler);

    // Local metrics storage
    std::map<std::string, double> metrics;

    MultiVersionTestEngine engine;

    try {
        std::cout << "=== Phase 0: Setup Baseline (v1 with 100 entities) ===\n";

        // Load v1
        std::string v1Path = "./libGameLogicModuleV1.dll";
        ASSERT_TRUE(engine.loadModuleVersion("GameLogic", 1, v1Path),
                    "Load GameLogic v1");

        // Configure 100% traffic to v1
        engine.setTrafficSplit("GameLogic", {{1, 1.0f}});

        // Verify
        ASSERT_EQ(engine.getEntityCount("GameLogic", 1), 100, "v1 has 100 entities");

        std::cout << "✓ Baseline established: v1 with 100 entities\n";

        std::this_thread::sleep_for(std::chrono::seconds(2));

        // ========================================
        std::cout << "\n=== Phase 1: Canary Deployment (10% v2, 90% v1) ===\n";

        auto phase1Start = std::chrono::high_resolution_clock::now();

        // Load v2
        std::string v2Path = "./libGameLogicModuleV2.dll";
        ASSERT_TRUE(engine.loadModuleVersion("GameLogic", 2, v2Path),
                    "Load GameLogic v2");

        auto phase1End = std::chrono::high_resolution_clock::now();
        auto loadTime = std::chrono::duration_cast<std::chrono::milliseconds>(phase1End - phase1Start);
        metrics["version_load_time_ms"] = loadTime.count();

        std::cout << "Version load time: " << loadTime.count() << "ms\n";
        ASSERT_LT(loadTime.count(), 200, "Load time < 200ms");

        // Configure canary: 10% v2, 90% v1
        engine.setTrafficSplit("GameLogic", {{1, 0.9f}, {2, 0.1f}});

        // Verify both versions loaded
        ASSERT_TRUE(engine.isVersionLoaded("GameLogic", 1), "v1 still loaded");
        ASSERT_TRUE(engine.isVersionLoaded("GameLogic", 2), "v2 loaded");

        auto split = engine.getTrafficSplit("GameLogic");
        ASSERT_NEAR(split[1], 90.0f, 2.0f, "v1 traffic ~90%");
        ASSERT_NEAR(split[2], 10.0f, 2.0f, "v2 traffic ~10%");

        std::cout << "✓ Canary deployment active: 10% v2, 90% v1\n";

        std::this_thread::sleep_for(std::chrono::seconds(5));

        // ========================================
        std::cout << "\n=== Phase 2: Progressive Migration v1 → v2 ===\n";

        // Step 1: 30% v2, 70% v1
        std::cout << "t=0s: Traffic split → 30% v2, 70% v1\n";
        engine.setTrafficSplit("GameLogic", {{1, 0.7f}, {2, 0.3f}});
        std::this_thread::sleep_for(std::chrono::seconds(3));

        split = engine.getTrafficSplit("GameLogic");
        ASSERT_NEAR(split[2], 30.0f, 2.0f, "v2 traffic ~30%");

        // Step 2: 50% v2, 50% v1
        std::cout << "t=3s: Traffic split → 50% v2, 50% v1\n";
        engine.setTrafficSplit("GameLogic", {{1, 0.5f}, {2, 0.5f}});
        std::this_thread::sleep_for(std::chrono::seconds(3));

        split = engine.getTrafficSplit("GameLogic");
        ASSERT_NEAR(split[2], 50.0f, 2.0f, "v2 traffic ~50%");

        // Step 3: 80% v2, 20% v1
        std::cout << "t=6s: Traffic split → 80% v2, 20% v1\n";
        engine.setTrafficSplit("GameLogic", {{1, 0.2f}, {2, 0.8f}});
        std::this_thread::sleep_for(std::chrono::seconds(3));

        split = engine.getTrafficSplit("GameLogic");
        ASSERT_NEAR(split[2], 80.0f, 2.0f, "v2 traffic ~80%");

        // Step 4: 100% v2, 0% v1 (migration complete)
        std::cout << "t=9s: Traffic split → 100% v2, 0% v1 (migration complete)\n";

        auto migrationStart = std::chrono::high_resolution_clock::now();
        bool migrated = engine.migrateState("GameLogic", 1, 2);
        auto migrationEnd = std::chrono::high_resolution_clock::now();
        auto migrationTime = std::chrono::duration_cast<std::chrono::milliseconds>(migrationEnd - migrationStart);

        metrics["state_migration_time_ms"] = migrationTime.count();
        ASSERT_TRUE(migrated, "State migration successful");
        ASSERT_LT(migrationTime.count(), 500, "Migration time < 500ms");

        engine.setTrafficSplit("GameLogic", {{1, 0.0f}, {2, 1.0f}});

        split = engine.getTrafficSplit("GameLogic");
        ASSERT_NEAR(split[2], 100.0f, 2.0f, "v2 traffic ~100%");

        std::cout << "✓ Progressive migration complete\n";

        std::this_thread::sleep_for(std::chrono::seconds(3));

        // ========================================
        std::cout << "\n=== Phase 3: Garbage Collection (unload v1) ===\n";

        std::cout << "v1 unused for 10s → triggering auto GC...\n";

        // Wait for GC threshold
        std::this_thread::sleep_for(std::chrono::seconds(12));

        auto gcStart = std::chrono::high_resolution_clock::now();
        engine.autoGC(10.0f);
        auto gcEnd = std::chrono::high_resolution_clock::now();
        auto gcTime = std::chrono::duration_cast<std::chrono::milliseconds>(gcEnd - gcStart);

        metrics["gc_time_ms"] = gcTime.count();
        ASSERT_LT(gcTime.count(), 100, "GC time < 100ms");

        // Verify v1 unloaded
        ASSERT_FALSE(engine.isVersionLoaded("GameLogic", 1), "v1 unloaded");
        ASSERT_TRUE(engine.isVersionLoaded("GameLogic", 2), "v2 still loaded");

        std::cout << "✓ Auto GC complete: v1 unloaded, v2 continues\n";

        std::this_thread::sleep_for(std::chrono::seconds(2));

        // ========================================
        std::cout << "\n=== Phase 4: Emergency Rollback (v2 → v1) ===\n";

        std::cout << "Simulating critical bug in v2 → triggering emergency rollback\n";

        auto rollbackStart = std::chrono::high_resolution_clock::now();

        // Reload v1
        ASSERT_TRUE(engine.loadModuleVersion("GameLogic", 1, v1Path),
                    "Reload v1 for rollback");

        // Migrate state back v2 → v1 (if possible, otherwise fresh start)
        // Note: v1 cannot migrate from v2 (fields missing), so this will fail gracefully
        engine.migrateState("GameLogic", 2, 1);

        // Redirect traffic to v1
        engine.setTrafficSplit("GameLogic", {{1, 1.0f}, {2, 0.0f}});

        auto rollbackEnd = std::chrono::high_resolution_clock::now();
        auto rollbackTime = std::chrono::duration_cast<std::chrono::milliseconds>(rollbackEnd - rollbackStart);

        metrics["rollback_time_ms"] = rollbackTime.count();
        ASSERT_LT(rollbackTime.count(), 300, "Rollback time < 300ms");

        // Verify rollback
        ASSERT_TRUE(engine.isVersionLoaded("GameLogic", 1), "v1 reloaded");
        split = engine.getTrafficSplit("GameLogic");
        ASSERT_NEAR(split[1], 100.0f, 2.0f, "v1 traffic ~100%");

        std::cout << "✓ Emergency rollback complete in " << rollbackTime.count() << "ms\n";

        std::this_thread::sleep_for(std::chrono::seconds(3));

        // ========================================
        std::cout << "\n=== Phase 5: Three-Way Coexistence (v1, v2, v3) ===\n";

        // Load v3
        std::string v3Path = "./libGameLogicModuleV3.dll";
        ASSERT_TRUE(engine.loadModuleVersion("GameLogic", 3, v3Path),
                    "Load GameLogic v3");

        // Configure 3-way split: 20% v1, 30% v2, 50% v3
        engine.setTrafficSplit("GameLogic", {{1, 0.2f}, {2, 0.3f}, {3, 0.5f}});

        // Verify 3 versions coexisting
        ASSERT_TRUE(engine.isVersionLoaded("GameLogic", 1), "v1 loaded");
        ASSERT_TRUE(engine.isVersionLoaded("GameLogic", 2), "v2 loaded");
        ASSERT_TRUE(engine.isVersionLoaded("GameLogic", 3), "v3 loaded");

        auto versions = engine.getLoadedVersions("GameLogic");
        ASSERT_EQ(versions.size(), 3, "3 versions loaded");

        split = engine.getTrafficSplit("GameLogic");
        ASSERT_NEAR(split[1], 20.0f, 2.0f, "v1 traffic ~20%");
        ASSERT_NEAR(split[2], 30.0f, 2.0f, "v2 traffic ~30%");
        ASSERT_NEAR(split[3], 50.0f, 2.0f, "v3 traffic ~50%");

        metrics["multi_version_count"] = versions.size();

        std::cout << "✓ Three-way coexistence active:\n";
        std::cout << "  v1: 20% traffic\n";
        std::cout << "  v2: 30% traffic\n";
        std::cout << "  v3: 50% traffic\n";

        std::this_thread::sleep_for(std::chrono::seconds(5));

        // ========================================
        std::cout << "\n=== Metrics Summary ===\n";
        reporter.addMetric("version_load_time_ms", metrics["version_load_time_ms"]);
        reporter.addMetric("state_migration_time_ms", metrics["state_migration_time_ms"]);
        reporter.addMetric("rollback_time_ms", metrics["rollback_time_ms"]);
        reporter.addMetric("gc_time_ms", metrics["gc_time_ms"]);
        reporter.addMetric("multi_version_count", metrics["multi_version_count"]);

        // Register assertions in reporter so printFinalReport() emits a verdict.
        // These mirror the ASSERT_* checks below — both must stay in sync.
        reporter.addAssertion("Version load time < 200ms",
                              metrics["version_load_time_ms"] < 200.0);
        reporter.addAssertion("State migration < 500ms",
                              metrics["state_migration_time_ms"] < 500.0);
        reporter.addAssertion("Rollback time < 300ms",
                              metrics["rollback_time_ms"] < 300.0);
        reporter.addAssertion("GC time < 100ms",
                              metrics["gc_time_ms"] < 100.0);
        reporter.addAssertion("3 versions coexisting",
                              metrics["multi_version_count"] == 3.0);

        // Hard assertions (exit on failure via ASSERT_* → std::exit)
        ASSERT_LT(metrics["version_load_time_ms"], 200.0,
                  "Version load time < 200ms");
        ASSERT_LT(metrics["state_migration_time_ms"], 500.0,
                  "State migration < 500ms");
        ASSERT_LT(metrics["rollback_time_ms"], 300.0,
                  "Rollback time < 300ms");
        ASSERT_LT(metrics["gc_time_ms"], 100.0,
                  "GC time < 100ms");
        ASSERT_EQ(metrics["multi_version_count"], 3.0,
                  "3 versions coexisting");

        // ========================================
        std::cout << "\n=== Final Report ===\n";
        // Nullify atexit pointer BEFORE printing — prevents double-print
        // (atexit is only needed for the ASSERT_* early-exit path)
        g_reporterForAtexit = nullptr;
        reporter.printFinalReport();
        std::cout << std::flush;

        return reporter.getExitCode();

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
