/**
 * Test: spdlog with register_logger (like stillhammer does)
 */

#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("spdlog_register_test.log");
    log << "=== spdlog Register Test ===" << std::endl;
    log << "Step 1: main() started" << std::endl;
    log.flush();

    std::cout << "Step 1: main() started" << std::endl;

    // Create logger like stillhammer does
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    std::vector<spdlog::sink_ptr> sinks{console_sink};
    auto logger = std::make_shared<spdlog::logger>("Test", sinks.begin(), sinks.end());

    log << "Step 2: Logger created" << std::endl;
    log.flush();

    // Register globally (like stillhammer does)
    spdlog::register_logger(logger);

    log << "Step 3: Logger registered" << std::endl;
    log.flush();

    logger->info("Hello from registered logger");

    log << "Success!" << std::endl;
    log.close();

    std::cout << "Success!" << std::endl;
    return 0;
}
