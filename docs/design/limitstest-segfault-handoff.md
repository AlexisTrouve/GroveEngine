# LimitsTest SEGFAULT — analysis + handoff

**Status (2026-07-12): DIAGNOSED, not fixed.** A reproducible SIGSEGV in the hot-reload stress test
`LimitsTest` (`tests/integration/test_07_limits.cpp` → exe `test_07_limits`) under **parallel** execution.
Root cause localized to a real **Windows `ModuleLoader` hot-reload/unload lifetime fragility** (not a simple
test misuse). The exact crash frame is not yet captured (gdb Heisenbugs it). No code changed — the test file is
back at its original state. This doc is the resume aid.

## TL;DR
- `test_07_limits` **alone → passes**. **6 copies in parallel → 6/6 SIGSEGV** (exit 139), 100% reproducible.
  Not a random flake — a deterministic latent bug that only **parallel memory pressure** tips over.
- Two latent bugs the original code accidentally BALANCES:
  - **Bug A (handle-leak accumulation):** the test reuses ONE `ModuleLoader` for ~9 independent loads of the
    same DLL. `ModuleLoader::load()` on a reused loader does NOT free the old handle — it only warns
    (`src/ModuleLoader.cpp:239-247`) — so ~9 DLL copies accumulate and fragment/exhaust the address space →
    SIGSEGV at load ~8+ under memory pressure. (The loader's own comment at `ModuleLoader.cpp:250-260` already
    names this: *"reducing the risk of SIGSEGV at reload 8+ from exhaustion"* / *"address space fragmentation"*.)
    **This is what fires in `ctest -j4`.**
  - **Bug B (unload lifetime):** actually *unloading* the module DLLs (FreeLibrary) crashes — a module/DLL
    lifetime problem in the reload/unload path. The original **masks Bug B by never unloading** (Bug A's leak).
- **Recommended fix (short-term, low-risk):** mark the DLL-hot-reload stress tests `RUN_SERIAL` in ctest so
  they never run under the parallel pressure that triggers Bug A. Deeper Bug A/B engine investigation = tracked
  debt (non-urgent — hot-reload works in single-run).

## Reproduce (Git Bash, from `build/tests`)
```bash
cd build/tests
# solo — passes:
./test_07_limits.exe; echo $?          # 0

# parallel — 100% SIGSEGV (exit 139 = 128+SIGSEGV):
for i in 1 2 3 4 5 6; do ( ./test_07_limits.exe >/tmp/lt_$i.log 2>&1; echo "$? " ) & done; wait
# every instance: "Segmentation fault", exit 139.
```
The real `ctest -j4` failure is the same thing: `LimitsTest` runs concurrently with 3 other memory-heavy tests
→ Bug A tips. Under isolated re-run (`ctest -R '^LimitsTest$'`) it passes (that's why it looked like a "flake").

## What the test does (the trigger)
`test_07_limits.cpp` is a hot-reload stress test. It uses ONE `ModuleLoader loader;` (line 41) and calls
`loader.load(...)` / `loader.reload(...)` **~9 times** for the same real `libHeavyStateModule.dll` across 5
sub-tests (TEST 1 large-state, TEST 2 timeout, TEST 3 50MB memory-pressure reload, TEST 4 incremental reloads,
TEST 5 corruption). The modules are `std::move`d into per-test `SequentialModuleSystem`s that all stay alive, so
the DLL copies accumulate. `ModuleLoader::load()` copies the DLL to a **unique temp file per load**
(`GetTempFileNameA`, cross-process-safe — NOT the collision point) and LoadLibrary's it.

## Localization (evidence)
`std::cout` section markers are LOST in the crash (buffered, unflushed); only flushed **spdlog** lines survive.
- **All 6 crash logs end at the identical line:** `[HeavyStateModule] [error] frameCount must be integer` →
  **TEST 5**, immediately after `moduleCorrupt->setState(corrupted)`. That `setState`
  (`tests/modules/HeavyStateModule.cpp:198-199`) `throw`s `std::runtime_error` when `validateState` fails
  (frameCount is a string) — an exception unwinding **across the DLL→exe boundary**.
- The log also shows `⚠️ Loading new module while previous handle still open` before the crash — the reused-loader
  anti-pattern, at load #8–9 (matches "reload 8+").

## Two differential tests (both refuted a simple hypothesis — this is the value)
1. **Give each independent load its own `ModuleLoader`** (still declared inside each test): the TEST 5 crash
   **disappears** — the run gets PAST TEST 5 to teardown, and crashes at `✅ Module unloaded successfully`.
   → **Confirms Bug A** (reused-loader accumulation WAS the TEST 5 crash) AND **exposes Bug B** (the crash moved
   to the unload path once handles are actually freed).
2. **Hoist all loaders to the top of `main`** (so they outlive the modules — a module's dtor is CODE IN ITS
   DLL, so the DLL must not unload first): crashes **even SOLO**, at the unload path.
   → The original's single-loader-declared-first + handle-leak was accidentally the MOST stable arrangement
   (never unloads → Bug B never fires; but Bug A fires under parallel pressure).

Net: it is NOT a one-line test fix. Separate loaders fix A but trip B; the two are entangled in the Windows
DLL load/unload lifetime.

## The exact crash frame is not yet captured
- **gdb is an anti-Heisenbug here:** under `gdb -batch -ex run -ex bt`, the traced instance runs CLEAN while the
  background load instances crash. gdb's Windows exception hooks / different heap layout prevent the fault.
- **To get the frame, use instead:** a Windows post-mortem crash dump (WER / `procdump -e -ma test_07_limits.exe`
  while running the parallel load), or an **ASan build** (ASan reports the fault with a stack + flags any
  use-after-free precisely). ASan can't run from MinGW — use WSL/VPS142 (`-DGROVE_ENABLE_ASAN=ON`), but the
  `dlopen` module path must build as a `.so` there; feasible but a real setup step. This is the next move to
  turn Bug B from "unload path" into an exact file:line.

## Recommended fixes
1. **Short-term (kill the suite flake, low-risk, validated reasoning):** in `tests/CMakeLists.txt`,
   `set_tests_properties(LimitsTest PROPERTIES RUN_SERIAL TRUE)` (and consider the same for the other
   DLL-reload-heavy stress tests: `ChaosMonkey`, `MemoryLeakHunter`, `StressTest`, `ProductionHotReload`). Solo
   passes ⇒ LimitsTest-alone passes ⇒ `RUN_SERIAL` passes. It stops LimitsTest running under concurrent
   memory pressure WITHOUT touching the fragile DLL code. (Trade-off: those tests no longer overlap other tests,
   a small wall-clock cost — MemoryLeakHunter already dominates at ~156s anyway.)
2. **Engine debt (tracked, non-urgent):** the `ModuleLoader` Windows hot-reload/unload lifetime.
   - **Bug A:** reused-loader `load()` leaks the previous handle (only warns). Options: hard-error instead of
     warn+leak, or a bounded-lifetime handle registry. But the API contract IS "one loader per module" — so this
     may stay a documented constraint (see CLAUDE.md "ModuleLoader Usage").
   - **Bug B:** unloading a module DLL while/after its module object exists faults. Needs the ASan/dump frame to
     pinpoint (a module dtor into an unmapped DLL? a static-destruction-order issue? spdlog logger from the DLL
     outliving it?). This is the load-bearing unknown.

## Files / anchors
- `tests/integration/test_07_limits.cpp` — the test (ONE `loader`, ~9 loads; TEST 5 corruption at line ~376).
- `tests/modules/HeavyStateModule.cpp` — the reloaded module; `setState` throws at `:198-199`,
  `validateState` "frameCount must be integer" at `:325-327`.
- `src/ModuleLoader.cpp:239-260` — the reused-load warn-don't-unload + the "SIGSEGV at reload 8+ / address-space
  fragmentation" mitigation comment (Bug A is already known here).
- CLAUDE.md → "ModuleLoader Usage" ("Don't reuse loader for multiple independent modules (causes SEGFAULT)").
- Related: [[quality-hardening]] (this surfaced during the Phase-4 leak/perf/doc gate work; the full 156-test
  suite otherwise green). `docs/design/quality-hardening-handoff.md`.

## Open decisions (for Alexi)
- Apply the `RUN_SERIAL` mitigation now, or leave as tracked debt with this doc?
- Is Bug A acceptable as a documented API constraint (one loader per module), or should the engine hard-fail /
  manage handle lifetime on reuse?
- Worth the ASan-on-Linux setup to get Bug B's exact frame, or park it (hot-reload works in single-run)?
