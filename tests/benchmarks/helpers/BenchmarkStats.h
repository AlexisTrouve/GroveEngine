#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace GroveEngine {
namespace Benchmark {

/**
 * Statistical analysis for benchmark samples.
 * Computes mean, median, percentiles, min, max, and standard deviation.
 */
class BenchmarkStats {
public:
    BenchmarkStats() : samples(), sorted(false) {}

    /**
     * Add a sample value to the dataset.
     */
    void addSample(double value) {
        samples.push_back(value);
        sorted = false;
    }

    /**
     * Get the mean (average) of all samples.
     */
    double mean() const {
        if (samples.empty()) return 0.0;
        return std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    }

    /**
     * Get the median (50th percentile) of all samples.
     */
    double median() {
        return percentile(0.50);
    }

    /**
     * Get the 95th percentile of all samples.
     */
    double p95() {
        return percentile(0.95);
    }

    /**
     * Get the 99th percentile of all samples.
     */
    double p99() {
        return percentile(0.99);
    }

    /**
     * Get the minimum value.
     */
    double min() const {
        if (samples.empty()) return 0.0;
        return *std::min_element(samples.begin(), samples.end());
    }

    /**
     * Get the maximum value.
     */
    double max() const {
        if (samples.empty()) return 0.0;
        return *std::max_element(samples.begin(), samples.end());
    }

    /**
     * Get the standard deviation.
     */
    double stddev() const {
        if (samples.size() < 2) return 0.0;

        double avg = mean();
        double variance = 0.0;
        for (double sample : samples) {
            double diff = sample - avg;
            variance += diff * diff;
        }
        variance /= (samples.size() - 1); // Sample standard deviation
        return std::sqrt(variance);
    }

    /**
     * Get the number of samples.
     */
    size_t count() const {
        return samples.size();
    }

    /**
     * Clear all samples.
     */
    void clear() {
        samples.clear();
        sorted = false;
    }

private:
    std::vector<double> samples;
    mutable bool sorted;

    void ensureSorted() const {
        if (!sorted && !samples.empty()) {
            std::sort(const_cast<std::vector<double>&>(samples).begin(),
                     const_cast<std::vector<double>&>(samples).end());
            const_cast<bool&>(sorted) = true;
        }
    }

    double percentile(double p) {
        if (samples.empty()) return 0.0;
        if (p < 0.0 || p > 1.0) {
            throw std::invalid_argument("Percentile must be between 0 and 1");
        }

        ensureSorted();

        if (samples.size() == 1) return samples[0];

        // Linear interpolation between closest ranks
        double rank = p * (samples.size() - 1);
        size_t lowerIndex = static_cast<size_t>(std::floor(rank));
        size_t upperIndex = static_cast<size_t>(std::ceil(rank));

        if (lowerIndex == upperIndex) {
            return samples[lowerIndex];
        }

        double fraction = rank - lowerIndex;
        return samples[lowerIndex] * (1.0 - fraction) + samples[upperIndex] * fraction;
    }
};

} // namespace Benchmark
} // namespace GroveEngine
