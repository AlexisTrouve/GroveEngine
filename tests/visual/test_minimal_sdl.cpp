/**
 * Minimal SDL test - just open a window
 * If this doesn't work, we have SDL/DLL issues
 */

#include <SDL.h>
#include <fstream>
#include <iostream>

#undef main

int main(int argc, char* argv[]) {
    // Write to file FIRST (before anything can crash)
    std::ofstream log("minimal_test.log");
    log << "=== Minimal SDL Test ===" << std::endl;
    log << "Step 1: Program started" << std::endl;
    log.flush();

    // Try SDL init
    log << "Step 2: Attempting SDL_Init..." << std::endl;
    log.flush();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        log << "ERROR: SDL_Init failed: " << SDL_GetError() << std::endl;
        log.close();
        std::cerr << "SDL_Init failed - check minimal_test.log\n";
        return 1;
    }

    log << "Step 3: SDL_Init SUCCESS" << std::endl;
    log.flush();

    // Try window creation
    log << "Step 4: Creating window..." << std::endl;
    log.flush();

    SDL_Window* window = SDL_CreateWindow(
        "Minimal Test",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        log << "ERROR: SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        log.close();
        return 1;
    }

    log << "Step 5: Window created SUCCESS" << std::endl;
    log << "Window is visible - press any key to close" << std::endl;
    log.flush();

    std::cout << "Window created! Check minimal_test.log for details.\n";
    std::cout << "Press any key in the window to close...\n";

    // Wait for key press
    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN) {
                running = false;
            }
        }
        SDL_Delay(10);
    }

    log << "Step 6: Cleaning up..." << std::endl;
    SDL_DestroyWindow(window);
    SDL_Quit();

    log << "Step 7: Program exited cleanly" << std::endl;
    log.close();

    std::cout << "Test completed successfully!\n";
    return 0;
}
