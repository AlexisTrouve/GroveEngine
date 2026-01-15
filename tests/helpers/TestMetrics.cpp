#include "TestMetrics.h"
#include <iostream>
#include <iomanip>

namespace grove {

void TestMetrics::recordFPS(float fps) {
    fpsValues.push_back(fps);
}

void TestMetrics::recordMemoryUsage(size_t bytes) {
    if (!hasInitialMemory) {
        initialMemory = bytes;
        hasInitialMemory = true;
    }
    memoryValues.push_back(bytes);
}

void TestMetrics::recordReloadTime(float ms) {
    reloadTimes.push_back(ms);
}

void TestMetrics::recordCrash(const std::string& reason) {
    crashReasons.push_back(reason);
}

float TestMetrics::getFPSMin() const {
    if (fpsValues.empty()) return 0.0f;
    return *std::min_element(fpsValues.begin(), fpsValues.end());
}

float TestMetrics::getFPSMax() const {
    if (fpsValues.empty()) return 0.0f;
    return *std::max_element(fpsValues.begin(), fpsValues.end());
}

float TestMetrics::getFPSAvg() const {
    if (fpsValues.empty()) return 0.0f;
    return std::accumulate(fpsValues.begin(), fpsValues.end(), 0.0f) / fpsValues.size();
}

float TestMetrics::getFPSStdDev() const {
    if (fpsValues.empty()) return 0.0f;
    float avg = getFPSAvg();
    float variance = 0.0f;
    for (float fps : fpsValues) {
        variance += std::pow(fps - avg, 2);
    }
    return std::sqrt(variance / fpsValues.size());
}

float TestMetrics::getFPSMinLast60s() const {
    if (fpsValues.empty()) return 0.0f;
    // Last 60 seconds = 3600 frames at 60 FPS
    size_t startIdx = fpsValues.size() > 3600 ? fpsValues.size() - 3600 : 0;
    auto it = std::min_element(fpsValues.begin() + startIdx, fpsValues.end());
    return *it;
}

float TestMetrics::getFPSAvgLast60s() const {
    if (fpsValues.empty()) return 0.0f;
    size_t startIdx = fpsValues.size() > 3600 ? fpsValues.size() - 3600 : 0;
    float sum = std::accumulate(fpsValues.begin() + startIdx, fpsValues.end(), 0.0f);
    return sum / (fpsValues.size() - startIdx);
}

size_t TestMetrics::getMemoryInitial() const {
    return initialMemory;
}

size_t TestMetrics::getMemoryFinal() const {
    if (memoryValues.empty()) return 0;
    return memoryValues.back();
}

size_t TestMetrics::getMemoryGrowth() const {
    if (memoryValues.empty()) return 0;
    return memoryValues.back() - initialMemory;
}

size_t TestMetrics::getMemoryPeak() const {
    if (memoryValues.empty()) return 0;
    return *std::max_element(memoryValues.begin(), memoryValues.end());
}

float TestMetrics::getReloadTimeAvg() const {
    if (reloadTimes.empty()) return 0.0f;
    return std::accumulate(reloadTimes.begin(), reloadTimes.end(), 0.0f) / reloadTimes.size();
}

float TestMetrics::getReloadTimeMin() const {
    if (reloadTimes.empty()) return 0.0f;
    return *std::min_element(reloadTimes.begin(), reloadTimes.end());
}

float TestMetrics::getReloadTimeMax() const {
    if (reloadTimes.empty()) return 0.0f;
    return *std::max_element(reloadTimes.begin(), reloadTimes.end());
}

float TestMetrics::getReloadTimeP99() const {
    if (reloadTimes.empty()) return 0.0f;
    auto sorted = reloadTimes;
    std::sort(sorted.begin(), sorted.end());
    size_t p99Index = static_cast<size_t>(sorted.size() * 0.99);
    if (p99Index >= sorted.size()) p99Index = sorted.size() - 1;
    return sorted[p99Index];
}

int TestMetrics::getReloadCount() const {
    return static_cast<int>(reloadTimes.size());
}

int TestMetrics::getCrashCount() const {
    return static_cast<int>(crashReasons.size());
}

const std::vector<std::string>& TestMetrics::getCrashReasons() const {
    return crashReasons;
}

void TestMetrics::printReport() const {
    std::cout << "╔══════════════════════════════════════════════════════════════\n";
    std::cout << "║ METRICS REPORT\n";
    std::cout << "╠══════════════════════════════════════════════════════════════\n";

    if (!fpsValues.empty()) {
        std::cout << "║ FPS:\n";
        std::cout << "║   Min:       " << std::setw(8) << std::fixed << std::setprecision(2) << getFPSMin() << "\n";
        std::cout << "║   Avg:       " << std::setw(8) << std::fixed << std::setprecision(2) << getFPSAvg() << "\n";
        std::cout << "║   Max:       " << std::setw(8) << std::fixed << std::setprecision(2) << getFPSMax() << "\n";
        std::cout << "║   Std Dev:   " << std::setw(8) << std::fixed << std::setprecision(2) << getFPSStdDev() << "\n";
    }

    if (!memoryValues.empty()) {
        std::cout << "║ Memory:\n";
        std::cout << "║   Initial:   " << std::setw(8) << std::fixed << std::setprecision(2)
                  << (initialMemory / 1024.0f / 1024.0f) << " MB\n";
        std::cout << "║   Final:     " << std::setw(8) << std::fixed << std::setprecision(2)
                  << (memoryValues.back() / 1024.0f / 1024.0f) << " MB\n";
        std::cout << "║   Peak:      " << std::setw(8) << std::fixed << std::setprecision(2)
                  << (getMemoryPeak() / 1024.0f / 1024.0f) << " MB\n";
        std::cout << "║   Growth:    " << std::setw(8) << std::fixed << std::setprecision(2)
                  << (getMemoryGrowth() / 1024.0f / 1024.0f) << " MB\n";
    }

    if (!reloadTimes.empty()) {
        std::cout << "║ Reload Times:\n";
        std::cout << "║   Count:     " << std::setw(8) << reloadTimes.size() << "\n";
        std::cout << "║   Avg:       " << std::setw(8) << std::fixed << std::setprecision(2)
                  << getReloadTimeAvg() << " ms\n";
        std::cout << "║   Min:       " << std::setw(8) << std::fixed << std::setprecision(2)
                  << getReloadTimeMin() << " ms\n";
        std::cout << "║   Max:       " << std::setw(8) << std::fixed << std::setprecision(2)
                  << getReloadTimeMax() << " ms\n";
        std::cout << "║   P99:       " << std::setw(8) << std::fixed << std::setprecision(2)
                  << getReloadTimeP99() << " ms\n";
    }

    if (!crashReasons.empty()) {
        std::cout << "║ Crashes:     " << crashReasons.size() << "\n";
        for (const auto& reason : crashReasons) {
            std::cout << "║   - " << reason << "\n";
        }
    }

    std::cout << "╚══════════════════════════════════════════════════════════════\n";
}

} // namespace grove
