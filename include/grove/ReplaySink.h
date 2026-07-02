#pragma once

/**
 * grove::ReplaySink — the structured replay log (IO contract §8, part 3).
 *
 * WHAT  : A bounded, thread-safe, OPT-IN sink that captures the stamped control-plane stream — one
 *         ReplayEvent (the message Envelope + its topic) per routed message. It is NOT a text logger: it
 *         stores the envelope FIELDS so the stream stays mergeable/queryable. Two views fall out by query:
 *         filter by `source` → a per-module timeline; merge-sort by `(tick, lamport)` → the canonical
 *         central timeline (the "replay log"). The single most valuable tool for a non-deterministic engine:
 *         record the stamped stream, replay it sorted, and the heisenbug that "only happens sometimes" shows
 *         its actual order.
 *
 * WHY   : The engine is async + non-deterministic by choice (iio-contract.md §2/§7): cross-module delivery
 *         order isn't reproducible live, but every control-plane message is stamped (§5) so a canonical order
 *         can be RECONSTRUCTED offline. This sink is the consumer that reads that stamped stream. It is a pure
 *         brick (no IntraIO/engine deps beyond the Envelope struct) so it unit-tests headless; the manager owns
 *         one and taps `routeMessage`. Being a debug tool, it is opt-in (zero cost when off) and DROPPABLE (a
 *         bounded ring, drop-oldest) — a logger must never stall the engine (§8 rule 2).
 *
 * HOW   : Header-only, std-only. A fixed-capacity circular buffer of ReplayEvent. `record()` is the hot path:
 *         a single atomic gate when disabled (returns immediately), else an O(1) push under the sink's OWN
 *         mutex (independent of the routing locks → no lock-ordering cycle) that OVERWRITES the oldest entry
 *         when full (keeps a sliding window of the last N events — you want the most recent history when a bug
 *         just fired). Queries snapshot the ring under the lock and sort a copy, so a debug/host thread can
 *         read the timeline while workers keep routing. v1 captures Envelope + topic; a payload digest,
 *         seq-based dedup/gap-detection, and `causedBy` correlation are explicit follow-on increments (they
 *         need the payload read / cross-event logic and are noted in the contract §8 as landing "with" the sink).
 */

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "IIO.h"  // grove::Envelope

namespace grove {

// One recorded control-plane message: the transport envelope (ordering/causality metadata) + its topic.
// Deliberately payload-free in v1 — the envelope + topic IS the replay timeline; capturing payload content
// is a flagged follow-on (a digest, to avoid pinning hot-loaded .so memory / dumping json on the hot path).
struct ReplayEvent {
    Envelope    env;    // source / seq / lamport / tick / simTime — the ordering metadata
    std::string topic;  // where the message was published
};

class ReplaySink {
public:
    // Turn recording ON and (re)initialize the ring to `capacity` events, clearing any prior contents +
    // counters. A capacity of 0 is treated as disabled (nothing can be stored). Idempotent-ish: calling
    // enable() again resizes + clears (a fresh capture session).
    void enable(size_t capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        capacity_ = capacity;
        ring_.assign(capacity, ReplayEvent{});
        head_ = 0;
        count_ = 0;
        captured_ = 0;
        dropped_ = 0;
        enabled_.store(capacity > 0, std::memory_order_release);
    }

    // Stop recording. The captured contents stay queryable (for post-mortem inspection) until the next
    // enable() clears them. A no-op if already disabled.
    void disable() { enabled_.store(false, std::memory_order_release); }

    bool enabled() const { return enabled_.load(std::memory_order_acquire); }

    // HOT PATH (called from routeMessage under the routing locks): record one stamped message. O(1),
    // thread-safe via the sink's own mutex, and a cheap atomic no-op when disabled (so tapping the router
    // costs one predictable-not-taken branch when the sink is off). Drop-oldest when the ring is full.
    void record(const Envelope& env, const std::string& topic) {
        if (!enabled_.load(std::memory_order_acquire)) return;  // opt-in: zero work when off
        std::lock_guard<std::mutex> lock(mutex_);
        if (capacity_ == 0) return;                             // disabled between the gate and the lock
        if (count_ == capacity_) {
            ++dropped_;                                          // overwriting a live entry = a dropped oldest
        } else {
            ++count_;
        }
        ring_[head_] = ReplayEvent{env, topic};
        head_ = (head_ + 1) % capacity_;
        ++captured_;
    }

    // --- queries (snapshot under the lock; safe to call while workers keep recording) ---

    // The live ring contents in INSERTION order (oldest → newest).
    std::vector<ReplayEvent> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshotLocked();
    }

    // Only the events from one publisher instance — the §8 "per-module view".
    std::vector<ReplayEvent> bySource(const std::string& source) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ReplayEvent> out;
        for (const ReplayEvent& e : snapshotLocked()) {
            if (e.env.source == source) out.push_back(e);
        }
        return out;
    }

    // The canonical central timeline — the same events merge-sorted into a reconstructable causal order:
    // by tick (coarse epoch), then lamport (causal order), tie-broken by source then seq for a total order.
    // This is THE replay log (§8): a deterministic ordering over a non-deterministically-delivered stream.
    std::vector<ReplayEvent> timeline() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ReplayEvent> out = snapshotLocked();
        std::stable_sort(out.begin(), out.end(), [](const ReplayEvent& a, const ReplayEvent& b) {
            if (a.env.tick != b.env.tick)       return a.env.tick < b.env.tick;
            if (a.env.lamport != b.env.lamport) return a.env.lamport < b.env.lamport;
            if (a.env.source != b.env.source)   return a.env.source < b.env.source;
            return a.env.seq < b.env.seq;
        });
        return out;
    }

    size_t   size() const     { std::lock_guard<std::mutex> lock(mutex_); return count_; }
    size_t   capacity() const { std::lock_guard<std::mutex> lock(mutex_); return capacity_; }
    // Total messages ever offered to the ring while enabled (includes the ones later overwritten).
    uint64_t capturedCount() const { std::lock_guard<std::mutex> lock(mutex_); return captured_; }
    // Total oldest-entries overwritten because the ring was full (the "droppable" count — watch it to size up).
    uint64_t droppedCount() const  { std::lock_guard<std::mutex> lock(mutex_); return dropped_; }

private:
    // Rebuild insertion order from the circular buffer. Caller holds mutex_.
    std::vector<ReplayEvent> snapshotLocked() const {
        std::vector<ReplayEvent> out;
        out.reserve(count_);
        // When full, the oldest live entry is at head_ (the next slot to overwrite); when not yet full, the
        // entries are [0, count_) and head_ == count_. Walk `count_` slots starting at the oldest.
        const size_t start = (count_ == capacity_ && capacity_ > 0) ? head_ : 0;
        for (size_t i = 0; i < count_; ++i) {
            out.push_back(ring_[(start + i) % capacity_]);
        }
        return out;
    }

    mutable std::mutex       mutex_;
    std::vector<ReplayEvent> ring_;          // fixed-capacity circular buffer
    size_t                   head_ = 0;      // next write index
    size_t                   count_ = 0;     // live entries (<= capacity_)
    size_t                   capacity_ = 0;
    std::atomic<bool>        enabled_{false};
    uint64_t                 captured_ = 0;  // total recorded (incl. overwritten)
    uint64_t                 dropped_ = 0;   // total overwritten (ring-full drops)
};

} // namespace grove
