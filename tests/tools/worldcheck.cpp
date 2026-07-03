/**
 * worldcheck — a headless CLI validator for a GroveEngine world-document (`.world` directory).
 *
 * WHAT  : Loads a `.world` dir, runs the grove::mapview semantic validator (WorldCheck) over its
 *         manifest + every chunk, and prints a graded report. Exit code is machine-friendly so it
 *         drops straight into CI or a producer's build script.
 *
 * WHY   : A producer (Theomen, a game) needs to prove its written `.world` is correct WITHOUT launching
 *         the GPU viewer and eyeballing pixels (which the doctrine rejects as proof). This turns the
 *         format's fail-franc guards + the semantic checks into a deterministic pass/fail answer that
 *         names the exact offending declaration.
 *
 * HOW   : Thin wrapper over disk::checkWorldDir (all the logic is there + unit-locked). Args:
 *           worldcheck <dir> [--strict] [--json]
 *         --strict makes warnings fail too; --json emits a machine-readable object. Exit: 0 = ok
 *         (clean, or warnings without --strict), 1 = errors (or warnings under --strict), 2 = usage.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include "grove/mapview/WorldCheck.h"
#include "grove/mapview/WorldCheckDisk.h"

using namespace grove::mapview;

namespace {

const char* severityName(Severity s) {
    switch (s) {
        case Severity::Error:   return "ERROR";
        case Severity::Warning: return "WARNING";
        case Severity::Info:    return "INFO";
    }
    return "?";
}

// Human-readable report to stdout.
void printHuman(const std::string& dir, const Report& r) {
    std::printf("worldcheck: %s\n", dir.c_str());
    std::printf("  fields=%zu  chunks=%zu  cells=%llu\n\n",
                r.fieldCount, r.chunkCount, static_cast<unsigned long long>(r.totalCells));

    for (const auto& d : r.diagnostics) {
        std::printf("%-8s[%s] %s\n", severityName(d.severity), d.where.c_str(), d.message.c_str());
    }
    if (!r.diagnostics.empty()) std::printf("\n");

    const std::size_t e = r.errors(), w = r.warnings();
    std::printf("RESULT: %zu error(s), %zu warning(s) — %s\n", e, w, r.ok() ? "OK" : "FAIL");
}

// Machine-readable JSON object to stdout.
void printJson(const std::string& dir, const Report& r) {
    nlohmann::json j;
    j["dir"] = dir;
    j["ok"] = r.ok();
    j["errors"] = r.errors();
    j["warnings"] = r.warnings();
    j["summary"] = {{"fields", r.fieldCount}, {"chunks", r.chunkCount}, {"cells", r.totalCells}};
    nlohmann::json diags = nlohmann::json::array();
    for (const auto& d : r.diagnostics) {
        diags.push_back({{"severity", severityName(d.severity)},
                         {"category", d.category},
                         {"where", d.where},
                         {"message", d.message}});
    }
    j["diagnostics"] = diags;
    std::printf("%s\n", j.dump(2).c_str());
}

} // namespace

int main(int argc, char** argv) {
    std::string dir;
    bool strict = false, json = false;

    // Parse args: one positional <dir>, plus optional flags in any order.
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--strict") == 0) {
            strict = true;
        } else if (std::strcmp(a, "--json") == 0) {
            json = true;
        } else if (a[0] == '-') {
            std::fprintf(stderr, "worldcheck: unknown option '%s'\n", a);
            return 2;
        } else if (dir.empty()) {
            dir = a;
        } else {
            std::fprintf(stderr, "worldcheck: unexpected extra argument '%s'\n", a);
            return 2;
        }
    }
    if (dir.empty()) {
        std::fprintf(stderr, "usage: worldcheck <world-dir> [--strict] [--json]\n");
        return 2;
    }

    const Report r = disk::checkWorldDir(dir);
    if (json) printJson(dir, r); else printHuman(dir, r);

    // Errors always fail; warnings fail only under --strict.
    if (r.errors() > 0) return 1;
    if (strict && r.warnings() > 0) return 1;
    return 0;
}
