/**
 * Test: Just include <filesystem>
 * See if this crashes before main() on Windows/MinGW
 */

#include <fstream>
#include <iostream>
#include <filesystem>

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("filesystem_test.log");
    log << "=== Filesystem Test ===" << std::endl;
    log << "main() started - filesystem include works!" << std::endl;
    log.close();

    std::cout << "Success! <filesystem> works on this system." << std::endl;
    return 0;
}
