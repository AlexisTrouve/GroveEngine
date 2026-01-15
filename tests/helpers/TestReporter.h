#pragma once
#include <string>
#include <map>
#include <vector>

namespace grove {

class TestReporter {
public:
    explicit TestReporter(const std::string& scenarioName);

    void addMetric(const std::string& name, float value);
    void addAssertion(const std::string& name, bool passed);

    void printFinalReport() const;
    int getExitCode() const; // 0 = pass, 1 = fail

private:
    std::string scenarioName;
    std::map<std::string, float> metrics;
    std::vector<std::pair<std::string, bool>> assertions;
};

} // namespace grove
