#include "TestReporter.h"
#include <iostream>

namespace grove {

TestReporter::TestReporter(const std::string& name) : scenarioName(name) {}

void TestReporter::addMetric(const std::string& name, float value) {
    metrics[name] = value;
}

void TestReporter::addAssertion(const std::string& name, bool passed) {
    assertions.push_back({name, passed});
}

void TestReporter::printFinalReport() const {
    std::cout << "\n";
    std::cout << "════════════════════════════════════════════════════════════════\n";
    std::cout << "FINAL REPORT: " << scenarioName << "\n";
    std::cout << "════════════════════════════════════════════════════════════════\n\n";

    // Metrics
    if (!metrics.empty()) {
        std::cout << "Metrics:\n";
        for (const auto& [name, value] : metrics) {
            std::cout << "  " << name << ": " << value << "\n";
        }
        std::cout << "\n";
    }

    // Assertions
    if (!assertions.empty()) {
        std::cout << "Assertions:\n";
        bool allPassed = true;
        for (const auto& [name, passed] : assertions) {
            std::cout << "  " << (passed ? "✓" : "✗") << " " << name << "\n";
            if (!passed) allPassed = false;
        }
        std::cout << "\n";

        if (allPassed) {
            std::cout << "Result: ✅ PASSED\n";
        } else {
            std::cout << "Result: ❌ FAILED\n";
        }
    }

    std::cout << "════════════════════════════════════════════════════════════════\n";
}

int TestReporter::getExitCode() const {
    for (const auto& [name, passed] : assertions) {
        if (!passed) return 1; // FAIL
    }
    return 0; // PASS
}

} // namespace grove
