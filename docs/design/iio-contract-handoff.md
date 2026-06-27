# IO contract — handoff / resume point

Companion to **[iio-contract.md](iio-contract.md)** (the permanent doctrine). This is the
*resume point*: how the contract was reached, what to build first, and the design questions still
open. Read the contract for the *what/why*; read this for *where we are and what's next*.

**Status: doctrine decided + documented. Nothing built this session.** The contract is a design
agreement, not code. The status ledger in the contract marks every line ✅ built / 🟡 decided /
🔵 deferred — most is 🟡.

## What this session decided (the trail)

The arc, so a fresh reader understands *why* the contract says what it does:

1. **Async by default** — the engine never blocks a publisher. Bought for speed + resilience
   (nothing stalls the world). This is also the prerequisite for relocatability: you can't move a
   module to a remote process if callers expect a synchronous reply.
2. **Non-deterministic by default, BUT carry the tools of determinism** — cross-module process
   order isn't reproducible, yet every control-plane message is stamped so a canonical order can
   be *reconstructed* offline (replay/debug/future lockstep) **without enforcing it live**.
   Determinism = deferred, not impossible.
3. **Confinement rule** — the non-determinism must live *only* in delivery order, never in module
   logic, or determinism becomes unrecoverable. (Already the intent: `process()` is doc'd "pure
   function".)
4. **Stamping toolkit chosen**: **Lamport** (causal order, not wall-clock) + **per-source seq**
   (dedup/gap) + **tick** (debug epoch) + **simTime** (so remote modules need no NTP). Vector
   clocks deferred. `exec-order` recorded in the *log*, not the envelope.
5. **EngineClock**: one authoritative clock, fixed timestep (`simTime = tick·dt`), **but not
   enforced on sim order** — time is deterministic + pausable (pause/slow-mo confirmed needed),
   execution stays async. The missing piece today: `step(float dt)` exists but `dt` never reaches
   modules.
6. **Dual logging = one stamped stream, two queries** (per-module filter / centralized
   merge-sort). The replay-log is the *immediate* debug payoff, not future insurance.

Foundations found in the code that shaped the contract (so it's faithful, not aspirational):
- `IModule::process(const IDataNode&)` is already documented "PURE FUNCTION" → confinement is
  pre-existing intent, now formalized.
- `IEngine::step(float dt)` exists but `dt` stops at the engine boundary → exactly the EngineClock gap.
- Backpressure is **already partly built** (`IntraIO` bounds the queue + drop-oldest, `IOHealth`
  metrics, 80%-full warning) — credited, not invented.

## Build order (suggested) — start here

1. **EngineClock first.** Smallest, most foundational, independent of the IIO rework. A
   `(tick, simTime, dt, realTime)` provider sampled once per tick in the main loop, fixed
   timestep. De-risks everything else and immediately unlocks pause/slow-mo.
2. **The envelope** — transport-owned header `{source, seq, lamport, tick, simTime, causedBy?}`.
   **Re-run the WSL TSan suite after** — it touches publish/route/deliver and the ABBA lock order
   ([[tsan-via-wsl-recipe]]).
3. **Structured replay sink** — once stamped, per-module + centralized logs fall out as two
   queries; build the async, droppable structured sink.
4. **Per-topic backpressure policy** — extend the existing bounded-queue infra with
   coalesce (latest-wins) / reject (critical) alongside the current global drop-oldest.
5. **Intra zero-copy delivery** (`shared_ptr<const>`) — the rendering-handoff open task;
   independent, benefits all control-plane traffic.

## Open design questions (decide before/while building)

These are **not yet decided** — flagged so they aren't lost:

- **How does the clock reach modules?** `process(const IDataNode&)` takes only an input node.
  Options: (a) change the signature to pass `(input, clock)` — wide blast radius, breaks every
  module; (b) a per-tick clock *topic* — but that's async/unordered, wrong for a clock; (c) an
  engine-provided accessor the module queries each tick. **(c) looks right but is unconfirmed.**
- **Fixed dt value** — `1/60`? configurable per-engine? And the accumulator's max-steps clamp
  (spiral-of-death guard) is unspecified.
- **Envelope structure** — header struct *alongside* the payload `IDataNode`, or reserved fields
  on the message? Must keep the §4 intra zero-copy (`shared_ptr<const>`) applicable to the payload.
- **Lamport increment site & ownership** — incremented on publish and on receive; per-IIO-instance
  counter vs a shared engine counter? (Affects the total-order tie-break.)
- **Per-topic backpressure** — which topics coalesce (`render:camera`, tension intents) vs reject
  (critical commands)? Needs enumeration as topics are added.

## Pointers

- **Doctrine**: [iio-contract.md](iio-contract.md) — the contract + status ledger + key files.
- **Memory**: `engine-io-contract.md` (project memory, survives sessions).
- **Related**: [rendering-throughput-handoff.md](rendering-throughput-handoff.md) (data-plane +
  the intra zero-copy task), [threaded-pool-handoff.md](threaded-pool-handoff.md) (the parallelism
  this contract runs on).
- **Key files**: `include/grove/IEngine.h` (`step(dt)`), `include/grove/IModule.h` (`process()`),
  `src/IntraIO.cpp` (delivery + backpressure), `src/IOFactory.cpp` (the tiers).
