/**
 * Test spdlog in isolation
 * If this crashes, spdlog is the culprit
 */

#include <fstream>
#include <iostream>

// Test just including spdlog headers
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#undef main

int main(int argc, char* argv[]) {
    // Write to file FIRST
    std::ofstream log("spdlog_test.log");
    log << "=== spdlog Test ===" << std::endl;
    log << "Step 1: Program started" << std::endl;
    log.flush();

    std::cout << "Step 1: Program started" << std::endl;

    // Try using spdlog
    try {
        log << "Step 2: Creating spdlog sinks..." << std::endl;
        log.flush();

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("spdlog_output.log", true);

        log << "Step 3: Sinks created" << std::endl;
        log.flush();

        std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("TestLogger", sinks.begin(), sinks.end());

        log << "Step 4: Logger created" << std::endl;
        log.flush();

        logger->info("spdlog test message");

        log << "Step 5: spdlog works!" << std::endl;
        log.flush();

        std::cout << "SUCCESS: spdlog works correctly" << std::endl;
    } catch (const std::exception& e) {
        log << "ERROR: " << e.what() << std::endl;
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    log << "Program completed successfully" << std::endl;
    log.close();

    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
