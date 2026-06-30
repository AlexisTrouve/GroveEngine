#pragma once

/**
 * grove::mapview::ChunkCache — resident-set streaming with LRU eviction under a budget (S1b).
 *
 * WHAT  : Holds the decoded chunks currently in RAM. ensureResident(visible) loads any missing visible chunks
 *         via the injected provider, marks them most-recently-used, and evicts least-recently-used NON-visible
 *         chunks once the resident count exceeds the budget. get() returns a resident chunk or nullptr.
 *
 * WHY   : This is steps 2–3 of the pipeline (mapview.md §4): stream visible chunks, evict beyond budget,
 *         keep only the working set in RAM (so an infinite world holds bounded memory — §3.6). Two invariants
 *         matter: never reload a resident chunk (the provider is the slow path), and never evict a currently-
 *         visible chunk (that would thrash). An absent chunk (provider.has == false) simply stays non-resident
 *         — fail-franc, the layer doesn't draw it, never a zeroed placeholder.
 *
 * HOW   : Header-only, std-only. An unordered_map<coord,Entry> for O(1) lookup + a std::list<coord> as the LRU
 *         order (front = MRU, back = LRU); each Entry caches its list iterator for O(1) touch/erase. Visible
 *         chunks are touched to the front, so the list back is always the oldest non-visible chunk; eviction
 *         pops the back while over budget and the back is not visible (stopping if only visible remain — the
 *         budget is then deliberately exceeded rather than thrash). Stateful is fine for a header-only helper
 *         (like ZoneNavigator); the provider injection keeps it pure.
 */

#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/Coord.h"
#include "grove/mapview/WorldDocument.h"

namespace grove {
namespace mapview {

// Hash for ChunkCoord (x,y,z) — a simple integer mix, good enough for cache bucketing.
struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(c.x)) * 0x9E3779B97F4A7C15ull;
        h ^= (static_cast<uint64_t>(static_cast<uint32_t>(c.y)) + 0x9E3779B9ull) + (h << 6) + (h >> 2);
        h ^= static_cast<uint64_t>(static_cast<uint16_t>(c.z)) * 0x85EBCA77ull;
        return static_cast<size_t>(h ^ (h >> 29));
    }
};

class ChunkCache {
public:
    // maxResident = soft budget on the number of resident chunks (exceeded only if all resident are visible).
    ChunkCache(IChunkProvider& provider, size_t maxResident)
        : provider_(provider), maxResident_(maxResident) {}

    // Make `visible` resident, touch them MRU, then evict LRU non-visible chunks while over budget.
    void ensureResident(const std::vector<ChunkCoord>& visible) {
        const std::unordered_set<ChunkCoord, ChunkCoordHash> visSet(visible.begin(), visible.end());

        for (const ChunkCoord& c : visible) {
            auto it = resident_.find(c);
            if (it != resident_.end()) {
                touch(it);
                continue;
            }
            if (!provider_.has(c)) continue;             // absent at source -> not resident (fail-franc)
            // Load BEFORE mutating the LRU list: provider_.load() may throw (I/O / decode / OOM), and a
            // push_front done first would leave a phantom node with no matching resident_ entry, desyncing
            // the cache and breaking budget enforcement. Load-then-mutate = strong exception safety.
            ChunkData data = provider_.load(c);
            lru_.push_front(c);
            resident_.emplace(c, Entry{std::move(data), lru_.begin()});
        }

        // Evict from the LRU back while over budget; stop if the back is visible (never thrash visible).
        while (resident_.size() > maxResident_ && !lru_.empty()) {
            const ChunkCoord victim = lru_.back();
            if (visSet.find(victim) != visSet.end()) break;
            lru_.pop_back();
            resident_.erase(victim);
        }
    }

    // A resident chunk, or nullptr if not resident (absent / evicted) — the caller must not treat null as zero.
    const ChunkData* get(ChunkCoord c) const {
        const auto it = resident_.find(c);
        return it == resident_.end() ? nullptr : &it->second.data;
    }

    bool isResident(ChunkCoord c) const { return resident_.find(c) != resident_.end(); }
    size_t residentCount() const { return resident_.size(); }
    size_t budget() const { return maxResident_; }

private:
    struct Entry {
        ChunkData data;
        std::list<ChunkCoord>::iterator lruIt;
    };

    void touch(std::unordered_map<ChunkCoord, Entry, ChunkCoordHash>::iterator it) {
        lru_.erase(it->second.lruIt);
        lru_.push_front(it->first);
        it->second.lruIt = lru_.begin();
    }

    IChunkProvider& provider_;
    size_t maxResident_;
    std::list<ChunkCoord> lru_;  // front = MRU, back = LRU
    std::unordered_map<ChunkCoord, Entry, ChunkCoordHash> resident_;
};

} // namespace mapview
} // namespace grove
