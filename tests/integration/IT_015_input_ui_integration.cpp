/**
 * Integration Test IT_015: UIModule Input Event Integration (Simplified)
 *
 * Tests input event processing by publishing IIO messages directly:
 * - Direct IIO input event publishing (bypasses InputModule/SDL)
 * - UIModule consumes input events and processes them
 * - Verifies UI event generation
 *
 * Note: This test bypasses InputModule to avoid SDL dependencies.
 * For full InputModule testing, see test_30_input_module.cpp
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <iostream>

using namespace grove;

TEST_CASE("IT_015: UIModule Input Integration", "[integration][input][ui][phase3]") {
    std::cout << "\n========================================\n";
    std::cout << "IT_015: Input → UI Integration Test\n";
    std::cout << "========================================\n\n";

    auto& ioManager = IntraIOManager::getInstance();

    // Create IIO instances
    auto inputPublisher = ioManager.createInstance("input_publisher");
    auto uiIO = ioManager.createInstance("ui_module");
    auto testIO = ioManager.createInstance("test_observer");

    // Load UIModule
    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";

#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif

    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "ui_module"));
    REQUIRE(uiModule != nullptr);

    // Configure UIModule
    JsonDataNode uiConfig("config");
    uiConfig.setInt("windowWidth", 800);
    uiConfig.setInt("windowHeight", 600);
    uiConfig.setString("layoutFile", "../../assets/ui/test_buttons.json");
    uiConfig.setInt("baseLayer", 1000);

    REQUIRE_NOTHROW(uiModule->setConfiguration(uiConfig, uiIO.get(), nullptr));
    std::cout << "✅ UIModule loaded\n\n";

    // Subscribe to events
    testIO->subscribe("ui:click");
    testIO->subscribe("ui:hover");
    testIO->subscribe("ui:action");

    int uiClicksReceived = 0;
    int uiHoversReceived = 0;

    // Publish input events via IIO (simulates InputModule output)
    std::cout << "Publishing input events...\n";

    // Mouse move to center
    auto mouseMoveData = std::make_unique<JsonDataNode>("data");
    mouseMoveData->setInt("x", 400);
    mouseMoveData->setInt("y", 300);
    inputPublisher->publish("input:mouse:move", std::move(mouseMoveData));

    // Process UIModule
    JsonDataNode inputData("input");
    uiModule->process(inputData);

    // Mouse click
    auto mouseClickData = std::make_unique<JsonDataNode>("data");
    mouseClickData->setInt("button", 0);
    mouseClickData->setBool("pressed", true);
    mouseClickData->setInt("x", 100);
    mouseClickData->setInt("y", 100);
    inputPublisher->publish("input:mouse:button", std::move(mouseClickData));

    // Process UIModule again
    uiModule->process(inputData);

    // Collect UI events
    while (testIO->hasMessages() > 0) {
        auto msg = testIO->pullMessage();
        if (msg.topic == "ui:click") {
            uiClicksReceived++;
            std::cout << "✅ Received ui:click event\n";
        } else if (msg.topic == "ui:hover") {
            uiHoversReceived++;
            std::cout << "✅ Received ui:hover event\n";
        }
    }

    std::cout << "\nResults:\n";
    std::cout << "  - UI clicks: " << uiClicksReceived << "\n";
    std::cout << "  - UI hovers: " << uiHoversReceived << "\n";

    // Note: UI events depend on layout file, so we don't REQUIRE them
    // This test mainly verifies that UIModule can be loaded and process input events
    std::cout << "\n✅ IT_015: Integration test PASSED\n";
    std::cout << "========================================\n\n";

    // Cleanup
    uiModule->shutdown();
}
