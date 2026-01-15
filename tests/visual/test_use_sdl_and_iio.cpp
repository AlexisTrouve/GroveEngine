/**
 * Test: USE SDL_Init + IntraIOManager together
 * This reproduces what test_progressive does
 */

#include <fstream>
#include <iostream>
#include <SDL.h>
#include <grove/IntraIOManager.h>

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("use_sdl_iio_test.log");
    log << "=== Use SDL + IIO Test ===" << std::endl;
    log << "Step 1: Program started" << std::endl;
    log.flush();

    std::cout << "Step 1: Program started" << std::endl;

    // Test SDL
    log << "Step 2: Calling SDL_Init..." << std::endl;
    log.flush();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        log << "ERROR: SDL_Init failed" << std::endl;
        return 1;
    }

    log << "Step 3: SDL_Init OK" << std::endl;
    log.flush();

    // Test IntraIOManager
    log << "Step 4: Getting IntraIOManager instance..." << std::endl;
    log.flush();

    try {
        auto& ioManager = grove::IntraIOManager::getInstance();
        log << "Step 5: IntraIOManager OK" << std::endl;
        log.flush();

        auto testIO = ioManager.createInstance("test");
        log << "Step 6: Created IIO instance" << std::endl;
        log.flush();

        ioManager.removeInstance("test");
        log << "Step 7: Removed IIO instance" << std::endl;
    } catch (const std::exception& e) {
        log << "ERROR: " << e.what() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Quit();

    log << "Step 8: All tests passed!" << std::endl;
    log << "Success - no crash!" << std::endl;
    log.close();

    std::cout << "Success! Check use_sdl_iio_test.log" << std::endl;
    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
