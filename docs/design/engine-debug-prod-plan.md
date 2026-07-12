# Engine — Debug / Prod plan

**Status: PLAN (not started). Decided 2026-07-12 after industry research.**

## The decision (why this doc exists)

We do **NOT** build two engine classes (`DebugEngine` + `ProductionEngine`). Research (Unreal /
Unity / Godot / Defold) is unanimous: **debug vs prod is a BUILD CONFIGURATION of one engine, not
two codebases.** Unreal ships 5 configs (Debug / DebugGame / Development / Test / Shipping) of the
*same* engine — shipping strips console/stats/profiling/debug-draw, keeps the same core.

So: `DebugEngine` stays the ONE engine class. A compile-time flag `GROVE_DEBUG` gates the "debug
skin" (introspection + verbose logging). Two builds fall out of one source; drifterra picks its
config, nothing to migrate.

Separately, research surfaced the *real* prod-readiness gaps — **crash reporting** and **memory
tracking** — which no build flag gives you. Those are Chantier B (real dev, independent of the flag).

---

## CHANTIER A — Debug/Prod as a build flag (`GROVE_DEBUG`)

Goal: `GROVE_DEBUG=ON` (default) = full introspection + verbose logging. `GROVE_DEBUG=OFF` (shipping)
= the debug skin compiled OUT (zero runtime cost). The prod core (threading / clock / streaming /
save / IIO health) is IDENTICAL in both.

### A0 — The gate: CMake option + `BuildConfig.h`
- **WHAT**: `option(GROVE_DEBUG "Debug build: introspection + verbose logging" ON)` in root
  `CMakeLists.txt`; propagate a compile definition to all grove targets. New header
  `include/grove/BuildConfig.h` exposing `#define GROVE_DEBUG 1/0` + a `constexpr bool
  grove::kDebugBuild` + helper `GROVE_DEBUG_ONLY(...)`.
- **WHY**: one switch, one place. `constexpr` flag lets code branch with `if constexpr` (no macro
  soup) where a runtime branch is fine; the macro is for compiling-out whole method bodies.
- **TEST** (`BuildConfigUnit`): default build asserts `kDebugBuild == true`. Shipping build (a
  dedicated `-DGROVE_DEBUG=OFF` config, like the sanitizer builds) asserts `false`. Locks the default
  + proves the flag flows through.

### A1 — Gate the per-frame logging (the real perf cost)
- **WHAT**: wrap `logFrameStart`/`logFrameEnd`/`logModuleHealth`/`logSocketHealth` calls in `step()`
  (and the `logger->trace(...)` hot-path lines) behind `#if GROVE_DEBUG`.
- **WHY**: these use `logger->trace(...)` (function calls) → the arguments are formatted EVERY frame
  even at trace-off, because spdlog only gates *inside* the call. `#if GROVE_DEBUG` removes the call
  site entirely in shipping — the true zero-cost.
- **TEST** (`ShippingStripUnit`, prove-it-bites): put a side-effecting counter call inside a gated log
  argument. Debug build → counter advances; shipping build → counter stays 0. That directly proves
  the argument is NOT evaluated in shipping (not just "the log is quiet").

### A2 — Gate the introspection methods
- **WHAT**: `dumpModuleState`, `dumpAllModulesState`, `getDetailedStatus`, `stepSingleFrame` → bodies
  conditional on `GROVE_DEBUG`. In shipping: keep the SYMBOL (callers still link) but make it a no-op
  / minimal-node return. **KEEP in shipping**: `pauseExecution`/`resumeExecution`/`isPaused` (cheap
  atomic; a pause menu is a legit prod feature) — only the *verbose* introspection is stripped.
- **WHY**: stripping the symbol would break any caller that links against it; a no-op body keeps the
  API stable while removing the cost + the information leak (a shipping build shouldn't dump internal
  state).
- **TEST**: debug build → `getDetailedStatus()` returns a rich node (has "modules"). Shipping build →
  returns the minimal/no-op node. Locks both contracts.

### A3 — Docs + kill the stale "evolution path"
- **WHAT**: document the two configs (what each strips, `cmake -DGROVE_DEBUG=OFF` to ship) in
  DEVELOPER_GUIDE + CLAUDE.md ("DebugEngine IS the engine; debug/prod = build flag"). Fix the stale
  `IEngine.h` "evolution path" comment (DebugEngine → HighPerfEngine → DataOrientedEngine is dead).
- **WHY**: the misnomer + dead comment are what caused this whole confusion. Document once, correctly.

### A4 — (optional) Retire the throwing engine-type stubs
- **WHAT**: `EngineType::PRODUCTION` / `HIGH_PERFORMANCE` throw "not yet implemented" in
  `EngineFactory`. Either collapse `EngineType` to just `DEBUG` (+ a comment), or keep the enum but
  document that engine *type* is not the debug/prod axis (the build flag is).
- **WHY**: they actively mislead (they imply the split we just decided against). Low risk — they throw
  today, so nothing depends on them working. Touches `EngineFactory` + its tests. Surgical, optional.

---

## CHANTIER B — The real prod-ready gaps (independent of the flag)

These are what "prod-ready" actually means per the research. Neither is given by a build flag; both
are real dev. Sequenced by ROI for a game that actually ships (drifterra).

### B1 — Crash reporter (minidump + engine context)  ← highest prod value
- **WHAT**: on a crash, write a dump + an engine-context snapshot to disk: frame #, `simTime`, clock
  state, module list, AND **the last N IIO messages from the ReplaySink**. Behind `ICrashBackend`:
  Windows = `MiniDumpWriteDump` via an SEH filter; POSIX = signal handler + backtrace (seed:
  `tests/helpers/CrashBacktrace.h` from the LimitsTest work). Installed in `initialize()`, removed in
  `shutdown()`. Flag `GROVE_CRASH_REPORTER` (ON by default — you want reports in BOTH debug and ship).
- **WHY**: "you cannot easily attach a profiler to a running retail build" — without an on-crash dump
  you're blind on shipped crashes. For a message-bus engine, **the last 200 IIO messages before the
  crash is the killer artifact** (you see the sequence that led to it), which we already capture in
  ReplaySink — this just persists it on death.
- **RISK**: sanitizers install their own handlers → gate the crash reporter OFF under ASan/TSan
  builds. Two platform impls behind one interface (the `ISoundBackend`/`IVideoBackend` pattern).
- **TEST** (`CrashReporterE2E`, process-isolated): a child process deliberately faults; the parent
  asserts a dump file exists with the expected context keys (frame/simTime/module list/last-messages).
  Same in-process-vs-child-process discipline as the LimitsTest SEH backtrace.

### B2 — Memory allocation tracking layer  ← second
- **WHAT**: `grove::mem::Tracker` — a lightweight tagged-allocation tracker (circular buffer of
  {size, tag, tick}); dump-to-disk on a threshold or on demand; a leak report (alloc without free)
  on dump. Scope to **grove's own hot allocations** (IIO messages, sprite/particle batches) via a
  tagged allocation helper — NOT a global `operator new` override. Flag `GROVE_MEM_TRACKING`
  (OFF by default; ON in a "test"/QA build).
- **WHY**: research is explicit — "have this tracking layer ready BEFORE you ship, not after the leak
  is reported." A *global* operator-new override is the textbook approach but invasive (captures every
  dep's allocations, breaks under sanitizers). Scoping to grove's tagged hot paths is more surgical
  AND more useful (you learn WHERE grove leaks: which topic, which batch).
- **RISK**: don't double-track under ASan (which already does allocation tracking) → gate off under
  sanitizers. Keep the tracker lock-free-ish / cheap so `GROVE_MEM_TRACKING=ON` is usable in a real
  QA session.
- **TEST** (`MemTrackerUnit`): alloc/free through the tracker → buffer records it; an un-freed tagged
  alloc → shows in the leak report; a freed one → doesn't. Prove-it-bites.

### B3 — (optional) Profiler zones  ← biggest DEBUG gap, lower urgency
- **WHAT**: a lightweight scoped-zone timer — `GROVE_PROFILE_ZONE("name")` accumulates per-zone,
  per-frame timings, queryable/dumpable (NOT a UI — a game/tool visualizes). Gated by `GROVE_DEBUG`.
- **WHY**: research flags "no real profiler" as GroveEngine's biggest debug-side gap vs Unity/Unreal.
  This is the instrumentation half (cheap); the visualization is consumer-side.
- **TEST** (`ProfileZoneUnit`): time a zone → the accumulator has a non-zero entry for that name;
  zones nest correctly. In shipping (`GROVE_DEBUG=OFF`) the macro compiles to nothing.

---

## Sequencing + priority

1. **A0 → A1 → A2 → A3** (Chantier A, in order). Cheap, closes the mental frame, produces the shipping
   build. A4 optional cleanup after.
2. **B1** (crash reporter) — highest prod value; do first of Chantier B.
3. **B2** (mem tracking) — before any real ship / when chasing a leak.
4. **B3** (profiler zones) — debug-side polish; when a perf problem needs it.

**What drifterra actually needs to ship**: A (a lean shipping build) + B1 (diagnose ship crashes).
B2/B3 are "when you hit a leak / perf wall." A is transparent to drifterra (default ON = today's
behavior); only the shipping build changes, opt-in.

## Cross-cutting rules
- Every slice: TDD (red test first), prove-it-bites, one commit, no ctest regression.
- Build-flag tests follow the sanitizer precedent: default ctest locks the DEBUG contract; a dedicated
  `-DGROVE_DEBUG=OFF` build config locks the SHIPPING contract (manual/CI gate, like LeakGate).
- Crash reporter + mem tracking + profiler all gate OFF under ASan/TSan (avoid handler/allocator
  conflicts).
- No new runtime deps: minidump = Windows SDK (already there), POSIX = `backtrace()` (libc). No libav-
  style external linking.

## Anchors
- `CMakeLists.txt` (root options) · `include/grove/BuildConfig.h` (new) · `src/DebugEngine.cpp`
  (step()/introspection) · `include/grove/DebugEngine.h` (decls) · `include/grove/IEngine.h` (stale
  comment) · `src/EngineFactory.cpp` (A4 stubs) · `tests/helpers/CrashBacktrace.h` (B1 seed) ·
  ReplaySink (B1 context source).
- Relates to [[quality-hardening]] (sanitizer gating), [[engine-io-contract]] (ReplaySink), memory
  [[limitstest-segfault]] (the SEH backtrace seed for B1).
