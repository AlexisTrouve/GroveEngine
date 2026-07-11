/**
 * test_iio_perf_gate — an anti-regression PERF GATE for the IIO zero-copy fan-out (quality Phase 4).
 *
 * WHAT : guards the load-bearing zero-copy win — publishing to N subscribers is O(1) in payload copies
 *        (a ref-count bump per subscriber), NOT O(N) json deep-copies. If someone reintroduces a
 *        per-subscriber payload copy, this gate fails.
 * WHY  : the zero-copy delivery shipped a real "O(N)->O(1) fan-out" win (see rendering-throughput-handoff.md
 *        + benchmark_iio_zerocopy.cpp). A perf win with no gate silently rots. The full A/B benchmark stays a
 *        by-hand release tool; this is the committed regression guard.
 * HOW  : a MACHINE-INDEPENDENT RATIO, not an absolute ns ceiling (which would flake across machines/builds).
 *        We publish the SAME fat payload to N=1 and to N=HI subscribers and compare the per-SUBSCRIBER cost:
 *          - O(1) fan-out  => the fixed publish cost amortizes over subscribers, so ns/pub/sub DROPS as N grows.
 *          - O(N) copying  => each subscriber pays one fat-json copy, so ns/pub/sub stays ~FLAT.
 *        We require ns/pub/sub(N=HI) to drop to < HALF of ns/pub/sub(N=1). The real drop is ~10x+; a
 *        per-subscriber-copy regression keeps the ratio near 1.0 -> gate fails. Huge separation, no flake.
 *        The node is built OUTSIDE the timer (that cost is identical for both N), isolating publish+route.
 *
 * On any platform this is a real gate (pure timing, no sanitizer needed). Plain main() (exit 1 on regression).
 */

#include "grove/IntraIOManager.h"
#include "grove/IntraIO.h"
#include "grove/JsonDataNode.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace grove;
using namespace std::chrono;

// Accumulator the subscriber handlers feed, so the compiler can't elide the delivery/read.
static volatile int64_t g_sink = 0;

// A "fat" payload — a 64-element array of sprite-like objects (a batch / state snapshot). Fat on purpose:
// a per-subscriber COPY of this is expensive + unmistakable, so a fan-out regression stands out sharply.
static nlohmann::json buildFatPayload() {
    auto sprite = [] {
        return nlohmann::json{{"x", 123.5}, {"y", 678.25}, {"w", 32.0}, {"h", 32.0}, {"rotation", 1.5708},
                              {"layer", 7}, {"color", 0xFF8040FFu}, {"texture", 42}, {"asset", "tiles/grass_07"},
                              {"flipX", false}, {"uv", {0.0, 0.0, 0.25, 0.25}}, {"tint", 0.85}};
    };
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < 64; ++i) arr.push_back(sprite());
    return nlohmann::json{{"sprites", arr}, {"batchLayer", 9}};   // top-level scalar the handler reads (anti-elision)
}

// ns per publish() for one fan-out width N. Node built OUTSIDE the timer; queues drained OUTSIDE the timer.
static double measure(int n, const nlohmann::json& payload, int M, int idTag) {
    auto& mgr = IntraIOManager::getInstance();
    const std::string pubId = "perfgate_pub_" + std::to_string(idTag);
    const std::string topic = "perfgate:topic:" + std::to_string(idTag);
    auto pub = mgr.createInstance(pubId);                       // DEFAULT (re-home) publisher — the common path
    std::vector<std::shared_ptr<IntraIO>> subs;
    subs.reserve(n);
    for (int i = 0; i < n; ++i) {
        auto s = mgr.createInstance("perfgate_sub_" + std::to_string(idTag) + "_" + std::to_string(i));
        s->subscribe(topic, [](const Message& m) { g_sink += m.data->getInt("batchLayer", 0); });
        subs.push_back(std::move(s));
    }
    auto drain = [&] { for (auto& s : subs) while (s->hasMessages() > 0) s->pullAndDispatch(); };

    for (int i = 0; i < 500; ++i) pub->publish(topic, std::make_unique<JsonDataNode>("p", payload));   // warmup
    drain();

    int64_t publishNs = 0, sinceDrain = 0;
    for (int m = 0; m < M; ++m) {
        auto node = std::make_unique<JsonDataNode>("p", payload);   // built OUTSIDE the timer
        auto t0 = high_resolution_clock::now();
        pub->publish(topic, std::move(node));
        publishNs += duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
        if (++sinceDrain >= 256) { drain(); sinceDrain = 0; }
    }
    drain();

    for (int i = 0; i < n; ++i) mgr.removeInstance("perfgate_sub_" + std::to_string(idTag) + "_" + std::to_string(i));
    mgr.removeInstance(pubId);
    return static_cast<double>(publishNs) / M;   // ns per publish()
}

int main() {
    spdlog::set_level(spdlog::level::off);
    IntraIOManager::getInstance().setLogLevel(spdlog::level::off);

    constexpr int M = 6000;       // enough for a stable ratio (~30x drop), fast enough for a gate (~6s)
    constexpr int N_HI = 32;      // wide fan-out to expose per-subscriber cost

    const nlohmann::json fat = buildFatPayload();
    const double ns1  = measure(1,    fat, M, 1);
    const double nsHi = measure(N_HI, fat, M, 2);

    const double perSub1  = ns1;              // ns per publish per subscriber at N=1
    const double perSubHi = nsHi / N_HI;      // ... at N=HI

    std::fprintf(stdout,
        "IIOPerfGate (fat payload, M=%d): ns/pub  N=1: %.0f  N=%d: %.0f  |  ns/pub/sub  N=1: %.0f  N=%d: %.0f  (drop %.1fx)\n",
        M, ns1, N_HI, nsHi, perSub1, N_HI, perSubHi, perSub1 / (perSubHi > 0 ? perSubHi : 1e-9));

    // THE INVARIANT: the per-subscriber marginal cost must DROP as fan-out grows (O(1) fan-out). A reintroduced
    // per-subscriber json copy would keep it ~flat. Require it to fall below HALF — real drop is ~10x+, a
    // regression is ~1.0x, so this 2x threshold is huge headroom that still catches the regression class.
    if (!(perSubHi < 0.5 * perSub1)) {
        std::fprintf(stderr,
            "FAIL: IIO fan-out is not O(1) — ns/pub/sub did not drop with N (N=1: %.0f, N=%d: %.0f). "
            "A per-subscriber payload copy may have been reintroduced.\n", perSub1, N_HI, perSubHi);
        std::fprintf(stderr, "g_sink=%lld\n", static_cast<long long>(g_sink));
        return 1;
    }
    std::fprintf(stdout, "IIOPerfGate: PASS — fan-out is O(1) (per-subscriber cost amortizes). g_sink=%lld\n",
                 static_cast<long long>(g_sink));
    return 0;
}
