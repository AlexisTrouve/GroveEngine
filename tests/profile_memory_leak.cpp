// ============================================================================
// profile_memory_leak.cpp - Detailed memory profiling for leak detection
// ============================================================================

#include "grove/ModuleLoader.h"
#include "grove/SequentialModuleSystem.h"
#include "grove/JsonDataNode.h"
#include "helpers/SystemUtils.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <thread>

using namespace grove;
namespace fs = std::filesystem;

void printMemory(const std::string& label) {
    size_t mem = getCurrentMemoryUsage();
    float mb = mem / (1024.0f * 1024.0f);
    std::cout << std::setw(40) << std::left << label << ": "
              << std::fixed << std::setprecision(3) << mb << " MB\n";
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "MEMORY LEAK PROFILER - Detailed Analysis\n";
    std::cout << "================================================================================\n\n";

    fs::path modulePath = "build/tests/libLeakTestModule.so";
    if (!fs::exists(modulePath)) {
        std::cerr << "❌ Module not found: " << modulePath << "\n";
        return 1;
    }

    // Disable verbose logging
    spdlog::set_level(spdlog::level::err);

    printMemory("1. Initial baseline");

    // Create loader and system
    ModuleLoader loader;
    printMemory("2. After ModuleLoader creation");

    auto moduleSystem = std::make_unique<SequentialModuleSystem>();
    moduleSystem->setLogLevel(spdlog::level::err);
    printMemory("3. After ModuleSystem creation");

    // Initial load
    {
        auto module = loader.load(modulePath, "LeakTestModule", false);
        printMemory("4. After initial load");

        nlohmann::json configJson = nlohmann::json::object();
        auto config = std::make_unique<JsonDataNode>("config", configJson);
        module->setConfiguration(*config, nullptr, nullptr);
        printMemory("5. After setConfiguration");

        moduleSystem->registerModule("LeakTestModule", std::move(module));
        printMemory("6. After registerModule");
    }

    std::cout << "\n--- Starting 10 reload cycles ---\n\n";

    size_t baselineAfterFirstLoad = getCurrentMemoryUsage();

    for (int i = 1; i <= 10; i++) {
        std::cout << "=== Cycle " << i << " ===\n";

        size_t memBefore = getCurrentMemoryUsage();

        // Extract module
        auto module = moduleSystem->extractModule();
        size_t memAfterExtract = getCurrentMemoryUsage();

        auto state = module->getState();
        size_t memAfterGetState = getCurrentMemoryUsage();

        auto config = std::make_unique<JsonDataNode>("config",
            dynamic_cast<const JsonDataNode&>(module->getConfiguration()).getJsonData());
        size_t memAfterGetConfig = getCurrentMemoryUsage();

        // Destroy old module
        module.reset();
        size_t memAfterReset = getCurrentMemoryUsage();

        // Reload
        auto newModule = loader.load(modulePath, "LeakTestModule", true);
        size_t memAfterLoad = getCurrentMemoryUsage();

        // Restore
        newModule->setConfiguration(*config, nullptr, nullptr);
        size_t memAfterSetConfig = getCurrentMemoryUsage();

        newModule->setState(*state);
        size_t memAfterSetState = getCurrentMemoryUsage();

        // Register
        moduleSystem->registerModule("LeakTestModule", std::move(newModule));
        size_t memAfterRegister = getCurrentMemoryUsage();

        // Process once
        moduleSystem->processModules(0.016f);
        size_t memAfterProcess = getCurrentMemoryUsage();

        // Analysis
        auto toKB = [](size_t delta) { return delta / 1024.0f; };

        std::cout << "  extractModule:     " << std::setw(8) << toKB(memAfterExtract - memBefore) << " KB\n";
        std::cout << "  getState:          " << std::setw(8) << toKB(memAfterGetState - memAfterExtract) << " KB\n";
        std::cout << "  getConfiguration:  " << std::setw(8) << toKB(memAfterGetConfig - memAfterGetState) << " KB\n";
        std::cout << "  module.reset:      " << std::setw(8) << toKB(memAfterReset - memAfterGetConfig) << " KB\n";
        std::cout << "  loader.load:       " << std::setw(8) << toKB(memAfterLoad - memAfterReset) << " KB\n";
        std::cout << "  setConfiguration:  " << std::setw(8) << toKB(memAfterSetConfig - memAfterLoad) << " KB\n";
        std::cout << "  setState:          " << std::setw(8) << toKB(memAfterSetState - memAfterSetConfig) << " KB\n";
        std::cout << "  registerModule:    " << std::setw(8) << toKB(memAfterRegister - memAfterSetState) << " KB\n";
        std::cout << "  processModules:    " << std::setw(8) << toKB(memAfterProcess - memAfterRegister) << " KB\n";
        std::cout << "  TOTAL THIS CYCLE:  " << std::setw(8) << toKB(memAfterProcess - memBefore) << " KB\n";

        size_t totalGrowth = memAfterProcess - baselineAfterFirstLoad;
        std::cout << "  Cumulative growth: " << std::setw(8) << toKB(totalGrowth) << " KB\n";
        std::cout << "\n";

        // Small delay to let system settle
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    size_t finalMem = getCurrentMemoryUsage();
    float totalGrowthKB = (finalMem - baselineAfterFirstLoad) / 1024.0f;
    float perCycleKB = totalGrowthKB / 10.0f;

    std::cout << "\n================================================================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "================================================================================\n";
    std::cout << "Baseline after first load: " << (baselineAfterFirstLoad / 1024.0f / 1024.0f) << " MB\n";
    std::cout << "Final memory:              " << (finalMem / 1024.0f / 1024.0f) << " MB\n";
    std::cout << "Total growth (10 cycles):  " << totalGrowthKB << " KB\n";
    std::cout << "Average per cycle:         " << perCycleKB << " KB\n";
    std::cout << "================================================================================\n";

    return 0;
}
