// ============================================================================
// AccessGuardUnit — locks the concurrency tripwire (G1).
//
// QUOI  : ScopedAccessGuard must flag a CONCURRENT overlap (a second thread enters while the first
//         is still inside) and must NOT flag a sequential handoff (thread A leaves, then thread B
//         enters). In a shipping build the guard is a no-op.
//
// POURQUOI: this tripwire is what turns the "one owning thread per instance" invariant from a
//         silent-heap-corruption footgun into a loud, actionable failure. A deterministic overlap
//         (forced via an atomic handshake — no timing luck) proves it bites exactly on the hazard.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/detail/AccessGuard.h>

#include <atomic>
#include <thread>

TEST_CASE("ScopedAccessGuard flags concurrent overlap, not sequential handoff", "[iio][guard][config]") {
#if GROVE_DEBUG
    std::atomic<int> active{0};
    std::atomic<bool> t1Inside{false};
    std::atomic<bool> release{false};
    const auto before = grove::detail::accessViolationCount().load();

    // Thread 1 enters the guarded section and HOLDS it until released.
    std::thread t1([&] {
        grove::detail::ScopedAccessGuard g(active, "publish", "inst-X");
        t1Inside.store(true);
        while (!release.load()) std::this_thread::yield();
    });
    while (!t1Inside.load()) std::this_thread::yield();

    // t1 is provably inside → entering here is a real OVERLAP → must be flagged exactly once.
    { grove::detail::ScopedAccessGuard g2(active, "publish", "inst-X"); }
    const auto afterOverlap = grove::detail::accessViolationCount().load();
    REQUIRE(afterOverlap == before + 1);

    release.store(true);
    t1.join();
    REQUIRE(active.load() == 0);   // both left the section cleanly

    // Sequential entry (nobody else inside) must NOT be flagged.
    { grove::detail::ScopedAccessGuard g3(active, "publish", "inst-X"); }
    REQUIRE(grove::detail::accessViolationCount().load() == afterOverlap);
#else
    // Shipping: the guard is an empty no-op — it never counts anything.
    std::atomic<int> active{0};
    const auto before = grove::detail::accessViolationCount().load();
    { grove::detail::ScopedAccessGuard g(active, "publish", "x"); }
    REQUIRE(grove::detail::accessViolationCount().load() == before);
#endif
}
