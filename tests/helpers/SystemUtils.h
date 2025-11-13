#pragma once
#include <cstddef>

namespace grove {

size_t getCurrentMemoryUsage();
int getOpenFileDescriptors();
float getCurrentCPUUsage();

} // namespace grove
