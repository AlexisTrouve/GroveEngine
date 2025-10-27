#pragma once

#include <random>
#include <cstdint>

namespace warfactory {

/**
 * @brief Centralized random number generator singleton
 *
 * Provides consistent, seedable random number generation across all modules.
 * Ensures reproducibility for testing and debugging while maintaining
 * high-quality random distribution.
 */
class RandomGenerator {
private:
    std::mt19937 gen;

    RandomGenerator() : gen(std::random_device{}()) {}

public:
    // Singleton access
    static RandomGenerator& getInstance() {
        static RandomGenerator instance;
        return instance;
    }

    // Delete copy/move constructors and operators
    RandomGenerator(const RandomGenerator&) = delete;
    RandomGenerator& operator=(const RandomGenerator&) = delete;
    RandomGenerator(RandomGenerator&&) = delete;
    RandomGenerator& operator=(RandomGenerator&&) = delete;

    /**
     * @brief Seed the random generator for reproducible sequences
     * @param seed Seed value (use same seed for identical results)
     */
    void seed(uint32_t seed) {
        gen.seed(seed);
    }

    /**
     * @brief Generate uniform float in range [min, max]
     */
    float uniform(float min, float max) {
        std::uniform_real_distribution<float> dis(min, max);
        return dis(gen);
    }

    /**
     * @brief Generate uniform int in range [min, max] (inclusive)
     */
    int uniformInt(int min, int max) {
        std::uniform_int_distribution<int> dis(min, max);
        return dis(gen);
    }

    /**
     * @brief Generate normal distribution float
     */
    float normal(float mean, float stddev) {
        std::normal_distribution<float> dis(mean, stddev);
        return dis(gen);
    }

    /**
     * @brief Generate uniform float in range [0.0, 1.0]
     */
    float unit() {
        return uniform(0.0f, 1.0f);
    }

    /**
     * @brief Generate boolean with given probability
     * @param probability Probability of returning true [0.0, 1.0]
     */
    bool boolean(float probability = 0.5f) {
        return unit() < probability;
    }

    /**
     * @brief Direct access to underlying generator for custom distributions
     */
    std::mt19937& getGenerator() {
        return gen;
    }
};

} // namespace warfactory