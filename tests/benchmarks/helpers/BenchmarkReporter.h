#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

namespace GroveEngine {
namespace Benchmark {

/**
 * Formatted reporter for benchmark results.
 * Provides consistent and readable output for benchmark data.
 */
class BenchmarkReporter {
public:
    BenchmarkReporter(std::ostream& out = std::cout) : out(out) {}

    /**
     * Print a header for a benchmark section.
     */
    void printHeader(const std::string& name) {
        out << "\n";
        printSeparator('=');
        out << "BENCHMARK: " << name << "\n";
        printSeparator('=');
    }

    /**
     * Print a single result metric.
     */
    void printResult(const std::string& metric, double value, const std::string& unit) {
        out << std::left << std::setw(20) << metric << ": "
            << std::right << std::setw(10) << std::fixed << std::setprecision(2)
            << value << " " << unit << "\n";
    }

    /**
     * Print a comparison between two values.
     */
    void printComparison(const std::string& name1, double val1,
                        const std::string& name2, double val2) {
        double percentChange = ((val2 - val1) / val1) * 100.0;
        std::string sign = percentChange >= 0 ? "+" : "";

        out << std::left << std::setw(20) << name1 << ": "
            << std::right << std::setw(10) << std::fixed << std::setprecision(2)
            << val1 << " µs\n";

        out << std::left << std::setw(20) << name2 << ": "
            << std::right << std::setw(10) << std::fixed << std::setprecision(2)
            << val2 << " µs  (" << sign << std::fixed << std::setprecision(1)
            << percentChange << "%)\n";
    }

    /**
     * Print a subsection separator.
     */
    void printSubseparator() {
        printSeparator('-');
    }

    /**
     * Print a summary footer.
     */
    void printSummary(const std::string& summary) {
        printSeparator('-');
        out << "✅ RESULT: " << summary << "\n";
        printSeparator('=');
        out << std::endl;
    }

    /**
     * Print detailed statistics.
     */
    void printStats(const std::string& label, double mean, double median,
                   double p95, double p99, double min, double max,
                   double stddev, const std::string& unit) {
        out << "\n" << label << " Statistics:\n";
        printSubseparator();
        printResult("Mean", mean, unit);
        printResult("Median", median, unit);
        printResult("P95", p95, unit);
        printResult("P99", p99, unit);
        printResult("Min", min, unit);
        printResult("Max", max, unit);
        printResult("Stddev", stddev, unit);
    }

    /**
     * Print a simple message.
     */
    void printMessage(const std::string& message) {
        out << message << "\n";
    }

    /**
     * Print a table header.
     */
    void printTableHeader(const std::string& col1, const std::string& col2,
                         const std::string& col3 = "") {
        out << "\n";
        out << std::left << std::setw(25) << col1
            << std::right << std::setw(15) << col2;
        if (!col3.empty()) {
            out << std::right << std::setw(15) << col3;
        }
        out << "\n";
        printSeparator('-');
    }

    /**
     * Print a table row.
     */
    void printTableRow(const std::string& col1, double col2,
                      const std::string& unit, double col3 = -1.0) {
        out << std::left << std::setw(25) << col1
            << std::right << std::setw(12) << std::fixed << std::setprecision(2)
            << col2 << " " << std::setw(2) << unit;

        if (col3 >= 0.0) {
            std::string sign = col3 >= 0 ? "+" : "";
            out << std::right << std::setw(12) << sign << std::fixed
                << std::setprecision(1) << col3 << "%";
        }
        out << "\n";
    }

private:
    std::ostream& out;

    void printSeparator(char c = '=') {
        out << std::string(60, c) << "\n";
    }
};

} // namespace Benchmark
} // namespace GroveEngine
