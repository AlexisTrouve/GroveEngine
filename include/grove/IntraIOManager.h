#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <shared_mutex>  // For shared_mutex (C++17)
#include <thread>
#include <chrono>
#include <atomic>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "IIO.h"
#include "ReplaySink.h"
#include <topictree/TopicTree.h>

using json = nlohmann::json;

namespace grove {

class IntraIO; // Forward declaration
class IIntraIODelivery; // Forward declaration

// Factory function for creating IntraIO (defined in IntraIO.cpp to avoid circular include)
std::shared_ptr<IntraIO> createIntraIOInstance(const std::string& instanceId, bool coreResident = false);

/**
 * @brief Central router for IntraIO instances
 *
 * IntraIOManager coordinates message passing between multiple IntraIO instances.
 * Each module gets its own IntraIO instance, and the manager handles routing
 * messages between them based on subscriptions.
 *
 * Architecture:
 * - One IntraIO instance per module (isolation)
 * - Central routing of messages between instances
 * - Pattern-based subscription matching
 * - Thread-safe operations
 *
 * Performance:
 * - Direct memory routing (no serialization)
 * - Pattern caching for fast lookup
 * - Batched delivery for efficiency
 */
class IntraIOManager {
private:
    std::shared_ptr<spdlog::logger> logger;
    mutable std::shared_mutex managerMutex;  // Reader-writer lock for instances

    // Registry of IntraIO instances
    std::unordered_map<std::string, std::shared_ptr<IIntraIODelivery>> instances;

    // Subscription info for each instance
    struct SubscriptionInfo {
        std::string instanceId;
        bool isLowFreq;
        int batchInterval; // milliseconds
    };

    // Ultra-fast topic routing using TopicTree
    topictree::TopicTree<std::string> topicTree;  // Maps patterns to instanceIds

    // Track subscription info per instance (for management)
    std::unordered_map<std::string, std::vector<std::string>> instancePatterns;  // instanceId -> patterns
    std::unordered_map<std::string, SubscriptionInfo> subscriptionInfoMap;  // pattern -> subscription info

    // Batching for low-frequency subscriptions
    struct BatchBuffer {
        std::string instanceId;
        std::string pattern;
        int batchInterval;
        std::chrono::steady_clock::time_point lastFlush;
        // topic + SHARED payload pairs (zero-copy buffered until flush; the json is extracted from
        // each shared node only at flush time when the coalesced batch array is built).
        std::vector<std::pair<std::string, std::shared_ptr<const IDataNode>>> messages;
    };
    std::unordered_map<std::string, BatchBuffer> batchBuffers; // pattern -> buffer
    mutable std::mutex batchMutex;
    std::thread batchThread;
    std::atomic<bool> batchThreadRunning{false};

    // FIX: [BUG E] -- CV + dedicated mutex for interruptible sleep in batchFlushLoop.
    // batchCVMutex is separate from batchMutex (shared_mutex) because condition_variable
    // requires a plain std::mutex. Destructor notifies batchCV so the thread wakes
    // immediately instead of waiting up to 100ms for sleep_for to expire.
    std::condition_variable batchCV;
    std::mutex batchCVMutex;

    void batchFlushLoop();
    void flushBatchBuffer(BatchBuffer& buffer);
    void flushBatchBufferSafe(BatchBuffer& buffer);  // Safe version - no nested locks

    // Statistics
    mutable std::atomic<size_t> totalRoutedMessages{0};
    mutable std::atomic<size_t> totalRoutes{0};

    // Current engine-time snapshot for envelope stamping (IO contract §5/§6). The engine pushes
    // (tick, simTime) once per step() via setSimTime(); routeMessage() reads them to stamp the
    // envelope. Atomics (not an EngineClock pointer) so a worker-thread routeMessage NEVER races
    // the engine thread advancing the clock. The pair may tear by ~one step at a frame boundary —
    // harmless (simTime == tick·dt; a one-frame skew on a coarse replay axis).
    std::atomic<uint64_t> m_currentTick{0};
    std::atomic<double>   m_currentSimTime{0.0};

    // Batched logging (pour éviter spam)
    static constexpr size_t LOG_BATCH_SIZE = 100;
    mutable std::atomic<size_t> messagesSinceLastLog{0};

    // Structured replay sink (IO contract §8, part 3): OPT-IN capture of the stamped control-plane stream.
    // routeMessage() taps it once, right after completing the envelope. Off by default — when disabled it
    // costs a single atomic-bool check per routed message (see ReplaySink::record). Its own mutex, independent
    // of the routing locks (no lock-ordering cycle).
    ReplaySink m_replaySink;

    // -------------------------------------------------------------------------
    // Destruction sentinel -- set to true at the very end of ~IntraIOManager().
    // Purpose: allows IntraIO::~IntraIO() (and any other post-exit code) to
    // detect that the singleton is gone before calling getInstance() again,
    // preventing use-after-free crashes during static-destruction ordering
    // (especially on Windows/MinGW where destruction order is not guaranteed).
    // -------------------------------------------------------------------------
    static std::atomic<bool> s_destroyed;

    // -------------------------------------------------------------------------
    // Live-instance pointer -- non-null ONLY while a manager object is fully
    // alive (set at the end of the constructor, cleared at the very start of the
    // destructor). Purpose: let callers act on the singleton WITHOUT forcing it
    // into existence. getInstance() is a Meyers singleton, so merely *checking*
    // via getInstance() would CREATE the manager (and spin up its batch thread) —
    // catastrophic if called during process teardown (e.g. ModuleLoader::unload()
    // running from ~ModuleLoader at exit). tryGetLiveInstance() returns nullptr
    // when no manager exists yet or one is being/has been destroyed, so such code
    // can safely skip instead of resurrecting it.
    // -------------------------------------------------------------------------
    static std::atomic<IntraIOManager*> s_liveInstance;

public:
    IntraIOManager();
    ~IntraIOManager();

    // Instance management. coreResident=true ⇒ the instance may share payloads by pointer (true
    // zero-copy in publish) because its publisher's code is core-resident for the whole process
    // (a static/core module). Default false ⇒ re-home payloads (safe for hot-loaded .so publishers).
    std::shared_ptr<IntraIO> createInstance(const std::string& instanceId, bool coreResident = false);
    void registerInstance(const std::string& instanceId, std::shared_ptr<IIntraIODelivery> instance);
    void removeInstance(const std::string& instanceId);

    // Drop all routing (TopicTree + per-instance patterns) for an instance WITHOUT
    // removing the instance itself. Used at hot-reload so a reloaded module's stale
    // routing is wiped while the instance is kept alive for re-subscription.
    void clearInstanceSubscriptions(const std::string& instanceId);

    std::shared_ptr<IntraIO> getInstance(const std::string& instanceId) const;

    // Routing (called by IntraIO instances). `payload` is the SHARED immutable node (zero-copy):
    // forwarded by pointer to each high-freq subscriber, no per-delivery json copy. seq + lamport
    // are the sender's envelope send-stamp (from IntraIO::publish); routeMessage adds tick/simTime.
    void routeMessage(const std::string& sourceid, const std::string& topic,
                      const std::shared_ptr<const IDataNode>& payload, uint64_t seq, uint64_t lamport,
                      const std::string& causedBy = "");

    // Engine-pushed simulation-time snapshot for envelope stamping (IO contract §5/§6). The engine
    // calls this once per step() right after the EngineClock advances; routeMessage() reads it to
    // stamp tick/simTime. Lock-free (atomic stores) so it never participates in the routing locks.
    void setSimTime(uint64_t tick, double simTime);
    void registerSubscription(const std::string& instanceId, const std::string& pattern, bool isLowFreq, int batchInterval = 1000);
    void unregisterSubscription(const std::string& instanceId, const std::string& pattern);

    // Management
    void clearAllRoutes();
    size_t getInstanceCount() const;
    std::vector<std::string> getInstanceIds() const;

    // Debug and monitoring
    json getRoutingStats() const;
    void setLogLevel(spdlog::level::level_enum level);

    // Structured replay log (IO contract §8, part 3). enableReplaySink turns ON capture with a bounded ring
    // of `capacity` events (drop-oldest): every routed control-plane message's envelope + topic is recorded.
    // Inspect via replaySink().timeline() (canonical (tick,lamport) order) / .bySource(id) (per-module view).
    // Off by default → zero routing cost. A debug/replay tool: async-safe (bounded, never stalls routing).
    void enableReplaySink(size_t capacity, bool capturePayload = false) { m_replaySink.enable(capacity, capturePayload); }
    void disableReplaySink() { m_replaySink.disable(); }
    const ReplaySink& replaySink() const { return m_replaySink; }

    // Singleton access (for global routing)
    static IntraIOManager& getInstance();

    // Returns true once the singleton destructor has completed.
    // Use this guard in any code that might call getInstance() during
    // static teardown (e.g. IntraIO::~IntraIO), to avoid use-after-free.
    static bool isDestroyed() { return s_destroyed.load(std::memory_order_acquire); }

    // Returns the live singleton pointer, or nullptr if no manager currently
    // exists (never created, or already being/finished destroyed). Unlike
    // getInstance(), this NEVER creates the singleton — safe to call from
    // teardown paths (see s_liveInstance doc above).
    static IntraIOManager* tryGetLiveInstance() {
        return s_liveInstance.load(std::memory_order_acquire);
    }
};

} // namespace grove
