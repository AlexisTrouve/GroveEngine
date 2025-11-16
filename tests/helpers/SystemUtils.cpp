#include "SystemUtils.h"
#include <fstream>
#include <string>
#include <dirent.h>
#include <sstream>
#include <glob.h>
#include <cstring>

namespace grove {

size_t getCurrentMemoryUsage() {
    // Linux: /proc/self/status -> VmRSS
    std::ifstream file("/proc/self/status");
    std::string line;

    while (std::getline(file, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream iss(line.substr(7));
            size_t kb;
            iss >> kb;
            return kb * 1024; // Convert to bytes
        }
    }

    return 0;
}

int getOpenFileDescriptors() {
    // Linux: /proc/self/fd
    int count = 0;
    DIR* dir = opendir("/proc/self/fd");

    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            count++;
        }
        closedir(dir);
    }

    return count - 2; // Exclude . and ..
}

float getCurrentCPUUsage() {
    // Simplifié - retourne 0 pour l'instant
    // Implémentation complète nécessite tracking du /proc/self/stat
    // entre deux lectures (utime + stime delta)
    return 0.0f;
}

int countTempFiles(const std::string& pattern) {
    glob_t globResult;
    memset(&globResult, 0, sizeof(globResult));

    int result = glob(pattern.c_str(), GLOB_TILDE, nullptr, &globResult);

    if (result != 0) {
        globfree(&globResult);
        return 0;
    }

    int count = globResult.gl_pathc;
    globfree(&globResult);

    return count;
}

int getMappedLibraryCount() {
    // Count unique .so libraries in /proc/self/maps
    std::ifstream file("/proc/self/maps");
    std::string line;
    int count = 0;
    std::string lastLib;

    while (std::getline(file, line)) {
        // Look for lines containing ".so"
        size_t soPos = line.find(".so");
        if (soPos != std::string::npos) {
            // Extract library path (after last space)
            size_t pathStart = line.rfind(' ');
            if (pathStart != std::string::npos) {
                std::string libPath = line.substr(pathStart + 1);
                // Only count if different from last one (avoid duplicates)
                if (libPath != lastLib) {
                    count++;
                    lastLib = libPath;
                }
            }
        }
    }

    return count;
}

} // namespace grove
