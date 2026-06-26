# Threaded + ThreadPool hosting — session handoff

Status as of the threaded/pool work (commits `3dcde96` → `9079f50`, all pushed to gitea +
github + bitbucket). This is the resume point for the module-system parallelism track.

## Where things stand

| System | Status |
|--------|--------|
| `SequentialModuleSystem` | Production. |
| `ThreadedModuleSystem` (Phase 2) | Complete **and consolidated** — one OS thread per module, real parallelism, hosted via the engine, locked by real-module E2E. |
| `ThreadPoolModuleSystem` (Phase 3) | **Complete** — shared pool + work-stealing, engine-integrated, benchmarked. |
| `ClusterModuleSystem` (Phase 4) | Not started. |

## What was done this session (commit trail)

- `3dcde96` / `5217831` — **hot-reload `.so` fix**: cross-DLL exception-object UAF on MinGW
  (FreeLibrary while a caught `std::runtime_error` is alive). Fix = **deferred DLL unload**
  (park the handle, free it one cycle later). Locked by `ReloadAfterThrow` (A/B test).
- `7ce3bb8` — **parallel threaded hosting**: one SHARED `ThreadedModuleSystem` for all
  threaded static modules (was one system per module → serial). `registerStaticModule(THREADED)`.
- `3201fd7` — **post-drain fix (load-bearing, archi A)**: the worker drains its module's IIO
  inbox **AFTER** `process()`, not before. A self-draining module (UIModule) pulls its own
  inbox inside `process()` (beginFrame resets input edges → processInput pulls+sets them);
  pre-draining stole those messages and beginFrame wiped the edges → clicks lost. Mirrors the
  engine's sequential pump-after-process ordering. Locked by `ThreadedRealUIE2E`.
- `92fc8c6` — **Phase 2 consolidation**: multiple REAL modules hosted THREADED together via the
  engine, real cross-module IIO chain. `test_threaded_real_multi_e2e`.
- `3ec88bf` — **Phase 3 slice 1**: `ThreadPoolModuleSystem` standalone (pool + work-stealing).
- `0ae8495` — **Phase 3 slice 2**: engine integration (`registerStaticModule(THREAD_POOL)`).
- `9079f50` — **Phase 3 slice 3**: benchmark.

## Load-bearing invariants (do NOT break)

1. **archi A — process()-then-drain, atomic per task, on the worker that runs it.** Both
   `ThreadedModuleSystem` and `ThreadPoolModuleSystem` drain a module's inbox AFTER its
   `process()`, on the same worker thread. This keeps self-draining modules (UIModule's
   beginFrame→processInput ordering) correct. The engine's `pumpModuleIO` SKIPS worker-drained
   modules (`moduleIsThreaded_ == true`, which now covers pool too).
2. **Pool barrier = acknowledgement counter (`workersDone`), not just `tasksRemaining==0`.** A
   first cut returned from the barrier as soon as tasks hit 0, which let a worker still
   spinning in the previous frame's loop read the next frame's re-armed `tasksRemaining` and
   steal a decrement → intermittent hang (~1/8 runs, invisible to a single run). The frame is
   closed only once EVERY worker has LEFT `runFrameTasks`. Mirrors Phase 2's `workersCompleted`.
3. **1 module = 1 task/frame, never re-entrant** — a task lives in one mutex-guarded deque,
   popped once; two workers never run the same module at once.
4. **Cross-frame happens-before via the barrier** (release/acquire) — a module picked up by a
   different worker next frame still sees last frame's state.

## Benchmark takeaway (slice 3, 16-core machine)

- **HEAVY** per-module CPU work: pool ≈ threaded (cores are the bottleneck, dispatch is noise).
- **LIGHT** per-module work, many modules: pool pulls ~30% ahead once N ≳ 4× cores (threaded
  pays for N OS threads; pool stays at M≈cores workers). Crossover ~64 modules at 16 cores.
- **Host guidance**: `THREADED` for a handful of heavy modules; `THREAD_POOL` for many
  lightweight modules (N ≫ cores). `benchmark_pool_vs_threaded` (not a ctest — wall-clock).

## OPEN TASKS (priority order)

1. **[rigour] TSan on the engine→pool path.** Slice 1 has TSan on the pool *standalone*; the
   threaded system has its hosted-via-engine TSan jumel (`test_threaded_hosting_e2e`, synthetic
   + TSan-able through `registerStaticModule(THREADED)`). The pool has NO such twin. Write a
   `test_pool_hosting_e2e` — the same synthetic Producer/Relay/Sink chain but hosted via
   `registerStaticModule(THREAD_POOL)`, plain `main()`, GPU/SDL-free — and run it under
   ThreadSanitizer (WSL recipe: see memory `tsan-via-wsl-recipe` / the `~/tsan_build` wrapper at
   the session scratchpad's `tsan/CMakeLists.txt`; add a `tsan_pool_hosting` target next to
   `tsan_threaded` / `tsan_pool_lifecycle`). **Low risk** (the engine→pool path recombines the
   pool barrier, IIO cross-thread routing, and archi A — each already TSan-proven separately;
   the `DebugEngine` pool branch is a trivial mirror of the threaded one), but it's the one
   coherence gap. **The run is CPU-heavy (WSL rebuild + TSan)** — do it on mains power.
2. **[minor] `thread_count` configurable through the engine.** `ThreadPoolModuleSystem` ctor
   takes `threadCount` (0 = auto = cores-1). `ModuleSystemFactory::setConfiguration` reads
   `thread_count`/`queue_size` but they're still TODO — not plumbed from `registerStaticModule`.
3. **[minor] threaded + pool coexisting in one engine** is wired (`processModuleSystems` drives
   both `threadedSystem_` and `poolSystem_`) but untested. Exotic — a host normally picks one.
4. **[opt] pool overhead at small N.** The bench shows the pool losing to threaded for N < ~64
   (light work) — its barrier + `runFrameTasks` busy-wait (spin + `yield`) burns CPU. Replacing
   the spins with condition_variables would cut that overhead (at some wakeup-latency cost).
5. **[future] Phase 4** — `ClusterModuleSystem` (distributed). Blank slate.

## Key files

- `include/grove/ThreadPoolModuleSystem.h` + `src/ThreadPoolModuleSystem.cpp` — the pool.
- `src/DebugEngine.cpp` (`registerStaticModule` THREAD_POOL branch, `processModuleSystems`,
  `shutdown`, `pumpModuleIO`) + `include/grove/DebugEngine.h` (`poolSystem_`, `moduleIsThreaded_`).
- `src/ModuleSystemFactory.cpp` — `THREAD_POOL` case.
- Tests: `test_threadpool_lifecycle` (standalone + TSan-able), `test_pool_real_multi_e2e` +
  `test_threaded_real_multi_e2e` (share `real_multi_scenario.h`), `test_threaded_hosting_e2e`
  (synthetic, TSan-able — the template for the open TSan task), `benchmark_pool_vs_threaded`.
- TSan: memory `tsan-via-wsl-recipe`; WSL wrapper builds into `~/tsan_build`, run via
  `setarch $(uname -m) -R`.
