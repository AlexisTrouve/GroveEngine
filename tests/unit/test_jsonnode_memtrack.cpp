// ============================================================================
// JsonNodeMemTrackUnit — proves grove::mem tracking is wired into JsonDataNode (FO2).
//
// QUOI  : create a JsonDataNode and read the tracker. When the build has GROVE_MEM_TRACKING on, the
//         node shows up live under the "iio:jsonnode" tag and disappears when freed; when off (the
//         default), nothing is recorded — the wiring is truly zero-cost.
//
// POURQUOI: B2's Tracker is proven in isolation; this proves it is actually WIRED into the IIO hot
//         path (JsonDataNode = the workhorse alloc), so a real leak hunt would see grove's own nodes.
//         Config-aware on GROVE_MEM_TRACKING (JsonDataNode.cpp's instrumentation + this TU share it).
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/JsonDataNode.h>
#include <grove/mem/Tracker.h>

TEST_CASE("JsonDataNode alloc/free is tracked iff GROVE_MEM_TRACKING is on", "[mem][iio][config]") {
    grove::mem::tracker().reset();

    auto* node = new grove::JsonDataNode("x", nlohmann::json::object());
    const auto rep = grove::mem::tracker().report();

#if defined(GROVE_MEM_TRACKING) && GROVE_MEM_TRACKING \
    && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    // Tracking build: the live node is visible under its tag.
    REQUIRE(rep["grove_mem"]["byTag"]["iio:jsonnode"]["count"] == 1);
    REQUIRE(grove::mem::tracker().stats().liveCount == 1);
    delete node;
    REQUIRE(grove::mem::tracker().stats().liveCount == 0);   // freed → no leak remains
#else
    // Default build: the wiring is compiled out — nothing recorded.
    REQUIRE(rep["grove_mem"]["byTag"].empty());
    delete node;
#endif

    grove::mem::tracker().reset();
}
