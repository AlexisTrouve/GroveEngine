#include "SystemUtils.h"
#include <fstream>
#include <string>
#include <sstream>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <dirent.h>
#include <glob.h>
#endif

namespace grove {

size_t getCurrentMemoryUsage() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
#else
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
#endif
}

int getOpenFileDescriptors() {
#ifdef _WIN32
    // Windows: Not easily available, return 0
    return 0;
#else
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
#endif
}

float getCurrentCPUUsage() {
    // Simplifié - retourne 0 pour l'instant
    // Implémentation complète nécessite tracking du /proc/self/stat
    // entre deux lectures (utime + stime delta)
    return 0.0f;
}

int countTempFiles(const std::string& pattern) {
#ifdef _WIN32
    // Windows: Use std::filesystem to count matching files
    int count = 0;
    try {
        std::filesystem::path dirPath = std::filesystem::path(pattern).parent_path();
        std::string filename = std::filesystem::path(pattern).filename().string();

        // Simple pattern matching - just count files starting with the prefix
        // This is a simplification; for full glob support, use a library
        if (std::filesystem::exists(dirPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                if (entry.is_regular_file()) {
                    std::string name = entry.path().filename().string();
                    // Check if name starts with the pattern prefix (before *)
                    size_t starPos = filename.find('*');
                    if (starPos != std::string::npos) {
                        std::string prefix = filename.substr(0, starPos);
                        if (name.find(prefix) == 0) {
                            count++;
                        }
                    }
                }
            }
        }
    } catch (...) {
        // Ignore errors
    }
    return count;
#else
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
#endif
}

int getMappedLibraryCount() {
#ifdef _WIN32
    // Windows: Count loaded modules
    HMODULE hMods[1024];
    DWORD cbNeeded;
    int count = 0;

    if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
        count = cbNeeded / sizeof(HMODULE);
    }

    return count;
#else
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
#endif
}

} // namespace grove
