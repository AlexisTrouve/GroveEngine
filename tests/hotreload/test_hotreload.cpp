#include <iostream>
#include <dlfcn.h>
#include <memory>
#include <thread>
#include <chrono>
#include <grove/IModule.h>
#include <grove/JsonDataNode.h>

using namespace grove;

/**
 * @brief Simple hot-reload test without full engine
 *
 * This test demonstrates:
 * - Dynamic module loading from .so
 * - State extraction before reload
 * - Module replacement
 * - State restoration after reload
 * - Performance measurement
 */

// Function pointers for module factory
typedef IModule* (*CreateModuleFn)();
typedef void (*DestroyModuleFn)(IModule*);

class SimpleModuleLoader {
private:
    void* handle = nullptr;
    CreateModuleFn createFn = nullptr;
    DestroyModuleFn destroyFn = nullptr;
    std::string modulePath;

public:
    SimpleModuleLoader(const std::string& path) : modulePath(path) {}

    ~SimpleModuleLoader() {
        if (handle) {
            dlclose(handle);
        }
    }

    bool load() {
        std::cout << "\n[Loader] Loading module: " << modulePath << std::endl;

        handle = dlopen(modulePath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            std::cerr << "[Loader] ERROR: Failed to load module: " << dlerror() << std::endl;
            return false;
        }

        // Clear any existing error
        dlerror();

        // Load factory functions
        createFn = (CreateModuleFn)dlsym(handle, "createModule");
        const char* dlsym_error = dlerror();
        if (dlsym_error) {
            std::cerr << "[Loader] ERROR: Cannot load createModule: " << dlsym_error << std::endl;
            dlclose(handle);
            handle = nullptr;
            return false;
        }

        destroyFn = (DestroyModuleFn)dlsym(handle, "destroyModule");
        dlsym_error = dlerror();
        if (dlsym_error) {
            std::cerr << "[Loader] ERROR: Cannot load destroyModule: " << dlsym_error << std::endl;
            dlclose(handle);
            handle = nullptr;
            return false;
        }

        std::cout << "[Loader] ✅ Module loaded successfully" << std::endl;
        return true;
    }

    void unload() {
        if (handle) {
            std::cout << "[Loader] Unloading module..." << std::endl;
            dlclose(handle);
            handle = nullptr;
            createFn = nullptr;
            destroyFn = nullptr;
        }
    }

    IModule* createModule() {
        if (!createFn) {
            std::cerr << "[Loader] ERROR: createModule function not loaded" << std::endl;
            return nullptr;
        }
        return createFn();
    }

    void destroyModule(IModule* module) {
        if (destroyFn && module) {
            destroyFn(module);
        }
    }
};

void printSeparator(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << std::string(60, '=') << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "🔥 GroveEngine Hot-Reload Test 🔥" << std::endl;
    std::cout << "=================================" << std::endl;

    std::string modulePath = "./libTestModule.so";
    if (argc > 1) {
        modulePath = argv[1];
    }

    std::cout << "Module path: " << modulePath << std::endl;

    // Create loader
    SimpleModuleLoader loader(modulePath);

    // Load module
    printSeparator("STEP 1: Initial Load");
    if (!loader.load()) {
        std::cerr << "Failed to load module!" << std::endl;
        return 1;
    }

    // Create module instance
    IModule* module = loader.createModule();
    if (!module) {
        std::cerr << "Failed to create module instance!" << std::endl;
        return 1;
    }

    // Configure module
    nlohmann::json config = {{"version", "v1.0"}};
    JsonDataNode configNode("config", config);
    module->setConfiguration(configNode, nullptr, nullptr);

    // Process a few times
    printSeparator("STEP 2: Process Module (Before Reload)");
    nlohmann::json inputData = {{"message", "Hello from test"}};
    JsonDataNode input("input", inputData);

    for (int i = 0; i < 3; i++) {
        std::cout << "\n--- Iteration " << (i + 1) << " ---" << std::endl;
        module->process(input);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Get state before reload
    printSeparator("STEP 3: Extract State for Hot-Reload");
    auto state = module->getState();
    std::cout << "[Test] State extracted successfully" << std::endl;

    // Hot-reload simulation
    printSeparator("STEP 4: HOT-RELOAD (Measure Performance)");

    auto startTime = std::chrono::high_resolution_clock::now();

    // 1. Destroy old instance
    std::cout << "[Test] Destroying old module instance..." << std::endl;
    loader.destroyModule(module);
    module = nullptr;

    // 2. Unload old .so
    std::cout << "[Test] Unloading old .so..." << std::endl;
    loader.unload();

    // 3. Reload .so
    std::cout << "[Test] Reloading .so..." << std::endl;
    if (!loader.load()) {
        std::cerr << "Failed to reload module!" << std::endl;
        return 1;
    }

    // 4. Create new instance
    std::cout << "[Test] Creating new module instance..." << std::endl;
    module = loader.createModule();
    if (!module) {
        std::cerr << "Failed to create new module instance!" << std::endl;
        return 1;
    }

    // 5. Reconfigure
    std::cout << "[Test] Reconfiguring module..." << std::endl;
    module->setConfiguration(configNode, nullptr, nullptr);

    // 6. Restore state
    std::cout << "[Test] Restoring state..." << std::endl;
    module->setState(*state);

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(endTime - startTime);

    std::cout << "\n🚀 HOT-RELOAD COMPLETED IN: " << duration.count() << "ms 🚀" << std::endl;

    // Process again to verify state was preserved
    printSeparator("STEP 5: Process Module (After Reload)");
    std::cout << "Counter should continue from where it left off..." << std::endl;

    for (int i = 0; i < 3; i++) {
        std::cout << "\n--- Iteration " << (i + 1) << " ---" << std::endl;
        module->process(input);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Check health
    printSeparator("STEP 6: Health Check");
    auto health = module->getHealthStatus();
    std::cout << "[Test] Module health: " << health->getData()->toString() << std::endl;

    // Cleanup
    printSeparator("CLEANUP");
    module->shutdown();
    loader.destroyModule(module);

    std::cout << "\n✅ Hot-Reload Test Completed Successfully!" << std::endl;
    std::cout << "⏱️  Total reload time: " << duration.count() << "ms" << std::endl;

    if (duration.count() < 1.0) {
        std::cout << "🔥 Classification: BLAZING (< 1ms)" << std::endl;
    } else if (duration.count() < 10.0) {
        std::cout << "⚡ Classification: VERY FAST (< 10ms)" << std::endl;
    } else {
        std::cout << "👍 Classification: ACCEPTABLE" << std::endl;
    }

    return 0;
}
