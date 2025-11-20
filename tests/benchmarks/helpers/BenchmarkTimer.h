#pragma once

#include <chrono>

namespace GroveEngine {
namespace Benchmark {

/**
 * High-resolution timer for benchmarking.
 * Uses std::chrono::high_resolution_clock for precise measurements.
 */
class BenchmarkTimer {
public:
    BenchmarkTimer() : startTime() {}

    /**
     * Start (or restart) the timer.
     */
    void start() {
        startTime = std::chrono::high_resolution_clock::now();
    }

    /**
     * Get elapsed time in milliseconds since start().
     */
    double elapsedMs() const {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - startTime);
        return duration.count() / 1000.0;
    }

    /**
     * Get elapsed time in microseconds since start().
     */
    double elapsedUs() const {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - startTime);
        return duration.count() / 1000.0;
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
};

} // namespace Benchmark
} // namespace GroveEngine
