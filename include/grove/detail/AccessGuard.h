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

#if GROVE_DEBUG

// RAII tripwire. `active` is a per-guarded-object atomic counter of threads currently inside the
// section. On construction, if another thread is already inside (prev != 0) the invariant is broken:
// log + bump accessViolationCount(). On destruction, leave the section.
class ScopedAccessGuard {
public:
    ScopedAccessGuard(std::atomic<int>& active, const char* op, const std::string& instanceId);
    ~ScopedAccessGuard();

    ScopedAccessGuard(const ScopedAccessGuard&)            = delete;
    ScopedAccessGuard& operator=(const ScopedAccessGuard&) = delete;

private:
    std::atomic<int>& active_;
};

#else  // shipping: the guard is an empty no-op (zero cost, zero footprint).

class ScopedAccessGuard {
public:
    ScopedAccessGuard(std::atomic<int>& /*active*/, const char* /*op*/, const std::string& /*id*/) {}
};

#endif // GROVE_DEBUG

} // namespace detail
} // namespace grove
