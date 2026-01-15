#pragma once
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <string>

namespace grove {

class TestMetrics {
public:
    // Enregistrement
    void recordFPS(float fps);
    void recordMemoryUsage(size_t bytes);
    void recordReloadTime(float ms);
    void recordCrash(const std::string& reason);

    // Getters - FPS
    float getFPSMin() const;
    float getFPSMax() const;
    float getFPSAvg() const;
    float getFPSStdDev() const;
    float getFPSMinLast60s() const;  // Pour stress test
    float getFPSAvgLast60s() const;

    // Getters - Memory
    size_t getMemoryInitial() const;
    size_t getMemoryFinal() const;
    size_t getMemoryPeak() const;
    size_t getMemoryGrowth() const;

    // Getters - Reload
    float getReloadTimeAvg() const;
    float getReloadTimeMin() const;
    float getReloadTimeMax() const;
    float getReloadTimeP99() const;  // Percentile 99
    int getReloadCount() const;

    // Getters - Crashes
    int getCrashCount() const;
    const std::vector<std::string>& getCrashReasons() const;

    // Rapport
    void printReport() const;

private:
    std::vector<float> fpsValues;
    std::vector<size_t> memoryValues;
    std::vector<float> reloadTimes;
    std::vector<std::string> crashReasons;

    size_t initialMemory = 0;
    bool hasInitialMemory = false;
};

} // namespace grove
