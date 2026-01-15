/**
 * Test: Include Logger.cpp directly (not as library)
 */

#include <fstream>
#include <iostream>

// Include the header
#include <logger/Logger.h>

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("logger_direct_test.log");
    log << "=== Logger Direct Test ===" << std::endl;
    log << "Step 1: main() started" << std::endl;
    log.flush();

    std::cout << "Step 1: main() started" << std::endl;

    stillhammer::LoggerConfig config;
    config.disableFile();
    auto slogger = stillhammer::createDomainLogger("Test", "test", config);

    log << "Step 2: Logger created" << std::endl;
    log.flush();

    slogger->info("Hello from direct logger");

    log << "Success!" << std::endl;
    log.close();

    std::cout << "Success!" << std::endl;
    return 0;
}
