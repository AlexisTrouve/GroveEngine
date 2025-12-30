/**
 * Test: Just stillhammer logger (no IntraIOManager)
 */

#include <fstream>
#include <iostream>
#include <logger/Logger.h>

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("logger_only_test.log");
    log << "=== Logger Only Test ===" << std::endl;
    log << "Step 1: main() started" << std::endl;
    log.flush();

    std::cout << "Step 1: main() started" << std::endl;

    log << "Step 2: Creating stillhammer logger..." << std::endl;
    log.flush();

    stillhammer::LoggerConfig config;
    config.disableFile();  // No file logging
    auto slogger = stillhammer::createDomainLogger("Test", "test", config);

    log << "Step 3: Logger created" << std::endl;
    log.flush();

    slogger->info("Hello from stillhammer logger");

    log << "Success!" << std::endl;
    log.close();

    std::cout << "Success! Check logger_only_test.log" << std::endl;
    return 0;
}
