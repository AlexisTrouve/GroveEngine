#pragma once

// ============================================================================
// ProfileZone.h — lightweight scoped CPU-timing zones (grove::profile).
//
// QUOI  : GROVE_PROFILE_ZONE("name") times the enclosing scope and accumulates it under a named
//         zone (total seconds + call count); Profiler::report() dumps all zones. The INSTRUMENTATION
//         half of a profiler — a game/tool visualizes; the engine just measures.
//
// POURQUOI: research flags "no real profiler" as GroveEngine's biggest debug-side gap vs Unity/Unreal.
//         This closes the cheap half: drop a one-line zone around any block to learn where frame time
//         goes. It is DEBUG-ONLY — the macro compiles to nothing in a shipping build (GROVE_DEBUG=OFF),
//         so a shipped binary pays zero cost (same policy as the engine's other debug instrumentation).
//
// COMMENT: pure (std + nlohmann), header-only. ScopedZone is RAII: it times ctor->dtor and adds to the
//         global Profiler on destruction, so nested zones each measure their own span. reset() is meant
//         to be called per frame (a rolling per-frame view) or per measurement window. Thread-safe via
//         one mutex; the timing itself is lock-free (only the add() at scope exit locks).
// ============================================================================

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include <grove/BuildConfig.h>   // GROVE_DEBUG — the profiler is debug-only (stripped in shipping)

namespace grove {
namespace profile {

// Accumulated timing for one named zone over the current window.
struct ZoneStat {
    double        totalSeconds = 0.0;
    std::uint64_t count        = 0;
};

// A registry of named zones. add() is called by ScopedZone at scope exit; queries snapshot under lock.
class Profiler {
public:
    void add(const char* name, double seconds) {
        std::lock_guard<std::mutex> lock(m_);
        auto& z = zones_[name ? name : ""];
        z.totalSeconds += seconds;
        z.count        += 1;
    }

    // Stats for one zone (a default {0,0} if never seen — so a stripped/never-run zone reads clean).
    ZoneStat zone(const std::string& name) const {
        std::lock_guard<std::mutex> lock(m_);
        auto it = zones_.find(name);
        return it == zones_.end() ? ZoneStat{} : it->second;
    }

    std::map<std::string, ZoneStat> snapshot() const {
        std::lock_guard<std::mutex> lock(m_);
        return zones_;
    }

    // Machine-readable dump: per-zone {seconds, count}. Root key "grove_profile".
    nlohmann::json report() const {
        std::lock_guard<std::mutex> lock(m_);
        nlohmann::json zones = nlohmann::json::object();
        for (const auto& kv : zones_) {
            zones[kv.first] = {{"seconds", kv.second.totalSeconds}, {"count", kv.second.count}};
        }
        return nlohmann::json{{"grove_profile", zones}};
    }

    // Clear all zones — call per frame for a rolling per-frame view, or per measurement window.
    void reset() {
        std::lock_guard<std::mutex> lock(m_);
        zones_.clear();
    }

private:
    mutable std::mutex m_;
    std::map<std::string, ZoneStat> zones_;
};

// The process-wide profiler the zone macro targets.
inline Profiler& profiler() {
    static Profiler p;
    return p;
}

// RAII scope timer: times construction -> destruction and folds the span into the named zone.
class ScopedZone {
public:
    explicit ScopedZone(const char* name) : name_(name), start_(Clock::now()) {}
    ~ScopedZone() {
        const double seconds = std::chrono::duration<double>(Clock::now() - start_).count();
        profiler().add(name_, seconds);
    }
    ScopedZone(const ScopedZone&)            = delete;
    ScopedZone& operator=(const ScopedZone&) = delete;

private:
    using Clock = std::chrono::high_resolution_clock;
    const char*       name_;
    Clock::time_point start_;
};

} // namespace profile
} // namespace grove

// ---- the zone macro ----------------------------------------------------------------------------
// GROVE_PROFILE_ZONE("name") times the enclosing block. DEBUG-only: compiles to nothing in a shipping
// build (GROVE_DEBUG=OFF). Uses a __LINE__-unique variable so several zones can nest/coexist in a scope.
#if GROVE_DEBUG
    #define GROVE_PROFILE_ZONE_CAT2(a, b) a##b
    #define GROVE_PROFILE_ZONE_CAT(a, b)  GROVE_PROFILE_ZONE_CAT2(a, b)
    #define GROVE_PROFILE_ZONE(name) \
        ::grove::profile::ScopedZone GROVE_PROFILE_ZONE_CAT(grove_zone_, __LINE__)(name)
#else
    #define GROVE_PROFILE_ZONE(name) ((void)0)
#endif
