#pragma once
#include <cstddef>
#include <string>

namespace grove {

size_t getCurrentMemoryUsage();
int getOpenFileDescriptors();
float getCurrentCPUUsage();

/**
 * @brief Count temp files matching pattern (e.g., "/tmp/grove_module_*")
 */
int countTempFiles(const std::string& pattern);

/**
 * @brief Get number of mapped .so libraries from /proc/self/maps
 */
int getMappedLibraryCount();

} // namespace grove
