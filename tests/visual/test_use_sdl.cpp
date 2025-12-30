/**
 * Test: Actually USE SDL_Init
 * Find out if calling SDL functions causes the crash
 */

#include <fstream>
#include <iostream>
#include <SDL.h>

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("use_sdl_test.log");
    log << "=== Use SDL Test ===" << std::endl;
    log << "Step 1: Program started" << std::endl;
    log.flush();

    std::cout << "Step 1: Program started" << std::endl;

    // Actually call SDL_Init (like test_progressive does)
    log << "Step 2: Calling SDL_Init..." << std::endl;
    log.flush();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        log << "ERROR: SDL_Init failed: " << SDL_GetError() << std::endl;
        std::cerr << "SDL_Init failed" << std::endl;
        return 1;
    }

    log << "Step 3: SDL_Init SUCCESS" << std::endl;
    log.flush();

    std::cout << "SDL_Init succeeded!" << std::endl;

    SDL_Quit();

    log << "Step 4: SDL_Quit done" << std::endl;
    log << "Success - no crash!" << std::endl;
    log.close();

    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
