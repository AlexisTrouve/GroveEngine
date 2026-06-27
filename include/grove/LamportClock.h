#pragma once

// ============================================================================
// grove::LamportClock — logical clock for causal ordering (header-only, pure).
//
// WHAT : A scalar logical clock (Lamport, 1978). `tick()` stamps a local event / an outgoing
//   send; `update(received)` folds in an incoming message's stamp as `max(local, received) + 1`.
//   The value is a monotonically increasing integer; ordering messages by (lamport, source)
//   yields a total order CONSISTENT WITH CAUSALITY.
//
// WHY  : The IO contract (docs/design/iio-contract.md §5) is non-deterministic in delivery
//   order but stamps every control-plane message so a canonical order can be reconstructed
//   offline (replay/debug). Lamport — NOT a wall-clock and NOT a single shared counter — is the
//   causality primitive: a wall-clock drifts and collides; a shared atomic counter can't cross a
//   process boundary (it would break the moment a module is relocated to LocalIO/NetworkIO, which
//   is the whole point of the envelope). A PER-NODE Lamport clock with the receive rule works
//   identically intra-process and across machines.
//
// HOW  : The receive rule is the load-bearing part: on receiving a message stamped `r`, set
//   `counter = max(counter, r) + 1`. This guarantees that whatever a node does AFTER consuming a
//   message is stamped strictly greater than that message — "effect after cause". `tick()` alone
//   handles purely local progress. Pure: no time, no I/O, no atomics — the caller serializes
//   access (the transport calls these under its per-instance lock). Locked by test_lamport_clock.
// ============================================================================

#include <cstdint>

namespace grove {

class LamportClock {
public:
    // A local event / an outgoing send: advance logical time and return the new stamp.
    uint64_t tick() { return ++counter_; }

    // An incoming receive carrying stamp `received`: counter = max(local, received) + 1.
    // A stale (lower) stamp never rewinds us; an equal stamp still strictly advances. Returns
    // the new value. This is what keeps a consumed cause strictly before any later local event.
    uint64_t update(uint64_t received) {
        counter_ = (received > counter_ ? received : counter_) + 1;
        return counter_;
    }

    uint64_t value() const { return counter_; }

    // Back to logical time 0 (e.g. a fresh run). Configuration-free, so nothing else to reset.
    void reset() { counter_ = 0; }

private:
    // Caller-serialized (no internal atomic): the transport mutates this only under the owning
    // IntraIO instance's operationMutex, so a plain integer is correct and cheap. Making it
    // atomic would NOT make tick()/update() atomic-as-a-unit anyway (they are read-modify-write).
    uint64_t counter_ = 0;
};

} // namespace grove
