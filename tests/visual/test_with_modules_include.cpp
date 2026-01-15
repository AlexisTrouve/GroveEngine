/**
 * Test with modules/ in include path (like test_progressive)
 * This will tell us if adding modules/ to includes causes the crash
 */

#include <fstream>
#include <iostream>

#include <SDL.h>
#include <spdlog/spdlog.h>
#include <grove/IntraIOManager.h>
#include <grove/ModuleLoader.h>

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("modules_include_test.log");
    log << "=== Modules Include Test ===" << std::endl;
    log << "Testing with modules/ in include directories" << std::endl;
    log.flush();

    std::cout << "If you see this, adding modules/ to includes doesn't crash" << std::endl;

    log << "Success - no crash!" << std::endl;
    log.close();

    std::cout << "Check modules_include_test.log" << std::endl;
    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
