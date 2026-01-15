#include <logger/Logger.h>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== StillHammer Logger Demo ===\n\n";

    // Test 1: Simple logger
    std::cout << "Test 1: Creating simple logger\n";
    auto log = stillhammer::createLogger("DemoApp");
    log->info("Application started");
    log->debug("Debug information");
    log->warn("This is a warning");

    // Test 2: Domain-organized loggers
    std::cout << "\nTest 2: Domain-organized loggers\n";
    auto networkLog = stillhammer::createDomainLogger("NetworkIO", "network");
    auto engineLog = stillhammer::createDomainLogger("EngineCore", "engine");

    networkLog->info("Listening on port 8080");
    engineLog->info("Engine initialized");

    // Test 3: Custom configuration
    std::cout << "\nTest 3: Custom configuration\n";
    stillhammer::LoggerConfig config;
    config.setDomain("custom")
          .setConsoleLevel(stillhammer::LogLevel::Warn)
          .setFileLevel(stillhammer::LogLevel::Trace);

    auto customLog = stillhammer::createLogger("CustomLogger", config);
    customLog->trace("This won't show on console");
    customLog->warn("But this will!");

    // Test 4: Multiple messages
    std::cout << "\nTest 4: Logging multiple messages\n";
    for (int i = 0; i < 5; i++) {
        networkLog->info("Packet {} received", i);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Test 5: Get existing logger
    std::cout << "\nTest 5: Retrieving existing logger\n";
    auto retrievedLog = stillhammer::getLogger("NetworkIO");
    if (retrievedLog) {
        retrievedLog->info("Retrieved logger works!");
    }

    // Flush all loggers
    std::cout << "\nFlushing all loggers...\n";
    stillhammer::flushAll();

    std::cout << "\n=== Demo Complete ===\n";
    std::cout << "Check the logs/ directory for output files:\n";
    std::cout << "  - logs/demo_app.log\n";
    std::cout << "  - logs/network/network_io.log\n";
    std::cout << "  - logs/engine/engine_core.log\n";
    std::cout << "  - logs/custom/custom_logger.log\n";

    return 0;
}
