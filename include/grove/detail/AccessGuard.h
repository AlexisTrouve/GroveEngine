#pragma once

// ============================================================================
// AccessGuard.h — a debug tripwire for the "one owning thread per instance" invariant.
//
// QUOI  : ScopedAccessGuard asserts that a guarded section is entered by AT MOST ONE THREAD AT A
//         TIME. It is NOT a lock — on a concurrent OVERLAP it logs an actionable error (op / instance
//         / thread) and bumps a process-wide violation counter, then lets execution continue.
//
// POURQUOI: some engine objects (e.g. IntraIO) are contractually single-owner-thread (each module
//         owns its IIO instance, driven by ONE worker). Concurrent access to ONE instance is a data
//         race that corrupts the heap in a release build — SILENTLY. A lock would hide the design
//         flaw (and risk the documented ABBA deadlock); this tripwire SURFACES it loudly in debug so
//         the violating call site is found and fixed, per the engineering law "a hard invariant is
//         checked at EVERY step with an actionable report (what / when / where / who)".
//
// COMMENT: detects OVERLAP, not thread identity — sequential handoff of an instance between threads
//         (never concurrent) is fine and does NOT trip it. Debug-only: in a shipping build the guard
//         is an empty no-op (zero cost). accessViolationCount() is a single process-wide counter
//         (used by tests + optional monitoring).
// ============================================================================

#include <atomic>
#include <cstdint>
#include <string>

#include <grove/BuildConfig.h>

namespace grove {
namespace detail {

// Process-wide count of detected concurrency-invariant violations (single instance across TUs).
std::atomic<std::uint64_t>& accessViolationCount();

// Per-guarded-object state: how many frames are inside, and WHICH thread owns them.
// `owner` holds a hash of the thread id currently inside (0 = nobody). Declared in both builds
// because guarded objects hold it as a member; in shipping nothing ever reads it.
struct AccessState {
    std::atomic<int> active{0};
    std::atomic<std::size_t> owner{0};
};

#if GROVE_DEBUG

// RAII tripwire. On construction: if the section was empty, claim it for this thread. If it was
// already occupied BY ANOTHER thread, the invariant is broken (log + bump accessViolationCount()).
// If it was occupied by THIS thread, it is a legitimate re-entrance and nothing is reported.
class ScopedAccessGuard {
public:
    ScopedAccessGuard(AccessState& state, const char* op, const std::string& instanceId);
    ~ScopedAccessGuard();

    ScopedAccessGuard(const ScopedAccessGuard&)            = delete;
    ScopedAccessGuard& operator=(const ScopedAccessGuard&) = delete;

private:
    AccessState& state_;
};

#else  // shipping: the guard is an empty no-op (zero cost, zero footprint).

class ScopedAccessGuard {
public:
    ScopedAccessGuard(AccessState& /*state*/, const char* /*op*/, const std::string& /*id*/) {}
};

#endif // GROVE_DEBUG

} // namespace detail
} // namespace grove
