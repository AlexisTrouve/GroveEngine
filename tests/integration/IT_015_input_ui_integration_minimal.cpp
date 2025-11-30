/**
 * IT_015 Minimal: UIModule Input Integration (Minimal Version)
 *
 * This is a minimal test that verifies IIO message publishing works
 * without loading actual modules (to avoid DLL loading issues on Windows)
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <iostream>

using namespace grove;

TEST_CASE("IT_015_Minimal: IIO Message Publishing", "[integration][input][ui][minimal]") {
    std::cout << "\n========================================\n";
    std::cout << "IT_015 Minimal: IIO Test\n";
    std::cout << "========================================\n\n";

    auto& ioManager = IntraIOManager::getInstance();

    // Create IIO instances
    auto publisher = ioManager.createInstance("publisher");
    auto subscriber = ioManager.createInstance("subscriber");

    // Subscribe to input events
    subscriber->subscribe("input:mouse:move");
    subscriber->subscribe("input:mouse:button");
    subscriber->subscribe("input:keyboard:key");

    int mouseMoveCount = 0;
    int mouseButtonCount = 0;
    int keyboardKeyCount = 0;

    // Publish input events
    std::cout << "Publishing input events...\n";

    // Mouse move
    auto mouseMoveData = std::make_unique<JsonDataNode>("data");
    mouseMoveData->setInt("x", 400);
    mouseMoveData->setInt("y", 300);
    publisher->publish("input:mouse:move", std::move(mouseMoveData));

    // Mouse button
    auto mouseButtonData = std::make_unique<JsonDataNode>("data");
    mouseButtonData->setInt("button", 0);
    mouseButtonData->setBool("pressed", true);
    mouseButtonData->setInt("x", 100);
    mouseButtonData->setInt("y", 100);
    publisher->publish("input:mouse:button", std::move(mouseButtonData));

    // Keyboard key
    auto keyData = std::make_unique<JsonDataNode>("data");
    keyData->setInt("scancode", 44);  // Space
    keyData->setBool("pressed", true);
    publisher->publish("input:keyboard:key", std::move(keyData));

    // Collect messages
    while (subscriber->hasMessages() > 0) {
        auto msg = subscriber->pullMessage();

        if (msg.topic == "input:mouse:move") {
            mouseMoveCount++;
            int x = msg.data->getInt("x", 0);
            int y = msg.data->getInt("y", 0);
            std::cout << "✅ Received input:mouse:move (" << x << ", " << y << ")\n";
        }
        else if (msg.topic == "input:mouse:button") {
            mouseButtonCount++;
            std::cout << "✅ Received input:mouse:button\n";
        }
        else if (msg.topic == "input:keyboard:key") {
            keyboardKeyCount++;
            std::cout << "✅ Received input:keyboard:key\n";
        }
    }

    // Verify
    std::cout << "\nResults:\n";
    std::cout << "  - Mouse moves: " << mouseMoveCount << "\n";
    std::cout << "  - Mouse buttons: " << mouseButtonCount << "\n";
    std::cout << "  - Keyboard keys: " << keyboardKeyCount << "\n";

    REQUIRE(mouseMoveCount == 1);
    REQUIRE(mouseButtonCount == 1);
    REQUIRE(keyboardKeyCount == 1);

    std::cout << "\n✅ IT_015_Minimal: Test PASSED\n";
    std::cout << "========================================\n\n";
}
