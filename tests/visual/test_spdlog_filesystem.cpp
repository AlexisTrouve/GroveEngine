/**
 * Test: spdlog + filesystem combined
 */

#include <fstream>
#include <iostream>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("spdlog_fs_test.log");
    log << "=== spdlog + filesystem Test ===" << std::endl;
    log << "Step 1: main() started" << std::endl;
    log.flush();

    // Use filesystem
    std::filesystem::path p = std::filesystem::current_path();
    log << "Step 2: Current path: " << p.string() << std::endl;
    log.flush();

    // Use spdlog
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    std::vector<spdlog::sink_ptr> sinks{console_sink};
    auto logger = std::make_shared<spdlog::logger>("Test", sinks.begin(), sinks.end());
    spdlog::register_logger(logger);

    log << "Step 3: Logger created" << std::endl;
    log.flush();

    logger->info("Hello");

    log << "Success!" << std::endl;
    log.close();

    std::cout << "Success!" << std::endl;
    return 0;
}
