/**
 * Test: Stillhammer Logger Thread-Safety
 *
 * Validates that stillhammer::createLogger() is thread-safe
 * when called concurrently from multiple threads.
 */

#include <logger/Logger.h>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

int main() {
    std::cout << "================================================================================\n";
    std::cout << "Stillhammer Logger Thread-Safety Test\n";
    std::cout << "================================================================================\n";
    std::cout << "Creating 50 loggers from 10 concurrent threads...\n\n";

    std::atomic<int> successCount{0};
    std::atomic<int> failureCount{0};

    auto createLoggers = [&](int threadId) {
        try {
            for (int i = 0; i < 5; i++) {
                std::string loggerName = "TestLogger_" + std::to_string(threadId) + "_" + std::to_string(i);

                // Multiple threads may try to create the same logger
                // The wrapper should handle this safely
                auto logger = stillhammer::createLogger(loggerName);

                if (logger) {
                    logger->info("Hello from thread {} logger {}", threadId, i);
                    successCount++;
                } else {
                    failureCount++;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "❌ Thread " << threadId << " exception: " << e.what() << "\n";
            failureCount++;
        }
    };

    // Spawn 10 threads creating loggers concurrently
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back(createLoggers, i);
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    std::cout << "\n";
    std::cout << "Results:\n";
    std::cout << "  - Success: " << successCount.load() << "\n";
    std::cout << "  - Failure: " << failureCount.load() << "\n";

    if (failureCount.load() == 0 && successCount.load() == 50) {
        std::cout << "\n✅ Logger thread-safety TEST PASSED\n";
        std::cout << "================================================================================\n";
        return 0;
    } else {
        std::cout << "\n❌ Logger thread-safety TEST FAILED\n";
        std::cout << "================================================================================\n";
        return 1;
    }
}
