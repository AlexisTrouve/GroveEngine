/**
 * GroveEngine Link Test
 * Just link GroveEngine::impl, don't use any features
 * If this crashes, problem is in static initialization
 */

#include <fstream>
#include <iostream>

#undef main

int main(int argc, char* argv[]) {
    // IMMEDIATELY write to file (before anything else)
    std::ofstream log("link_test.log");
    log << "=== GroveEngine Link Test ===" << std::endl;
    log << "Program started" << std::endl;
    log.flush();

    std::cout << "If you see this, GroveEngine::impl linking doesn't crash" << std::endl;
    std::cout << "Check link_test.log for confirmation" << std::endl;

    log << "Program completed successfully" << std::endl;
    log << "GroveEngine::impl is linked but not used - no crash!" << std::endl;
    log.close();

    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
