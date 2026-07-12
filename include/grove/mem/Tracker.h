#pragma once

// ============================================================================
// Tracker.h — a lightweight, tagged allocation-tracking layer (grove::mem).
//
// QUOI  : record each tracked allocation (pointer + size + a stable string tag) and its free, so at
//         any point you can dump WHAT IS STILL LIVE grouped by tag — i.e. a leak report. Plus a pair
//         of opt-in macros (GROVE_MEM_TRACK_ALLOC / _FREE) that hot paths sprinkle at their own
//         alloc/free sites; they compile to NOTHING unless GROVE_MEM_TRACKING is on.
//
// POURQUOI: "have the tracking layer READY before you ship, not after the leak is reported" — you
//         cannot attach a profiler to a retail build, so the instrumentation must already be there,
//         dormant, to switch on. We deliberately do NOT override the global operator new (that
//         captures every dependency's allocations and fights the sanitizers); instead grove tags its
//         OWN hot allocations, so the report tells you WHERE grove leaks (which topic, which batch),
//         not just "something leaked".
//
// COMMENT: pure (std + nlohmann), header-only, like grove::save / grove::crash. Thread-safe via one
//         mutex; O(1) per alloc/free (a hash-map insert/erase). The tag is a borrowed string literal
//         (not owned — pass "iio:msg", "sprite:batch", ...). The macros gate off under sanitizers
//         too (ASan already tracks allocations — don't double-count / fight it).
// ============================================================================

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace grove {
namespace mem {

// Per-allocation record (kept while the allocation is live). `tag` is a borrowed stable string.
struct AllocInfo {
    std::size_t size = 0;
    const char* tag  = "";
    std::uint64_t seq = 0;   // allocation order (for "oldest live" triage)
};

// Records live allocations by pointer; the set of still-live records IS the leak report.
class Tracker {
public:
    // Record a tracked allocation. No-op on a null pointer. Overwrites any prior record for the same
    // pointer (a reused address after a missed free — the newer wins).
    void onAlloc(const void* p, std::size_t size, const char* tag) {
        if (p == nullptr) return;
        std::lock_guard<std::mutex> lock(m_);
        auto it = live_.find(p);
        if (it != live_.end()) { liveBytes_ -= it->second.size; }  // reused address: drop the stale size
        live_[p] = AllocInfo{size, tag ? tag : "", ++seq_};
        liveBytes_ += size;
        ++totalAllocs_;
    }

    // Record a free. No-op on null or an untracked pointer (a free of something we never tracked).
    void onFree(const void* p) {
        if (p == nullptr) return;
        std::lock_guard<std::mutex> lock(m_);
        auto it = live_.find(p);
        if (it == live_.end()) return;
        liveBytes_ -= it->second.size;
        live_.erase(it);
        ++totalFrees_;
    }

    struct Stats {
        std::size_t   liveCount = 0;
        std::size_t   liveBytes = 0;
        std::uint64_t totalAllocs = 0;
        std::uint64_t totalFrees = 0;
    };

    Stats stats() const {
        std::lock_guard<std::mutex> lock(m_);
        return Stats{live_.size(), liveBytes_, totalAllocs_, totalFrees_};
    }

    // Live bytes per tag — the leak report grouped the way you triage it.
    std::map<std::string, std::size_t> liveBytesByTag() const {
        std::lock_guard<std::mutex> lock(m_);
        std::map<std::string, std::size_t> out;
        for (const auto& kv : live_) out[kv.second.tag] += kv.second.size;
        return out;
    }

    // Machine-readable leak report: totals + per-tag {count, bytes}. Root key "grove_mem".
    nlohmann::json report() const {
        std::lock_guard<std::mutex> lock(m_);
        std::map<std::string, std::pair<std::size_t, std::size_t>> byTag;  // tag -> {count, bytes}
        for (const auto& kv : live_) {
            auto& e = byTag[kv.second.tag];
            e.first  += 1;
            e.second += kv.second.size;
        }
        nlohmann::json tags = nlohmann::json::object();
        for (const auto& t : byTag) {
            tags[t.first] = {{"count", t.second.first}, {"bytes", t.second.second}};
        }
        return nlohmann::json{{"grove_mem", {
            {"liveCount", live_.size()},
            {"liveBytes", liveBytes_},
            {"totalAllocs", totalAllocs_},
            {"totalFrees", totalFrees_},
            {"byTag", tags},
        }}};
    }

    // Clear all state (a fresh tracking session). Mainly for tests / between measurement windows.
    void reset() {
        std::lock_guard<std::mutex> lock(m_);
        live_.clear();
        liveBytes_ = 0;
        seq_ = 0;
        totalAllocs_ = 0;
        totalFrees_ = 0;
    }

private:
    mutable std::mutex m_;
    std::unordered_map<const void*, AllocInfo> live_;
    std::size_t   liveBytes_   = 0;
    std::uint64_t seq_         = 0;
    std::uint64_t totalAllocs_ = 0;
    std::uint64_t totalFrees_  = 0;
};

// The process-wide tracker the macros target (a hot path can't thread a Tracker instance to its
// alloc site). Function-local static → safe, lazy, header-only.
inline Tracker& tracker() {
    static Tracker t;
    return t;
}

} // namespace mem
} // namespace grove

// ---- opt-in instrumentation macros --------------------------------------------------------------
// Sprinkle at grove's own alloc/free sites. ZERO cost unless GROVE_MEM_TRACKING is defined non-zero,
// and always stripped under a sanitizer build (ASan already tracks allocations — don't fight it).
#if defined(GROVE_MEM_TRACKING) && GROVE_MEM_TRACKING \
    && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    #define GROVE_MEM_TRACK_ALLOC(p, size, tag) ::grove::mem::tracker().onAlloc((p), (size), (tag))
    #define GROVE_MEM_TRACK_FREE(p)             ::grove::mem::tracker().onFree((p))
#else
    #define GROVE_MEM_TRACK_ALLOC(p, size, tag) ((void)0)
    #define GROVE_MEM_TRACK_FREE(p)             ((void)0)
#endif
