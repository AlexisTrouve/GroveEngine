/**
 * IIO Zero-Copy Throughput Benchmark
 * ==================================================================================
 * WHAT: measures the PUBLISH+ROUTE cost of the IIO bus (ns per publish()) under the
 *   zero-copy delivery model, across two axes:
 *     1. fan-out N (subscribers per topic) — proves the per-subscriber cost is a
 *        ref-count bump, NOT a json copy (the O(N)→O(1) fan-out win of ZC-2/3).
 *     2. publisher mode — coreResident (true zero-copy, shares the original node, 0
 *        json copies) vs re-home (the safe default, 1 json copy into a core node).
 *        The delta = the single-copy elimination of ZC-4, and it scales with payload size.
 *
 * WHY: the zero-copy change SHIPPED with a claimed win ("O(N)→O(1)", "2 copies→1→0") but
 *   no measurement. Doctrine: a perf claim without a benchmark is an affirmation, not a fact.
 *   This turns the claim into numbers. (NOT a ctest — it's a wall-clock benchmark, run by hand.)
 *
 * HOW: for each (payload, mode, N) cell, time ONLY the publish() calls (the node is built
 *   outside the timer — that cost is identical across modes). Subscriber queues are drained
 *   OUTSIDE the timed window so we isolate publish+route. Report ns/publish + ns/publish/sub.
 *   The fan-out A/B vs the OLD per-subscriber-copy code is done separately via a pre-ZC worktree.
 */

#include "grove/IntraIOManager.h"
#include "grove/IntraIO.h"
#include "grove/JsonDataNode.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <string>
#include <memory>

using namespace grove;
using namespace std::chrono;

// Accumulator the subscriber handlers feed, so the compiler can't elide the delivery/read.
static volatile int64_t g_sink = 0;

// Build a realistic message payload. kind 0 = "sprite-like" (~12 scalar fields + a tiny uv array);
// kind 1 = "fat" (a 64-element array of sprite-like objects — models a batch / state snapshot).
// The json is built ONCE; each publish wraps a fresh copy of it in a node (outside the timer).
static nlohmann::json buildPayload(int kind) {
    auto sprite = []() {
        return nlohmann::json{
            {"x", 123.5}, {"y", 678.25}, {"w", 32.0}, {"h", 32.0},
            {"rotation", 1.5708}, {"layer", 7}, {"color", 0xFF8040FFu},
            {"texture", 42}, {"asset", "tiles/grass_07"}, {"flipX", false},
            {"uv", {0.0, 0.0, 0.25, 0.25}}, {"tint", 0.85}
        };
    };
    if (kind == 0) return sprite();
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < 64; ++i) arr.push_back(sprite());
    return nlohmann::json{{"sprites", arr}};
}

// Drain every subscriber's inbox (outside the timed window).
static void drainAll(std::vector<std::shared_ptr<IntraIO>>& subs) {
    for (auto& s : subs) {
        while (s->hasMessages() > 0) s->pullAndDispatch();
    }
}

// Measure ns/publish for one (mode, N, payload) cell.
static double measureCell(bool coreResident, int n, const nlohmann::json& payload,
                          int M, int idTag) {
    auto& mgr = IntraIOManager::getInstance();
    const std::string pubId = "zcb_pub_" + std::to_string(idTag);
    const std::string topic = "zcb:topic:" + std::to_string(idTag);

    auto pub = mgr.createInstance(pubId, coreResident);
    std::vector<std::shared_ptr<IntraIO>> subs;
    subs.reserve(n);
    for (int i = 0; i < n; ++i) {
        auto s = mgr.createInstance("zcb_sub_" + std::to_string(idTag) + "_" + std::to_string(i));
        // Cheap handler: touch one field so the delivery + payload read aren't optimized away.
        s->subscribe(topic, [](const Message& m) { g_sink += m.data->getInt("layer", 0); });
        subs.push_back(std::move(s));
    }

    // Warmup (fills caches, lets the allocator settle) — not timed.
    for (int i = 0; i < 1000; ++i) {
        pub->publish(topic, std::make_unique<JsonDataNode>("p", payload));
    }
    drainAll(subs);

    // Timed: accumulate ONLY the publish() duration; drain in chunks outside the timer so
    // subscriber queues don't grow unbounded (which would skew memory/cache, not publish cost).
    constexpr int DRAIN_EVERY = 256;
    int64_t publishNs = 0;
    int sinceDrain = 0;
    for (int m = 0; m < M; ++m) {
        auto node = std::make_unique<JsonDataNode>("p", payload);   // built OUTSIDE the timer
        auto t0 = high_resolution_clock::now();
        pub->publish(topic, std::move(node));
        publishNs += duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
        if (++sinceDrain >= DRAIN_EVERY) { drainAll(subs); sinceDrain = 0; }
    }
    drainAll(subs);

    // Teardown the cell's instances so ids/queues don't leak into the next cell.
    for (int i = 0; i < n; ++i) mgr.removeInstance("zcb_sub_" + std::to_string(idTag) + "_" + std::to_string(i));
    mgr.removeInstance(pubId);

    return static_cast<double>(publishNs) / M;   // ns per publish()
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "IIO ZERO-COPY THROUGHPUT BENCHMARK  (ns per publish, lower = better)\n";
    std::cout << "================================================================================\n";
    std::cout << "publish() cost = route to N subscriber queues. Node built outside the timer.\n";
    std::cout << "  re-home   = default instance: 1 json copy into a core node, then shared.\n";
    std::cout << "  coreRes   = coreResident=true: shares the ORIGINAL node, 0 json copies.\n\n";

    spdlog::set_level(spdlog::level::off);
    // The manager + instances use NAMED loggers unaffected by the global level; silence them too,
    // else per-100-message routing-stats spam buries the table (the engine ctor's raw std::cout
    // "Created instance" lines we just live with — few, and filterable).
    IntraIOManager::getInstance().setLogLevel(spdlog::level::off);

    const int M = 40000;
    const std::vector<int> Ns = {1, 4, 16, 64};
    const char* payloadNames[2] = {"sprite (~12 fields)", "fat (64-sprite array)"};

    int idTag = 0;
    for (int kind = 0; kind < 2; ++kind) {
        nlohmann::json payload = buildPayload(kind);
        std::cout << "PAYLOAD: " << payloadNames[kind] << "   (M=" << M << " publishes/cell)\n";
        std::cout << "  " << std::setw(4) << "N"
                  << std::setw(14) << "re-home ns"
                  << std::setw(14) << "coreRes ns"
                  << std::setw(12) << "copy win"
                  << std::setw(16) << "re-home/sub"
                  << std::setw(16) << "coreRes/sub" << "\n";
        std::cout << "  " << std::string(72, '-') << "\n";
        for (int n : Ns) {
            double rehome = measureCell(/*coreResident=*/false, n, payload, M, idTag++);
            double zero   = measureCell(/*coreResident=*/true,  n, payload, M, idTag++);
            double win    = rehome / (zero > 0 ? zero : 1e-9);
            std::cout << "  " << std::setw(4) << n
                      << std::setw(14) << std::fixed << std::setprecision(1) << rehome
                      << std::setw(14) << zero
                      << std::setw(11) << std::setprecision(2) << win << "x"
                      << std::setw(16) << std::setprecision(1) << (rehome / n)
                      << std::setw(16) << (zero / n) << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "Reading the table:\n";
    std::cout << "  - 'copy win' = re-home / coreRes = cost of the ONE publish-side json copy that\n";
    std::cout << "    coreResident eliminates (ZC-4). It grows with payload size (sprite vs fat).\n";
    std::cout << "  - '/sub' columns = ns per subscriber. If these DROP as N grows, the per-subscriber\n";
    std::cout << "    marginal cost is a ref-count bump, not a json copy => O(1) fan-out (ZC-2/3).\n";
    std::cout << "  - g_sink (anti-elision): " << static_cast<long long>(g_sink) << "\n";
    std::cout << "================================================================================\n";
    return 0;
}
