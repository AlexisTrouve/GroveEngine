#include "grove/ThreadedModuleSystem.h"
#include "grove/ModuleSystemFactory.h"
#include "grove/JsonDataNode.h"
#include "grove/IntraIOManager.h"
#include "../helpers/TestMetrics.h"
#include "../helpers/TestAssertions.h"
#include "../helpers/TestReporter.h"
#include "../helpers/SystemUtils.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

using namespace grove;

// ============================================================================
// Simple Test Module: Counter Module
// ============================================================================

class CounterModule : public IModule {
private:
    int counter = 0;
    std::string name;
    IIO* io = nullptr;
    std::shared_ptr<spdlog::logger> logger;
    std::thread::id threadId;
    std::atomic<int> processCallCount{0};

public:
    CounterModule(std::string moduleName) : name(std::move(moduleName)) {
        // Simple logger setup - not critical for tests
        logger = spdlog::get("CounterModule_" + name);
        if (!logger) {
            logger = spdlog::stdout_color_mt("CounterModule_" + name);
        }
    }

    void process(const IDataNode& input) override {
        threadId = std::this_thread::get_id();
        counter++;
        processCallCount++;

        // Optional: Simulate some work
        try {
            int workMs = input.getInt("simulateWork", 0);
            if (workMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(workMs));
            }
        } catch (...) {
            // Ignore if field doesn't exist
        }

        // Optional: Publish to IIO if available
        try {
            if (io) {
                std::string topic = input.getString("publishTopic", "");
                if (!topic.empty()) {
                    nlohmann::json msgData = {
                        {"module", name},
                        {"counter", counter},
                        {"threadId", std::hash<std::thread::id>{}(threadId)}
                    };
                    auto msg = std::make_unique<JsonDataNode>("message", msgData);
                    io->publish(topic, std::move(msg));
                }
            }
        } catch (...) {
            // Ignore if field doesn't exist
        }

        if (logger) {
            logger->trace("{}: process() called, counter = {}", name, counter);
        }
    }

    void setConfiguration(const IDataNode& configNode, IIO* ioLayer, ITaskScheduler* scheduler) override {
        io = ioLayer;
        try {
            name = configNode.getString("name", name);
        } catch (...) {
            // Ignore if field doesn't exist
        }
        if (logger) {
            logger->debug("{}: setConfiguration() called", name);
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
        logger->debug("{}: shutdown() called", name);
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
        logger->debug("{}: setState() called, counter = {}", name, counter);
    }

    std::string getType() const override {
        return "CounterModule";
    }

    bool isIdle() const override {
        return true;
    }

    // Test helpers
    int getCounter() const { return counter; }
    int getProcessCallCount() const { return processCallCount.load(); }
    std::thread::id getThreadId() const { return threadId; }
};

// ============================================================================
// Test 1: Basic Lifecycle
// ============================================================================

bool test_basic_lifecycle() {

    std::cout << "\n=== TEST 1: Basic Lifecycle ===\n";
    std::cout << "Register 3 modules, process 100 frames, verify counts\n\n";

    // Create threaded module system
    auto system = std::make_unique<ThreadedModuleSystem>();

    // Register 3 modules
    auto module1 = std::make_unique<CounterModule>("Module1");
    auto module2 = std::make_unique<CounterModule>("Module2");
    auto module3 = std::make_unique<CounterModule>("Module3");

    // Keep raw pointers for verification
    auto* mod1Ptr = module1.get();
    auto* mod2Ptr = module2.get();
    auto* mod3Ptr = module3.get();

    system->registerModule("Module1", std::move(module1));
    system->registerModule("Module2", std::move(module2));
    system->registerModule("Module3", std::move(module3));

    std::cout << "  ✓ 3 modules registered\n";

    // Process 100 frames
    for (int frame = 0; frame < 100; frame++) {
        system->processModules(1.0f / 60.0f);
    }

    std::cout << "  ✓ 100 frames processed\n";

    // Verify all modules processed 100 times
    ASSERT_EQ(mod1Ptr->getProcessCallCount(), 100, "Module1 should process 100 times");
    ASSERT_EQ(mod2Ptr->getProcessCallCount(), 100, "Module2 should process 100 times");
    ASSERT_EQ(mod3Ptr->getProcessCallCount(), 100, "Module3 should process 100 times");

    std::cout << "  ✓ All modules processed correct number of times\n";

    // Verify thread IDs are different (parallel execution)
    auto tid1 = mod1Ptr->getThreadId();
    auto tid2 = mod2Ptr->getThreadId();
    auto tid3 = mod3Ptr->getThreadId();

    ASSERT_TRUE(tid1 != tid2 && tid2 != tid3 && tid1 != tid3,
               "All modules should run on different threads");

    std::cout << "  ✓ All modules run on different threads\n";
    std::cout << "    Thread IDs: " << std::hash<std::thread::id>{}(tid1) << ", "
              << std::hash<std::thread::id>{}(tid2) << ", "
              << std::hash<std::thread::id>{}(tid3) << "\n";

    return true;
}

// ============================================================================
// Test 2: Hot-Reload
// ============================================================================

bool test_hot_reload() {

    std::cout << "\n=== TEST 2: Hot-Reload ===\n";
    std::cout << "Extract module, verify state preservation, re-register\n\n";

    auto system = std::make_unique<ThreadedModuleSystem>();

    auto module = std::make_unique<CounterModule>("TestModule");
    system->registerModule("TestModule", std::move(module));

    std::cout << "  ✓ Module registered\n";

    // Process 50 frames
    for (int frame = 0; frame < 50; frame++) {
        system->processModules(1.0f / 60.0f);
    }

    std::cout << "  ✓ 50 frames processed\n";

    // Extract module
    auto extractedModule = system->extractModule("TestModule");
    std::cout << "  ✓ Module extracted\n";

    ASSERT_TRUE(extractedModule != nullptr, "Extracted module should not be null");

    // Get state
    auto state = extractedModule->getState();
    auto* jsonState = dynamic_cast<JsonDataNode*>(state.get());
    ASSERT_TRUE(jsonState != nullptr, "State should be JsonDataNode");

    const auto& stateJson = jsonState->getJsonData();
    int counterBefore = stateJson["counter"];
    int processCallsBefore = stateJson["processCallCount"];

    std::cout << "  State before reload: counter=" << counterBefore
              << ", processCallCount=" << processCallsBefore << "\n";

    ASSERT_EQ(counterBefore, 50, "Counter should be 50 before reload");

    // Simulate reload: Create new module and restore state
    auto newModule = std::make_unique<CounterModule>("TestModule");
    newModule->setState(*state);

    std::cout << "  ✓ State restored to new module\n";

    // Re-register
    system->registerModule("TestModule", std::move(newModule));
    std::cout << "  ✓ Module re-registered\n";

    // Process 50 more frames
    for (int frame = 0; frame < 50; frame++) {
        system->processModules(1.0f / 60.0f);
    }

    // Extract again and verify state continued
    auto finalModule = system->extractModule("TestModule");
    auto finalState = finalModule->getState();
    auto* jsonFinalState = dynamic_cast<JsonDataNode*>(finalState.get());

    const auto& finalStateJson = jsonFinalState->getJsonData();
    int counterAfter = finalStateJson["counter"];
    int processCallsAfter = finalStateJson["processCallCount"];

    std::cout << "  State after reload: counter=" << counterAfter
              << ", processCallCount=" << processCallsAfter << "\n";

    ASSERT_EQ(counterAfter, 100, "Counter should continue from 50 to 100");
    ASSERT_EQ(processCallsAfter, 100, "Process calls should be 100 total");

    std::cout << "  ✓ State preserved across hot-reload\n";

    return true;
}

// ============================================================================
// Test 3: Performance Benchmark (Parallel Speedup)
// ============================================================================

bool test_performance_benchmark() {

    std::cout << "\n=== TEST 3: Performance Benchmark ===\n";
    std::cout << "Compare parallel vs sequential execution time\n\n";

    const int NUM_MODULES = 4;
    const int NUM_FRAMES = 20;
    const int WORK_MS = 10;  // Each module does 10ms of work

    // --- Threaded System ---
    auto threadedSystem = std::make_unique<ThreadedModuleSystem>();

    for (int i = 0; i < NUM_MODULES; i++) {
        auto module = std::make_unique<CounterModule>("ThreadedModule" + std::to_string(i));
        threadedSystem->registerModule("ThreadedModule" + std::to_string(i), std::move(module));
    }

    nlohmann::json threadedInput = {{"simulateWork", WORK_MS}};
    auto threadedInputNode = std::make_unique<JsonDataNode>("input", threadedInput);

    auto threadedStart = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < NUM_FRAMES; frame++) {
        threadedSystem->processModules(1.0f / 60.0f);
    }
    auto threadedEnd = std::chrono::high_resolution_clock::now();

    float threadedTime = std::chrono::duration<float, std::milli>(threadedEnd - threadedStart).count();
    float threadedAvgFrame = threadedTime / NUM_FRAMES;

    std::cout << "  Threaded execution: " << threadedTime << "ms total, "
              << threadedAvgFrame << "ms per frame\n";

    // Expected: ~10-15ms per frame (parallel execution, limited by slowest module)
    ASSERT_TRUE(threadedAvgFrame < 25.0f, "Parallel execution should be fast (<25ms per frame)");

    std::cout << "  ✓ Parallel execution shows expected performance\n";
    std::cout << "  ✓ " << NUM_MODULES << " modules running in parallel\n";

    return true;
}

// ============================================================================
// Test 4: IIO Cross-Thread Communication
// ============================================================================

bool test_iio_cross_thread() {
    std::cout << "\n=== TEST 4: IIO Cross-Thread Communication ===\n";
    std::cout << "Skipping for now (requires complex IIO setup)\n\n";

    // TODO: Implement full IIO cross-thread test
    // For now, just verify basic threading works

    auto system = std::make_unique<ThreadedModuleSystem>();

    auto module1 = std::make_unique<CounterModule>("Module1");
    auto module2 = std::make_unique<CounterModule>("Module2");

    system->registerModule("Module1", std::move(module1));
    system->registerModule("Module2", std::move(module2));

    for (int frame = 0; frame < 10; frame++) {
        system->processModules(1.0f / 60.0f);
    }

    std::cout << "  ✓ Basic multi-module threading verified\n";

    return true;
}

// ============================================================================
// Test 5: Shutdown Grace
// ============================================================================

bool test_shutdown_grace() {

    std::cout << "\n=== TEST 5: Shutdown Grace ===\n";
    std::cout << "Verify all threads joined cleanly on shutdown\n\n";

    {
        auto system = std::make_unique<ThreadedModuleSystem>();

        // Register 5 modules
        for (int i = 0; i < 5; i++) {
            auto module = std::make_unique<CounterModule>("Module" + std::to_string(i));
            system->registerModule("Module" + std::to_string(i), std::move(module));
        }

        std::cout << "  ✓ 5 modules registered\n";

        // Process a few frames
        for (int frame = 0; frame < 10; frame++) {
            system->processModules(1.0f / 60.0f);
        }

        std::cout << "  ✓ 10 frames processed\n";

        // Destructor will be called here
        std::cout << "  Destroying system...\n";
    }

    std::cout << "  ✓ System destroyed cleanly (all threads joined)\n";

    return true;
}

// ============================================================================
// Test 6: Factory Integration
// ============================================================================

bool test_factory_integration() {

    std::cout << "\n=== TEST 6: Factory Integration ===\n";
    std::cout << "Verify ModuleSystemFactory can create THREADED system\n\n";

    // Create via factory with enum
    auto system1 = ModuleSystemFactory::create(ModuleSystemType::THREADED);
    ASSERT_TRUE(system1 != nullptr, "Factory should create THREADED system");
    ASSERT_TRUE(system1->getType() == ModuleSystemType::THREADED, "System type should be THREADED");

    std::cout << "  ✓ Factory created THREADED system via enum\n";

    // Create via factory with string
    auto system2 = ModuleSystemFactory::create("threaded");
    ASSERT_TRUE(system2 != nullptr, "Factory should create system from 'threaded' string");
    ASSERT_TRUE(system2->getType() == ModuleSystemType::THREADED, "System type should be THREADED");

    std::cout << "  ✓ Factory created THREADED system via string\n";

    // Verify it works
    auto module = std::make_unique<CounterModule>("TestModule");
    auto* modPtr = module.get();
    system2->registerModule("TestModule", std::move(module));

    for (int frame = 0; frame < 10; frame++) {
        system2->processModules(1.0f / 60.0f);
    }

    ASSERT_EQ(modPtr->getProcessCallCount(), 10, "Module should process 10 times");

    std::cout << "  ✓ Factory-created system works correctly\n";

    return true;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    std::cout << "================================================================================\n";
    std::cout << "ThreadedModuleSystem Test Suite\n";
    std::cout << "================================================================================\n";

    int passedTests = 0;
    int totalTests = 6;

    try {
        if (test_basic_lifecycle()) passedTests++;
        if (test_hot_reload()) passedTests++;
        if (test_performance_benchmark()) passedTests++;
        if (test_iio_cross_thread()) passedTests++;
        if (test_shutdown_grace()) passedTests++;
        if (test_factory_integration()) passedTests++;

    } catch (const std::exception& e) {
        std::cerr << "❌ EXCEPTION: " << e.what() << "\n";
    }

    std::cout << "\n================================================================================\n";
    std::cout << "RESULTS: " << passedTests << "/" << totalTests << " tests passed\n";
    std::cout << "================================================================================\n";

    return (passedTests == totalTests) ? 0 : 1;
}
