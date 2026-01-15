/**
 * Test GroveEngine headers progressively
 * Find which header causes the crash
 */

#include <fstream>
#include <iostream>

// Test levels - uncomment one at a time
#define TEST_LEVEL_1 1  // Just SDL
#define TEST_LEVEL_2 1  // + spdlog
#define TEST_LEVEL_3 1  // + IntraIO headers
#define TEST_LEVEL_4 1  // + ModuleLoader headers

#include <SDL.h>

#if TEST_LEVEL_2
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#endif

#if TEST_LEVEL_3
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#endif

#if TEST_LEVEL_4
#include <grove/ModuleLoader.h>
#include <grove/JsonDataNode.h>
#endif

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("headers_test.log");
    log << "=== Headers Progressive Test ===" << std::endl;
    log << "Test Level 1: SDL" << std::endl;
#if TEST_LEVEL_2
    log << "Test Level 2: spdlog" << std::endl;
#endif
#if TEST_LEVEL_3
    log << "Test Level 3: IntraIO headers" << std::endl;
#endif
#if TEST_LEVEL_4
    log << "Test Level 4: ModuleLoader headers" << std::endl;
#endif
    log.flush();

    std::cout << "Headers loaded successfully!" << std::endl;
    std::cout << "Check headers_test.log" << std::endl;

    log << "All headers loaded - no crash!" << std::endl;
    log.close();

    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
