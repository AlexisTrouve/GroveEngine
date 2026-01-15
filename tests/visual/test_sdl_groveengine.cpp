/**
 * Test: SDL2 + GroveEngine linked together (but not used)
 * This will tell us if linking SDL2 with GroveEngine causes the crash
 */

#include <fstream>
#include <iostream>

// Include headers but don't use them
#include <SDL.h>
#include <spdlog/spdlog.h>
#include <grove/IntraIOManager.h>
#include <grove/ModuleLoader.h>

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("sdl_groveengine_test.log");
    log << "=== SDL + GroveEngine Test ===" << std::endl;
    log << "All libraries linked (SDL2 + spdlog + GroveEngine)" << std::endl;
    log << "But not using any functions" << std::endl;
    log.flush();

    std::cout << "If you see this, linking SDL2 with GroveEngine doesn't crash" << std::endl;

    log << "Success - no crash!" << std::endl;
    log.close();

    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
