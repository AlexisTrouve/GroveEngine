#pragma once

// ============================================================================
// CrashContext.h — the pure, engine-agnostic crash-report payload (grove::crash).
//
// QUOI  : a plain-data snapshot of "what the engine was doing" at (or just before) a crash —
//         clock, frame, module topology, and THE LAST N IIO MESSAGES — plus serializers to a
//         stable JSON report and a one-line triage summary.
//
// POURQUOI: a minidump gives you the native call stack, but for a message-bus engine the single
//         most valuable post-mortem artifact is the SEQUENCE OF MESSAGES that led to the fault —
//         which a minidump can't show. We capture that (from the ReplaySink, filled engine-side)
//         next to the dump. This header is the "what gets written"; it is deliberately PURE
//         (std + nlohmann only, like grove::save / grove::anim) so it has zero platform coupling
//         and is fully unit-testable WITHOUT actually crashing — the platform handler (B1b) and
//         the engine wiring (B1c) only fill it and hand it to a writer.
//
// COMMENT: no dependency on ReplaySink / EngineClock — the engine reads those and populates the
//         struct, keeping grove::crash free of engine internals. `debugBuild` defaults to the
//         active build config (BuildConfig.h) so a report always records whether it came from a
//         debug or a shipping binary — the first thing you check when triaging a field crash.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <grove/BuildConfig.h>   // grove::kDebugBuild — stamp the report with the build config

namespace grove {
namespace crash {

// One IIO message, summarized for the crash trail. NO payload — just the ordering/identity
// metadata (topic + Envelope fields) so the trail is cheap to capture and safe to write from a
// crash handler. Maps 1:1 from a ReplaySink ReplayEvent (topic + env.{source,tick,seq,lamport}).
struct MessageTrace {
    std::string topic;      // where it was published (e.g. "render:sprite")
    std::string source;     // which module/socket published it
    uint64_t    tick = 0;   // coarse epoch (engine clock tick when published)
    uint64_t    seq = 0;    // per-source sequence number
    uint64_t    lamport = 0;// causal order across sources
};

// The full engine snapshot written alongside the minidump. Pure data; every field is filled by
// the engine (B1c). Anything the native dump already has (registers, stack) is deliberately NOT
// duplicated here — this carries only the ENGINE-LEVEL state a dump can't reconstruct.
struct CrashContext {
    std::string reason;                     // signal / exception description (e.g. "SIGSEGV", "EXCEPTION_ACCESS_VIOLATION")

    // Engine clock (the authoritative sim time — see EngineClock).
    uint64_t tick = 0;
    double   simTime = 0.0;
    double   timeScale = 1.0;
    bool     paused = false;

    uint64_t frameCount = 0;                // step() count at crash time

    std::vector<std::string> moduleNames;   // registered module topology

    // The last-N IIO messages before the crash, OLDEST -> NEWEST (the killer diagnostic: the
    // event sequence that led to the fault). Bounded engine-side (pull the tail of the timeline).
    std::vector<MessageTrace> recentMessages;

    bool debugBuild = grove::kDebugBuild;   // which build produced this report (debug vs shipping)
};

// Serialize to the stable JSON report written next to the minidump. Root key "grove_crash" so a
// report file is self-identifying. Ordering of recentMessages is preserved (oldest -> newest).
inline nlohmann::json toJson(const CrashContext& c) {
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& m : c.recentMessages) {
        msgs.push_back({
            {"topic", m.topic}, {"source", m.source},
            {"tick", m.tick}, {"seq", m.seq}, {"lamport", m.lamport}
        });
    }
    return nlohmann::json{{"grove_crash", {
        {"reason", c.reason},
        {"build", c.debugBuild ? "debug" : "shipping"},
        {"clock", {{"tick", c.tick}, {"simTime", c.simTime}, {"timeScale", c.timeScale}, {"paused", c.paused}}},
        {"frameCount", c.frameCount},
        {"modules", c.moduleNames},
        {"recentMessages", msgs},
    }}};
}

// One-line human triage string for the crash LOG (the JSON is the machine artifact; this is the
// line an operator reads first). Names the build, reason, frame/tick, module count, and the very
// last message seen — usually the most-implicated event.
inline std::string summary(const CrashContext& c) {
    const std::string last = c.recentMessages.empty()
        ? std::string("(none)")
        : (c.recentMessages.back().topic + "@" + c.recentMessages.back().source);
    return "CRASH [" + std::string(c.debugBuild ? "debug" : "shipping") + "] reason='" + c.reason
         + "' frame=" + std::to_string(c.frameCount)
         + " tick=" + std::to_string(c.tick)
         + " modules=" + std::to_string(c.moduleNames.size())
         + " lastMsg=" + last;
}

} // namespace crash
} // namespace grove
