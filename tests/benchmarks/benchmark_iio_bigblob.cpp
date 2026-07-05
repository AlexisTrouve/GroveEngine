/**
 * IIO Big-Blob Routing Benchmark — localize Drifterra's "~39 ms IIO tax".
 * ==================================================================================
 * WHAT: reproduces Drifterra's MEASURED message profile for a swarm scene and splits the
 *   per-frame cost into (a) publish (build blob + route to the consumer's queue) and
 *   (b) drain (pullAndDispatch = dequeue + topic-match + HANDLER BODY). Two consumer
 *   variants: an EMPTY handler (pure engine machinery) and a per-agent DESERIALIZE handler
 *   (getBlob + iterate N — what a real consumer does). Ramps N = agents.
 *
 * WHY: Drifterra's differential test found ~39 ms/frame @4054 agents in engine.step,
 *   UNATTRIBUTED to sim/flood/render/scene, and only ~28 messages/frame (NOT per-agent) —
 *   modules are registerStaticModule (coreResident) and render is a single blob. Their
 *   hypothesis: the IntraIOManager route / deep-copy of the big blobs, or the per-message
 *   pullAndDispatch machinery. The engine code says otherwise (shared_ptr end-to-end, no blob
 *   deep-copy, O(messages)≈28), so the only O(N) thing is the consumer HANDLER BODY. Doctrine:
 *   re-prove by measurement, not by reading. This benchmark IS the measurement.
 *
 * THE READ: if the EMPTY-handler frame is ~sub-ms at 4054 while the per-agent-handler frame is
 *   ~tens of ms, the engine machinery is EXONERATED — the ~39 ms is the consumer's per-agent
 *   handler work (deserializing the blobs), which runs inside pullAndDispatch (inside
 *   engine.step) but is APPLICATION code, not IntraIOManager. If the empty-handler frame is
 *   itself ~39 ms, the machinery is the wall and we profile the engine.
 *
 * PROFILE (per frame, ~28 messages): 4 big blobs sized to N —
 *   move:state 5 floats/agent · render 8 · walk 3 · pos 2  (≈ 283 KB/frame @4054, matches the
 *   80+128+45+30 KB reported) — plus 24 small json messages (events/triggers) to hit ~28 total.
 *   Publisher + consumer are BOTH coreResident (mimics two registerStaticModule modules).
 *
 * RUN: ./tests/benchmark_iio_bigblob   (NOT a ctest — wall-clock, run by hand.)
 */

#include "grove/IntraIOManager.h"
#include "grove/IntraIO.h"
#include "grove/JsonDataNode.h"
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace grove;
using namespace std::chrono;

namespace {

// Anti-elision sink the consumer handlers feed (so the blob read/dispatch isn't optimized away).
volatile double g_sink = 0.0;

// The 4 big-blob topics + their floats-per-agent (models move:state/render/walk/pos byte sizes).
struct BlobTopic { const char* topic; int floatsPerAgent; };
const BlobTopic BLOBS[4] = {
    {"move:state", 5}, {"render", 8}, {"walk", 3}, {"pos", 2},
};
constexpr int SMALL_MSGS = 24;   // small json events/triggers → ~28 messages/frame total

// Build a blob-backed node: `floatsPerAgent * n` raw floats under setBlob("data", ...). Bytes are
// deterministic (i*.5) so the per-agent deserialize handler has real data to sum. Built fresh each
// publish (setBlob copies) — exactly like a swarm module rebuilding its state blob every frame.
std::unique_ptr<JsonDataNode> makeBlobNode(const char* topic, int floatsPerAgent, int n,
                                           std::vector<float>& scratch) {
    const size_t count = size_t(floatsPerAgent) * size_t(n);
    scratch.resize(count);
    for (size_t i = 0; i < count; ++i) scratch[i] = float(i) * 0.5f;
    auto node = std::make_unique<JsonDataNode>(topic);
    node->setInt("count", n);
    node->setBlob("data", reinterpret_cast<const uint8_t*>(scratch.data()), count * sizeof(float));
    return node;
}

// One measured cell: N agents, a chosen consumer handler. Returns avg publish ms + drain ms / frame.
struct Cell { double publishMs; double drainMs; };

Cell measure(int n, int frames, bool perAgentHandler, int idTag) {
    auto& mgr = IntraIOManager::getInstance();
    const std::string pubId = "bbb_pub_" + std::to_string(idTag);
    const std::string subId = "bbb_sub_" + std::to_string(idTag);

    // BOTH coreResident = two registerStaticModule modules (publish shares the node, 0 copies).
    auto pub = mgr.createInstance(pubId, /*coreResident=*/true);
    auto sub = mgr.createInstance(subId, /*coreResident=*/true);

    // The consumer subscribes to every blob topic + the small topic. EMPTY handler = pure machinery;
    // per-agent handler = getBlob + iterate all N floats (a realistic deserialize on receipt).
    auto emptyH    = [](const Message&) { g_sink += 1.0; };
    auto peragentH = [](const Message& m) {
        const auto* b = m.data->getBlob("data");
        if (!b) return;
        const float* f = reinterpret_cast<const float*>(b->data());
        const size_t k = b->size() / sizeof(float);
        double acc = 0.0;
        for (size_t i = 0; i < k; ++i) acc += f[i];   // O(N) work in the handler body
        g_sink += acc;
    };
    for (const auto& bt : BLOBS) {
        if (perAgentHandler) sub->subscribe(bt.topic, peragentH);
        else                 sub->subscribe(bt.topic, emptyH);
    }
    sub->subscribe("evt", emptyH);

    std::vector<float> scratch;
    auto drain = [&] { while (sub->hasMessages() > 0) sub->pullAndDispatch(); };

    auto publishFrame = [&] {
        for (const auto& bt : BLOBS) pub->publish(bt.topic, makeBlobNode(bt.topic, bt.floatsPerAgent, n, scratch));
        for (int i = 0; i < SMALL_MSGS; ++i) {
            auto e = std::make_unique<JsonDataNode>("evt");
            e->setInt("id", i); e->setDouble("v", double(i) * 1.5);
            pub->publish("evt", std::move(e));
        }
    };

    // Warmup (not timed).
    for (int f = 0; f < 64; ++f) { publishFrame(); drain(); }

    int64_t pubNs = 0, drainNs = 0;
    for (int f = 0; f < frames; ++f) {
        auto t0 = high_resolution_clock::now();
        publishFrame();
        auto t1 = high_resolution_clock::now();
        drain();
        auto t2 = high_resolution_clock::now();
        pubNs   += duration_cast<nanoseconds>(t1 - t0).count();
        drainNs += duration_cast<nanoseconds>(t2 - t1).count();
    }

    mgr.removeInstance(subId);
    mgr.removeInstance(pubId);

    return { double(pubNs) / frames / 1e6, double(drainNs) / frames / 1e6 };
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    spdlog::set_level(spdlog::level::off);
    IntraIOManager::getInstance().setLogLevel(spdlog::level::off);

    std::printf("================================================================================\n");
    std::printf("IIO BIG-BLOB ROUTING BENCHMARK — where is Drifterra's ~39 ms/frame?\n");
    std::printf("================================================================================\n");
    std::printf("Per frame: 4 big blobs (move:state/render/walk/pos, sized to N) + %d small = ~%d msgs.\n",
                SMALL_MSGS, SMALL_MSGS + 4);
    std::printf("Both instances coreResident (= two registerStaticModule modules; publish = 0 copies).\n");
    std::printf("  publish ms = build blob (setBlob copy) + route to the consumer queue (IntraIOManager).\n");
    std::printf("  drain ms   = pullAndDispatch (dequeue + topic-match + HANDLER BODY).\n\n");

    const std::vector<int> Ns = {480, 1000, 2000, 4054};
    const int frames = 2000;

    std::printf("  %-7s | %-24s | %-28s\n", "N", "EMPTY handler (machinery)", "PER-AGENT handler (deserialize)");
    std::printf("  %-7s | %-11s %-11s | %-13s %-13s\n", "", "publish ms", "drain ms", "publish ms", "drain ms");
    std::printf("  %s\n", std::string(74, '-').c_str());

    int idTag = 0;
    for (int n : Ns) {
        Cell empt = measure(n, frames, /*perAgentHandler=*/false, idTag++);
        Cell pera = measure(n, frames, /*perAgentHandler=*/true,  idTag++);
        std::printf("  %-7d | %-11.3f %-11.3f | %-13.3f %-13.3f\n",
                    n, empt.publishMs, empt.drainMs, pera.publishMs, pera.drainMs);
    }

    std::printf("\nReading it:\n");
    std::printf("  - EMPTY-handler drain = the PURE engine machinery (dequeue+match+empty dispatch).\n");
    std::printf("    If it stays ~sub-ms as N grows, IntraIOManager route/pull is NOT the ~39 ms.\n");
    std::printf("  - publish ms = build(setBlob copy)+route. If ~sub-ms at 4054, the blob is not deep-\n");
    std::printf("    copied on the bus (shared_ptr) — only the swarm's own setBlob build shows here.\n");
    std::printf("  - PER-AGENT drain - EMPTY drain = the O(N) HANDLER BODY (deserialize). If THAT is the\n");
    std::printf("    tens of ms, the ~39 ms is the consumer's per-agent work, not the engine machinery.\n");
    std::printf("  - g_sink (anti-elision): %.1f\n", double(g_sink));
    std::printf("================================================================================\n");
    return 0;
}
