# LimitsTest SEGFAULT — SOLVED (post-mortem)

**Status (2026-07-12): FIXED at the root. NO engine bug.** A reproducible SIGSEGV in the hot-reload stress
test `LimitsTest` (`tests/integration/test_07_limits.cpp`) under parallel execution. Root cause: a **cross-DLL
object-lifetime bug in the TEST** — it held a module's `getState()` result (a `JsonDataNode` whose vtable lives
in the module DLL) across a `reload()` that unloads that DLL, so the object's virtual destructor later jumped
into the freed DLL region. The engine's hot-reload path itself is CLEAN (it already re-homes state to survive
unload). Fixed by releasing the module-owned state before its DLL unloads. `RUN_SERIAL` (an earlier stop-gap)
was REMOVED — the flake is gone at the root.

## The bug, exactly
- `test_07_limits.cpp:87` `auto state = heavyModule->getState();` — a `unique_ptr<IDataNode>` holding a
  `JsonDataNode` **allocated by the module DLL**. `grove_impl` is a STATIC lib, so the DLL carries its OWN copy
  of `JsonDataNode`'s vtable/type_info → the object's vtable pointer points INTO that DLL.
- `test_07_limits.cpp:111` `loader.reload(...)` FreeLibrary's that old DLL.
- `state` (and `stateAfter` at :123) stay alive in `main` until teardown. Destroying them calls
  `~JsonDataNode` **via a vtable in the unmapped DLL** → `EXCEPTION_ACCESS_VIOLATION`.
- **Why parallel-only / flaky:** after `FreeLibrary`, Windows doesn't immediately unmap the region. In an
  isolated run it's often still resident → the virtual dtor "works" by luck. Under memory pressure (a parallel
  ctest run) the OS reclaims/unmaps the freed DLL faster → the dtor faults reliably. (This is also why gdb can't
  repro it — see below.)

## The fix (test-side; engine untouched)
Release the raw `getState()` results BEFORE the reload unloads their DLL:
```cpp
// after the size probe, before reload():
state.reset();
// after the particle-count check:
stateAfter.reset();
```
Verified: **6 parallel instances × 3 rounds = 0/6 crashes** (was 6/6), and full `ctest -j4` = **156/156** with
`RUN_SERIAL` removed. Suite back to ~161s.

## Why the engine is NOT buggy
`ModuleLoader::reload()` (`src/ModuleLoader.cpp:785-805`) **already** handles this exact hazard — its "Step 1b"
RE-HOMES the extracted state into a host-owned `JsonDataNode` (built in `grove_impl`, whose vtable is in the
never-unloaded host image) before `unload()`. Its comment describes the bug precisely, including *"SIGSEGV
intermittent... impossible to reproduce under gdb."* So the hot-reload path is safe; the crash only happened
because the TEST held a getState() result RAW across the unload, bypassing that protection.

## How it was cracked (method — reusable)
1. **Reproduce reliably:** run N copies in parallel (`for i in ...; do ./test_07_limits.exe & done`). Isolated
   passed; **6-parallel = 6/6 SIGSEGV (exit 139)**, 100% — a deterministic latent bug, not a random flake.
2. **Bitness check killed a false lead:** the exe/dll are **PE32+ (64-bit)** → the loader's own comment blaming
   "address-space exhaustion at reload 8+" is IMPOSSIBLE (128 TB user space). Re-proving the documented cause
   (doctrine) saved a wrong fix.
3. **gdb is an anti-Heisenbug here** — the traced instance runs clean while background ones die (gdb's Windows
   exception hooks / heap layout suppress the fault). So an **in-process SEH backtrace** was used instead:
   `tests/helpers/CrashBacktrace.h` (`SetUnhandledExceptionFilter` + `StackWalk64`, printing module+offset;
   symbolize offline with `addr2line -f -C -e <module> 0x<VA>`, VA = ImageBase + offset). It caught the fault
   with no perturbation.
4. **Symbolized frame (definitive):** `#00 test_07_limits.exe +0xd040d` → (via `nm`/`addr2line` with the full
   VA `0x1400d040d`) `std::default_delete<grove::IDataNode>::operator()` at `unique_ptr.h:93` — a
   `unique_ptr<IDataNode>` destructor, faulting on a `0x7ffc…438` DLL-region address (a vtable slot in a freed
   DLL; consistent low bits across ASLR'd runs = same vtable target).

## Reusable helper left in the tree
`tests/helpers/CrashBacktrace.h` — header-only, no-op off-Windows. To debug a similar Windows crash: `#include`
it, call `installCrashHandler()` at the top of `main()`, link `dbghelp`, build the target (+ the .dll it loads)
with `-g`, run, and `addr2line` the printed offsets. It succeeds where gdb Heisenbugs.

## Lessons
- A `getState()` result (or any polymorphic object a hot-reloadable module returns) is **tied to that module's
  DLL lifetime**. Don't hold it across a reload/unload — copy/re-home it (as `reload()` does) or release it
  first. Worth a line in the hot-reload docs / `IModule::getState` contract.
- "Documented cause" ≠ proof: the loader comment's "exhaustion" was wrong for a 64-bit build. Measure.
- gdb isn't the only tracer; an in-process SEH handler beats a Heisenbug.

## Anchors
- `tests/integration/test_07_limits.cpp:87,123` — the fix (`state.reset()` / `stateAfter.reset()`).
- `src/ModuleLoader.cpp:785-805` — the engine's correct state re-homing (why the engine is clean).
- `tests/helpers/CrashBacktrace.h` — the diagnostic helper.
- Related: [[quality-hardening]] (surfaced during the Phase-4 gate work).
