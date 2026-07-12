# IIO concurrency race — investigation handoff (RESOLVED via TSan)

**Status (2026-07-12): RESOLVED. TSan on VPS142 pinned the residual race; fixed + TSan-re-proven clean.**

## RESOLUTION (TSan on VPS142)
Built `-DGROVE_ENABLE_TSAN=ON` on VPS142 (g++ 14.2), ran test_11 under `setarch -R`. TSan named ONE
data race: **ConsumerModule.cpp:40 (the subscribe callback) read+written concurrently by 2 consumer
threads, both via `IntraIO::pullAndDispatch()`** — because TEST 6 drained ONE consumerIO from 3 threads
and pullAndDispatch dispatches the callback OUTSIDE operationMutex (phase 2, the ABBA-deadlock fix), so
the callbacks ran concurrently. It's the SAME one-owning-thread-per-instance violation as publish, on
the PULL side. (The publish side was already caught by G1's guard — TSan's log also shows 492 guard
violations on the shared-producerIO publishers.)

**Fix (commit 29190c1):** (1) extended the ScopedAccessGuard tripwire to pullAndDispatch (shares
m_activeCallers with publish — one thread at a time across ALL instance ops); (2) test_11 TEST 6 now
uses one consumer thread + 5 stable per-instance publishers (the supported concurrency). **TSan re-run
on the fix: RUN_DONE rc=0, ZERO data races, ZERO guard violations** (halt_on_error=0 → reports all).
Windows test_11: 15/15. The "reads as fully locked yet corrupts" mystery below was resolved: the racing
access is the module CALLBACK (unlocked phase-2 dispatch), not the engine's deque — reading missed it
because the callback is in the test module (.so), invoked via std::function past the lock.

---
## Original investigation (kept for reference)

## Symptom
`IOSystemStress` (`tests/integration/test_11_io_system.cpp`, ctest #… `test_11_io_system`) fails
intermittently with exit `0xC0000374` = STATUS_HEAP_CORRUPTION. ~10% at HEAD. It crashes DURING
"TEST 6: Thread Safety" (the concurrent pub/consume phase), not at teardown — the last logs show
routing in flight (~10700 messages), never "TEST 6 PASSED".

## Localization by measurement (differential cuts)
TEST 6 launches 5 publisher threads (each `producerIO->publish("thread:test", ...)` ×100) + 3 consumer
threads (`consumerIO->pullAndDispatch()`). The consumer sub is high-freq → IMMEDIATE delivery
(`routeMessage` line 343 `deliverMessage`, NOT the batch buffer).

| Config | Result (runs) |
|---|---|
| Original: 5 pub on ONE shared producerIO, 3 con | crash ~10% |
| Cut A: 5 pub shared, **1 con** | crash 4/20 |
| Cut B: **1 pub**, 1 con | 12/12 OK |
| Cut C: 5 pub **each own instance**, 1 con | 20/20 → but see below |
| Cut C repeat: 5 pub own instance (created in-thread), 1 con | 4/18 crash |
| Cut C stable: 5 pub own STABLE instance (pre-created), 1 con | 1/25 crash (~4%) |

**Two findings:**
1. **Concurrent publish on ONE shared instance** is a big contributor (Cut A vs Cut B). IntraIO is
   single-owning-thread by contract; ≥2 threads in `publish()` on one instance is a data race. → G1.
2. **A residual ~4% race remains even with correct per-instance usage** (Cut C stable). It needs
   MULTIPLE concurrent routers (5 pub) + a consumer (Cut B with 1 pub is clean). This is a deeper race
   in concurrent routing/delivery to a shared subscriber.

## What was FIXED (G1, shipped — commit 2ad677b)
`grove::detail::ScopedAccessGuard` (`include/grove/detail/AccessGuard.h`, `src/detail/AccessGuard.cpp`):
a DEBUG tripwire (not a lock) wired into `IntraIO::publish`. On a concurrent overlap it logs an
actionable error (op / instance / thread) + bumps `accessViolationCount()`. Turns the silent
heap-corruption into a loud, findable failure. On the existing shared-instance stress it fires ~492×.
The IntraIO.h / IIO.h "thread-safe" claims were narrowed to the real contract: **thread-safe ACROSS
instances; one owning thread per instance; never share one instance across threads.**

## The residual race — where reading hit its limit
Everything on the immediate-delivery path READS as fully locked, yet it still corrupts under ≥2
concurrent routers + a consumer:
- `IntraIO::publish` (src/IntraIO.cpp:53): per-instance state (seq/lamport/payload) under
  `operationMutex`; `routeMessage` called AFTER releasing it (ABBA-deadlock fix).
- `IntraIOManager::routeMessage` (src/IntraIOManager.cpp:226): whole body under
  `scoped_lock(managerMutex, batchMutex)` (exclusive) → concurrent routers are serialized. Immediate
  delivery at line 343 calls the target's `deliverMessage` while holding managerMutex.
- `IntraIO::deliverMessage` (src/IntraIO.cpp:361): enqueues under the target's `operationMutex`.
- `IntraIO::pullAndDispatch` (src/IntraIO.cpp:179): dequeues (front/pop_front) under `operationMutex`
  (phase 1), releases it, then invokes callbacks (phase 2) — another ABBA-deadlock fix.
So enqueue + dequeue share the target's `operationMutex`; routers serialize on `managerMutex`. No
concurrent access to the message deque is VISIBLE. The corruption is subtle (a lock that isn't the one
it looks like, a member touched in an unlocked branch, an iterator/refcount edge) — exactly TSan's job.

## NEXT STEP — TSan on VPS142
Windows/MinGW has no TSan. The project's recipe: build on Linux/VPS142 (Tailscale) with
`-DGROVE_ENABLE_TSAN=ON`, build `test_11_io_system` (+ its .so modules), run it, read the race report.
test_11 is already cross-platform (`.so`/`.dll` via `modPath`). NOTE: the threaded MODULE SYSTEMS are
already TSan-proven clean ([[module-system-parallelism-status]]) — so this is raw-IntraIO ad-hoc
concurrency (direct publish/pull threads), a shape the coordinated module-system drain doesn't exercise.
Once TSan names the racing access: fix it (preserve the documented ABBA-deadlock avoidance in
publish/pullAndDispatch — do NOT just wrap more in managerMutex), then re-pattern TEST 6 to the
supported per-instance shape and confirm deterministic green (was ~4% flaky).

## Reachability / priority
The SUPPORTED concurrent path (module systems) is TSan-clean, so this does not affect the normal engine
loop. It bites raw concurrent IntraIO usage (multiple threads routing to a shared subscriber). Medium
priority: real but not on the hot supported path. G1 makes the most common violation (concurrent
publish on one instance) loud in the meantime.

## Anchors
- `tests/integration/test_11_io_system.cpp` (TEST 6, ~line 474). Reproduce: run it ~20× in a loop.
- `src/IntraIO.cpp` (publish/deliverMessage/pullAndDispatch), `src/IntraIOManager.cpp` (routeMessage).
- Guard: `include/grove/detail/AccessGuard.h`. Relates to [[module-system-parallelism-status]],
  [[tsan-via-wsl-recipe]], [[engine-io-contract]].
