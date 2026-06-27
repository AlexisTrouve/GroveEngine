# The Engine IO Contract — async, non-deterministic, replay-capable

This is the **foundational contract** of GroveEngine's communication layer: how modules
talk, what guarantees they get, and what they must *not* assume. It is the reference for
anyone touching IIO, the engine loop, time, logging, or the future Local/Network tiers.

It was co-decided as doctrine, not derived from existing code. Some of it is already built,
most is decided-but-not-yet-built, some is deliberately deferred. **Every claim below carries
a status marker** so nobody mistakes a decision for a fact:

| Marker | Meaning |
|--------|---------|
| ✅ **BUILT** | exists in the code today, verifiable |
| 🟡 **DECIDED** | agreed contract, **not yet implemented** |
| 🔵 **DEFERRED** | intentionally postponed; the door is kept open |

---

## The contract in one paragraph

> The engine is **async by default** — publishers never block on consumers. It is
> **non-deterministic by default** — the order in which messages are processed across modules
> is not guaranteed to repeat run-to-run. We trade determinism for **global resilience and
> speed**: nothing stalls the world, and modules run full-tilt in parallel. But we **carry the
> tools of determinism on every message** (logical clock, tick, sequence, sim-time) so that a
> canonical order can be *reconstructed* offline — for replay, debugging, and an eventual opt-in
> lockstep — **without ever enforcing it live**. Determinism is therefore *deferred*, not
> *impossible*. The price of that future is paid now, cheaply, by stamping; it is not paid by
> slowing anything down today.

Two consequences drive everything else:

1. **Non-determinism is confined to the scheduler/transport — never to module logic.** A module
   must behave as a (quasi-)pure function of its state plus the messages it received this tick.
   If the only source of non-determinism is *delivery order*, determinism can later be switched
   on by serializing that order — for free, touching no module. If non-determinism leaks into
   module logic (wall-clock reads, unseeded RNG, hash-map iteration order, address dependence),
   it becomes irrecoverable.
2. **Async + resilience forces a backpressure decision.** An unbounded inbox under load is a
   memory blow-up, not resilience. Every queue is bounded with a defined drop/coalesce policy.

---

## 1. Async by default ✅ (loop) / 🟡 (full contract)

Publishers fire into topics and move on; consumers process on their own cadence
(`IModule::process()` is driven by the per-module ModuleSystem). This is what the
Threaded/ThreadPool systems already do.

Async is the **load-bearing choice**. It buys both:

- **Speed** — no global barrier, no lockstep; the slowest module never stalls the others
  (this *is* the Phase 2/3 parallelism).
- **Resilience** — a slow / dead / remote peer cannot freeze the world. This is the prerequisite
  for relocatability (§4): you cannot move a module to another process if callers block on a
  synchronous reply.

### Rare exceptions (named so they don't surprise)

Async is the default, not an absolute. These legitimately need ordering / a barrier:

- **Within-frame causal pipelines** — e.g. `input → sim → render`. If render reads a sim result
  that arrived late, you get visible lag/tearing. `SceneCollector::finalize()` is already such a
  per-frame barrier.
- **Hot-reload quiescence** — extracting a module's state needs a point where it is *not*
  processing. `ModuleLoader::reload()` already handles this.
- **Future lockstep** (deterministic multiplayer / replay enforcement) — 🔵 deferred.

---

## 2. Non-deterministic — but with the tools of determinism 🟡

We are non-deterministic by *choice of cost*, not by accident:

> Non-determinism is free. Determinism costs an ordered merge per tick. **We decline to pay that
> cost now** — but we keep the receipts (the ordering metadata) so we can buy it later.

### The confinement rule (doctrine)

> **Module logic is deterministic given its inputs. Non-determinism lives only in delivery
> order/timing.**

This is *already the design intent*: `IModule::process(const IDataNode&)` is documented as a
"PURE FUNCTION with minimal side effects" testable in isolation. We are formalizing it, not
inventing it. Concretely, module logic must **not**:

- read the OS wall-clock — read the engine clock instead (§5);
- use unseeded RNG — seed it; the seed is part of replayable state;
- depend on `unordered_map` iteration order where it affects output — sort, or use ordered containers;
- depend on pointer/object addresses (ASLR-varying) — sort by content keys, never by address.

### What stamping does *not* fix (so we don't oversell it)

Ordering metadata is **~20% of determinism**. The other 80% is the list above (RNG, float
non-associativity in parallel reductions, hash-map order, address dependence). Timestamps give
you the *ability* to order; they do not make the system deterministic on their own.

---

## 3. Two planes: control vs data 🟡 (split) / ✅ (data path)

Not all traffic is equal. The contract splits cleanly:

| Plane | Carries | Transport | Stamped? | Relocatable? | Example |
|-------|---------|-----------|----------|--------------|---------|
| **Control-plane** | events, intents, requests/responses, state | IIO (json + envelope) | yes | yes | a pathfinding request/result, `ui:click`, `audio:intent` |
| **Data-plane** | high-volume GPU-ready floods | direct API (POD) | no | no | `submitSpriteBatch` (sprite crowds) ✅ |

The elegant part: **what you'd want to relocate (control-plane) is exactly what json handles
well; what json handles badly (the flood) is exactly what you never relocate.** You never send
100k sprites over a network. The architecture splits along the right line on its own.

`submitSpriteBatch` / `SceneCollector::addSpritesBulk` is the data-plane and is ✅ shipped — see
[rendering-throughput-handoff.md](rendering-throughput-handoff.md).

---

## 4. Canonical payload = JSON everywhere 🟡 (contract) / ✅ (intra today)

The goal is **location transparency**: a module addresses *topics*, never pointers, and does not
know whether its peer is in-process, in another process, or on another machine. Moving the
WIP pathfinder onto a remote server tomorrow must be a *transport config* change — zero lines of
the pathfinder or its clients.

`IOFactory` already commits to three tiers behind one `IIO` interface:

| Tier | Boundary | Status | Payload cost |
|------|----------|--------|--------------|
| `intra` → IntraIO | same process, direct calls | ✅ BUILT | should be zero-copy |
| `local` → LocalIO | same machine (pipes/sockets) | 🔵 stub (throws "not yet implemented") | serializes |
| `network` → NetworkIO | TCP/WS, distributed | 🔵 stub | serializes + wire |

**JSON is already the "same payload everywhere"** we want: it is both ergonomic *and*
serializable, so it crosses every tier (`dump()`/`parse()`). Portability does **not** require a
flat format. A flat/zero-copy representation (FlatBuffers / shared-memory) is only about
*cross-process zero-copy*, which is dwarfed by pipe/network latency for the low-volume control
plane — so it is 🔵 **deferred, profile-gated**, decided only when a concrete Local boundary
becomes hot.

The intra zero-copy optimization (deliver a `shared_ptr<const>` immutable payload instead of
deep-copying the json — [rendering-throughput-handoff.md](rendering-throughput-handoff.md),
open task #1) is **compatible with all of this**: it changes intra *delivery*, not the payload
*type*. The pathfinder still publishes json; in-process it is shared by pointer, on a remote it
is `dump()`'d.

---

## 5. The message envelope 🟡

Ordering/causality metadata is an **envelope** concern, owned by the transport — **not** part of
the user's json payload. The transport stamps and reads the header; the module owns only the
payload. This is what lets the transport do dedup / gap-detection / ordering generically, and
keeps modules from hand-rolling sequence numbers.

```
message = { header: <envelope>, payload: <json> }   // header transport-owned, payload module-owned
```

Envelope fields (control-plane only — see §3):

| Field | What | Why | Status |
|-------|------|-----|--------|
| `source` | publisher instance id | per-producer ordering, routing | 🟡 |
| `seq` | monotonic per-source counter | gap detection, **dedup**, per-source order | 🟡 |
| `lamport` | logical clock (scalar) | **causal total order**, tie-broken by source — canonical reconstructable order for replay | 🟡 |
| `tick` | engine epoch (§6) | coarse debug/replay axis ("show me tick 4096"); cheap, kept even where unsynced | 🟡 |
| `simTime` | deterministic sim time (= `tick·dt`) | shipped so a **remote module needs no clock sync** — it reads the tick's time from the message | 🟡 |
| `causedBy?` | optional correlation id | ties response→request; traces the causal DAG across a relocated module | 🟡 |

**Lamport, not wall-clock.** Wall-clock timestamps are *not* an ordering tool (drift,
non-monotonic across machines, ms collisions). Lamport is a causality counter (one integer,
incremented on send/receive) giving a total order consistent with causality.

🔵 **Vector clocks deferred.** They detect genuine concurrency (partial order) but cost
O(nodes)/message. Lamport + `seq` covers replay + dedup + ~90% of needs. Adopt vector clocks
only when we need to *prove commutativity* of concurrent events.

**`exec-order` is not in the envelope.** The order in which a worker actually dequeued and
processed a message is a *receive-side* fact, recorded in the **log** (§8), not in the message.
Lamport = canonical/portable order; exec-order = forensic "what actually ran". Both useful, different homes.

---

## 6. The EngineClock ✅ BUILT (part 1: 1a + 1b + 1c)

**Built.** `grove::EngineClock` (`include/grove/EngineClock.h`) is the engine's single
authoritative clock: a pure, header-only fixed-timestep accumulator. `DebugEngine` owns one by
value, advances it once per `step(dt)` (`m_clock.advance(deltaTime)`), and exposes it to the host
via `IEngine::clock()` (read **and** control: pause/resume/setTimeScale). Each module is handed
the same clock **read-only** at registration via `IModule::setClock(const EngineClock*)` and reads
`tick()/simTime()/dt()` inside `process()`. The old gap — `step(dt)` existed but `dt` never reached
modules — is closed.

**Contract:** the main engine owns **one authoritative clock**, samples real time **once per
tick**, derives `(tick, simTime, dt)`, and that is the **only** time any module ever sees. No
module calls the OS clock. This is the *enforcement mechanism* for the confinement rule (§2).

### How the clock reaches modules ✅ (decided: injection, option c)

The open question was: signature change `process(input, clock)` (a), a clock *topic* (b), or an
injected handle (c). **Decided: (c)** — `virtual void setClock(const EngineClock*)`, a **non-breaking
default no-op** on `IModule`; the engine calls `module->setClock(&m_clock)` once at registration.
Rationale:

- **Consistent with the existing contract.** The engine already injects long-lived service pointers
  (`IIO* io`, `ITaskScheduler* scheduler`) via `setConfiguration`, stored and read during
  `process()`. The clock is one more engine-owned service of the same kind — not a special case.
- **The clock is a per-tick *global*** (same value for every message this tick), so a per-call
  parameter (a) buys nothing and taxes every module + the TSan-proven parallel hot path. (c) injects
  once; timeless modules ignore it (default no-op) and are untouched.
- **A topic (b) is wrong for ground truth** — async/unordered/droppable under backpressure; the clock
  must be a synchronous authoritative read, not a message.
- **Reversible.** (c)→(a) later is a deliberate batched refactor if ever wanted; (a) is a one-way door
  across every module. Start cheap.

**Relocation note.** A pointer can't cross a process boundary, so a **remote** (Local/Network-tier)
module does not use `setClock` — it reads `simTime`/`tick` from each message **envelope** (§5). Intra
= injected pointer; remote = envelope field. Same notion of "now", two transports — by design.

Locked by `EngineClockUnit` (the pure clock, 13 cases) + `EngineClockHosting` (E2E: a hosted module
reads the injected clock; host-driven pause/slow-mo seen by the module).

### Fixed timestep — but not enforced on sim order

- **Fixed timestep** ✅ BUILT: `dt` is constant (e.g. `1/60`), so `simTime = tick·dt` (derived,
  never summed → no float drift). The classic accumulator pattern (Gaffer, "Fix Your Timestep"):
  accumulate scaled real time, emit fixed `dt` steps, with a spiral-of-death cap that drops the
  unrun remainder (a long hitch makes sim time slip, never sprint to catch up).
- **Elegant consequence:** under fixed timestep, **`tick` and `simTime` are isomorphic** —
  "the engine clock" and "the tick counter" become one authoritative source.
- **NOT enforced on sim order** ✅ decided: fixed `dt` makes *time* deterministic and pausable;
  it does **not** serialize sim execution. Modules still process in parallel/async,
  non-deterministic order — just stamped. We pay zero watermark/barrier; we keep the async
  speed/resilience. (See §7 for why enforcing live order would cost a wait.)

### The three clocks — never conflate them

| Clock | Is | Used by |
|-------|----|---------|
| **Lamport** | causality, *not time* | ordering / replay |
| **simTime** (`tick·dt`) | simulation time: deterministic, **pausable, time-scalable** | deterministic game logic |
| **realTime** | wall-clock, non-deterministic | profiling, timeouts, network staleness |

A module must know *which* it reads. Determinism-critical logic → `simTime` + `tick`. Timeouts /
profiling → `realTime` (and accepts non-determinism there).

### Payoffs

- **Deterministic time** — every module at tick N sees the exact same timestamp; recorded
  per-tick samples replay identically.
- **Pause / slow-mo / fast-forward** ✅ BUILT — time comes from one source: `timeScale=0`
  pauses (sim frozen, realTime keeps flowing), `realDelta·timeScale` slows or speeds. Driven by the
  host via `engine.clock()`; every hosted module sees it transparently. A module reading the OS clock
  could never be paused. (`pause()` remembers the active scale so `resume()` restores a slow-mo.)
- **Distributed sync for free** — a remote module does *not* NTP-sync to the engine; it reads the
  tick's `simTime` from the message it received (§5). NTP-style real-clock sync is needed only to
  measure *staleness*, a profiling/resilience concern, never the deterministic path.

---

## 7. Determinism: recorded, not enforced 🟡 / 🔵

The boundary that justifies "fixed but not enforced on sim order":

> **Reconstructing order offline (replay) is cheap. Enforcing order live is a wait.**

To guarantee live ordering, a receiver must *wait* until it is sure it has every message with
timestamp ≤ T before processing — a **watermark/barrier** (the Flink problem). Waiting
re-introduces exactly the latency and stall-risk async killed. You cannot have *both* "process
immediately" (async, resilient) and "process in guaranteed global order" (deterministic) at once.

Therefore:

- **Now (🟡):** stamp everything (§5), record the stream, never enforce. Determinism is
  *reconstructable* for replay/debug.
- **Later (🔵):** determinism-on-demand = a subsystem *opts into* a barrier and accepts its
  latency. Enforcement is per-subsystem, never global.

---

## 8. Logging & replay 🟡 (one stamped stream, two views)

Per-module logging and a centralized log are **not two systems** — the centralized log *is* the
per-module logs merged on the envelope metadata:

- filter by `source` → **per-module view** (debug in isolation);
- merge-sort by `(tick, lamport)` → **centralized canonical timeline** = the **replay log**;
- sort by `exec-order` per worker → **forensic** "what actually ran when".

Three rules so it stays sound:

1. **Structured, not text.** The replay/central sink stores the envelope *fields* (+ a payload
   digest), not a formatted string — otherwise it isn't mergeable/queryable. The human-readable
   spdlog domain loggers (`createDomainLogger`) stay for humans; this is a separate structured
   event sink. Two outputs, one event.
2. **Async + droppable.** The logger is a consumer like any other; it must never stall the engine.
   Ring buffer, drop-on-overflow under pressure (same backpressure doctrine, §9).
3. **Control-plane only.** ~100 msgs/frame is loggable; logging every sprite is suicide (§3).

**The replay log is the immediate payoff — today, not "later".** For a non-deterministic engine,
recording the stamped stream and replaying it sorted is *the* single most valuable debugging
tool: it reproduces the heisenbug that "only happens sometimes". This justifies the stamping cost
now, independent of any future lockstep.

---

## 9. Backpressure ✅ (partially built) / 🟡 (per-topic policy)

Async + resilience *forces* bounded queues. **This is already partly built** (recently revived —
the metrics had gone dead):

- ✅ `IntraIO` bounds the total queued messages to `maxQueueSize` and **drops oldest first**
  (`src/IntraIO.cpp` ~L424-432).
- ✅ `IOHealth` exposes `queueSize`, `maxQueueSize`, `dropping`, `droppedMessageCount`,
  `averageProcessingRate`; `DebugEngine` warns at 80% full.
- ✅ High-freq / low-freq queue split exists.

🟡 **Missing: a per-topic policy.** Global drop-oldest is one strategy; some topics want
**coalesce** (latest-wins — e.g. `render:camera`, a tension intent) and some want **reject**
(never drop a critical command). The contract: backpressure policy is a *per-topic* property, not
one global rule. This is what turns "resilience" from a word into a property.

---

## 10. Testing under non-determinism 🟡 (discipline)

A non-deterministic engine breaks "publish then assert immediately" tests → flaky → "unverified"
under the low-trust doctrine. The pattern:

- **Assert convergence, not instantaneous order** — wait-until-with-timeout, not "after publish X,
  state == Y now".
- **TSan stays the rigor tool** — it proves race-freedom *even under non-deterministic order*
  ([[tsan-via-wsl-recipe]]). Our existing WSL TSan harness is exactly aligned; re-run it after any
  change to publish/route/deliver.

---

## Status ledger (the honest summary)

| Item | Status |
|------|--------|
| Async loop, per-module process(), Threaded/ThreadPool parallelism | ✅ BUILT |
| `IModule::process()` as a pure-ish function (confinement intent) | ✅ BUILT (documented) |
| IIO intra transport, json payload | ✅ BUILT |
| `IEngine::step(float dt)` — dt at the engine boundary | ✅ BUILT |
| Backpressure: bounded queue + drop-oldest + IOHealth + 80% warn | ✅ BUILT |
| Data-plane direct path (`submitSpriteBatch`) | ✅ BUILT |
| TSan harness (WSL) | ✅ BUILT |
| Message envelope `{source, seq, lamport, tick, simTime, causedBy?}` | 🟡 DECIDED |
| EngineClock (fixed timestep, exposes tick/simTime/dt/realTime to modules) | ✅ BUILT |
| Clock → module handoff: `setClock` injection + `IEngine::clock()` host accessor | ✅ BUILT |
| Pause / slow-mo / time-scale via the clock | ✅ BUILT |
| Lamport logical clock + per-source seq + dedup | 🟡 DECIDED |
| Dual logging = stamped stream, two views; structured replay sink; exec-order | 🟡 DECIDED |
| RNG seeding discipline | 🟡 DECIDED |
| Intra zero-copy delivery (`shared_ptr<const>`) | 🟡 DECIDED (rendering handoff task #1) |
| Per-topic backpressure policy (coalesce / reject) | 🟡 DECIDED |
| Live determinism enforcement (sort + apply canonical order) | 🔵 DEFERRED |
| Vector clocks | 🔵 DEFERRED |
| Flat payload / shared-memory zero-copy cross-process | 🔵 DEFERRED |
| LocalIO / NetworkIO implementation | 🔵 DEFERRED (stubs) |
| Lockstep / cross-machine replay determinism | 🔵 DEFERRED |

## Open decisions & suggested build order

1. ~~**EngineClock first**~~ ✅ **DONE** (part 1: `EngineClock` + `step()` advances it + `setClock`
   injection + `IEngine::clock()`). Locked by `EngineClockUnit` + `EngineClockHosting`.
2. **The envelope** ← *next* — add the transport-owned header; wire `seq`/`lamport`/`tick`/`simTime`.
   The clock now provides `tick`/`simTime`, so the envelope's time fields have a real source to stamp
   from. **Re-run the WSL TSan suite after** (touches publish/route/deliver and the ABBA lock order).
3. **Structured replay sink** — once messages are stamped, the per-module + centralized logs fall
   out as two queries; build the structured sink (async, droppable).
4. **Per-topic backpressure policy** — extend the existing bounded-queue infra with coalesce/reject.
5. **Intra zero-copy delivery** (`shared_ptr<const>`) — the rendering-handoff open task; independent,
   benefits all control-plane traffic.

## Key files

- `include/grove/EngineClock.h` — ✅ the authoritative fixed-timestep clock (pure, header-only).
- `include/grove/IEngine.h` — `step(float dt)` (advances the clock) + `clock()` host accessor.
- `include/grove/IModule.h` — `process(const IDataNode&)`, the pure-function contract; `setClock()` injection.
- `src/DebugEngine.cpp` — owns `m_clock`, advances it in `step()`, injects it via `setClock` in
  `registerStaticModule`; the concrete engine loop; 80%-full health warning.
- `tests/unit/test_engine_clock.cpp` + `tests/integration/test_engine_clock_hosting.cpp` — the locks.
- `include/grove/IOFactory.h` + `src/IOFactory.cpp` — the intra/local/network tiers (local/network are stubs).
- `src/IntraIO.cpp` — json deep-copy delivery (zero-copy target) + bounded queue / drop-oldest / IOHealth.
- `src/IntraIOManager.cpp` — per-delivery re-wrap (zero-copy target).
- `docs/design/rendering-throughput-handoff.md` — the data-plane path + the intra zero-copy task.

---

*This contract is the engine's spine. Change it deliberately, and update this doc when you do.*
