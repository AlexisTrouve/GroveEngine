// Simple test to verify atomic barrier optimization works
#include <grove/ThreadedModuleSystem.h>
#include <grove/IModule.h>
#include <grove/IDataNode.h>
#include <iostream>
#include <atomic>
#include <chrono>

using namespace grove;

class SimpleCounterModule : public IModule {
private:
    std::atomic<int> counter{0};

public:
    void process(const IDataNode& input) override {
        counter.fetch_add(1);
        // Simulate some work
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    void shutdown() override {}

    int getCounter() const { return counter.load(); }
};

int main() {
    std::cout << "Testing Atomic Barrier Optimization..." << std::endl;

    // Create system with 4 worker threads
    auto system = std::make_unique<ThreadedModuleSystem>();

    // Create 4 simple counter modules
    auto mod1 = std::make_unique<SimpleCounterModule>();
    auto mod2 = std::make_unique<SimpleCounterModule>();
    auto mod3 = std::make_unique<SimpleCounterModule>();
    auto mod4 = std::make_unique<SimpleCounterModule>();

    auto* mod1Ptr = mod1.get();
    auto* mod2Ptr = mod2.get();
    auto* mod3Ptr = mod3.get();
    auto* mod4Ptr = mod4.get();

    system->registerModule("Mod1", std::move(mod1));
    system->registerModule("Mod2", std::move(mod2));
    system->registerModule("Mod3", std::move(mod3));
    system->registerModule("Mod4", std::move(mod4));

    std::cout << "Registered 4 modules" << std::endl;

    // Process 1000 frames
    std::cout << "Processing 1000 frames..." << std::endl;
    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; i++) {
        system->processModules(1.0f / 60.0f);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    float duration = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    std::cout << "Time: " << duration << "ms" << std::endl;
    std::cout << "Avg frame time: " << (duration / 1000.0f) << "ms" << std::endl;

    // Verify all modules processed exactly 1000 times
    int c1 = mod1Ptr->getCounter();
    int c2 = mod2Ptr->getCounter();
    int c3 = mod3Ptr->getCounter();
    int c4 = mod4Ptr->getCounter();

    std::cout << "Module counts: " << c1 << ", " << c2 << ", " << c3 << ", " << c4 << std::endl;

    if (c1 == 1000 && c2 == 1000 && c3 == 1000 && c4 == 1000) {
        std::cout << "SUCCESS: All modules processed exactly 1000 times!" << std::endl;
        return 0;
    } else {
        std::cout << "FAIL: Module counts incorrect!" << std::endl;
        return 1;
    }
}
