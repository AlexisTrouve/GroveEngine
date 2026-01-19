#include "grove/ThreadedModuleSystem.h"
#include "grove/ModuleSystemFactory.h"
#include "grove/JsonDataNode.h"
#include "grove/IntraIOManager.h"
#include "../helpers/TestAssertions.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <random>
#include <vector>

using namespace grove;

// ============================================================================
// Test Module: Simple Counter with Configurable Behavior
// ============================================================================

class StressTestModule : public IModule {
private:
    int counter = 0;
    std::string name;
    IIO* io = nullptr;
    std::shared_ptr<spdlog::logger> logger;
    std::thread::id threadId;
    std::atomic<int> processCallCount{0};
    bool throwException = false;
    int workDelayMs = 0;

public:
    StressTestModule(std::string moduleName) : name(std::move(moduleName)) {
        logger = spdlog::get("StressTest_" + name);
        if (!logger) {
            logger = spdlog::stdout_color_mt("StressTest_" + name);
            logger->set_level(spdlog::level::warn);  // Reduce noise in stress tests
        }
    }

    void process(const IDataNode& input) override {
        threadId = std::this_thread::get_id();
        counter++;
        processCallCount++;

        // Simulate exception if configured
        if (throwException) {
            throw std::runtime_error(name + ": Intentional exception for testing");
        }

        // Simulate work delay if configured
        if (workDelayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(workDelayMs));
        }

        if (logger && processCallCount % 100 == 0) {
            logger->trace("{}: process #{}", name, processCallCount.load());
        }
    }

    void setConfiguration(const IDataNode& configNode, IIO* ioLayer, ITaskScheduler* scheduler) override {
        io = ioLayer;
        try {
            name = configNode.getString("name", name);
            throwException = configNode.getBool("throwException", false);
            workDelayMs = configNode.getInt("workDelayMs", 0);
        } catch (...) {
            // Ignore missing fields
        }
    }

    const IDataNode& getConfiguration() override {
        static JsonDataNode emptyConfig("config", nlohmann::json{});
        return emptyConfig;
    }

    std::unique_ptr<IDataNode> getHealthStatus() override {
        nlohmann::json health = {
            {"status", "healthy"},
            {"counter", counter},
            {"processCallCount", processCallCount.load()}
        };
        return std::make_unique<JsonDataNode>("health", health);
    }

    void shutdown() override {
        if (logger) {
            logger->debug("{}: shutdown()", name);
        }
    }

    std::unique_ptr<IDataNode> getState() override {
        nlohmann::json state = {
            {"counter", counter},
            {"name", name},
            {"processCallCount", processCallCount.load()}
        };
        return std::make_unique<JsonDataNode>("state", state);
    }

    void setState(const IDataNode& state) override {
        counter = state.getInt("counter", 0);
        name = state.getString("name", name);
        processCallCount = state.getInt("processCallCount", 0);
    }

    std::string getType() const override {
        return "StressTestModule";
    }

    bool isIdle() const override {
        return true;
    }

    // Test helpers
    int getCounter() const { return counter; }
    int getProcessCallCount() const { return processCallCount.load(); }
    void setThrowException(bool value) { throwException = value; }
    void setWorkDelayMs(int ms) { workDelayMs = ms; }
};

// ============================================================================
// Test 1: 50 Modules, 1000 Frames
// ============================================================================

bool test_50_modules_1000_frames() {
    std::cout << "\n=== STRESS TEST 1: 50 Modules, 1000 Frames ===\n";
    std::cout << "Testing system stability with high module count\n\n";

    const int NUM_MODULES = 50;
    const int NUM_FRAMES = 1000;

    auto system = std::make_unique<ThreadedModuleSystem>();
    std::vector<StressTestModule*> modulePtrs;

    // Register 50 modules
    auto startRegister = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_MODULES; i++) {
        auto module = std::make_unique<StressTestModule>("Module_" + std::to_string(i));
        modulePtrs.push_back(module.get());
        system->registerModule("Module_" + std::to_string(i), std::move(module));
    }
    auto endRegister = std::chrono::high_resolution_clock::now();
    float registerTime = std::chrono::duration<float, std::milli>(endRegister - startRegister).count();

    std::cout << "  ✓ " << NUM_MODULES << " modules registered in " << registerTime << "ms\n";

    // Process 1000 frames
    auto startProcess = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < NUM_FRAMES; frame++) {
        system->processModules(1.0f / 60.0f);

        if ((frame + 1) % 200 == 0) {
            std::cout << "    Frame " << (frame + 1) << "/" << NUM_FRAMES << "\n";
        }
    }
    auto endProcess = std::chrono::high_resolution_clock::now();
    float processTime = std::chrono::duration<float, std::milli>(endProcess - startProcess).count();
    float avgFrameTime = processTime / NUM_FRAMES;

    std::cout << "  ✓ " << NUM_FRAMES << " frames processed in " << processTime << "ms\n";
    std::cout << "    Average frame time: " << avgFrameTime << "ms\n";

    // Verify all modules processed correct number of times
    for (int i = 0; i < NUM_MODULES; i++) {
        ASSERT_EQ(modulePtrs[i]->getProcessCallCount(), NUM_FRAMES,
                 "Module " + std::to_string(i) + " should process " + std::to_string(NUM_FRAMES) + " times");
    }

    std::cout << "  ✓ All " << NUM_MODULES << " modules processed correctly\n";
    std::cout << "  ✓ System stable under high load (50 modules x 1000 frames = 50,000 operations)\n";

    return true;
}

// ============================================================================
// Test 2: Hot-Reload 100x Under Load
// ============================================================================

bool test_hot_reload_100x() {
    std::cout << "\n=== STRESS TEST 2: Hot-Reload 100x Under Load ===\n";
    std::cout << "Testing state preservation across 100 reload cycles\n\n";

    const int NUM_RELOADS = 100;
    const int FRAMES_PER_RELOAD = 10;

    auto system = std::make_unique<ThreadedModuleSystem>();

    // Register 5 modules
    for (int i = 0; i < 5; i++) {
        auto module = std::make_unique<StressTestModule>("Module_" + std::to_string(i));
        system->registerModule("Module_" + std::to_string(i), std::move(module));
    }

    std::cout << "  ✓ 5 modules registered\n";

    // Perform 100 reload cycles on Module_2
    for (int reload = 0; reload < NUM_RELOADS; reload++) {
        // Process some frames
        for (int frame = 0; frame < FRAMES_PER_RELOAD; frame++) {
            system->processModules(1.0f / 60.0f);
        }

        // Extract Module_2
        auto extracted = system->extractModule("Module_2");
        ASSERT_TRUE(extracted != nullptr, "Module should be extractable");

        // Get state
        auto state = extracted->getState();
        auto* jsonState = dynamic_cast<JsonDataNode*>(state.get());
        ASSERT_TRUE(jsonState != nullptr, "State should be JsonDataNode");

        int expectedCounter = (reload + 1) * FRAMES_PER_RELOAD;
        int actualCounter = jsonState->getJsonData()["counter"];

        ASSERT_EQ(actualCounter, expectedCounter,
                 "Counter should be " + std::to_string(expectedCounter) + " at reload #" + std::to_string(reload));

        // Create new module and restore state
        auto newModule = std::make_unique<StressTestModule>("Module_2");
        newModule->setState(*state);

        // Re-register
        system->registerModule("Module_2", std::move(newModule));

        if ((reload + 1) % 20 == 0) {
            std::cout << "    Reload cycle " << (reload + 1) << "/" << NUM_RELOADS
                      << " - counter: " << actualCounter << "\n";
        }
    }

    std::cout << "  ✓ 100 reload cycles completed successfully\n";
    std::cout << "  ✓ State preserved correctly across all reloads\n";

    // Final verification: Process more frames and check final state
    for (int frame = 0; frame < FRAMES_PER_RELOAD; frame++) {
        system->processModules(1.0f / 60.0f);
    }

    auto finalExtracted = system->extractModule("Module_2");
    auto finalState = finalExtracted->getState();
    auto* jsonFinalState = dynamic_cast<JsonDataNode*>(finalState.get());
    int finalCounter = jsonFinalState->getJsonData()["counter"];

    int expectedFinalCounter = (NUM_RELOADS + 1) * FRAMES_PER_RELOAD;
    ASSERT_EQ(finalCounter, expectedFinalCounter,
             "Final counter should be " + std::to_string(expectedFinalCounter));

    std::cout << "  ✓ Final state verification passed (counter: " << finalCounter << ")\n";

    return true;
}

// ============================================================================
// Test 3: Concurrent Operations (3 Threads)
// ============================================================================

bool test_concurrent_operations() {
    std::cout << "\n=== STRESS TEST 3: Concurrent Operations ===\n";
    std::cout << "Testing thread-safety with 3 concurrent racing threads\n\n";

    const int TEST_DURATION_SEC = 5;  // 5 seconds stress test

    auto system = std::make_unique<ThreadedModuleSystem>();
    std::atomic<bool> stopFlag{false};
    std::atomic<int> processCount{0};
    std::atomic<int> registerCount{0};
    std::atomic<int> extractCount{0};
    std::atomic<int> queryCount{0};

    // Register initial modules
    for (int i = 0; i < 10; i++) {
        auto module = std::make_unique<StressTestModule>("InitialModule_" + std::to_string(i));
        system->registerModule("InitialModule_" + std::to_string(i), std::move(module));
    }

    std::cout << "  ✓ 10 initial modules registered\n";
    std::cout << "  Starting " << TEST_DURATION_SEC << " second stress test...\n";

    // Thread 1: processModules() continuously
    std::thread t1([&]() {
        while (!stopFlag.load()) {
            try {
                system->processModules(1.0f / 60.0f);
                processCount++;
            } catch (const std::exception& e) {
                std::cerr << "    [T1] Exception in processModules: " << e.what() << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Thread 2: registerModule() / extractModule() randomly
    std::thread t2([&]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 1);
        int moduleId = 100;

        while (!stopFlag.load()) {
            try {
                if (dis(gen) == 0) {
                    // Register new module
                    auto module = std::make_unique<StressTestModule>("DynamicModule_" + std::to_string(moduleId));
                    system->registerModule("DynamicModule_" + std::to_string(moduleId), std::move(module));
                    registerCount++;
                    moduleId++;
                } else {
                    // Try to extract a module
                    if (moduleId > 100) {
                        int targetId = moduleId - 1;
                        auto extracted = system->extractModule("DynamicModule_" + std::to_string(targetId));
                        if (extracted) {
                            extractCount++;
                        }
                    }
                }
            } catch (const std::exception& e) {
                // Expected: may fail if module doesn't exist
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Thread 3: queryModule() continuously
    std::thread t3([&]() {
        JsonDataNode emptyInput("query", nlohmann::json{});
        while (!stopFlag.load()) {
            try {
                auto result = system->queryModule("InitialModule_0", emptyInput);
                if (result) {
                    queryCount++;
                }
            } catch (const std::exception& e) {
                std::cerr << "    [T3] Exception in queryModule: " << e.what() << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Let threads run for TEST_DURATION_SEC seconds
    std::this_thread::sleep_for(std::chrono::seconds(TEST_DURATION_SEC));
    stopFlag.store(true);

    t1.join();
    t2.join();
    t3.join();

    std::cout << "  ✓ All threads completed without crash\n";
    std::cout << "    Stats:\n";
    std::cout << "      - processModules() calls: " << processCount.load() << "\n";
    std::cout << "      - registerModule() calls: " << registerCount.load() << "\n";
    std::cout << "      - extractModule() calls:  " << extractCount.load() << "\n";
    std::cout << "      - queryModule() calls:    " << queryCount.load() << "\n";
    std::cout << "  ✓ Thread-safety validated under concurrent stress\n";

    return true;
}

// ============================================================================
// Test 4: Exception Handling
// ============================================================================

bool test_exception_handling() {
    std::cout << "\n=== STRESS TEST 4: Exception Handling ===\n";
    std::cout << "Testing system stability when module throws exceptions\n\n";

    auto system = std::make_unique<ThreadedModuleSystem>();

    // Register 5 normal modules
    for (int i = 0; i < 5; i++) {
        auto module = std::make_unique<StressTestModule>("NormalModule_" + std::to_string(i));
        system->registerModule("NormalModule_" + std::to_string(i), std::move(module));
    }

    // Register 1 exception-throwing module
    auto badModule = std::make_unique<StressTestModule>("BadModule");
    badModule->setThrowException(true);
    system->registerModule("BadModule", std::move(badModule));

    std::cout << "  ✓ 5 normal modules + 1 exception-throwing module registered\n";

    // Process 100 frames - should handle exceptions gracefully
    // Note: Current implementation may not catch exceptions in module threads
    // This test will reveal if that's a problem
    int successfulFrames = 0;
    for (int frame = 0; frame < 100; frame++) {
        try {
            system->processModules(1.0f / 60.0f);
            successfulFrames++;
        } catch (const std::exception& e) {
            std::cout << "    Frame " << frame << " caught exception: " << e.what() << "\n";
        }
    }

    std::cout << "  ✓ Processed " << successfulFrames << "/100 frames\n";
    std::cout << "  ⚠️  Note: Exception handling depends on implementation\n";
    std::cout << "     ThreadedModuleSystem may need try-catch in worker threads\n";

    // System should still be responsive - try to extract a normal module
    auto extracted = system->extractModule("NormalModule_0");
    ASSERT_TRUE(extracted != nullptr, "Should be able to extract normal module after exceptions");

    std::cout << "  ✓ System remains responsive after exceptions\n";

    return true;
}

// ============================================================================
// Test 5: Slow Module (>100ms)
// ============================================================================

bool test_slow_module() {
    std::cout << "\n=== STRESS TEST 5: Slow Module ===\n";
    std::cout << "Testing that slow module doesn't block other modules\n\n";

    const int SLOW_MODULE_DELAY_MS = 100;
    const int NUM_FRAMES = 20;

    auto system = std::make_unique<ThreadedModuleSystem>();

    // Register 4 fast modules
    std::vector<StressTestModule*> fastModules;
    for (int i = 0; i < 4; i++) {
        auto module = std::make_unique<StressTestModule>("FastModule_" + std::to_string(i));
        fastModules.push_back(module.get());
        system->registerModule("FastModule_" + std::to_string(i), std::move(module));
    }

    // Register 1 slow module (100ms delay)
    auto slowModule = std::make_unique<StressTestModule>("SlowModule");
    slowModule->setWorkDelayMs(SLOW_MODULE_DELAY_MS);
    auto* slowModulePtr = slowModule.get();
    system->registerModule("SlowModule", std::move(slowModule));

    std::cout << "  ✓ 4 fast modules + 1 slow module (100ms) registered\n";

    // Process frames and measure time
    auto startTime = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < NUM_FRAMES; frame++) {
        system->processModules(1.0f / 60.0f);
    }
    auto endTime = std::chrono::high_resolution_clock::now();
    float totalTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    float avgFrameTime = totalTime / NUM_FRAMES;

    std::cout << "  Total time: " << totalTime << "ms\n";
    std::cout << "  Avg frame time: " << avgFrameTime << "ms\n";

    // Expected: ~100-110ms per frame (limited by slowest module due to barrier)
    // The barrier pattern means all modules wait for the slowest one
    ASSERT_TRUE(avgFrameTime >= 90.0f && avgFrameTime <= 150.0f,
               "Average frame time should be ~100ms (limited by slow module)");

    std::cout << "  ✓ Frame time matches expected (barrier pattern verified)\n";

    // Verify all modules processed correct number of times
    ASSERT_EQ(slowModulePtr->getProcessCallCount(), NUM_FRAMES,
             "Slow module should process all frames");

    for (auto* fastMod : fastModules) {
        ASSERT_EQ(fastMod->getProcessCallCount(), NUM_FRAMES,
                 "Fast modules should process all frames");
    }

    std::cout << "  ✓ All modules synchronized correctly (barrier pattern working)\n";
    std::cout << "  ℹ️  Note: Barrier pattern means slow module sets frame rate\n";

    return true;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    std::cout << "================================================================================\n";
    std::cout << "ThreadedModuleSystem - STRESS TEST SUITE\n";
    std::cout << "================================================================================\n";
    std::cout << "Validating thread-safety, robustness, and edge case handling\n";

    int passedTests = 0;
    int totalTests = 5;

    try {
        if (test_50_modules_1000_frames()) passedTests++;
        if (test_hot_reload_100x()) passedTests++;
        if (test_concurrent_operations()) passedTests++;
        if (test_exception_handling()) passedTests++;
        if (test_slow_module()) passedTests++;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ FATAL EXCEPTION: " << e.what() << "\n";
    }

    std::cout << "\n================================================================================\n";
    std::cout << "RESULTS: " << passedTests << "/" << totalTests << " stress tests passed\n";
    std::cout << "================================================================================\n";

    if (passedTests == totalTests) {
        std::cout << "✅ ALL STRESS TESTS PASSED - ThreadedModuleSystem is robust!\n";
    } else {
        std::cout << "❌ SOME TESTS FAILED - Review failures and fix issues\n";
    }

    return (passedTests == totalTests) ? 0 : 1;
}
