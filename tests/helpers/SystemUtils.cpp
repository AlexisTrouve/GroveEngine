#include "SystemUtils.h"
#include <fstream>
#include <string>
#include <dirent.h>
#include <sstream>

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

} // namespace grove
