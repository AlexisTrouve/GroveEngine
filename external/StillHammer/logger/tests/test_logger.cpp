#include <catch2/catch_test_macros.hpp>
#include <logger/Logger.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace stillhammer;

// Helper to check if file exists and contains text
bool fileContains(const std::string& filepath, const std::string& text) {
    if (!std::filesystem::exists(filepath)) {
        return false;
    }

    std::ifstream file(filepath);
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return content.find(text) != std::string::npos;
}

TEST_CASE("Logger: Basic creation and logging", "[logger]") {
    // Clean up any previous test logs
    std::filesystem::remove_all("logs");

    auto log = createLogger("TestLogger");

    REQUIRE(log != nullptr);
    REQUIRE(log->name() == "TestLogger");

    log->info("Test message");
    log->debug("Debug message");
    log->warn("Warning message");

    // Flush to ensure file is written
    log->flush();

    // Give filesystem time to sync
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check that log file was created
    REQUIRE(std::filesystem::exists("logs/test_logger.log"));

    // Check that messages were logged
    REQUIRE(fileContains("logs/test_logger.log", "Test message"));
    REQUIRE(fileContains("logs/test_logger.log", "Warning message"));
}

TEST_CASE("Logger: Domain-based organization", "[logger]") {
    std::filesystem::remove_all("logs");

    auto networkLog = createDomainLogger("NetworkIO", "network");
    auto engineLog = createDomainLogger("EngineCore", "engine");

    REQUIRE(networkLog != nullptr);
    REQUIRE(engineLog != nullptr);

    networkLog->info("Packet received");
    engineLog->info("Engine started");

    networkLog->flush();
    engineLog->flush();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check domain directories were created
    REQUIRE(std::filesystem::exists("logs/network/network_io.log"));
    REQUIRE(std::filesystem::exists("logs/engine/engine_core.log"));

    // Check correct messages in correct files
    REQUIRE(fileContains("logs/network/network_io.log", "Packet received"));
    REQUIRE(fileContains("logs/engine/engine_core.log", "Engine started"));

    // Ensure cross-contamination didn't happen
    REQUIRE_FALSE(fileContains("logs/network/network_io.log", "Engine started"));
    REQUIRE_FALSE(fileContains("logs/engine/engine_core.log", "Packet received"));
}

TEST_CASE("Logger: Custom configuration", "[logger]") {
    std::filesystem::remove_all("logs");

    LoggerConfig config;
    config.setDomain("custom")
          .setConsoleLevel(LogLevel::Warn)
          .setFileLevel(LogLevel::Trace)
          .setPattern("[%n] %v");

    auto log = createLogger("CustomLogger", config);

    REQUIRE(log != nullptr);

    log->trace("Trace message");
    log->debug("Debug message");
    log->info("Info message");
    log->warn("Warn message");

    log->flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // File should have all messages (fileLevel = Trace)
    REQUIRE(fileContains("logs/custom/custom_logger.log", "Trace message"));
    REQUIRE(fileContains("logs/custom/custom_logger.log", "Debug message"));
    REQUIRE(fileContains("logs/custom/custom_logger.log", "Info message"));
    REQUIRE(fileContains("logs/custom/custom_logger.log", "Warn message"));
}

TEST_CASE("Logger: Get existing logger", "[logger]") {
    std::filesystem::remove_all("logs");

    auto log1 = createLogger("SharedLogger");
    log1->info("Message 1");

    // Get the same logger by name
    auto log2 = getLogger("SharedLogger");

    REQUIRE(log2 != nullptr);
    REQUIRE(log1 == log2);  // Should be the same instance

    log2->info("Message 2");

    log1->flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Both messages should be in the same file
    REQUIRE(fileContains("logs/shared_logger.log", "Message 1"));
    REQUIRE(fileContains("logs/shared_logger.log", "Message 2"));
}

TEST_CASE("Logger: Snake case conversion", "[logger]") {
    std::filesystem::remove_all("logs");

    auto log1 = createLogger("MyTestLogger");
    auto log2 = createLogger("IntraIOManager");
    auto log3 = createLogger("HTTPServer");

    log1->info("test");
    log2->info("test");
    log3->info("test");

    log1->flush();
    log2->flush();
    log3->flush();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check snake_case filenames
    REQUIRE(std::filesystem::exists("logs/my_test_logger.log"));
    REQUIRE(std::filesystem::exists("logs/intra_io_manager.log"));
    REQUIRE(std::filesystem::exists("logs/httpserver.log"));
}

TEST_CASE("Logger: Flush all loggers", "[logger]") {
    std::filesystem::remove_all("logs");

    auto log1 = createLogger("Logger1");
    auto log2 = createLogger("Logger2");
    auto log3 = createLogger("Logger3");

    log1->info("Message from logger 1");
    log2->info("Message from logger 2");
    log3->info("Message from logger 3");

    // Flush all at once
    flushAll();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    REQUIRE(fileContains("logs/logger1.log", "Message from logger 1"));
    REQUIRE(fileContains("logs/logger2.log", "Message from logger 2"));
    REQUIRE(fileContains("logs/logger3.log", "Message from logger 3"));
}
