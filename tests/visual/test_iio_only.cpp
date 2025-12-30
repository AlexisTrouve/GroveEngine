/**
 * Test: IntraIOManager::getInstance() only
 * Find if IIO singleton initialization is the problem
 */

#include <fstream>
#include <iostream>
#include <grove/IntraIOManager.h>

#undef main

int main(int argc, char* argv[]) {
    std::ofstream log("iio_only_test.log");
    log << "=== IIO Only Test ===" << std::endl;
    log << "Step 1: Program started" << std::endl;
    log.flush();

    std::cout << "Step 1: Program started" << std::endl;

    log << "Step 2: Calling getInstance()..." << std::endl;
    log.flush();

    try {
        auto& ioManager = grove::IntraIOManager::getInstance();
        log << "Step 4: getInstance() SUCCESS" << std::endl;
        log.flush();

        log << "Step 5: Test passed!" << std::endl;
    } catch (const std::exception& e) {
        log << "ERROR: " << e.what() << std::endl;
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        log << "ERROR: Unknown exception" << std::endl;
        std::cerr << "ERROR: Unknown exception" << std::endl;
        return 1;
    }

    log << "Success - no crash!" << std::endl;
    log.close();

    std::cout << "Success! Check iio_only_test.log" << std::endl;
    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
