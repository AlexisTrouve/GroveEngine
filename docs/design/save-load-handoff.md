# Save / Load — handoff

**Status (2026-07-12): SHIPPED (core + engine glue), tests green, 3 commits LOCAL incl. this handoff (push pending; last pushed = fed7090).**
Whole-game save/load, built on the per-module `getState()`/`setState()` contract (same as hot-reload).

## What's built
- **`include/grove/save/SaveFile.h`** — pure header-only core (commit `c903635`, local). Versioned
  container `{moduleName -> state json}` -> disk. API: `capture(name, state)` / `captureModule(name, module)` ·
  `save(path)` · `load(path)` · `restoreInto(name, module)` · `has`/`state`/`moduleNames` · `loadedFormatVersion`/
  `savedAtUnixMs`. File format: `{"grove_save":{formatVersion:1, savedAtUnixMs, modules:{name:state}}}`.
- **`DebugEngine::saveState(path)` / `loadState(path)`** — engine glue (commit `9dd78cc`). One call
  captures/restores every registered module. Reaches each via new non-destructive
  `SequentialModuleSystem::getModule()` (leaves it registered, unlike `extractModule()`).

## Commits (LOCAL — last pushed = fed7090; PUSH PENDING)
- `c903635` feat(save): SaveFile core
- `9dd78cc` feat(save): DebugEngine saveState/loadState glue

## Cross-DLL safety (the load-bearing design choice — the LimitsTest lesson)
`capture()` DEEP-COPIES the state json immediately; it NEVER holds the `IDataNode` a hot-loaded module returned
(its vtable lives in the module DLL — holding it across a reload/unload faults). `restoreInto()` builds a
HOST-owned `JsonDataNode` before `setState()`. So a save survives a module reload/unload. Mirrors
`ModuleLoader::reload()`'s own re-homing. See `docs/design/limitstest-segfault-handoff.md`.

## Contract + limitations
- Call `saveState`/`loadState` **BETWEEN frames** (not during `step()`) — `getState()` must not race `process()`.
- **SEQUENTIAL-hosted modules only** (dynamic_cast to SequentialModuleSystem — same limitation as hot-reload /
  the state dump). THREADED / THREAD_POOL modules are skipped with a warning.
- Fail-soft: `load` returns false (+ clears) on missing/malformed/wrong-shape/future-version. A module absent
  from a save keeps its state; a saved module no longer registered is ignored; a corrupt state that makes
  `setState()` throw is caught + logged per module (never aborts the whole load).
- Added to `DebugEngine` (NOT the `IEngine` interface) to avoid forcing the method on other impls.

## Two usage patterns
- **Whole-engine:** `engine.saveState("s.json")` / `engine.loadState("s.json")`.
- **Direct-drive (what drifterra needs — it owns its module objects):** `save::SaveFile sf;
  sf.captureModule("fleet", mod); sf.save(path);` … `sf.load(path); sf.restoreInto("fleet", mod);`.

## Tests (all green; prove-it-bites each)
- `SaveFileUnit` (`tests/unit/test_save_file.cpp`) — 23 assertions: round-trip, restoreInto via a real module,
  deep-copy-survives-source-destruction (the cross-DLL property), fail-soft on missing/malformed/future/wrong.
- `SaveEngineE2E` (`tests/integration/test_save_engine.cpp`) — two engines, distinct per-module states (7/42)
  round-tripped through disk, absent-module untouched, missing-file -> false.
- Full suite **158/158** (a transient -j4 flake on the 1st run, clean on re-run; the additions are purely
  additive so no existing-test behavior changed).

## Follow-ons (not done)
1. **THREADED / THREAD_POOL support** — needs a thread-safe module-state accessor (getState under the per-module
   `processMutex`, like `queryModule`/`extractModule` already lock). Then saveState/loadState cover all hosting
   strategies. Biggest gap.
2. **Format migration** — only a `formatVersion` gate today (rejects newer). No v1->v2 migration path yet.
3. **`IEngine::saveState` (polymorphic)** — currently DebugEngine-only; promote to the interface if wanted (add
   a default no-op impl so other IEngine impls don't break).
4. **Save metadata / slots** — e.g. a thumbnail, playtime, a save-slot manager. Game-side, but the engine could
   offer conveniences.

## Anchors
- `include/grove/save/SaveFile.h` · `src/DebugEngine.cpp` (saveState/loadState) ·
  `include/grove/SequentialModuleSystem.h` (`getModule()`) · `include/grove/DebugEngine.h` (decls).
- Docs: DEVELOPER_GUIDE "Save / Load" + CLAUDE.md.
- Memory: [[save-load]]. Relates to [[limitstest-segfault]] (cross-DLL lesson), [[scene-entity-layer]]
  (FxModule getState/setState is a natural save target).
